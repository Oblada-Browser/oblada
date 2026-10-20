#!/usr/bin/env bash
# Checks out the Chromium release Oblada is pinned to (chromium/CHROMIUM_VERSION) with its
# dependencies and toolchains, then links Oblada's sources in and applies Oblada's patches.
# Safe to re-run: it resumes an interrupted sync and moves the checkout to a new version.
set -euo pipefail
source "$(dirname "$0")/env.sh"

mkdir -p "$OBLADA_CHROMIUM"
touch "$OBLADA_CHROMIUM/.metadata_never_index" # keep Spotlight out of the sources

if [ ! -d "$OBLADA_CHROMIUM/depot_tools/.git" ]; then
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$OBLADA_CHROMIUM/depot_tools"
fi

# "managed": False means gclient never moves src itself; this script pins it to the release tag.
cat > "$OBLADA_CHROMIUM/.gclient" <<'EOF'
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git",
  "managed": False,
  "custom_deps": {},
  "custom_vars": {"checkout_pgo_profiles": False},
}]
target_os = ["mac"]
EOF

if [ ! -d "$SRC/.git" ]; then
  rm -rf "$SRC" # a clone interrupted before git finished
  git clone --depth 1 --branch "$CHROMIUM_VERSION" https://chromium.googlesource.com/chromium/src.git "$SRC"
elif [ "$(git -C "$SRC" describe --tags --exact-match 2>/dev/null)" != "$CHROMIUM_VERSION" ]; then
  "$HERE/scripts/patches.sh" reset
  git -C "$SRC" fetch --depth 1 origin "+refs/tags/$CHROMIUM_VERSION:refs/tags/$CHROMIUM_VERSION"
  git -C "$SRC" checkout "refs/tags/$CHROMIUM_VERSION"
fi

cd "$OBLADA_CHROMIUM"
gclient sync --nohooks --no-history --delete_unversioned_trees
gclient runhooks
"$HERE/scripts/patches.sh" apply
"$HERE/scripts/sparkle.sh"
echo "Chromium $CHROMIUM_VERSION is ready in $SRC"
