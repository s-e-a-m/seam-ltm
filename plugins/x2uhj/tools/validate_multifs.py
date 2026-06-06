"""Contrast a fixed coefficient set against the per-rate table across sample rates.

The shipped design fixes (f, Q) at 48 kHz; realized via the bilinear form at
other rates its quadrature drifts. The per-rate table re-runs the fit at each
rate, so its quadrature stays flat. This is the central result of the
sample-rate-independence claim.
"""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from rbj import cascade_phase

RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]
F_LO, F_HI = 20.0, 20000.0

def _sections(entry):
    return ([(s["f"], s["Q"]) for s in entry["H_R"]],
            [(s["f"], s["Q"]) for s in entry["H_I"]])

def _max_err(hr, hi, fs, freqs):
    diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
    return float(np.degrees(np.abs(diff - (-np.pi / 2)).max()))

def fixed_set_errors():
    """Max quadrature error of the shipped fixed 48k coefficient set at each rate."""
    with open("coeffs.json") as fp:
        hr, hi = _sections(json.load(fp))
    freqs = np.geomspace(F_LO, F_HI, 512)
    return {fs: _max_err(hr, hi, fs, freqs) for fs in RATES}

def per_rate_errors():
    """Max quadrature error of the per-rate table, each entry at its own rate."""
    with open("coeffs_perfs.json") as fp:
        table = json.load(fp)["rates"]
    freqs = np.geomspace(F_LO, F_HI, 512)
    out = {}
    for fs in RATES:
        hr, hi = _sections(table[f"{fs:.1f}"])
        out[fs] = _max_err(hr, hi, fs, freqs)
    return out

def main():
    fixed, per = fixed_set_errors(), per_rate_errors()
    rates = sorted(RATES)
    plt.figure(figsize=(9, 5))
    plt.plot([r / 1000 for r in rates], [fixed[r] for r in rates], "o-", label="fixed 48k set (drifts)")
    plt.plot([r / 1000 for r in rates], [per[r] for r in rates], "s-", label="per-rate table (flat)")
    plt.xlabel("sample rate (kHz)"); plt.ylabel("max |phase diff - 90 deg| (deg)")
    plt.title("UHJ quadrature: fixed coefficients versus per-rate design")
    plt.legend(); plt.grid(True, alpha=0.3); plt.tight_layout()
    plt.savefig("../doc/math/figures/multifs_validation.png", dpi=120)
    print("fixed:", fixed)
    print("per-rate:", per)

if __name__ == "__main__":
    main()
