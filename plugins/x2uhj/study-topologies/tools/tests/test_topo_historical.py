import numpy as np
import topo_historical
from topology import band_freqs

def test_delay_allpass_unity_magnitude():
    h = topo_historical.delay_allpass_response(0.7, 32, 48000.0, band_freqs())
    assert np.allclose(np.abs(h), 1.0, atol=1e-9)

def test_phase_difference_is_returned():
    c = topo_historical.design(1, 48000.0)
    d = topo_historical.phase(c, 48000.0, band_freqs())
    assert d.shape == band_freqs().shape
    assert np.all(np.isfinite(d))
