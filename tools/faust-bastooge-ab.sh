#!/usr/bin/env bash
# Do the Faust specification (stt.bastooge_w / stt.bastooge_d in seam.st.lib)
# and the hand-written C++ chains describe the same filters? Compares impulse
# responses sample by sample. W: 11 cut frequencies (every bell-table row is
# reached, plus 90 and 300 for the clamp) and D: 3, each at 2 sample rates.
set -euo pipefail
cd "$(dirname "$0")"
LIBS=../../faust-libraries/src
N=8192
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

clang++ -O2 -std=c++17 -I../plugins/_common -o "$TMP/cppir" bastooge-ir-dump.cpp

status=0
for CH in w d; do
  if [ "$CH" = w ]; then FCS="90 100 117 132 137 160 179 202 228 250 300"; else FCS="100 137 250"; fi
  for FC in $FCS; do
    cat > "$TMP/b.dsp" <<DSP
import("seam.st.lib");
process = stt.bastooge_$CH($FC);
DSP
    faust -double -lang cpp -I "$LIBS" -a faust-ab-arch.cpp -o "$TMP/b.cpp" "$TMP/b.dsp"
    clang++ -O2 -std=c++17 -I/usr/local/include -I/opt/homebrew/include -o "$TMP/faustir" "$TMP/b.cpp"
    for FS in 48000 96000; do
      "$TMP/faustir" "$FS" "$N" > "$TMP/faust.txt"
      "$TMP/cppir" "$CH" "$FC" "$FS" "$N" > "$TMP/cpp.txt"
      python3 - "$TMP/faust.txt" "$TMP/cpp.txt" "$CH" "$FC" "$FS" <<'PY' || status=1
import sys
a=[float(x) for x in open(sys.argv[1])]
b=[float(x) for x in open(sys.argv[2])]
assert len(a)==len(b), "different lengths: %d vs %d" % (len(a),len(b))
worst=max(abs(x-y) for x,y in zip(a,b))
ok = worst <= 1e-12
print("%s %s %s %s worst=%.3e" % ("PASS" if ok else "FAIL", *sys.argv[3:6], worst))
sys.exit(0 if ok else 1)
PY
    done
  done
done
exit $status
