#!/bin/bash
# run.sh -- build the probes, run check.py (identity, mutation, energy) and memory.sh.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
DT=$HERE/../stunedrev-delays            # Davide's nextprime.h, renamed
B=$HERE/build; mkdir -p "$B"
for p in sq ph ex pi osq oph oex opi dsq dph dex dpi mut; do
  faust -double -dlt 4096 -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$B/$p.cpp"
  c++ -O2 -std=c++17 -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" -I"$DT" "$B/$p.cpp" -o "$B/$p"
done
cd "$B" && "$HERE/../../../../.venv/bin/python" "$HERE/check.py"
NEW_LIBS=$NEW_LIBS SEAM_LIBS=$SEAM_LIBS "$HERE/memory.sh"
