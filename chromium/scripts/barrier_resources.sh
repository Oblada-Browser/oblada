#!/usr/bin/env bash
# Puts the resources of the Barrier's ad-blocking engine in the checkout, as
# src/third_party/oblada_barrier/resources.json, which the build embeds
# (oblada/components/barrier/BUILD.gn): uBlock Origin's scriptlets and redirect resources,
# and Brave's own, which filter lists' +js() and $redirect rules use. From a uBlock Origin
# release and a commit of Brave's adblock-resources, pinned by SHA-256, put together by
# barrier_resources.mjs with Chromium's Node. Run by sync.sh and build.sh; does nothing
# when they're there.
set -euo pipefail
source "$(dirname "$0")/env.sh"

UBLOCK_VERSION=1.75.0
UBLOCK_SHA256=a518c7d6e6b3f81d1a738befeb617abba59f93bd6b1e626da079a1003e9db52b
BRAVE_RESOURCES_COMMIT=6da9b247c3368baa8b4f24cd02046e18702a6527
BRAVE_RESOURCES_SHA256=d085eb0754b7a54d9e1a18a0314b1946b066f61a15457de834d98beb53b4e40f
DIR="$SRC/third_party/oblada_barrier"
VERSION="$UBLOCK_VERSION $BRAVE_RESOURCES_COMMIT $(shasum -a 256 "$HERE/scripts/barrier_resources.mjs")"

[ "$(cat "$DIR/VERSION" 2>/dev/null)" = "$VERSION" ] && exit 0

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/ublock.tar.gz" \
  "https://github.com/gorhill/uBlock/archive/refs/tags/$UBLOCK_VERSION.tar.gz"
echo "$UBLOCK_SHA256  $WORK/ublock.tar.gz" | shasum -a 256 -c --quiet
curl -fsSL -o "$WORK/brave-resources.json" \
  "https://raw.githubusercontent.com/brave/adblock-resources/$BRAVE_RESOURCES_COMMIT/dist/resources.json"
echo "$BRAVE_RESOURCES_SHA256  $WORK/brave-resources.json" | shasum -a 256 -c --quiet
tar -xzf "$WORK/ublock.tar.gz" -C "$WORK"

NODE="$(find "$SRC/third_party/node" -path '*/bin/node' -type f | head -1)"
rm -rf "$DIR"
mkdir -p "$DIR"
"$NODE" "$HERE/scripts/barrier_resources.mjs" "$WORK/uBlock-$UBLOCK_VERSION" \
  "$WORK/brave-resources.json" "$DIR/resources.json"
cp "$WORK/uBlock-$UBLOCK_VERSION/LICENSE.txt" "$DIR/LICENSE"
# For oblada://credits, which lists the third_party directories with their licenses.
cat > "$DIR/README.chromium" <<EOF
Name: uBlock Origin's and Brave's ad-blocking resources
URL: https://github.com/gorhill/uBlock
Version: $UBLOCK_VERSION
License: GPL-3.0
License File: LICENSE
Security Critical: yes
Shipped: yes

Description:
The scriptlets and redirect resources of uBlock Origin $UBLOCK_VERSION (GPL-3.0), with
Brave's own (https://github.com/brave/adblock-resources at $BRAVE_RESOURCES_COMMIT,
MPL-2.0), which the Barrier's ad-blocking engine (adblock-rust) injects into pages and
answers blocked requests with. Put here by Oblada's chromium/scripts/barrier_resources.sh.
EOF
echo "$VERSION" > "$DIR/VERSION"
echo "The Barrier's resources are in $DIR"
