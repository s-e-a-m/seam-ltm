import numpy as np
from analog_prototype import analog_allpass_phase, cascade_phase_analog
from rbj import cascade_phase

def test_phase_endpoints():
    """A 2nd-order analog all-pass goes 0 → -π → -2π as ω sweeps 0 → ω0 → ∞."""
    f0 = 1000.0
    assert abs(analog_allpass_phase(f0, 0.7071, np.array([1e-3]))[0]) < 1e-3
    np.testing.assert_allclose(
        analog_allpass_phase(f0, 0.7071, np.array([f0]))[0], -np.pi, atol=1e-6)
    assert analog_allpass_phase(f0, 0.7071, np.array([1e7]))[0] < -2*np.pi + 1e-2

def test_phase_is_monotonic_decreasing():
    freqs = np.geomspace(1.0, 1e6, 4096)
    ph = analog_allpass_phase(1000.0, 0.7071, freqs)
    assert np.all(np.diff(ph) <= 1e-9)

def test_analog_matches_rbj_at_low_frequency():
    """Below ~Nyquist/20 the digital RBJ phase tracks the analog prototype (bilinear warping stays negligible)."""
    fs = 48000.0
    freqs = np.geomspace(20.0, fs/20.0, 256)
    sections = [(300.0, 0.5), (3000.0, 0.4)]
    ana = cascade_phase_analog(sections, freqs)
    dig = cascade_phase(sections, fs, freqs)
    np.testing.assert_allclose(dig, ana, atol=np.radians(2.0))
