# compare.py -- value-by-value comparison of the two raw float64 files written by run.sh.
import struct, sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read(); n = len(a) // 8
A = struct.unpack(f"{n}d", a); B = struct.unpack(f"{n}d", b)
mm = [i for i in range(n) if A[i] != B[i]]
first = f", first at {mm[0] * 1e-4:.4f} m: Faust {A[mm[0]]:.0f}, DDELAY {B[mm[0]]:.0f}" if mm else ""
print(f"{sys.argv[3]}: {n} values, {len(mm)} mismatches{first}; 7.291 m -> {A[72910]:.0f} samples")
