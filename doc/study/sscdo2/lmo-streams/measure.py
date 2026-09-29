# measure.py -- run from build/ by run.sh: 20 s at 48 kHz after a 1 s pre-roll.
import numpy as np, subprocess
SR = 48000; N = SR * 20
def get(p, ch):
    subprocess.run([f"./{p}", str(SR), str(N), f"out={p}{ch}.f64", f"ch={ch}", "pre=1"], check=True)
    return np.fromfile(f"{p}{ch}.f64")
def r(a, b): return np.corrcoef(a, b)[0, 1]
def db(x): return 20 * np.log10(np.sqrt(np.mean(x ** 2)))
pre = [get("pre2", c) for c in range(8)]
print("N=2, block 1 minus block 0 before the merge, max |diff| per channel:",
      [f"{np.max(np.abs(pre[4 + m] - pre[m])):.1e}" for m in range(4)])
for name in ("gs1", "gs2", "dav"):
    y = [get(name, c) for c in range(4)]
    print(f"{name}: RMS dBFS", [f"{db(v):.2f}" for v in y],
          " r(0,1)=%.3f r(0,2)=%.3f r(1,3)=%.3f r(2,3)=%.3f" % (r(y[0], y[1]), r(y[0], y[2]), r(y[1], y[3]), r(y[2], y[3])))
