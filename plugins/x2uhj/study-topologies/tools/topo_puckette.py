"""Puckette Pd hilbert~ topology, a fixed fourth-order all-pass pair.

Pd's hilbert~ abstraction realises the 90-degree pair as two paths, each a
cascade of two all-pass biquads, with fixed coefficients adapted from a 4X
patch by Emmanuel Favreau (circa 1982). Puckette credits Regalia for the pair
design. The coefficients are fixed and normalised, so the design ignores the
sample rate and the protected frequency band scales with it.

Two caveats frame a fair comparison.
Pd's native convention places path B about +90 degrees ahead of path A, so this
module returns phase(A) - phase(B) to align with the study's -90 degree target.
The original design holds the quadrature within about one degree over roughly
100 Hz to 12 kHz at 44.1 kHz; its error grows toward the 20 Hz and 20 kHz band
edges, so the full-band metric reports a larger number than the design band.

Each Pd biquad reads `biquad~ fb1 fb2 ff1 ff2 ff3` and realises
    H(z) = (ff1 + ff2 z^-1 + ff3 z^-2) / (1 - fb1 z^-1 - fb2 z^-2),
which is an all-pass whenever the numerator coefficients reverse the
denominator coefficients.
"""
import numpy as np
from scipy.signal import freqz

name = "puckette-hilbert"

# Path A and path B, each a list of Pd biquad~ coefficient rows [fb1, fb2, ff1, ff2, ff3].
_PATH_A = [[1.94632, -0.94657, 0.94657, -1.94632, 1.0],
           [0.83774, -0.06338, 0.06338, -0.83774, 1.0]]
_PATH_B = [[-0.02569, 0.260502, -0.260502, 0.02569, 1.0],
           [1.8685, -0.870686, 0.870686, -1.8685, 1.0]]

def biquad_response(row, fs, freqs):
    """Complex response of one Pd biquad~ row [fb1, fb2, ff1, ff2, ff3]."""
    fb1, fb2, ff1, ff2, ff3 = row
    b = [ff1, ff2, ff3]
    a = [1.0, -fb1, -fb2]
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    _, h = freqz(b, a, worN=w)
    return h

def _path_phase(rows, fs, freqs):
    total = np.zeros(len(freqs))
    for row in rows:
        total = total + np.unwrap(np.angle(biquad_response(row, fs, freqs)))
    return total

def design(order, fs, **_ignored):
    """Return the fixed Pd hilbert~ coefficients (order and fs are ignored)."""
    return {"A": [r[:] for r in _PATH_A], "B": [r[:] for r in _PATH_B]}

def phase(coeffs, fs, freqs):
    """Phase difference path A minus path B, in radians (aligned to the -90 deg target)."""
    return _path_phase(coeffs["A"], fs, freqs) - _path_phase(coeffs["B"], fs, freqs)

def cost(coeffs):
    # 4 multiplies per all-pass biquad, matching the RBJ topology's count.
    return (len(coeffs["A"]) + len(coeffs["B"])) * 4
