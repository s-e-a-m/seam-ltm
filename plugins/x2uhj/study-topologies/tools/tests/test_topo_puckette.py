import numpy as np
import topo_puckette
from topology import band_freqs, phase_error_deg

def test_each_biquad_is_allpass_unity():
    c = topo_puckette.design(2, 48000.0)
    for row in c["A"] + c["B"]:
        h = topo_puckette.biquad_response(row, 48000.0, band_freqs())
        assert np.allclose(np.abs(h), 1.0, atol=1e-6)

def test_cost_four_per_biquad():
    c = topo_puckette.design(2, 48000.0)
    assert topo_puckette.cost(c) == (2 + 2) * 4

def test_holds_quadrature_in_its_design_band_at_48k():
    """The fixed design holds the 90 degree pair within a few degrees over 100 Hz to 12 kHz."""
    c = topo_puckette.design(2, 48000.0)
    freqs = np.geomspace(100.0, 12000.0, 256)
    assert phase_error_deg(topo_puckette.phase(c, 48000.0, freqs)) < 3.0

def test_design_ignores_fs_fixed_coefficients():
    """The coefficients are fixed; design returns the same rows at any sample rate."""
    a = topo_puckette.design(2, 44100.0)
    b = topo_puckette.design(2, 192000.0)
    assert a == b
