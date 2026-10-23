#!/usr/bin/env bash
# Packs the build into a DMG, Oblada-silicon.dmg in the build directory: the app and a link
# to Applications over branding/dmg's background. Every release's DMG has that name (for
# Apple silicon Macs), so the website's download button always leads to the latest one
# (releases/latest/download/Oblada-silicon.dmg). Next to it, Oblada-<version>-<arch>.aar
# (Oblada's version, chromium/oblada/VERSION) is the same app for updates: an Apple
# Archive, which the updater unpacks without mounting a disk (mounting a DMG can stall in
# macOS's disk check). Prints the archive's path, then the DMG's, last.
#   dmg.sh [--release] [--app <Oblada.app>] [--version <x.y.z>] [--feed <url>]
#          [--public-key <key>] [--out <dir>]
# --release packs the release build (build.sh --release), as release.sh does, in its
# directory. The other options are for the update test (updates.mjs): another app,
# version, feed and key.
#
# The app in the DMG updates itself (oblada/browser/updater): its Info.plist gets the
# feed of the latest release, the public half of the key releases are signed with
# (oblada/browser/updater/PUBLIC_KEY, made by `make update-key`), and Oblada's version
# as the bundle version, which the updater compares with the feed's. Without a key, it
# doesn't update itself.
# The build is signed by the linker alone, which macOS calls damaged once downloaded: the
# app in the DMG is signed ad hoc as a whole, helpers included (there's no Developer ID
# yet, so no notarization either). dmgbuild writes the window's layout without opening
# Finder; it's installed on first use, in the build directory.
set -euo pipefail
source "$(dirname "$0")/env.sh"

# hdiutil warns that its commands are deprecated, every time.
quiet() { "$@" 2> >(grep -v 'is deprecated' >&2); }

APP="$OUT/Oblada.app"
VERSION="$(tr -d '[:space:]' < "$HERE/oblada/VERSION")"
FEED="https://github.com/Oblada-Browser/oblada/releases/latest/download/appcast.xml"
KEY_FILE="$HERE/oblada/browser/updater/PUBLIC_KEY"
PUBLIC_KEY="$(cat "$KEY_FILE" 2>/dev/null | tr -d '[:space:]' || true)"
DEST="$OUT"
while [ $# -gt 0 ]; do
  case "$1" in
    --release) APP="$RELEASE_OUT/Oblada.app"; DEST="$RELEASE_OUT"; shift; continue ;;
    --app) APP="$2" ;;
    --version) VERSION="$2" ;;
    --feed) FEED="$2" ;;
    --public-key) PUBLIC_KEY="$2" ;;
    --out) DEST="$2" ;;
    *) echo "usage: dmg.sh [--release] [--app <app>] [--version <x.y.z>] [--feed <url>] [--public-key <key>] [--out <dir>]" >&2; exit 2 ;;
  esac
  shift 2
done
[ -d "$APP" ] || { echo "no app at $APP: run build.sh first (--release for the release build)" >&2; exit 1; }
[ -d "$APP/Contents/Frameworks/Sparkle.framework" ] ||
  { echo "$APP has no Sparkle.framework: build it again (build.sh)" >&2; exit 1; }
ARCH="$(lipo -archs "$APP/Contents/MacOS/Oblada" | tr ' ' '-')"
DMG="$DEST/Oblada-silicon.dmg"
ARCHIVE="$DEST/Oblada-$VERSION-$ARCH.aar"

VENV="$OUT/dmgbuild"
if [ ! -x "$VENV/bin/dmgbuild" ]; then
  python3 -m venv "$VENV" >&2
  "$VENV/bin/pip" install --quiet dmgbuild==1.6.7 >&2
fi

WORK="$(mktemp -d)"
trap 'hdiutil detach -quiet "$WORK/mnt" 2>/dev/null || true; rm -rf "$WORK"' EXIT

ditto "$APP" "$WORK/Oblada.app"
xattr -cr "$WORK/Oblada.app"
PLIST="$WORK/Oblada.app/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$VERSION" "$PLIST"
if [ -n "$PUBLIC_KEY" ]; then
  plutil -replace SUFeedURL -string "$FEED" "$PLIST"
  plutil -replace SUPublicEDKey -string "$PUBLIC_KEY" "$PLIST"
  # Checks once a day without asking, and downloads what it finds.
  plutil -replace SUEnableAutomaticChecks -bool YES "$PLIST"
  plutil -replace SUAutomaticallyUpdate -bool YES "$PLIST"
  # The feed must be signed too, and an update is checked before it's even unpacked.
  plutil -replace SURequireSignedFeed -bool YES "$PLIST"
  plutil -replace SUVerifyUpdateBeforeExtraction -bool YES "$PLIST"
else
  echo "warning: no $KEY_FILE (make update-key): this DMG's app won't update itself" >&2
fi
codesign --force --deep --sign - "$WORK/Oblada.app" 2>/dev/null
codesign --verify --deep --strict "$WORK/Oblada.app"

mkdir -p "$DEST"
rm -f "$DMG" "$ARCHIVE"
aa archive -D "$WORK/Oblada.app" -o "$ARCHIVE"
# The app as it comes out of the archive passes the strict check.
mkdir "$WORK/unpacked"
aa extract -i "$ARCHIVE" -d "$WORK/unpacked"
codesign --verify --deep --strict "$WORK/unpacked/Oblada.app"
rm -rf "$WORK/unpacked"

quiet "$VENV/bin/dmgbuild" -s "$HERE/scripts/dmg_settings.py" \
  -D app="$WORK/Oblada.app" -D background="$HERE/../branding/dmg/background.png" \
  Oblada "$DMG" >&2

# The app as it comes out of the DMG still passes the strict check.
mkdir "$WORK/mnt"
quiet hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$WORK/mnt" "$DMG"
codesign --verify --deep --strict "$WORK/mnt/Oblada.app"
quiet hdiutil detach -quiet "$WORK/mnt"

echo "SHA-256 $(shasum -a 256 "$DMG" | cut -d' ' -f1)" >&2
echo "$ARCHIVE"
echo "$DMG"
