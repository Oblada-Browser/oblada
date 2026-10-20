# Sourced by the other scripts. The Chromium checkout lives outside this repo
# ($OBLADA_CHROMIUM, default ~/chromium): ~35 GB of sources plus ~60 GB per build.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OBLADA_CHROMIUM="${OBLADA_CHROMIUM:-$HOME/chromium}"
SRC="$OBLADA_CHROMIUM/src"
OUT="$SRC/out/Oblada"
# Else the checkout's existing build directory, whatever it's called: renaming one makes
# the build start over (~6 hours), as its state is tied to its path.
if [ ! -d "$OUT" ]; then
  for dir in "$SRC"/out/*/; do
    if [ -f "$dir/args.gn" ]; then OUT="${dir%/}"; break; fi
  done
fi
CHROMIUM_VERSION="$(tr -d '[:space:]' < "$HERE/CHROMIUM_VERSION")"
export PATH="$OBLADA_CHROMIUM/depot_tools:$PATH"
