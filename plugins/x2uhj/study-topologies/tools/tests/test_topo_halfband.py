import numpy as np
import topo_halfband
from topology import band_freqs, phase_error_deg

def test_design_returns_two_paths():
    c = topo_halfband.design(4, 48000.0)
    assert len(c["A"]) >= 1 and len(c["B"]) >= 1

def test_cost_one_multiply_per_section():
    c = topo_halfband.design(4, 48000.0)
    assert topo_halfband.cost(c) == len(c["A"]) + len(c["B"])

def test_quadrature_reasonable_midband():
    c = topo_halfband.design(4, 48000.0)
    freqs = np.geomspace(200.0, 8000.0, 256)
    err = phase_error_deg(topo_halfband.phase(c, 48000.0, freqs))
    assert err < 10.0
