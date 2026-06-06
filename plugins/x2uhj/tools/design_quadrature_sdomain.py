"""Derive the H_R/H_I quadrature pair on the analog prototype (fs-free).

Target: phase(H_I) - phase(H_R) = -90° across [F_LO, F_HI].
Each network = 3 analog all-pass sections; we optimize the 12 parameters
(f0_k, Q_k) with least-squares on log-spaced frequencies, in the s-domain,
so the result carries no sample-rate dependence.
"""
import json
import numpy as np
from scipy.optimize import least_squares
from analog_prototype import cascade_phase_analog

F_LO, F_HI = 20.0, 20000.0
N_SECTIONS = 3
TARGET = -np.pi / 2.0
freqs = np.geomspace(F_LO, F_HI, 512)

# Seed from the shipped digital design so the optimizer starts in basin.
X0 = np.array([
    141.9, 0.2019, 671.7, 0.2122, 18654.0, 0.3031,   # H_R seed
    24.0, 0.3090, 2992.0, 0.3848, 3220.0, 0.0963,     # H_I seed
])

def unpack(x):
    hr = [(x[0], x[1]), (x[2], x[3]), (x[4], x[5])]
    hi = [(x[6], x[7]), (x[8], x[9]), (x[10], x[11])]
    return hr, hi

def residuals(x):
    hr, hi = unpack(x)
    diff = cascade_phase_analog(hi, freqs) - cascade_phase_analog(hr, freqs)
    return diff - TARGET

lo = np.array([10.0, 0.01] * (2 * N_SECTIONS))
hi_b = np.array([1e6, 5.0] * (2 * N_SECTIONS))

def main():
    res = least_squares(residuals, X0, bounds=(lo, hi_b), xtol=1e-13, ftol=1e-13)
    hr, hi_sec = unpack(res.x)
    err_deg = np.degrees(np.abs(residuals(res.x)))
    out = {
        "domain": "analog", "band": [F_LO, F_HI],
        "H_R": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hr],
        "H_I": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hi_sec],
        "max_error_deg": round(float(err_deg.max()), 6),
    }
    with open("coeffs_analog.json", "w") as fp:
        json.dump(out, fp, indent=2)
    print(f"max phase error: {err_deg.max():.4f} deg -> coeffs_analog.json")

if __name__ == "__main__":
    main()
