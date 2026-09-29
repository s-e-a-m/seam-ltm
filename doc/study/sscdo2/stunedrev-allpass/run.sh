#!/bin/bash
# run.sh -- build the probes (double, and float for moorer and sch) and run check.py.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
B=$HERE/build; mkdir -p "$B"
build() { # name precision
  faust -$2 -dlt 4096 -pn $1 -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$B/$1_$2.cpp"
  c++ -O2 -std=c++17 -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" "$B/$1_$2.cpp" -o "$B/$1_$2"
}
for p in moorer dav std sch inphi mut; do build $p double; done
for p in moorer sch; do build $p single; done
cd "$B" && "$HERE/../../../../.venv/bin/python" "$HERE/check.py"
