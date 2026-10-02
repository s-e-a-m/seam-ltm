#!/bin/bash
# run.sh -- build the probes and print the tables in README.md.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
SEAM_LIBS=${SEAM_LIBS:-$(cd "$(dirname "$0")/../../../../../faust-libraries/src" && pwd)}
HERE=$(cd "$(dirname "$0")" && pwd)
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
B=$HERE/build; mkdir -p "$B"
for p in dcb dry bandir foll ring nyq55 nyq75; do
  faust -double -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probes.dsp" -o "$B/$p.cpp"
  c++ -O2 -std=c++17 -I"$(faust --includedir)" "$B/$p.cpp" -o "$B/$p" 2>/dev/null
done
cd "$B" && "$HERE/../../../../.venv/bin/python" "$HERE/measure.py"
