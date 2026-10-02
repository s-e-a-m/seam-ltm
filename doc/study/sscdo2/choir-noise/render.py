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

# the floor between the bands, channel 0 (f = 48 Hz, a = 1), relative to the peaks
with open(os.path.join(R, "render-log.md"), "a") as fh:
    fh.write("\nChannel 0, mean floor between the bands (median of 0.3-0.7 of each gap) relative to the mean peak:\n\n| version | floor re peaks |\n|---|---|\n")
    for p, _ in PROBES:
        x = sig[p][:, 0]
        S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / SR)
        pk = np.mean([S[np.argmin(np.abs(f - 48 * k))] for k in range(1, 17)])
        fl = np.mean([np.median(S[(f > 48 * (k + 0.3)) & (f < 48 * (k + 0.7))]) for k in range(1, 16)])
        fh.write(f"| `{p}` | {10 * math.log10(fl / pk):.1f} dB |\n")
print(open(os.path.join(R, "render-log.md")).read())

# outside the bank: v4 minus v64, per channel (Giuseppe's spectrum analyser)
def band_db(x, lo, hi):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / SR)
    return 10 * math.log10(float(np.mean(S[(f > lo) & (f < hi)])))
with open(os.path.join(R, "render-log.md"), "a") as fh:
    fh.write("\nOutside the bank, `v4` minus `v64`:\n\n| channel | 20-30 Hz | 5-7 kHz | 20-30 kHz |\n|---|---|---|---|\n")
    for c in range(4):
        fh.write(f"| {c} | " + " | ".join(f"{band_db(sig['v4'][:, c], lo, hi) - band_db(sig['v64'][:, c], lo, hi):+.1f} dB"
                                       for lo, hi in [(20, 30), (5000, 7000), (20000, 30000)]) + " |\n")
print(open(os.path.join(R, "render-log.md")).read())
