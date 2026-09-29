#!/usr/bin/env python3
"""check.py -- run from build/ by run.sh.

The 16 800 delays of stunedrev at 96 kHz (4 lines, 42 sections, 1-100 ms):
sdt.stdel against an independent reference and against Davide's chain, the
sections that change at the starting times, the distinctness of the primes
under three rules, and the margin of sdt.stmd at every rate.
"""
import itertools, math
import numpy as np

SR = 96000
K = [math.sqrt(2), (1 + math.sqrt(5)) / 2, math.e, math.pi]
NAMES = ["√2", "φ", "e", "π"]
START = [83, 47, 7, 71]              # the Pd patch's starting times

N = 2_700_000                        # above the longest delay at 192 kHz
sieve = np.ones(N + 1, dtype=bool); sieve[:2] = False
for p in range(2, int(N ** 0.5) + 1):
    if sieve[p]: sieve[p * p::p] = False
primes = np.nonzero(sieve)[0]
def np_strict(n): return int(primes[np.searchsorted(primes, n, side="right")])   # SEAM sff.np
def np_geq(n):    return int(primes[np.searchsorted(primes, n, side="left")])    # smallest prime >= n
def seam_rule(x):
    n = int(math.floor(x + 0.5)); return np_strict(n) if n >= 2 else n
def exact(j, i, ms, sr=SR): return ms * (i + 1) * K[j] * sr / 1000

seam = np.fromfile("seam.f64").reshape(4, 42, 100)
dav = np.fromfile("dav.f64").reshape(4, 42, 100)

ref = np.array([[[seam_rule(exact(j, i, ms)) for ms in range(1, 101)] for i in range(42)] for j in range(4)])
print(f"sdt.stdel against an independent reference (rounding, prime > n): {int(np.sum(seam != ref))} mismatches of 16800")
diff = seam != dav
gap = np.abs(seam - dav)
print(f"sdt.stdel against Davide's chain (truncation, prime > n):        {int(np.sum(diff))} differ, "
      f"largest move {int(gap.max())} samples ({1000 * gap.max() / SR:.3f} ms)")
j, i, m = np.unravel_index(np.argmax(gap), gap.shape)
x = exact(j, i, m + 1)
print(f"  largest: line {NAMES[j]}, section {i}, {m + 1} ms: exact {x:.2f}; truncated {int(x)} -> {int(dav[j, i, m])}; "
      f"rounded {int(math.floor(x + 0.5))} (prime: {bool(sieve[int(math.floor(x + 0.5))])}) -> {int(seam[j, i, m])}")

print("\nAt the starting times (83, 47, 7, 71 ms):\n\n| line | ms | sections that change: i (Davide -> SEAM) |\n|---|---|---|")
for j in range(4):
    ms = START[j]
    ch = [f"{i} ({int(dav[j, i, ms - 1])} -> {int(seam[j, i, ms - 1])})" for i in range(42) if diff[j, i, ms - 1]]
    print(f"| {NAMES[j]} | {ms} | {', '.join(ch) if ch else 'none'} |")

before = [int(math.floor(exact(0, i, 83) + 0.5)) for i in range(42)]
after = [int(seam[0, i, 82]) for i in range(42)]
share = lambda L: sum(math.gcd(a, b) > 1 for a, b in itertools.combinations(L, 2))
print(f"\nLine √2 at 83 ms: pairs sharing a factor before the prime {share(before)} of 861, after {share(after)}; "
      f"distinct primes {len(set(after))}")

rules = {"SEAM: round, then prime > n": lambda x: seam_rule(x),
         "Davide: truncate, then prime > n": lambda x: np_strict(int(x)),
         "round, then prime >= n": lambda x: np_geq(int(math.floor(x + 0.5)))}
print("\n| rule | duplicates within a line | slider pairs of two lines sharing a prime |\n|---|---|---|")
for name, f in rules.items():
    S = [[set() for _ in range(100)] for _ in range(4)]; within = 0
    for jj in range(4):
        for ms in range(1, 101):
            v = [f(exact(jj, ii, ms)) for ii in range(42)]
            within += len(v) - len(set(v)); S[jj][ms - 1] = set(v)
    cross = sum(bool(S[a][x] & S[b][y]) for a, b in itertools.combinations(range(4), 2) for x in range(100) for y in range(100))
    print(f"| {name} | {within} | {cross} of 60000 ({100 * cross / 60000:.2f}%) |")
spacing = 1 * math.sqrt(2) * SR / 1000
pl = primes[primes < 1_300_000]
print(f"\nNearest sections: {spacing:.1f} samples apart (1 ms, √2, 96 kHz); largest prime gap below 1.3 M: {int(np.max(np.diff(pl)))}")

worst = -10 ** 9
for sr in (44100, 48000, 88200, 96000, 176400, 192000):
    for jj in range(4):
        for ii in range(42):
            md = int(100 * (ii + 1) * K[jj] * sr / 1000) + 150      # sdt.stmd at this rate
            for ms in range(1, 101):
                worst = max(worst, (seam_rule(exact(jj, ii, ms, sr)) - 1) - md)   # apfv reads t - 1
pb = primes[primes < 2_533_393 + 200]
print(f"sdt.stmd margin: largest prime gap below the longest delay at 192 kHz {int(np.max(np.diff(pb)))}; "
      f"worst (needed - buffer) over every rate, time and section: {worst} (must be <= 0)")
