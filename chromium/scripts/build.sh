#!/usr/bin/env bash
# Builds Oblada into $OBLADA_CHROMIUM/src/out/Oblada with chromium/args.gn.
#   build.sh                      the browser
#   build.sh <targets>            anything else, e.g. build.sh unit_tests
#   build.sh --release [targets]  the release build, an official one, in out/Release: with
#                                 chromium/args.release.gn too, and the Safe Browsing key
#                                 from the keychain (env.sh). Hours the first time
set -euo pipefail
source "$(dirname "$0")/env.sh"

ARGS=("$HERE/args.gn")
if [ "${1:-}" = --release ]; then
  shift
  OUT="$RELEASE_OUT"
  ARGS+=("$HERE/args.release.gn")
fi

"$HERE/scripts/patches.sh" apply
"$HERE/scripts/crates.sh"
"$HERE/scripts/barrier_resources.sh"
"$HERE/scripts/sparkle.sh"
mkdir -p "$OUT"
args() {
  cat "${ARGS[@]}"
  if [ "$OUT" = "$RELEASE_OUT" ]; then
    local key
    key="$(safe_browsing_key)"
    if [[ "$key" =~ ^[A-Za-z0-9_-]+$ ]]; then
      echo "oblada_safe_browsing_api_key = \"$key\""
    else
      echo "no Safe Browsing key in the keychain (make safe-browsing-key): Safe Browsing won't work" >&2
    fi
  fi
}
GN_ARGS="$(args)"
printf '%s\n' "$GN_ARGS" | cmp -s - "$OUT/args.gn" || printf '%s\n' "$GN_ARGS" > "$OUT/args.gn"
[ -f "$OUT/build.ninja" ] || (cd "$SRC" && gn gen "out/$(basename "$OUT")")
cd "$SRC"
autoninja -C "out/$(basename "$OUT")" "${@:-chrome}"
