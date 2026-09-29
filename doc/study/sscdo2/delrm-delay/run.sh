#!/bin/bash
# run.sh -- sma.imt2npsamp against DDELAY's C++, every 0.1 mm from 0 to 30 m, at four rates,
# then the same comparison with the Faust side moved by 1 mm, which must fail.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
ARCH=$HERE/../lmo-bandfilter/harness/arch.cpp
B=$HERE/build; mkdir -p "$B"
faust -double -I "$NEW_LIBS" -I "$SEAM_LIBS" -a "$ARCH" "$HERE/probe.dsp" -o "$B/probe.cpp"
c++ -O2 -std=c++17 -I"$(faust --includedir)" -I"$SEAM_LIBS/h" -I"$SEAM_LIBS" "$B/probe.cpp" -o "$B/probe" 2>/dev/null
c++ -O2 -std=c++17 "$HERE/ddelay_ref.cpp" -o "$B/ref"
N=300001
for sr in 44100 48000 96000 192000; do
  "$B/probe" $sr $N out="$B/f.f64" >/dev/null; "$B/ref" $sr $N 0.0001 "$B/r.f64"
  python3 "$HERE/compare.py" "$B/f.f64" "$B/r.f64" "$sr Hz"
done
"$B/probe" 48000 $N out="$B/f.f64" off=0.001 >/dev/null; "$B/ref" 48000 $N 0.0001 "$B/r.f64"
python3 "$HERE/compare.py" "$B/f.f64" "$B/r.f64" "MUTATION +1 mm, 48000 Hz (must show mismatches)"
