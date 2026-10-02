#!/usr/bin/env python3
"""measure.py -- the choir's chain at 48 and 96 kHz. Run from build/ by run.sh."""
import subprocess
import numpy as np

def run(p, sr, sec, pre=0, inp=None):
    a = [f"./{p}", str(sr), str(int(sec * sr)), f"out={p}.f64", f"pre={pre}"]
    if inp: a.append(f"in={inp}")
    subprocess.run(a, check=True)
    return np.fromfile(f"{p}.f64")

db = lambda x: 20 * np.log10(np.sqrt(np.mean(x ** 2)))

print("| SR | fi.dcblocker on 48 Hz | noise band at 48 Hz, Q 350 (exact power) |")
print("|---|---|---|")
for sr in (48000, 96000):
    loss = db(run("dcb", sr, 4, 1)) - db(run("dry", sr, 4, 1))
    h = run("bandir", sr, 80, inp="impulse")       # harness writes float32: 1e-7 of the peak
    pw = 10 * np.log10(float(np.sum(h ** 2)) / 3)
    print(f"| {sr} | {loss:+.2f} dB | {pw:+.2f} dBFS |")

print("\n| SR | follower, 1 s after the input stops | analysis band, 1 s after | band to -60 dB |")
print("|---|---|---|---|")
for sr in (48000, 96000):
    f = run("foll", sr, 3); r = run("ring", sr, 30)
    pk = lambda x, i: np.max(np.abs(x[i - sr // 48: i]))     # peak over one period
    fa = 20 * np.log10(f[2 * sr - 1] / pk(f, sr))
    ra = 20 * np.log10(pk(r, 2 * sr) / pk(r, sr))
    t = next(k for k in range(sr, len(r), sr // 48) if pk(r, k + sr // 48) < pk(r, sr) * 1e-3)
    print(f"| {sr} | {fa:+.2f} dB (exp(-1/1.5): {20 * np.log10(np.exp(-1 / 1.5)):+.2f}) | {ra:+.2f} dB | {(t - sr) / sr:.1f} s |")

for p in ("nyq55", "nyq75"):
    for sr in (48000, 96000):
        y = run(p, sr, 0.5)
        fin = np.isfinite(y)
        print(f"{p} SR {sr}: finite {fin.mean() * 100:.1f} %, max |y| {np.max(np.abs(y[fin])) if fin.any() else float('nan'):.3g}")
