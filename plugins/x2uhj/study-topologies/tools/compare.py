"""Harness: measure each topology across sample rates and order into results.json.

For each modern topology we record, at a reference order:
  per_fs_error  : max quadrature error designed at each rate (flat)
  fixed_error   : max quadrature error of one reference-rate design at each rate (drift)
  cost          : multiplies per sample
  error_vs_order: max error at the reference rate for a sweep of order
  group_delay   : differential group delay between the two paths at REF_FS
The reference rate is 48 kHz; the reference order is 3 (RBJ) or 4 (polyphase/halfband).
"""
import json, os, sys
# Ensure study-tools dir and shared x2uhj tools (rbj, analog_prototype) are importable
# when compare.py is run directly (outside pytest which uses conftest.py for path setup).
_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.abspath(os.path.join(_HERE, "..", "..", "tools"))
for _p in (_HERE, _SHARED):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import numpy as np
import topo_rbj, topo_polyphase, topo_halfband
from topology import STD_RATES, band_freqs, phase_error_deg

REF_FS = 48000.0
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "results.json")

def _per_fs(mod, order, design_kwargs):
    out = {}
    for fs in STD_RATES:
        c = mod.design(order, fs, **design_kwargs)
        out[f"{fs:.1f}"] = round(phase_error_deg(mod.phase(c, fs, band_freqs())), 4)
    return out

def _fixed(mod, order, design_kwargs):
    cref = mod.design(order, REF_FS, **design_kwargs)
    out = {}
    for fs in STD_RATES:
        out[f"{fs:.1f}"] = round(phase_error_deg(mod.phase(cref, fs, band_freqs())), 4)
    return out

def _error_vs_order(mod, orders, design_kwargs):
    out = {}
    for o in orders:
        try:
            c = mod.design(o, REF_FS, **design_kwargs)
            out[str(o)] = round(phase_error_deg(mod.phase(c, REF_FS, band_freqs())), 4)
        except RuntimeError as exc:
            out[str(o)] = None  # convergence guard fired
    return out

def _group_delay(mod, c, fs):
    """Differential group delay between the two paths at fs, in microseconds."""
    freqs = np.geomspace(20.0, 20000.0, 64)
    diff = mod.phase(c, fs, freqs)
    omega = 2.0 * np.pi * freqs
    gd_s = -np.gradient(diff, omega)
    return {"freqs": [round(float(f), 3) for f in freqs],
            "gd_us": [round(float(g * 1e6), 4) for g in gd_s]}

# Every topology's design(order, fs, **kwargs) accepts the same call shape; extra kwargs like `mode` are topology-specific.
def _entry(mod, order, orders, design_kwargs):
    c = mod.design(order, REF_FS, **design_kwargs)
    return {
        "ref_order": order,
        "cost": mod.cost(c),
        "per_fs_error": _per_fs(mod, order, design_kwargs),
        "fixed_error": _fixed(mod, order, design_kwargs),
        "error_vs_order": _error_vs_order(mod, orders, design_kwargs),
        "group_delay": _group_delay(mod, c, REF_FS),
    }

def build():
    return {"topologies": {
        "rbj-biquad": _entry(topo_rbj, 3, [2, 3, 4], {}),
        "polyphase-1st": _entry(topo_polyphase, 4, [2, 3, 4], {"mode": "perfs"}),
        "halfband-elliptic": _entry(topo_halfband, 4, [2, 3, 4], {}),
    }}

def main():
    data = build()
    with open(OUT, "w") as fp:
        json.dump(data, fp, indent=2, sort_keys=True)
    print("wrote", os.path.abspath(OUT))

if __name__ == "__main__":
    main()
