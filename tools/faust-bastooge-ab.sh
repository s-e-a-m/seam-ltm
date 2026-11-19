#!/usr/bin/env bash
# Do the Faust specification (stt.bastooge_w / stt.bastooge_d in seam.st.lib)
# and the hand-written C++ chains describe the same filters? Compares impulse
# responses sample by sample. W: 11 cut frequencies (every bell-table row is
# reached, plus 90 and 300 for the clamp) and D: 3, each at 2 sample rates.
# Then the whole decoder, stt.bastooge(160, test, bypass) against
# Seam::bastooge::Engine at 48 kHz: test 0..4 x bypass 0/1 x 4 faces with the
# impulse on a0, and for test 0 also on a1, a2, a3 (64 cases).
set -euo pipefail
cd "$(dirname "$0")"
LIBS=../../faust-libraries/src
N=8192
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

clang++ -O2 -std=c++17 -I../plugins/_common -I../plugins/bamodulex/source -I../plugins/bastooge/source -o "$TMP/cppir" bastooge-ir-dump.cpp

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

# The whole decoder. One Faust build per (test, bypass, input, face): the
# arch is one in, one out, so the impulse is routed to input <in> and one
# face is selected.
FC=160; FS=48000
for T in 0 1 2 3 4; do
  if [ "$T" = 0 ]; then INS="0 1 2 3"; else INS="0"; fi
  for B in 0 1; do
    for IN in $INS; do
      case $IN in
        0) ROUTE="(_,0,0,0)";; 1) ROUTE="(0,_,0,0)";;
        2) ROUTE="(0,0,_,0)";; 3) ROUTE="(0,0,0,_)";;
      esac
      for F in 0 1 2 3; do
        cat > "$TMP/e.dsp" <<DSP
import("stdfaust.lib");
import("seam.st.lib");
process = _ : $ROUTE : stt.bastooge($FC, $T, $B) : ba.selector($F, 4);
DSP
        faust -double -lang cpp -I "$LIBS" -a faust-ab-arch.cpp -o "$TMP/e.cpp" "$TMP/e.dsp"
        clang++ -O2 -std=c++17 -I/usr/local/include -I/opt/homebrew/include -o "$TMP/faustir" "$TMP/e.cpp"
        "$TMP/faustir" "$FS" "$N" > "$TMP/faust.txt"
        "$TMP/cppir" e "$FC" "$FS" "$N" "$T" "$B" "$IN" "$F" > "$TMP/cpp.txt"
        python3 - "$TMP/faust.txt" "$TMP/cpp.txt" "$T" "$B" "$IN" "$F" <<'PY' || status=1
import sys
a=[float(x) for x in open(sys.argv[1])]
b=[float(x) for x in open(sys.argv[2])]
assert len(a)==len(b), "different lengths: %d vs %d" % (len(a),len(b))
worst=max(abs(x-y) for x,y in zip(a,b))
peak=max(abs(x) for x in a)
ok = worst <= 1e-12
print("%s bastooge test=%s bypass=%s in=a%s face=%s worst=%.3e peak=%.3g" % ("PASS" if ok else "FAIL", *sys.argv[3:7], worst, peak))
sys.exit(0 if ok else 1)
PY
      done
    done
  done
done
exit $status
