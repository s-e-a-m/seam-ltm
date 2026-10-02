#!/usr/bin/env python3
"""measure.py -- how correlated are the 16 bands of a voice fed by ONE stream?

Two bands driven by the same white noise have correlation
    r = sum(h1*h2) / sqrt(sum(h1^2) * sum(h2^2)),
the inner product of their impulse responses: exact, with no estimation
noise (a band 0.14 Hz wide would need hours of signal for a stable estimate).
fi.svf.bp is Simper's SVF: the bilinear transform, prewarped at f, of
H(s) = s / (s^2 + s/Q + 1); the model is checked against the Faust probe.
With one stream per band (64 streams) r = 0 by construction.
Run from build/ by run.sh.
"""
import os, subprocess
import numpy as np
from scipy.signal import lfilter

SR, Q = 96000, 350.0
CH = [(48, 1.0), (48, 1.01), (96, 1.1), (96, 0.9)]
N = int(80 * SR)        # 80 s: the 48 Hz band decays by 1e-15 well within it

def svf_bp(f):
    g = np.tan(np.pi * f / SR); k = 1 / Q
    # bilinear of g s'/(s'^2 + k g s' + g^2) with s' = (z-1)/(z+1)
    b = np.array([g, 0, -g]); a = np.array([1 + k * g + g * g, 2 * g * g - 2, 1 - k * g + g * g])
    return b / a[0], a / a[0]

def ir(f, n=N):
    x = np.zeros(n); x[0] = 1
    return lfilter(*svf_bp(f), x)

# 1. the model against Faust (harness output is float32)
h = ir(48, 2 * SR)
subprocess.run(["./bp48", str(SR), str(2 * SR), "in=impulse", "out=bp48.f64"], check=True)
y = np.fromfile("bp48.f64")
print(f"model vs fi.svf.bp(48, 350), 2 s of impulse response: max error {np.max(np.abs(y - h)) / np.max(np.abs(h)):.1e} of the peak")

# 2. exact r between the bands of each voice, one stream
print("\n| channel | f | a | max |r| between two bands | level of the sum, one stream vs 16 |")
print("|---|---|---|---|---|")
H = {}
for c, (f, a) in enumerate(CH):
    fr = [f * (k + 1) ** a for k in range(16)]
    hs = np.array([ir(x) for x in fr]); H[c] = (fr, hs)
    G = hs @ hs.T; d = np.sqrt(np.diag(G)); R = G / np.outer(d, d)
    off = np.abs(R[~np.eye(16, dtype=bool)])
    # variance of the sum: one stream = sum of all G; 16 streams = trace
    dl = 10 * np.log10(G.sum() / np.trace(G))
    i, j = divmod(int(np.argmax(np.abs(R - np.eye(16)))), 16)
    print(f"| {c} | {f} | {a} | {off.max():.1e} (bands {i+1} and {j+1}) | {dl:+.4f} dB |")

# 3. between channels in the original: stream k feeds band k of EVERY channel,
#    so channels c1 and c2 share exactly the pairs (band k of c1, band k of c2)
print("\n| channels | r in the original (stream k to band k everywhere) | bands that coincide |")
print("|---|---|---|")
for c1 in range(4):
    for c2 in range(c1 + 1, 4):
        (f1, h1), (f2, h2) = H[c1], H[c2]
        cross = sum(float(h1[k] @ h2[k]) for k in range(16))
        e1 = sum(float(h1[k] @ h1[k]) for k in range(16)); e2 = sum(float(h2[k] @ h2[k]) for k in range(16))
        same = [k + 1 for k in range(16) if abs(f1[k] - f2[k]) < 1e-9]
        print(f"| {c1}-{c2} | {cross / np.sqrt(e1 * e2):+.4f} | {', '.join(map(str, same)) or 'none'} |")

# 4. the probes: level of each voice, 4 against 64 streams (10 s after 20 s of pre-roll)
print("\n| channel | RMS one stream (dBFS) | RMS 16 streams (dBFS) | difference |")
print("|---|---|---|---|")
for c in range(4):
    lv = []
    for p in (f"v4_{c}", f"v64_{c}"):
        subprocess.run([f"./{p}", str(SR), str(10 * SR), "pre=20", f"out={p}.f64"], check=True)
        lv.append(20 * np.log10(np.sqrt(np.mean(np.fromfile(f"{p}.f64") ** 2))))
    print(f"| {c} | {lv[0]:.2f} | {lv[1]:.2f} | {lv[0] - lv[1]:+.2f} dB |")

# 5. how the equivalence depends on Q (the original's slider spans 10 to 1000)
print("\n| Q | max |r| within a voice (any channel) | level of the sum, one stream vs 16 (worst channel) |")
print("|---|---|---|")
for q in (10, 30, 100, 350, 1000):
    Q = float(q); worst_r = worst_l = 0.0
    for f, a in CH:
        n = int(min(80, max(2, 30 * q / (np.pi * f))) * SR)
        hs = np.array([ir(f * (k + 1) ** a, n) for k in range(16)])
        G = hs @ hs.T; d = np.sqrt(np.diag(G)); R = G / np.outer(d, d)
        worst_r = max(worst_r, np.abs(R[~np.eye(16, dtype=bool)]).max())
        worst_l = max(worst_l, 10 * np.log10(G.sum() / np.trace(G)))
    print(f"| {q} | {worst_r:.1e} | {worst_l:+.3f} dB |")
