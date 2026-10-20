#!/usr/bin/env bash
# Builds Oblada into $OBLADA_CHROMIUM/src/out/Oblada with chromium/args.gn.
#   build.sh            the browser
#   build.sh <targets>  anything else, e.g. build.sh unit_tests
set -euo pipefail
source "$(dirname "$0")/env.sh"

"$HERE/scripts/patches.sh" apply
mkdir -p "$OUT"
cmp -s "$HERE/args.gn" "$OUT/args.gn" || cp "$HERE/args.gn" "$OUT/args.gn"
[ -f "$OUT/build.ninja" ] || (cd "$SRC" && gn gen "out/$(basename "$OUT")")
cd "$SRC"
autoninja -C "out/$(basename "$OUT")" "${@:-chrome}"
