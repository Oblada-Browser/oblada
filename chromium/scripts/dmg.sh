#!/usr/bin/env bash
# Packs the build into a DMG, Oblada-<version>-<arch>.dmg in the build directory: the app
# and a link to Applications over branding/dmg's background. Prints the DMG's path last.
#   dmg.sh
# The build is signed by the linker alone, which macOS calls damaged once downloaded: the
# app in the DMG is signed ad hoc as a whole, helpers included (there's no Developer ID
# yet, so no notarization either). dmgbuild writes the window's layout without opening
# Finder; it's installed on first use, in the build directory.
set -euo pipefail
source "$(dirname "$0")/env.sh"

# hdiutil warns that its commands are deprecated, every time.
quiet() { "$@" 2> >(grep -v 'is deprecated' >&2); }

APP="$OUT/Oblada.app"
[ -d "$APP" ] || { echo "no build in $OUT: run build.sh first" >&2; exit 1; }
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")"
ARCH="$(lipo -archs "$APP/Contents/MacOS/Oblada" | tr ' ' '-')"
DMG="$OUT/Oblada-$VERSION-$ARCH.dmg"

VENV="$OUT/dmgbuild"
if [ ! -x "$VENV/bin/dmgbuild" ]; then
  python3 -m venv "$VENV" >&2
  "$VENV/bin/pip" install --quiet dmgbuild==1.6.7 >&2
fi

WORK="$(mktemp -d)"
trap 'hdiutil detach -quiet "$WORK/mnt" 2>/dev/null || true; rm -rf "$WORK"' EXIT

ditto "$APP" "$WORK/Oblada.app"
xattr -cr "$WORK/Oblada.app"
codesign --force --deep --sign - "$WORK/Oblada.app" 2>/dev/null
codesign --verify --deep --strict "$WORK/Oblada.app"

rm -f "$DMG"
quiet "$VENV/bin/dmgbuild" -s "$HERE/scripts/dmg_settings.py" \
  -D app="$WORK/Oblada.app" -D background="$HERE/../branding/dmg/background.png" \
  Oblada "$DMG" >&2

# The app as it comes out of the DMG still passes the strict check.
mkdir "$WORK/mnt"
quiet hdiutil attach -quiet -nobrowse -readonly -noautoopen -mountpoint "$WORK/mnt" "$DMG"
codesign --verify --deep --strict "$WORK/mnt/Oblada.app"
quiet hdiutil detach -quiet "$WORK/mnt"

echo "SHA-256 $(shasum -a 256 "$DMG" | cut -d' ' -f1)" >&2
echo "$DMG"
