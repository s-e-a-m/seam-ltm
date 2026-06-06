"""Re-derive the H_R/H_I quadrature all-pass pair by minimax phase fitting.

Design target: phase(H_I) - phase(H_R) = -90 deg over [F_LO, F_HI].
Each network = 3 RBJ all-pass sections. We optimize the 12 parameters
(f_k, Q_k for 6 sections) with least-squares on log-spaced frequencies.
"""
import json
import numpy as np
from scipy.optimize import least_squares
from rbj import cascade_phase

FS = 48000.0
F_LO, F_HI = 20.0, 20000.0
N_SECTIONS = 3                      # per network
TARGET = -np.pi / 2.0              # -90 degrees

freqs = np.geomspace(F_LO, F_HI, 512)

# Initial guess seeded from the 2023 empirical table (real then imag).
X0 = np.array([
    374.1, 0.1093, 666.8, 0.4210, 17551.0, 0.5750,   # H_R seed
    35.61, 0.2571, 3723.0, 0.3464, 6786.0, 0.1200,    # H_I seed
])

def unpack(x):
    hr = [(x[0], x[1]), (x[2], x[3]), (x[4], x[5])]
    hi = [(x[6], x[7]), (x[8], x[9]), (x[10], x[11])]
    return hr, hi

def residuals(x):
    hr, hi = unpack(x)
    diff = cascade_phase(hi, FS, freqs) - cascade_phase(hr, FS, freqs)
    return diff - TARGET

# Bounds: frequencies within (0, Nyquist), Q strictly positive.
lo = np.array([10.0, 0.01] * (2 * N_SECTIONS))
hi = np.array([FS/2 - 1, 5.0] * (2 * N_SECTIONS))

res = least_squares(residuals, X0, bounds=(lo, hi), xtol=1e-12, ftol=1e-12)
hr, hi_sec = unpack(res.x)

err_deg = np.degrees(np.abs(residuals(res.x)))
midband = (freqs >= 100.0) & (freqs <= 10000.0)
print(f"max phase error: {err_deg.max():.4f} deg  (mean {err_deg.mean():.4f})")
print(f"mid-band (100Hz-10kHz) max phase error: {err_deg[midband].max():.4f} deg")

out = {
    "fs_design": FS, "band": [F_LO, F_HI],
    "H_R": [{"f": f, "Q": q} for (f, q) in hr],
    "H_I": [{"f": f, "Q": q} for (f, q) in hi_sec],
    "max_error_deg": float(err_deg.max()),
    "midband_max_error_deg": float(err_deg[midband].max()),
}
with open("coeffs.json", "w") as fp:
    json.dump(out, fp, indent=2)
print("wrote coeffs.json")
