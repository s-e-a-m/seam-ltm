#!/usr/bin/env python3
"""check.py -- run from build/ by run.sh.

Impulse responses of the five all-pass forms, 42 sections, line e at 7 ms,
96 kHz, 60 s (the whole energy of the line: its centroid is at 17.2 s).
"""
import subprocess
import numpy as np

SR, SEC = 96000, 60
N = SR * SEC

def ir(name):
    subprocess.run([f"./{name}", str(SR), str(N), "in=impulse", f"out={name}.f64"], check=True)
    return np.fromfile(f"{name}.f64")

ref = ir("moorer_double")
print(f"reference: sjm.apfv chain, energy {np.sum(ref**2):.9f} (all-pass: 1), "
      f"centroid {np.sum(np.arange(N) * ref**2) / np.sum(ref**2) / SR:.2f} s\n")
print("| form | max abs difference from sjm.apfv (double) |\n|---|---|")
for name, label in (("dav_double", "Davide's `apf`"), ("std_double", "`fi.allpass_comb(md,t,-g)`"),
                    ("sch_double", "`sms.apfv` (Schroeder)"), ("inphi_double", "in-phi-rev `apf` (Schroeder)"),
                    ("mut_double", "mutation: `sjm.apfv` with -g")):
    print(f"| {label} | {np.max(np.abs(ir(name) - ref)):.3g} |")

def spec(h):
    H = np.abs(np.fft.rfft(h)); f = np.fft.rfftfreq(len(h), 1 / SR)
    return H[(f >= 20) & (f <= 20000)]

# float against double of the same form: the truncation at 60 s cancels out
print("\n| form | float: max abs difference from its double | energy lost in float | max abs spectrum dev, dB |\n|---|---|---|---|")
for form in ("moorer", "sch"):
    hd, hs = ir(form + "_double"), ir(form + "_single")
    d = 20 * np.log10(spec(hs) / spec(hd))
    print(f"| {form} | {np.max(np.abs(hs - hd)):.3g} | {np.sum(hd**2) - np.sum(hs**2):+.3g} | {np.max(np.abs(d)):.3g} |")
