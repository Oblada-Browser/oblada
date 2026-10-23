#!/usr/bin/env bash
# Builds Oblada into $OBLADA_CHROMIUM/src/out/Oblada with chromium/args.gn.
#   build.sh                      the browser
#   build.sh <targets>            anything else, e.g. build.sh unit_tests
#   build.sh --release [targets]  the release build, an official one, in out/Release: with
#                                 chromium/args.release.gn too. Hours the first time
set -euo pipefail
source "$(dirname "$0")/env.sh"

ARGS=("$HERE/args.gn")
if [ "${1:-}" = --release ]; then
  shift
  OUT="$RELEASE_OUT"
  ARGS+=("$HERE/args.release.gn")
fi

"$HERE/scripts/patches.sh" apply
"$HERE/scripts/sparkle.sh"
mkdir -p "$OUT"
cat "${ARGS[@]}" | cmp -s - "$OUT/args.gn" || cat "${ARGS[@]}" > "$OUT/args.gn"
[ -f "$OUT/build.ninja" ] || (cd "$SRC" && gn gen "out/$(basename "$OUT")")
cd "$SRC"
autoninja -C "out/$(basename "$OUT")" "${@:-chrome}"
