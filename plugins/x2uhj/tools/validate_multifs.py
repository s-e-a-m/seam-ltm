"""Measure quadrature phase error of the analog design across sample rates.

The analog (f0, Q) design is fs-free; each rate realizes it with the RBJ
bilinear form. The residual error growth with fs is the bilinear warping,
quantified here at the four common audio rates.
"""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from rbj import cascade_phase

RATES = [44100.0, 48000.0, 96000.0, 192000.0]
F_LO, F_HI = 20.0, 20000.0

def _design():
    with open("coeffs_analog.json") as fp:
        d = json.load(fp)
    hr = [(s["f"], s["Q"]) for s in d["H_R"]]
    hi = [(s["f"], s["Q"]) for s in d["H_I"]]
    return hr, hi

def phase_error_table():
    hr, hi = _design()
    freqs = np.geomspace(F_LO, F_HI, 512)
    table = {}
    for fs in RATES:
        diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
        table[fs] = float(np.degrees(np.abs(diff - (-np.pi/2)).max()))
    return table

def main():
    hr, hi = _design()
    freqs = np.geomspace(F_LO, F_HI, 512)
    plt.figure(figsize=(9, 5))
    for fs in RATES:
        diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
        err = np.degrees(np.abs(diff - (-np.pi/2)))
        plt.semilogx(freqs, err, label=f"{fs/1000:.1f} kHz (max {err.max():.2f}°)")
    plt.xlabel("Hz"); plt.ylabel("|phase diff − 90°| (deg)")
    plt.title("UHJ quadrature error vs sample rate (analog design)")
    plt.legend(); plt.grid(True, which="both", alpha=0.3); plt.tight_layout()
    plt.savefig("../doc/math/figures/multifs_validation.png", dpi=120)
    print("table:", phase_error_table())

if __name__ == "__main__":
    main()
