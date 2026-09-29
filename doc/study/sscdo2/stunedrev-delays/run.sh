#!/bin/bash
# run.sh -- compare the two next_pr, build the delay probes, run check.py.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
B=$HERE/build; mkdir -p "$B"
cc -O2 -I"$HERE" -I"$SEAM_LIBS/h" "$HERE/npcmp.c" -o "$B/npcmp" -lm
cc -O2 -DMUTATE -I"$HERE" "$HERE/npcmp.c" -o "$B/npcmp_mut" -lm
echo -n "nextprime, Davide against SEAM:      "; "$B/npcmp"
echo -n "nextprime, mutation (must mismatch): "; if "$B/npcmp_mut"; then echo "MUTATION NOT CAUGHT"; exit 1; fi
for p in seam dav; do
  faust -double -pn $p -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$B/$p.cpp"
  c++ -O2 -std=c++17 -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" -I"$HERE" "$B/$p.cpp" -o "$B/$p"
  "$B/$p" 96000 16800 out="$B/$p.f64"
done
cd "$B" && "$HERE/../../../../.venv/bin/python" "$HERE/check.py"
