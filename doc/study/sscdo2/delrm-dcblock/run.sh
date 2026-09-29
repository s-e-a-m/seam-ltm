#!/bin/bash
# run.sh -- build the probes with the offline harness of ../lmo-bandfilter/ and run analyze.py.
# FAUSTFLOAT=double: the harness buffers are otherwise float, and the sample-for-sample
# checks would compare float32 roundings (~1e-8) instead of the filters.
#   ./run.sh [check|measure|render|all] [SOURCE.wav]
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
BIN=$HERE/build/bin; mkdir -p "$BIN"
for p in comb combhp trip triphp triporig dry dcb dcbat dcbatx; do
  faust -double -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$BIN/$p.cpp"
  c++ -O2 -std=c++17 -DFAUSTFLOAT=double -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" "$BIN/$p.cpp" -o "$BIN/$p" 2>/dev/null
done
"$HERE/../../../../.venv/bin/python" "$HERE/analyze.py" "$@"
