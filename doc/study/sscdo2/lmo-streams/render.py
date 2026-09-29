#!/usr/bin/env python3
"""render.py -- listening A/B of one noise stream against several.

usage (from this folder, after ./run.sh has built the probes):
    ../../../../.venv/bin/python render.py

For gs1, gs2 and dav (probes.dsp) it writes 5 s at 48 kHz after a 1 s
pre-roll, in two forms: the four channels (LFU, RFD, RBU, LBD, for STONED or
a tetrahedral layout) and channels 0 and 1 as a stereo pair, for headphones,
where the correlation of adjacent channels is heard as width. One gain for
all files (the probes share their level), -20 dBFS RMS, 20 ms fades, 24-bit.
"""
import math, os, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
R = os.path.join(HERE, "renders")
SR, SEC, TARGET = 48000, 5, -20.0
PROBES = [("gs1", "Giuseppe's lmo(1,4,1000): one stream"),
          ("gs2", "Giuseppe's lmo(2,4,1000): two streams, cyclic"),
          ("dav", "sdt.lmoosc(4,1000): one stream per channel, as in the original")]

def get(p, ch):
    out = os.path.join(B, f"r_{p}{ch}.f64")
    subprocess.run([os.path.join(B, p), str(SR), str(SR * SEC), f"out={out}", f"ch={ch}", "pre=1"], check=True)
    y = np.fromfile(out); os.remove(out); return y

sig = {p: np.stack([get(p, c) for c in range(4)], axis=1) for p, _ in PROBES}
rms = math.sqrt(float(np.mean(np.concatenate([s.ravel() for s in sig.values()]) ** 2)))
g = 10 ** (TARGET / 20) / rms
k = int(0.02 * SR); w = 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
os.makedirs(R, exist_ok=True)
rows = []
for p, label in PROBES:
    y = sig[p] * g
    y[:k] *= w[:, None]; y[-k:] *= w[::-1, None]
    if np.max(np.abs(y)) >= 1: raise RuntimeError(f"{p} would clip")
    sf.write(os.path.join(R, f"lmo_{p}_4ch.wav"), y, SR, subtype="PCM_24")
    sf.write(os.path.join(R, f"lmo_{p}_ch01_stereo.wav"), y[:, :2], SR, subtype="PCM_24")
    r01 = np.corrcoef(y[:, 0], y[:, 1])[0, 1]
    rows.append(f"| `lmo_{p}_4ch.wav`, `lmo_{p}_ch01_stereo.wav` | {label} | {r01:+.3f} |")
with open(os.path.join(R, "render-log.md"), "w") as fh:
    fh.write(f"{SEC} s at {SR} Hz after a 1 s pre-roll, f = 1000 Hz; one gain for all files, {20 * math.log10(g):+.2f} dB, "
             f"{TARGET:.0f} dBFS RMS, 20 ms fades, 24-bit.\n\n"
             "| files | probe | r(0,1) |\n|---|---|---|\n" + "\n".join(rows) + "\n")
print(open(os.path.join(R, "render-log.md")).read())
