"""Shared constants and helpers for the quadrature topology study.

Each topology module exposes:
  design(order, fs) -> dict        # JSON-serialisable coefficients
  phase(coeffs, fs, freqs) -> array  # phase(path_I) - phase(path_R), radians
  cost(coeffs) -> int              # multiplies per sample
The quadrature target is a phase difference of -90 degrees across the band.
"""
import numpy as np

STD_RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]
F_LO, F_HI = 20.0, 20000.0
TARGET = -np.pi / 2.0

def band_freqs(n=512):
    """Log-spaced evaluation frequencies across the audio band."""
    return np.geomspace(F_LO, F_HI, n)

def phase_error_deg(diff):
    """Maximum absolute deviation of a phase difference from the -90 degree target."""
    return float(np.degrees(np.abs(np.asarray(diff) - TARGET).max()))
