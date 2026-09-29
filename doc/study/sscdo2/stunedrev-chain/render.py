#!/usr/bin/env python3
"""render.py -- the contrabass clarinet through the four lines of sdt.stunedrev.

usage (from this folder, after ./run.sh has built the probes):
    ../../../../.venv/bin/python render.py [SOURCE.wav] [--max SECONDS] [--times T1 T2 T3 T4]

SOURCE defaults to ../delrm-comb/renders/ccb_dry.wav, committed: the low C of
a contrabass clarinet, microphone at 1 m, 96 kHz, 3.81 s. The lines run at
the source's rate, at the Pd patch's starting times (83, 47, 7, 71 ms) unless
--times is given.

Each line is rendered until its output holds 99.9 % of the input's energy
(an all-pass chain returns all of it, late) or for --max seconds (default
600). The four files share one gain, set so that the loudest peak is at
-1 dBFS, so their levels compare. The WAVs are not committed: they are
large, and this script regenerates them; render-log.md is committed.
"""
import argparse, math, os, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(HERE, "build")
R = os.path.join(HERE, "renders")
LINES = [("sq", "sqrt2", "√2"), ("ph", "phi", "φ"), ("ex", "e", "e"), ("pi", "pi", "π")]

ap = argparse.ArgumentParser()
ap.add_argument("source", nargs="?", default=os.path.join(HERE, "..", "delrm-comb", "renders", "ccb_dry.wav"))
ap.add_argument("--max", type=float, default=600.0)
ap.add_argument("--times", type=float, nargs=4, default=[83, 47, 7, 71])
a = ap.parse_args()

x, sr = sf.read(a.source, dtype="float64")
if x.ndim > 1: x = x[:, 0]
os.makedirs(R, exist_ok=True)
n = int(len(x) + a.max * sr)
np.concatenate([x, np.zeros(n - len(x))]).astype(np.float32).tofile(os.path.join(B, "src.f32"))
ein = float(np.sum(x.astype(np.float32).astype(np.float64) ** 2))

outs, rows = [], []
for (p, fname, label), ms in zip(LINES, a.times):
    f = os.path.join(B, f"render_{p}.f64")
    subprocess.run([os.path.join(B, p), str(sr), str(n), "in=file:" + os.path.join(B, "src.f32"), f"out={f}", f"ms={ms:g}"],
                   check=True)
    y = np.fromfile(f).astype(np.float32); os.remove(f)
    c = np.cumsum(y.astype(np.float64) ** 2) / ein
    end = int(np.argmax(c >= 0.999)) + 1 if c[-1] >= 0.999 else len(y)
    outs.append((fname, label, ms, y[:end], c[end - 1]))

peak = max(float(np.max(np.abs(y))) for _, _, _, y, _ in outs)
g = 10 ** (-1 / 20) / peak
sf.write(os.path.join(R, "note_dry.wav"), x, sr, subtype="PCM_24")
for fname, label, ms, y, frac in outs:
    name = f"stunedrev_{fname}_{ms:g}ms.wav"
    sf.write(os.path.join(R, name), y * g, sr, subtype="PCM_24")
    e = y.astype(np.float64) ** 2
    cen = float(np.sum(np.arange(len(y)) * e) / np.sum(e) / sr)
    rows.append(f"| {name} | {label} | {ms:g} | {len(y) / sr:.1f} | {100 * frac:.1f} % | {cen:.1f} |")

with open(os.path.join(R, "render-log.md"), "w") as fh:
    fh.write(f"Source: `{os.path.relpath(a.source, HERE)}`, {sr} Hz, {len(x) / sr:.2f} s, written again as `note_dry.wav`.\n"
             f"Each line rendered until its output holds 99.9 % of the input's energy, or {a.max:g} s.\n"
             f"One gain for the four lines, {20 * math.log10(g):+.2f} dB, loudest peak at -1 dBFS; 24-bit.\n\n"
             "| file | line | ms | length s | input energy returned | energy centroid s |\n|---|---|---|---|---|---|\n"
             + "\n".join(rows) + "\n")
print(open(os.path.join(R, "render-log.md")).read())
