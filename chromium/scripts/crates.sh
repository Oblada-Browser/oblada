#!/usr/bin/env bash
# Puts the Rust crates Oblada adds to Chromium's in the checkout: adblock (Brave's
# ad-blocking engine, for the Barrier) and its dependencies. The patches to
# third_party/rust/chromium_crates_io (Cargo.toml, Cargo.lock, gnrt_config.toml) and to the
# BUILD.gn files of crates Chromium already has say what they are; this downloads their
# sources from crates.io into chromium_crates_io/vendor with Chromium's tool, gnrt, and
# generates their BUILD.gn files, as Chromium does when it imports a crate. The patches gnrt
# applies to some of them (licenses they don't publish) are in chromium/crates/patches.
# Run by sync.sh and build.sh, after patches.sh apply; does nothing when they're there.
set -euo pipefail
source "$(dirname "$0")/env.sh"

CRATES="$SRC/third_party/rust/chromium_crates_io"
STAMP="$CRATES/.oblada-crates"

for dir in "$HERE"/crates/patches/*/; do
  mkdir -p "$CRATES/patches/$(basename "$dir")"
  cp "$dir"/*.patch "$CRATES/patches/$(basename "$dir")/"
done

# What they were made from: the lock file and gnrt's patches.
state="$(cat "$CRATES/Cargo.lock" "$HERE"/crates/patches/*/*.patch | shasum -a 256)"
[ "$(cat "$STAMP" 2>/dev/null)" = "$state" ] && exit 0

cd "$SRC"
vpython3 tools/crates/run_gnrt.py vendor
vpython3 tools/crates/run_gnrt.py gen
echo "$state" > "$STAMP"
echo "Oblada's Rust crates are in $CRATES/vendor"
