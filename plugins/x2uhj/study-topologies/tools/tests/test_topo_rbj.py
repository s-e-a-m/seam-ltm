import json, os
import numpy as np
import topo_rbj
from topology import band_freqs, phase_error_deg

SHARED = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools"))

def test_design_meets_quadrature_per_fs():
    for fs in (44100.0, 48000.0, 96000.0, 192000.0):
        c = topo_rbj.design(3, fs)
        err = phase_error_deg(topo_rbj.phase(c, fs, band_freqs()))
        assert err < 2.5, f"{fs}: {err:.2f}"

def test_cost_is_four_multiplies_per_biquad():
    c = topo_rbj.design(3, 48000.0)
    assert topo_rbj.cost(c) == 3 * 2 * 4

def test_matches_shipped_perrate_table_at_48k():
    with open(os.path.join(SHARED, "coeffs_perfs.json")) as fp:
        ref = json.load(fp)["rates"]["48000.0"]
    c = topo_rbj.design(3, 48000.0)
    ref_err = ref["max_error_deg"]
    got_err = phase_error_deg(topo_rbj.phase(c, 48000.0, band_freqs()))
    assert abs(got_err - ref_err) < 0.25
