"""Canonical RBJ all-pass model, identical math to the C++ core."""
import numpy as np

def rbj_allpass(f, Q, fs):
    """Return (b, a) for a 2nd-order all-pass at frequency f, quality Q."""
    w = 2.0 * np.pi * f / fs
    alpha = np.sin(w) / (2.0 * Q)
    n = 1.0 + alpha
    a1 = -2.0 * np.cos(w) / n
    a2 = (1.0 - alpha) / n
    b = np.array([a2, a1, 1.0])
    a = np.array([1.0, a1, a2])
    return b, a

def cascade_phase(sections, fs, freqs):
    """Unwrapped phase (radians) of a cascade of (f,Q) all-pass sections."""
    from scipy.signal import freqz
    phase = np.zeros_like(freqs, dtype=float)
    w = 2.0 * np.pi * freqs / fs
    for (f, Q) in sections:
        b, a = rbj_allpass(f, Q, fs)
        _, h = freqz(b, a, worN=w)
        phase += np.unwrap(np.angle(h))
    return phase
