"""Sample-rate-free analog all-pass prototype.

The 2nd-order analog all-pass with centre frequency f0 and quality Q is
    H(s) = (s² - (ω0/Q)s + ω0²) / (s² + (ω0/Q)s + ω0²),  ω0 = 2π f0.
Its magnitude is unity at every ω; its phase is
    φ(ω) = -2·atan2( (ω0/Q)·ω , ω0² - ω² ).
The design lives in physical (f0, Q) units, independent of any sample rate.
The plugin realizes each section with the bilinear RBJ form in rbj.py.
"""
import numpy as np

def analog_allpass_phase(f0, Q, freqs):
    """Unwrapped phase (radians) of one analog all-pass at the given Hz points."""
    w = 2.0 * np.pi * np.asarray(freqs, dtype=float)
    w0 = 2.0 * np.pi * f0
    phase = -2.0 * np.arctan2((w0 / Q) * w, w0 * w0 - w * w)
    return np.unwrap(phase)

def cascade_phase_analog(sections, freqs):
    """Sum of analog all-pass phases for a list of (f0, Q) sections."""
    total = np.zeros(len(np.asarray(freqs, dtype=float)))
    for (f0, Q) in sections:
        total = total + analog_allpass_phase(f0, Q, freqs)
    return total
