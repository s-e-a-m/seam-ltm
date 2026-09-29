#!/bin/bash
# build.sh -- compile every probe of probe.dsp with the offline harness of ../lmo-bandfilter/.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
BIN=$HERE/build/bin; mkdir -p "$BIN"
for p in int0 int1 int2 pre0 pre1 pre2 post0 post1 post2 selfrm lib; do
  if [ -z "${FORCE:-}" ] && [ -x "$BIN/$p" ] && [ "$BIN/$p" -nt "$HERE/probe.dsp" ]; then continue; fi
  faust -double -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$BIN/$p.cpp"
  c++ -O2 -std=c++17 -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" "$BIN/$p.cpp" -o "$BIN/$p" 2>/dev/null
  echo "built $p"
done
