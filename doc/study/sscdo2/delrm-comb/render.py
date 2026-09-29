#!/usr/bin/env python3
"""render.py -- the comb on a recording of the contrabass clarinet.

usage (from this folder, after ./run.sh has built the probes):
    ../../../../.venv/bin/python render.py [SOURCE.wav]

SOURCE defaults to CCB_petalonio_oriz_DO.wav on the MDAGIFT volume: eight
microphones around a contrabass clarinet playing its low C, 96 kHz, 3.56 s of
sound. Track 1 is the microphone at 1 m. The comb runs at the recording's rate.
"""
import math, os, subprocess, sys
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
R = os.path.join(HERE, "renders")
SRC = sys.argv[1] if len(sys.argv) > 1 else "/Volumes/MDAGIFT/CCB_petalonio_oriz_DO.wav"
DISTANCES = [5.0, 7.291, 10.0]        # 7.291 m = Davide's initial 22 ms
TARGET = -20.0

x, sr = sf.read(SRC, dtype="float64")
x = x[:, 0]
nz = np.nonzero(x)[0]
x = x[: nz[-1] + 1 + int(0.25 * sr)]  # the note, plus 0.25 s of the tail of silence
n = len(x)
x.astype(np.float32).tofile(os.path.join(B, "ccb.f32"))

def run(mt):
    out = os.path.join(B, "y.f64")
    subprocess.run([os.path.join(B, "lib"), str(sr), str(n), f"out={out}", f"mt={mt}", "in=file:" + os.path.join(B, "ccb.f32")],
                   check=True, capture_output=True)
    return np.fromfile(out)

def fade(y, s=0.02):
    k = int(s * sr); w = 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
    y = y.copy(); y[:k] *= w; y[-k:] *= w[::-1]; return y

def write(name, y, log, match=True):
    y = fade(y)
    g = 10 ** (TARGET / 20) / math.sqrt(float(np.mean(y ** 2))) if match else 1.0
    y = y * g
    if np.max(np.abs(y)) >= 1: raise RuntimeError(f"{name} would clip")
    sf.write(os.path.join(R, name), y, sr, subtype="PCM_24")
    log.append(f"| {name} | {20 * math.log10(math.sqrt(float(np.mean(y ** 2)))):.1f} | {20 * math.log10(g):+.2f} |")

def partials(y, f0=29.7, count=13):
    seg = y[int(0.5 * sr): int(3.0 * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))); f = np.fft.rfftfreq(len(seg), 1 / sr)
    out = []
    for h in range(1, count + 1, 2):
        m = (f > h * f0 * 0.97) & (f < h * f0 * 1.03)
        j = np.argmax(S[m]); out.append((float(f[m][j]), float(S[m][j])))
    return out

def delay_of(mt):
    mm = math.floor(mt * 1000 + 0.5) / 1000
    k = int(math.floor(mm * sr / 331.4 + 0.5))
    c = k + 2 if k & 1 else k + 1
    while any(c % d == 0 for d in range(3, int(c ** 0.5) + 1, 2)): c += 2
    return c

log = []
write("ccb_dry.wav", x, log)
px = partials(x)
rows = []
for mt in DISTANCES:
    y = run(mt)
    write(f"ccb_comb_{mt:.3f}m.wav", y, log)
    M = delay_of(mt)
    py = partials(y)
    rows.append((mt, M, [(fx, 20 * math.log10(ay / ax), 20 * math.log10(abs(2 * math.cos(math.pi * fx * M / sr))))
                         for (fx, ax), (_, ay) in zip(px, py)]))
with open(os.path.join(R, "render-log.md"), "w") as fh:
    fh.write(f"Source: track 1 (microphone at 1 m) of `{os.path.basename(SRC)}`, {sr} Hz, {n / sr:.2f} s.\n"
             f"All renders normalised to {TARGET:.0f} dBFS RMS, 20 ms fades, 24-bit.\n\n"
             "| file | RMS dBFS | gain dB |\n|---|---|---|\n" + "\n".join(log) + "\n\n"
             "Comb gain on the odd partials, measured (output re input at each partial peak, 0.5–3.0 s) against 2·|cos(π·f·M/SR)|:\n\n")
    for mt, M, parts in rows:
        fh.write(f"**{mt:.3f} m, M = {M} samples ({1000 * M / sr:.2f} ms), peaks every {sr / M:.2f} Hz**\n\n"
                 "| partial Hz | measured dB | theory dB |\n|---|---|---|\n"
                 + "\n".join(f"| {f:.1f} | {g:+.1f} | {t:+.1f} |" for f, g, t in parts) + "\n\n")
print(open(os.path.join(R, "render-log.md")).read())
