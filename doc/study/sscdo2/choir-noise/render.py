#!/usr/bin/env python3
"""render.py -- listening A/B of the choir's noise.

usage (from this folder, after ./run.sh has built the probes):
    ../../../../.venv/bin/python render.py

The 16 noise bands of each voice, without the envelopes of the input, so
that only the noise is heard, in three versions (probes.dsp):
  vo   the original: the four channels carry the same 16 streams
  v4   one stream per voice, block 3 of multinoise(12) (the decision)
  v64  one stream per band, 64 streams of multinoise(72)
6 s at 96 kHz after a 20 s pre-roll (the bands take seconds to form, Q = 350),
as four channels (LFU, RFD, RBU, LBD) and as channels 0 and 1 in a stereo
pair for headphones, where the correlation between voices is heard as width.
One gain for all files, -20 dBFS RMS over all of them, 20 ms fades, 24-bit.
"""
import math, os, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
R = os.path.join(HERE, "renders")
SR, SEC, PRE, TARGET = 96000, 6, 20, -20.0
PROBES = [("vo", "the original: the same 16 streams in every channel"),
          ("v4", "one stream per voice, `sdt.choirnoise(4)`"),
          ("v64", "one stream per band, 64 streams")]

def get(p, c):
    out = os.path.join(B, f"r_{p}_{c}.f64")
    subprocess.run([os.path.join(B, f"{p}_{c}"), str(SR), str(SR * SEC), f"out={out}", f"pre={PRE}"], check=True)
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
    sf.write(os.path.join(R, f"choir_{p}_4ch.wav"), y, SR, subtype="PCM_24")
    sf.write(os.path.join(R, f"choir_{p}_ch01_stereo.wav"), y[:, :2], SR, subtype="PCM_24")
    r01 = np.corrcoef(y[:, 0], y[:, 1])[0, 1]
    lv = " / ".join(f"{20 * math.log10(math.sqrt(float(np.mean(y[:, c] ** 2)))):.1f}" for c in range(4))
    rows.append(f"| `choir_{p}_4ch.wav`, `choir_{p}_ch01_stereo.wav` | {label} | {r01:+.3f} | {lv} |")
with open(os.path.join(R, "render-log.md"), "w") as fh:
    fh.write(f"{SEC} s at {SR} Hz after a {PRE} s pre-roll; one gain for all files, {20 * math.log10(g):+.2f} dB, "
             f"{TARGET:.0f} dBFS RMS over all, 20 ms fades, 24-bit.\n\n"
             "| files | version | r(0,1) | RMS per channel (dBFS) |\n|---|---|---|---|\n" + "\n".join(rows) + "\n")
print(open(os.path.join(R, "render-log.md")).read())
