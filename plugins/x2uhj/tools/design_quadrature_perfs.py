"""Per-rate quadrature design: the sample-rate-independent procedure.

A fixed (f, Q) set realized via the bilinear form drifts with fs, because
the bilinear warping shifts the broadband phase. The fs-free quantity is the
design procedure: fit phase(H_I) - phase(H_R) = -90° in the digital domain
at the actual sample rate. We run that fit at each standard rate and store
the resulting (f, Q) table for the plugin to select by fs.
"""
import json
import numpy as np
from scipy.optimize import least_squares
from rbj import cascade_phase

F_LO, F_HI = 20.0, 20000.0
N_SECTIONS = 3
TARGET = -np.pi / 2.0
STD_RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]

# Shared seed (Hz, Q) for all rates; the optimizer adapts per fs.
SEED = np.array([
    141.9, 0.2019, 671.7, 0.2122, 18654.0, 0.3031,   # H_R
    24.0, 0.3090, 2992.0, 0.3848, 3220.0, 0.0963,     # H_I
])

def _unpack(x):
    hr = [(x[0], x[1]), (x[2], x[3]), (x[4], x[5])]
    hi = [(x[6], x[7]), (x[8], x[9]), (x[10], x[11])]
    return hr, hi

def design_at(fs):
    freqs = np.geomspace(F_LO, F_HI, 512)
    def residuals(x):
        hr, hi = _unpack(x)
        return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs) - TARGET
    lo = np.array([10.0, 0.01] * (2 * N_SECTIONS))
    hi_b = np.array([fs / 2 - 1.0, 5.0] * (2 * N_SECTIONS))
    res = least_squares(residuals, SEED, bounds=(lo, hi_b), xtol=1e-13, ftol=1e-13)
    hr, hi_sec = _unpack(res.x)
    err = float(np.degrees(np.abs(residuals(res.x)).max()))
    return hr, hi_sec, err

def main():
    out = {"band": [F_LO, F_HI], "rates": {}}
    for fs in STD_RATES:
        hr, hi_sec, err = design_at(fs)
        out["rates"][f"{fs:.1f}"] = {
            "H_R": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hr],
            "H_I": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hi_sec],
            "max_error_deg": round(err, 6),
        }
    with open("coeffs_perfs.json", "w") as fp:
        json.dump(out, fp, indent=2)
    print("rates:", {k: v["max_error_deg"] for k, v in out["rates"].items()})

if __name__ == "__main__":
    main()
