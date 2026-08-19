#!/usr/bin/env bash
# Do the Faust specification and the hand-written C++ port describe the same
# filter? Compares impulse responses sample by sample, at two sample rates.
set -euo pipefail
cd "$(dirname "$0")"
LIBS=../../faust-libraries/src
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/pink.dsp" <<'DSP'
import("seam.lib");
process = sfi.pink_filter_mz;
DSP

clang++ -O2 -std=c++17 -I../plugins/multipink/source -o "$TMP/cppir" pink-ir-dump.cpp

status=0
for FS in 48000 96000; do
    faust -I "$LIBS" -double -a faust-ab-arch.cpp -o "$TMP/pink_$FS.cpp" "$TMP/pink.dsp"
    clang++ -O2 -std=c++17 -I/usr/local/include -o "$TMP/faustir_$FS" "$TMP/pink_$FS.cpp"
    "$TMP/faustir_$FS" "$FS" 4096 > "$TMP/faust_$FS.txt"
    "$TMP/cppir"       "$FS" 4096 > "$TMP/cpp_$FS.txt"
    python3 - "$TMP/faust_$FS.txt" "$TMP/cpp_$FS.txt" "$FS" <<'PY' || status=1
import sys
a=[float(x) for x in open(sys.argv[1])]
b=[float(x) for x in open(sys.argv[2])]
assert len(a)==len(b), "different lengths: %d vs %d" % (len(a),len(b))
worst=max(abs(x-y) for x,y in zip(a,b))
print("fs=%s  worst |faust-cpp| over 4096 samples: %.3e  %s"
      % (sys.argv[3], worst, "PASS" if worst < 1e-9 else "FAIL"))
sys.exit(0 if worst < 1e-9 else 1)
PY
done
exit $status
