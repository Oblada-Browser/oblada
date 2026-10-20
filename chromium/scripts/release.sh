#!/usr/bin/env bash
# Publishes the current commit as a GitHub release, with its DMG (dmg.sh). The release is
# marked as the latest; the website's download button links to its DMG by name.
#   release.sh <notes.md>    e.g. release.sh /tmp/notes.md
# The release is Oblada's version (chromium/oblada/VERSION, e.g. 0.1.1), and so is its tag:
# raise it first. The Chromium it is built on and the DMG's SHA-256 are added to the
# notes. Run again on the same commit, it replaces the release's DMG and notes.
set -euo pipefail
source "$(dirname "$0")/env.sh"
NOTES="$(cd "$(dirname "${1:?usage: release.sh <notes.md>}")" && pwd)/$(basename "$1")"
VERSION="$(tr -d '[:space:]' < "$HERE/oblada/VERSION")"
TAG="$VERSION"
cd "$HERE/.."

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
"$HERE/scripts/build.sh" >&2
"$HERE/scripts/patches.sh" update >/dev/null
[ -z "$(git status --porcelain)" ] || {
  echo "the checkout has edits that aren't in chromium/patches (see git status)" >&2
  exit 1
}

DMG="$("$HERE/scripts/dmg.sh" | tail -1)"
BODY="$(mktemp)"
trap 'rm -f "$BODY"' EXIT
{
  cat "$NOTES"
  printf '\nBuilt on Chromium %s.\n' "$CHROMIUM_VERSION"
  printf '\nSHA-256 of `%s`: `%s`\n' "$(basename "$DMG")" "$(shasum -a 256 "$DMG" | cut -d' ' -f1)"
} > "$BODY"

if gh release view "$TAG" >/dev/null 2>&1; then
  gh release upload "$TAG" "$DMG" --clobber
  gh release edit "$TAG" --notes-file "$BODY" --latest
else
  gh release create "$TAG" "$DMG" --target "$(git rev-parse HEAD)" \
    --title "Oblada $VERSION" --notes-file "$BODY" --latest
fi
