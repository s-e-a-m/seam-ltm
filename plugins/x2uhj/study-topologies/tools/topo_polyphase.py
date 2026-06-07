"""Niemitalo first-order polyphase all-pass topology.

Each section realises H(z) = (a^2 - z^-2)/(1 - a^2 z^-2), one multiply per
section. Two paths A and B carry coefficient lists; their phase difference
targets -90 degrees. Mode "fixed" uses the published coefficients; mode
"perfs" re-optimises the coefficients at the actual sample rate.
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.signal import freqz
from topology import TARGET, band_freqs

name = "polyphase-1st"

# Published coefficients (Niemitalo; as shipped in WigWare), order 4 per path.
_FIXED_A = [0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737]
_FIXED_B = [0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278]

def section_response(a, fs, freqs):
    """Complex response of one section H(z)=(a^2 - z^-2)/(1 - a^2 z^-2)."""
    a2 = a * a
    b = [a2, 0.0, -1.0]
    den = [1.0, 0.0, -a2]
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    _, h = freqz(b, den, worN=w)
    return h

def _path_phase(coeffs, fs, freqs):
    total = np.zeros(len(freqs))
    for a in coeffs:
        total = total + np.unwrap(np.angle(section_response(a, fs, freqs)))
    return total

def phase(coeffs, fs, freqs):
    # Path B carries an extra one-sample delay relative to path A (polyphase).
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    pa = _path_phase(coeffs["A"], fs, freqs)
    pb = _path_phase(coeffs["B"], fs, freqs) - w  # z^-1 relative delay on B
    return pb - pa

def design(order, fs, mode="fixed"):
    if mode == "fixed":
        n = min(order, 4)
        return {"A": _FIXED_A[:n], "B": _FIXED_B[:n], "mode": "fixed"}
    # perfs: optimise the per-section coefficients at this fs.
    freqs = band_freqs()
    x0 = np.array(_FIXED_A[:order] + _FIXED_B[:order]) if order <= 4 else \
        np.linspace(0.4, 0.999, 2 * order)
    def split(x):
        return {"A": list(x[:order]), "B": list(x[order:]), "mode": "perfs"}
    def residuals(x):
        return phase(split(x), fs, freqs) - TARGET
    lo = np.full(2 * order, 0.01); hi = np.full(2 * order, 0.999999)
    res = least_squares(residuals, x0, bounds=(lo, hi), xtol=1e-13, ftol=1e-13)
    c = split(res.x)
    c["A"] = [round(v, 9) for v in c["A"]]
    c["B"] = [round(v, 9) for v in c["B"]]
    return c

def cost(coeffs):
    return len(coeffs["A"]) + len(coeffs["B"])
