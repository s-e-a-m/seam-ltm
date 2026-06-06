import numpy as np
from scipy.signal import freqz
from rbj import rbj_allpass, cascade_phase

def test_allpass_magnitude_is_unity():
    """An all-pass section has unit magnitude at every frequency."""
    b, a = rbj_allpass(1000.0, 0.7071, 48000.0)
    w = np.linspace(0, np.pi, 2048, endpoint=False)
    _, h = freqz(b, a, worN=w)
    assert np.allclose(np.abs(h), 1.0, atol=1e-9)

def test_golden_coefficients():
    """Coefficients match an independent hand computation (f=1k, Q=1/√2, fs=48k)."""
    b, a = rbj_allpass(1000.0, 0.70710678, 48000.0)
    assert a[0] == 1.0
    np.testing.assert_allclose(a[1], -1.815341, atol=1e-5)
    np.testing.assert_allclose(a[2],  0.831006, atol=1e-5)
    # All-pass symmetry: b = reverse(a).
    np.testing.assert_allclose(b, a[::-1], atol=1e-12)

def test_cascade_phase_is_sum():
    """Cascade phase equals the sum of section phases at every frequency."""
    freqs = np.geomspace(20.0, 20000.0, 64)
    one = cascade_phase([(1000.0, 0.7071)], 48000.0, freqs)
    two = cascade_phase([(1000.0, 0.7071), (1000.0, 0.7071)], 48000.0, freqs)
    np.testing.assert_allclose(two, 2.0 * one, atol=1e-9)
