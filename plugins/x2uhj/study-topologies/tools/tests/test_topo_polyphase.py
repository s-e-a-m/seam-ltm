import numpy as np
import topo_polyphase
from topology import band_freqs, phase_error_deg

def test_section_phase_is_allpass_unity():
    h = topo_polyphase.section_response(0.7, 48000.0, band_freqs())
    assert np.allclose(np.abs(h), 1.0, atol=1e-9)

def test_fixed_mode_returns_literature_coeffs():
    c = topo_polyphase.design(4, 48000.0, mode="fixed")
    assert len(c["A"]) == 4 and len(c["B"]) == 4
    assert abs(c["A"][0] - 0.6923878) < 1e-9
    assert abs(c["A"][3] - 0.9987488452737) < 1e-9
    assert abs(c["B"][0] - 0.4021921162426) < 1e-9


def test_fixed_mode_order_too_high_raises():
    import pytest
    with pytest.raises(ValueError):
        topo_polyphase.design(5, 48000.0, mode="fixed")

def test_cost_one_multiply_per_section():
    c = topo_polyphase.design(4, 48000.0, mode="fixed")
    assert topo_polyphase.cost(c) == 4 + 4

def test_perfs_mode_improves_quadrature_at_high_rate():
    c = topo_polyphase.design(4, 96000.0, mode="perfs")
    err = phase_error_deg(topo_polyphase.phase(c, 96000.0, band_freqs()))
    assert err < 5.0
