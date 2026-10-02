#!/usr/bin/env python3
"""spec.py -- sdt.choir against the original. Run from build/ by run.sh.

The input of channel c is 16 sines at f·k (k = 1..16), 0.05 each, so that every
analysis band hears its partial. 20 s; the comparisons start after 10 s.
d_orig = ours - orig·choirdens: zero if sdt.choirchan is the original without
the DC blocker and with the voices at their 96 kHz level (choirdens = 1 at
96 kHz, sqrt(1/2) at 48 kHz). d_full = ours - sdt.choir on that channel: zero
if the four-channel routing gives channel c its input and its 16 streams.
"""
import subprocess
import numpy as np

F = [48, 48, 96, 96]
def tone(c, sr, sec, f=None):
    t = np.arange(int(sec * sr)) / sr
    x = sum(0.05 * np.sin(2 * np.pi * (f or F[c]) * k * t) for k in range(1, 17) if (f or F[c]) * k < sr / 2)
    path = f"in{c}_{sr}_{sec}_{f or F[c]}.f32"; x.astype(np.float32).tofile(path); return path

def run(p, sr, sec, inp):
    subprocess.run([f"./{p}", str(sr), str(int(sec * sr)), f"out={p}.f64", f"in=file:{inp}"], check=True)
    return np.fromfile(f"{p}.f64")

print("| SR | channel | peak of the original | max |ours - orig·choirdens| | max |ours - sdt.choir| |")
print("|---|---|---|---|---|")
for sr in (96000, 48000):
    for c in range(4):
        inp = tone(c, sr, 20); s = slice(10 * sr, None)
        pk = np.max(np.abs(run(f"o{c}", sr, 20, inp)[s]))
        do = np.max(np.abs(run(f"do{c}", sr, 20, inp)[s])); df = np.max(np.abs(run(f"df{c}", sr, 20, inp)[s]))
        print(f"| {sr} | {c} | {pk:.3g} | {do / pk:.1e} of the peak | {df / pk:.1e} of the peak |")

sr = 48000
inp = tone(0, sr, 2, 1000)
for p in ("nyq_ours", "nyq_orig"):
    y = run(p, sr, 2, inp); fin = np.isfinite(y)
    print(f"{p} at 48 kHz, f = 5000 Hz: finite {fin.mean() * 100:.1f} %, max |y| {np.max(np.abs(y[fin])) if fin.any() else float('nan'):.3g}")
