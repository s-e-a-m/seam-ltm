"""RBJ second-order biquad all-pass cascade topology.

Two cascades of order RBJ all-pass biquads, designed per sample rate by a
digital minimax phase fit (the same procedure as design_quadrature_perfs.py).
"""
import numpy as np
from scipy.optimize import least_squares
from rbj import cascade_phase
from topology import F_LO, F_HI, TARGET, band_freqs

name = "rbj-biquad"

_SEED_HR = [(141.9, 0.2019), (671.7, 0.2122), (18654.0, 0.3031),
            (60.0, 0.2), (4000.0, 0.3), (12000.0, 0.3)]
_SEED_HI = [(24.0, 0.3090), (2992.0, 0.3848), (3220.0, 0.0963),
            (120.0, 0.3), (1500.0, 0.3), (9000.0, 0.3)]

def design(order, fs):
    hr_seed = _SEED_HR[:order]; hi_seed = _SEED_HI[:order]
    x0 = np.array([v for fq in hr_seed for v in fq] +
                  [v for fq in hi_seed for v in fq])
    freqs = band_freqs()
    def unpack(x):
        hr = [(x[2*i], x[2*i+1]) for i in range(order)]
        hi = [(x[2*(order+i)], x[2*(order+i)+1]) for i in range(order)]
        return hr, hi
    def residuals(x):
        hr, hi = unpack(x)
        return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs) - TARGET
    lo = np.array([10.0, 0.01] * (2 * order))
    hi_b = np.array([fs/2 - 1.0, 5.0] * (2 * order))
    res = least_squares(residuals, x0, bounds=(lo, hi_b), xtol=1e-13, ftol=1e-13)
    hr, hi = unpack(res.x)
    return {"order": order, "H_R": [[round(f,6), round(q,6)] for f,q in hr],
            "H_I": [[round(f,6), round(q,6)] for f,q in hi]}

def phase(coeffs, fs, freqs):
    hr = [tuple(s) for s in coeffs["H_R"]]
    hi = [tuple(s) for s in coeffs["H_I"]]
    return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)

def cost(coeffs):
    return (len(coeffs["H_R"]) + len(coeffs["H_I"])) * 4
