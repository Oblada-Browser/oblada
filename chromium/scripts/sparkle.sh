#!/usr/bin/env bash
# Puts Sparkle (https://sparkle-project.org), which Oblada updates itself with on macOS, in
# the checkout as src/third_party/sparkle: Sparkle.framework, bundled into the app by the
# build, and its tools in bin/ (sign_update, generate_keys) for releases. It's Sparkle's
# own prebuilt release, pinned by version and SHA-256. Run by sync.sh and build.sh; does
# nothing when it's already there.
set -euo pipefail
source "$(dirname "$0")/env.sh"

SPARKLE_VERSION=2.10.0
SPARKLE_SHA256=c2bf58aa8387266ac179357b1415d6f2635f044da8be41042af32425dae6da0c
DIR="$SRC/third_party/sparkle"

[ "$(cat "$DIR/VERSION" 2>/dev/null)" = "$SPARKLE_VERSION" ] && exit 0

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/sparkle.tar.xz" \
  "https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz"
echo "$SPARKLE_SHA256  $WORK/sparkle.tar.xz" | shasum -a 256 -c --quiet
mkdir "$WORK/sparkle"
tar -xf "$WORK/sparkle.tar.xz" -C "$WORK/sparkle"

rm -rf "$DIR"
mkdir -p "$DIR"
mv "$WORK/sparkle/Sparkle.framework" "$WORK/sparkle/bin" "$WORK/sparkle/LICENSE" "$DIR/"
# For oblada://credits, which lists the third_party directories with their licenses.
cat > "$DIR/README.chromium" <<EOF
Name: Sparkle
URL: https://sparkle-project.org
Version: $SPARKLE_VERSION
License: MIT
License File: LICENSE
Security Critical: yes
Shipped: yes

Description:
The software update framework Oblada updates itself with on macOS, from its releases on
GitHub. Put here by Oblada's chromium/scripts/sparkle.sh.
EOF
echo "$SPARKLE_VERSION" > "$DIR/VERSION"
echo "Sparkle $SPARKLE_VERSION is in $DIR"
