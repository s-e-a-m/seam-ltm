import json, subprocess, sys, os
import numpy as np
from rbj import cascade_phase

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STD_RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]

def _run():
    subprocess.run([sys.executable, "design_quadrature_perfs.py"], cwd=HERE, check=True)
    with open(os.path.join(HERE, "coeffs_perfs.json")) as fp:
        return json.load(fp)

def test_one_entry_per_standard_rate():
    d = _run()
    assert set(float(k) for k in d["rates"].keys()) == set(STD_RATES)

def test_each_rate_meets_quadrature_at_its_own_fs():
    """Each per-rate design holds the 90° quadrature at the rate it was designed for."""
    d = _run()
    freqs = np.geomspace(20.0, 20000.0, 512)
    for k, entry in d["rates"].items():
        fs = float(k)
        hr = [(s["f"], s["Q"]) for s in entry["H_R"]]
        hi = [(s["f"], s["Q"]) for s in entry["H_I"]]
        diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
        err = np.degrees(np.abs(diff - (-np.pi/2)).max())
        assert err < 2.5, f"{fs}: {err:.2f} deg"   # 44.1k is the worst at ~2.04 deg

def test_determinism():
    a = _run(); b = _run()
    assert a == b

def test_max_errors_match_known_good():
    """Guard against optimizer drift on scipy/numpy upgrades."""
    d = _run()
    expected = {
        "44100.0": 2.04, "48000.0": 1.36, "88200.0": 0.53,
        "96000.0": 0.51, "176400.0": 0.43, "192000.0": 0.43,
    }
    for k, exp in expected.items():
        got = d["rates"][k]["max_error_deg"]
        assert abs(got - exp) < 0.05, f"{k}: {got} vs {exp}"
