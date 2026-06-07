"""Schroeder 1958 differential all-pass, illustrative historical measurement.

Schroeder produced a stereo effect from one signal using a differential
all-pass whose phase difference meanders between plus and minus 90 degrees.
This module measures that differential all-pass for context; its design goal
is decorrelation across the band, distinct from a held 90 degrees.
"""
import numpy as np
from scipy.signal import freqz

name = "schroeder-1958"

def delay_allpass_response(g, m, fs, freqs):
    """Response of a delay all-pass H(z) = (-g + z^-m)/(1 - g z^-m)."""
    b = np.zeros(m + 1); b[0] = -g; b[m] = 1.0
    a = np.zeros(m + 1); a[0] = 1.0; a[m] = -g
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    _, h = freqz(b, a, worN=w)
    return h

def design(order, fs):
    """One illustrative differential pair (a delay all-pass against a flat path)."""
    return {"g": 0.7, "m": 32}

def phase(coeffs, fs, freqs):
    """Differential phase between the all-pass path and a flat (zero-phase) path."""
    h = delay_allpass_response(coeffs["g"], coeffs["m"], fs, freqs)
    return np.unwrap(np.angle(h))

def cost(coeffs):
    return 2  # one multiply for g in numerator and denominator
