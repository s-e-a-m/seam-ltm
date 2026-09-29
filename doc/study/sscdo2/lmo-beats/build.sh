#!/bin/bash
# build.sh -- compile every probe of dsp/probes.dsp to an offline binary.
# usage: ./build.sh            (FORCE=1 rebuilds)
# The harness is the one of ../lmo-bandfilter/ (one output, raw float64).
set -euo pipefail
# ---- paths (edit these on another machine) ---------------------------------
# A clone of https://github.com/grame-cncm/faustlibraries at 0965ea2 or later:
# sdt.lmoband needs its SVF fi.lowpass/fi.highpass.
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
FAUST=${FAUST:-faust}
CXX=${CXX:-c++}
# ----------------------------------------------------------------------------
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
BIN=$HERE/build/bin; mkdir -p "$BIN"
for p in lmo2_ch osc_ch one_ch twocall_ch; do
  out=$BIN/$p
  if [ -z "${FORCE:-}" ] && [ -x "$out" ] && [ "$out" -nt "$HERE/dsp/probes.dsp" ] && [ "$out" -nt "$SEAM_LIBS/seam.tedesco.lib" ]; then
    echo "up to date $p"; continue; fi
  "$FAUST" -double -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/dsp/probes.dsp" -o "$out.cpp"
  "$CXX" -O2 -std=c++17 -I"$("$FAUST" --includedir)" "$out.cpp" -o "$out" 2>&1 | { grep -v -i warning || true; }
  echo "built $p"
done
