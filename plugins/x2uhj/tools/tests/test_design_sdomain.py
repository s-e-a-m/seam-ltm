import json, subprocess, sys, os
import numpy as np
from analog_prototype import cascade_phase_analog

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def _run_design():
    subprocess.run([sys.executable, "design_quadrature_sdomain.py"],
                   cwd=HERE, check=True)
    with open(os.path.join(HERE, "coeffs_analog.json")) as fp:
        return json.load(fp)

def test_schema():
    d = _run_design()
    assert len(d["H_R"]) == 3 and len(d["H_I"]) == 3
    for s in d["H_R"] + d["H_I"]:
        assert s["f"] > 0 and s["Q"] > 0

def test_quadrature_target_met():
    """Phase difference holds near -90° across the audio band."""
    d = _run_design()
    hr = [(s["f"], s["Q"]) for s in d["H_R"]]
    hi = [(s["f"], s["Q"]) for s in d["H_I"]]
    freqs = np.geomspace(20.0, 20000.0, 512)
    diff = cascade_phase_analog(hi, freqs) - cascade_phase_analog(hr, freqs)
    err_deg = np.degrees(np.abs(diff - (-np.pi/2)))
    assert err_deg.max() < 2.0

def test_determinism():
    a = _run_design(); b = _run_design()
    assert a["H_R"] == b["H_R"] and a["H_I"] == b["H_I"]
