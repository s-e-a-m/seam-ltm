import json, subprocess, sys, os
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
from topology import STD_RATES

def _run():
    subprocess.run([sys.executable, "compare.py"], cwd=HERE, check=True)
    with open(os.path.join(HERE, "..", "results.json")) as fp:
        return json.load(fp)

def test_has_modern_topologies():
    d = _run()
    assert {"rbj-biquad", "polyphase-1st", "halfband-elliptic"} <= set(d["topologies"].keys())

def test_each_topology_has_per_rate_errors():
    d = _run()
    expected = {f"{fs:.1f}" for fs in STD_RATES}
    for t, entry in d["topologies"].items():
        assert expected <= set(entry["per_fs_error"].keys())
        for err in entry["per_fs_error"].values():
            assert isinstance(err, (int, float))

def test_group_delay_present():
    d = _run()
    for entry in d["topologies"].values():
        gd = entry["group_delay"]
        assert len(gd["freqs"]) == len(gd["gd_us"]) > 0
        assert all(isinstance(v, (int, float)) for v in gd["gd_us"])

def test_deterministic():
    assert _run() == _run()
