#!/usr/bin/env python3
"""check.py -- run from build/ by run.sh.

1. Identity: sdt.stline against Davide's apf with the same delays (structure),
   and against Davide's stunedrev.dsp where the primes agree (the original).
2. Mutation: the structural check must see g = 0.7.
3. Energy: impulse responses at the starting times, 150 s at 96 kHz.
"""
import math, subprocess
import numpy as np

SR = 96000
LINES = [("sq", "√2", math.sqrt(2), 83, 17), ("ph", "φ", (1 + 5 ** 0.5) / 2, 47, 3),
         ("ex", "e", math.e, 7, 19), ("pi", "π", math.pi, 71, 7)]
# second time of each line: a time where SEAM's primes and Davide's agree
# on all 42 sections (../stunedrev-delays/)

def run(p, ms, n, inp, name):
    subprocess.run([f"./{p}", str(SR), str(n), inp, f"out={name}.f64", f"ms={ms}"], check=True)
    return np.fromfile(f"{name}.f64")

NN = 220_000
rng = np.random.default_rng(20260929)
rng.uniform(-0.5, 0.5, NN).astype(np.float32).tofile("noise.f32")
noise = "in=file:noise.f32"

print(f"Noise input, {NN} samples at 96 kHz, double. Max abs difference:\n")
print("| line | structure: `sdt.stline` against Davide's `apf`, same delays, starting time "
      "| original: against `stunedrev.dsp`, primes agreeing | original at the starting time |\n|---|---|---|---|")
for p, name, k, ms, agree in LINES:
    a = run(p, ms, NN, noise, "a"); b = run("d" + p, ms, NN, noise, "b")
    c = run(p, agree, NN, noise, "c"); d = run("o" + p, agree, NN, noise, "d")
    e = run("o" + p, ms, NN, noise, "e")
    print(f"| {name} | {np.max(np.abs(a - b)):.3g} ({ms} ms) | {np.max(np.abs(c - d)):.3g} ({agree} ms) "
          f"| {np.max(np.abs(a - e)):.3g} ({ms} ms) |")
m = run("mut", 83, NN, noise, "m"); b = run("dsq", 83, NN, noise, "b")
print(f"\nMutation (line √2, 83 ms, g = 0.7 against 1/√2): max abs difference {np.max(np.abs(m - b)):.3g}, must be > 0")

def isp(n):
    if n < 2: return False
    if n % 2 == 0: return n == 2
    return all(n % d for d in range(3, math.isqrt(n) + 1, 2))
def seam_delay(k, i, ms):
    n = int(math.floor(ms * (i + 1) * k * SR / 1000 + 0.5)); c = n + 2 if n & 1 else n + 1
    while not isp(c): c += 2
    return c

SEC = 150
print(f"\nImpulse responses at the starting times, {SEC} s. An all-pass chain returns all the energy (1);"
      " each section delays it on average by its own t, so the centroid is the sum of the 42 delays.\n")
print("| line | ms | longest section | sum of the delays | measured centroid "
      "| energy by 10 s | 30 s | 60 s | 150 s |\n|---|---|---|---|---|---|---|---|---|")
for p, name, k, ms, _ in LINES:
    h = run(p, ms, SR * SEC, "in=impulse", "ir"); c = np.cumsum(h ** 2)
    d = [seam_delay(k, i, ms) for i in range(42)]
    cen = np.sum(np.arange(len(h)) * h ** 2) / c[-1] / SR
    cen_s = f"{cen:.2f} s" if c[-1] > 0.999 else "(tail beyond 150 s)"
    print(f"| {name} | {ms} | {max(d) / SR:.2f} s | {sum(d) / SR:.1f} s | {cen_s} "
          f"| {c[10 * SR]:.3f} | {c[30 * SR]:.3f} | {c[60 * SR]:.3f} | {c[-1]:.3f} |")
print(f"\nDirect path (-g)^42 = {(1 / math.sqrt(2)) ** 42:.2e}, {20 * math.log10((1 / math.sqrt(2)) ** 42):.0f} dB")
