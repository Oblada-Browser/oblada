#!/usr/bin/env bash
# Publishes the current commit as a GitHub release, with its DMG and update archive
# (dmg.sh) and its update feed (appcast.py), the archive and the feed signed with the
# update key in the login keychain (make update-key). Releases are official builds, built
# apart in out/Release (build.sh --release): their binaries are stripped, and their
# symbols stay on this Mac, in $OBLADA_CHROMIUM/symbols, for their crash reports
# (symbolicate.py). The release is marked as the latest:
# installed apps find it by its feed (releases/latest/download/appcast.xml), the website's
# download button by its DMG (releases/latest/download/Oblada-silicon.dmg).
#   release.sh <notes.md>    e.g. release.sh /tmp/notes.md
# The release is Oblada's version (chromium/oblada/VERSION, e.g. 0.1.1), and so is its tag:
# raise it first. The Chromium it is built on and the DMG's SHA-256 are added to the
# notes, the version's changelog (changelogs/) to the feed. Run again on the same commit,
# it replaces the release's files and notes.
set -euo pipefail
source "$(dirname "$0")/env.sh"
NOTES="$(cd "$(dirname "${1:?usage: release.sh <notes.md>}")" && pwd)/$(basename "$1")"
VERSION="$(tr -d '[:space:]' < "$HERE/oblada/VERSION")"
TAG="$VERSION"
SPARKLE="$SRC/third_party/sparkle/bin"
CHANGELOG="$HERE/../changelogs/${VERSION//./_}.json"
cd "$HERE/.."

[ -f "$CHANGELOG" ] || { echo "no changelog for $VERSION: write $CHANGELOG" >&2; exit 1; }
# The keychain's key signs what the apps will check with PUBLIC_KEY.
PUBLIC_KEY="$(cat "$HERE/oblada/browser/updater/PUBLIC_KEY" 2>/dev/null | tr -d '[:space:]' || true)"
[ -n "$PUBLIC_KEY" ] || { echo "no update key: make update-key first" >&2; exit 1; }
[ "$("$SPARKLE/generate_keys" -p 2>/dev/null)" = "$PUBLIC_KEY" ] || {
  echo "the login keychain has no update key matching oblada/browser/updater/PUBLIC_KEY" >&2
  exit 1
}

# The DMG is built from the commit the tag names, as it is on GitHub: nothing uncommitted,
# here or in the checkout's Chromium files.
[ -z "$(git status --porcelain)" ] || { echo "commit your changes first" >&2; exit 1; }
git fetch --quiet --tags origin
[ -n "$(git branch -r --contains HEAD)" ] || { echo "push HEAD first" >&2; exit 1; }
if git rev-parse --quiet --verify "refs/tags/$TAG" >/dev/null &&
   [ "$(git rev-parse "$TAG^{commit}")" != "$(git rev-parse HEAD)" ]; then
  echo "$TAG is another commit's release: raise chromium/oblada/VERSION" >&2
  exit 1
fi
"$HERE/scripts/build.sh" --release >&2
"$HERE/scripts/patches.sh" update >/dev/null
[ -z "$(git status --porcelain)" ] || {
  echo "the checkout has edits that aren't in chromium/patches (see git status)" >&2
  exit 1
}

PACKED="$("$HERE/scripts/dmg.sh" --release)"
ARCHIVE="$(tail -2 <<< "$PACKED" | head -1)"
DMG="$(tail -1 <<< "$PACKED")"

DSYMS="$(mktemp -d)"
BODY="$(mktemp)"
trap 'rm -rf "$DSYMS" "$BODY"' EXIT

# The app's binaries are stripped: the dSYMs made when they were linked, matched by UUID,
# are kept for the release's crash reports.
uuids() { xcrun dwarfdump --uuid "$@" 2>/dev/null | awk '{ print $2 }' || true; }
# One file at a time: dwarfdump stops at the first file that isn't a binary.
APP_UUIDS="$(find "$RELEASE_OUT/Oblada.app" -type f -perm -u+x -exec xcrun dwarfdump --uuid {} \; 2>/dev/null |
  awk '{ print $2 }' || true)"
for dsym in "$RELEASE_OUT"/*.dSYM; do
  if grep -qxF "$(uuids "$dsym")" <<< "$APP_UUIDS"; then ditto "$dsym" "$DSYMS/$(basename "$dsym")"; fi
done
[ -d "$DSYMS/Oblada Framework.dSYM" ] || { echo "no dSYM for the app's framework in $RELEASE_OUT" >&2; exit 1; }
SYMBOLS="$OBLADA_CHROMIUM/symbols/$(basename "$ARCHIVE" .aar)-dSYMs.aar"
mkdir -p "$(dirname "$SYMBOLS")"
rm -f "$SYMBOLS"
aa archive -d "$DSYMS" -o "$SYMBOLS"
echo "symbols: $SYMBOLS ($(ls "$DSYMS" | wc -l | tr -d ' ') dSYMs)" >&2

APPCAST="$(dirname "$DMG")/appcast.xml"
python3 "$HERE/scripts/appcast.py" --archive "$ARCHIVE" --version "$VERSION" --out "$APPCAST" \
  --url "https://github.com/Oblada-Browser/oblada/releases/download/$TAG/$(basename "$ARCHIVE")" \
  --changelog "$CHANGELOG"
{
  cat "$NOTES"
  printf '\nBuilt on Chromium %s.\n' "$CHROMIUM_VERSION"
  printf '\nSHA-256 of `%s`: `%s`\n' "$(basename "$DMG")" "$(shasum -a 256 "$DMG" | cut -d' ' -f1)"
} > "$BODY"

if gh release view "$TAG" >/dev/null 2>&1; then
  gh release upload "$TAG" "$DMG" "$ARCHIVE" "$APPCAST" --clobber
  gh release edit "$TAG" --notes-file "$BODY" --latest
else
  gh release create "$TAG" "$DMG" "$ARCHIVE" "$APPCAST" --target "$(git rev-parse HEAD)" \
    --title "Oblada $VERSION" --notes-file "$BODY" --latest
fi
