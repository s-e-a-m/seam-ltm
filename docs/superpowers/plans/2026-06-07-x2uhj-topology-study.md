# Quadrature All-Pass Topology Study — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compare wideband-90° quadrature all-pass topologies (RBJ biquad cascade, Niemitalo first-order polyphase, Harris-Berdahl-Abel half-band) on one harness across four metrics, producing a standalone study document that informs a future quadrature plugin.

**Architecture:** Each topology implements a uniform Python interface (`design`, `phase`, `cost`) in its own module under `plugins/x2uhj/study-topologies/tools/`. A harness `compare.py` sweeps topologies × sample rates × order into a deterministic `results.json`; `plots.py` renders figures; a LaTeX document compiles them into a study PDF. Shared math reuses the reviewed `plugins/x2uhj/tools/` modules.

**Tech Stack:** Python 3.14 (numpy, scipy, matplotlib, pytest) in `plugins/x2uhj/tools/.venv`; LaTeX via `latexmk`/`pdflatex`.

**Writing rules (every `.tex` and `.md` prose line):** one sentence per line; affirmative explanatory voice (avoid "not", "rather than", "without").

**Spec:** `docs/superpowers/specs/2026-06-07-x2uhj-topology-study-design.md`

**Conventions:**
- Branch `docs/x2uhj-uhj-math` is already checked out. Commit after each task.
- Python via the existing venv: `cd plugins/x2uhj/tools && .venv/bin/python ...` (one venv serves both directories).
- Study tests run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest -v`.
- End commit messages with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Shared modules (already present, reused)
- `plugins/x2uhj/tools/rbj.py`: `rbj_allpass(f, Q, fs)`, `cascade_phase(sections, fs, freqs)`.
- `plugins/x2uhj/tools/analog_prototype.py`: `analog_allpass_phase(f0, Q, freqs)`, `cascade_phase_analog(sections, freqs)`.
- `plugins/x2uhj/tools/coeffs_perfs.json`: the per-rate RBJ table the study's RBJ topology must reproduce.

## Conventions for the interface
- `order` means **all-pass sections per path** (both paths use the same count). Each topology has two paths whose phase difference targets −90°.
- `design(order, fs)` returns a topology-specific `dict` (JSON-serialisable).
- `phase(coeffs, fs, freqs)` returns the **phase difference** `phase(path_I) − phase(path_R)` in radians (numpy array).
- `cost(coeffs)` returns multiplies per sample (int).
- `STD_RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]`; band `F_LO, F_HI = 20.0, 20000.0`; `TARGET = -pi/2`.

---

## File structure

| Path | Responsibility |
|---|---|
| `plugins/x2uhj/study-topologies/tools/topology.py` | shared constants, `phase_error_deg()` helper, `STD_RATES` |
| `plugins/x2uhj/study-topologies/tools/conftest.py` | put study tools dir and `../../tools` on sys.path |
| `plugins/x2uhj/study-topologies/tools/topo_rbj.py` | RBJ second-order biquad cascade topology |
| `plugins/x2uhj/study-topologies/tools/topo_polyphase.py` | Niemitalo first-order polyphase (fixed + per-fs) |
| `plugins/x2uhj/study-topologies/tools/topo_halfband.py` | elliptic half-band → Hilbert topology |
| `plugins/x2uhj/study-topologies/tools/topo_historical.py` | Schroeder 1958 differential all-pass (illustrative) |
| `plugins/x2uhj/study-topologies/tools/compare.py` | harness → `results.json` |
| `plugins/x2uhj/study-topologies/tools/plots.py` | figures from `results.json` |
| `plugins/x2uhj/study-topologies/tools/tests/` | tests per module + harness |
| `plugins/x2uhj/study-topologies/README.md` | scope: scripts, how to run, how it fits |
| `plugins/x2uhj/study-topologies/doc/topology-study.tex` | the study document |
| `plugins/x2uhj/study-topologies/doc/figures/` | generated figures |
| `plugins/x2uhj/study-topologies/results.json` | generated, deterministic |

---

## Task 1: Scaffold and shared interface helpers

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/topology.py`
- Create: `plugins/x2uhj/study-topologies/tools/conftest.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_topology.py`

- [ ] **Step 1: Write conftest for imports**

Create `plugins/x2uhj/study-topologies/tools/conftest.py`:
```python
# Allow importing study tools and the shared x2uhj tools (rbj, analog_prototype).
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "tools")))
```

- [ ] **Step 2: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_topology.py`:
```python
import numpy as np
from topology import STD_RATES, F_LO, F_HI, TARGET, phase_error_deg

def test_constants():
    assert STD_RATES[0] == 44100.0 and STD_RATES[-1] == 192000.0
    assert (F_LO, F_HI) == (20.0, 20000.0)
    assert abs(TARGET + np.pi/2) < 1e-12

def test_phase_error_deg_zero_when_on_target():
    freqs = np.geomspace(F_LO, F_HI, 64)
    diff = np.full_like(freqs, TARGET)
    assert phase_error_deg(diff) < 1e-9

def test_phase_error_deg_reports_max_deviation():
    freqs = np.geomspace(F_LO, F_HI, 64)
    diff = np.full_like(freqs, TARGET)
    diff[10] = TARGET + np.radians(3.0)
    assert abs(phase_error_deg(diff) - 3.0) < 1e-6
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topology.py -v`
Expected: FAIL (`ModuleNotFoundError: topology`).

- [ ] **Step 4: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/topology.py`:
```python
"""Shared constants and helpers for the quadrature topology study.

Each topology module exposes:
  design(order, fs) -> dict        # JSON-serialisable coefficients
  phase(coeffs, fs, freqs) -> array  # phase(path_I) - phase(path_R), radians
  cost(coeffs) -> int              # multiplies per sample
The quadrature target is a phase difference of -90 degrees across the band.
"""
import numpy as np

STD_RATES = [44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0]
F_LO, F_HI = 20.0, 20000.0
TARGET = -np.pi / 2.0

def band_freqs(n=512):
    """Log-spaced evaluation frequencies across the audio band."""
    return np.geomspace(F_LO, F_HI, n)

def phase_error_deg(diff):
    """Maximum absolute deviation of a phase difference from the -90 degree target."""
    return float(np.degrees(np.abs(np.asarray(diff) - TARGET).max()))
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topology.py -v`
Expected: 3 passed.

- [ ] **Step 6: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/topology.py plugins/x2uhj/study-topologies/tools/conftest.py plugins/x2uhj/study-topologies/tools/tests/test_topology.py
git commit -m "feat(topology-study): scaffold and shared interface helpers

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: RBJ second-order biquad cascade topology

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/topo_rbj.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_topo_rbj.py`

Background: this topology reuses the per-rate digital minimax fit already validated in `plugins/x2uhj/tools/design_quadrature_perfs.py`. For `order == 3` at a standard rate it must reproduce the numbers in `plugins/x2uhj/tools/coeffs_perfs.json`. An RBJ all-pass biquad costs 4 multiplies (`a2*x + a1*x1 + x2 - a1*y1 - a2*y2`, the `1.0*x2` term is free).

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_topo_rbj.py`:
```python
import json, os
import numpy as np
import topo_rbj
from topology import band_freqs, phase_error_deg

SHARED = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

def test_design_meets_quadrature_per_fs():
    for fs in (44100.0, 48000.0, 96000.0, 192000.0):
        c = topo_rbj.design(3, fs)
        err = phase_error_deg(topo_rbj.phase(c, fs, band_freqs()))
        assert err < 2.5, f"{fs}: {err:.2f}"

def test_cost_is_four_multiplies_per_biquad():
    c = topo_rbj.design(3, 48000.0)
    # 3 sections per path, 2 paths, 4 multiplies each.
    assert topo_rbj.cost(c) == 3 * 2 * 4

def test_matches_shipped_perrate_table_at_48k():
    """The RBJ topology reproduces the validated per-rate table within tolerance."""
    with open(os.path.join(SHARED, "coeffs_perfs.json")) as fp:
        ref = json.load(fp)["rates"]["48000.0"]
    c = topo_rbj.design(3, 48000.0)
    ref_err = ref["max_error_deg"]
    got_err = phase_error_deg(topo_rbj.phase(c, 48000.0, band_freqs()))
    assert abs(got_err - ref_err) < 0.25
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_rbj.py -v`
Expected: FAIL (`ModuleNotFoundError: topo_rbj`).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/topo_rbj.py`:
```python
"""RBJ second-order biquad all-pass cascade topology.

Two cascades of order RBJ all-pass biquads, designed per sample rate by a
digital minimax phase fit (the same procedure as design_quadrature_perfs.py).
"""
import numpy as np
from scipy.optimize import least_squares
from rbj import cascade_phase
from topology import F_LO, F_HI, TARGET, band_freqs

name = "rbj-biquad"

# Seed (Hz, Q) pairs spanning the band, repeated/truncated to the requested order.
_SEED_HR = [(141.9, 0.2019), (671.7, 0.2122), (18654.0, 0.3031),
            (60.0, 0.2), (4000.0, 0.3), (12000.0, 0.3)]
_SEED_HI = [(24.0, 0.3090), (2992.0, 0.3848), (3220.0, 0.0963),
            (120.0, 0.3), (1500.0, 0.3), (9000.0, 0.3)]

def design(order, fs):
    hr_seed = _SEED_HR[:order]; hi_seed = _SEED_HI[:order]
    x0 = np.array([v for fq in hr_seed for v in fq] +
                  [v for fq in hi_seed for v in fq])
    freqs = band_freqs()
    def unpack(x):
        hr = [(x[2*i], x[2*i+1]) for i in range(order)]
        hi = [(x[2*(order+i)], x[2*(order+i)+1]) for i in range(order)]
        return hr, hi
    def residuals(x):
        hr, hi = unpack(x)
        return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs) - TARGET
    lo = np.array([10.0, 0.01] * (2 * order))
    hi_b = np.array([fs/2 - 1.0, 5.0] * (2 * order))
    res = least_squares(residuals, x0, bounds=(lo, hi_b), xtol=1e-13, ftol=1e-13)
    hr, hi = unpack(res.x)
    return {"order": order, "H_R": [[round(f,6), round(q,6)] for f,q in hr],
            "H_I": [[round(f,6), round(q,6)] for f,q in hi]}

def phase(coeffs, fs, freqs):
    hr = [tuple(s) for s in coeffs["H_R"]]
    hi = [tuple(s) for s in coeffs["H_I"]]
    return cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)

def cost(coeffs):
    return (len(coeffs["H_R"]) + len(coeffs["H_I"])) * 4
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_rbj.py -v`
Expected: 3 passed. If `test_matches_shipped_perrate_table_at_48k` is just outside tolerance, report the two numbers; do NOT loosen beyond 0.25° without reporting.

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/topo_rbj.py plugins/x2uhj/study-topologies/tools/tests/test_topo_rbj.py
git commit -m "feat(topology-study): RBJ biquad cascade topology

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Niemitalo first-order polyphase topology

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/topo_polyphase.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_topo_polyphase.py`

Background: the section is `H(z) = (a^2 - z^-2)/(1 - a^2 z^-2)`, one multiply per section (a^2 precomputed). Two paths A and B carry the stored coefficient lists; the phase difference targets −90°. The literature/WigWare fixed coefficients (order 4 per path) are:
- path A (`F1`): 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737
- path B (`F2`): 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278

Two modes: `design(order, fs, mode="fixed")` returns the literature coefficients (order capped at 4); `mode="perfs"` re-optimises the per-section coefficients with a minimax fit at the actual fs.

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_topo_polyphase.py`:
```python
import numpy as np
import topo_polyphase
from topology import band_freqs, phase_error_deg

def test_section_phase_is_allpass_unity():
    """One first-order polyphase section has unit magnitude (phase-only)."""
    h = topo_polyphase.section_response(0.7, 48000.0, band_freqs())
    assert np.allclose(np.abs(h), 1.0, atol=1e-9)

def test_fixed_mode_returns_literature_coeffs():
    c = topo_polyphase.design(4, 48000.0, mode="fixed")
    assert abs(c["A"][0] - 0.6923878) < 1e-9
    assert abs(c["B"][0] - 0.4021921162426) < 1e-9

def test_cost_one_multiply_per_section():
    c = topo_polyphase.design(4, 48000.0, mode="fixed")
    assert topo_polyphase.cost(c) == 4 + 4

def test_perfs_mode_improves_quadrature_at_high_rate():
    """Per-fs redesign reaches a small error at 96 kHz."""
    c = topo_polyphase.design(4, 96000.0, mode="perfs")
    err = phase_error_deg(topo_polyphase.phase(c, 96000.0, band_freqs()))
    assert err < 5.0
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_polyphase.py -v`
Expected: FAIL (`ModuleNotFoundError: topo_polyphase`).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/topo_polyphase.py`:
```python
"""Niemitalo first-order polyphase all-pass topology.

Each section realises H(z) = (a^2 - z^-2)/(1 - a^2 z^-2), one multiply per
section. Two paths A and B carry coefficient lists; their phase difference
targets -90 degrees. Mode "fixed" uses the published coefficients; mode
"perfs" re-optimises the coefficients at the actual sample rate.
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.signal import freqz
from topology import TARGET, band_freqs

name = "polyphase-1st"

# Published coefficients (Niemitalo; as shipped in WigWare), order 4 per path.
_FIXED_A = [0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737]
_FIXED_B = [0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278]

def section_response(a, fs, freqs):
    """Complex response of one section H(z)=(a^2 - z^-2)/(1 - a^2 z^-2)."""
    a2 = a * a
    b = [a2, 0.0, -1.0]
    den = [1.0, 0.0, -a2]
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    _, h = freqz(b, den, worN=w)
    return h

def _path_phase(coeffs, fs, freqs):
    total = np.zeros(len(freqs))
    for a in coeffs:
        total = total + np.unwrap(np.angle(section_response(a, fs, freqs)))
    return total

def phase(coeffs, fs, freqs):
    # Path B carries an extra one-sample delay relative to path A (polyphase).
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    pa = _path_phase(coeffs["A"], fs, freqs)
    pb = _path_phase(coeffs["B"], fs, freqs) - w  # z^-1 relative delay on B
    return pb - pa

def design(order, fs, mode="fixed"):
    if mode == "fixed":
        n = min(order, 4)
        return {"A": _FIXED_A[:n], "B": _FIXED_B[:n], "mode": "fixed"}
    # perfs: optimise the per-section coefficients at this fs.
    freqs = band_freqs()
    x0 = np.array(_FIXED_A[:order] + _FIXED_B[:order]) if order <= 4 else \
        np.linspace(0.4, 0.999, 2 * order)
    def split(x):
        return {"A": list(x[:order]), "B": list(x[order:]), "mode": "perfs"}
    def residuals(x):
        return phase(split(x), fs, freqs) - TARGET
    lo = np.full(2 * order, 0.01); hi = np.full(2 * order, 0.999999)
    res = least_squares(residuals, x0, bounds=(lo, hi), xtol=1e-13, ftol=1e-13)
    c = split(res.x)
    c["A"] = [round(v, 9) for v in c["A"]]
    c["B"] = [round(v, 9) for v in c["B"]]
    return c

def cost(coeffs):
    return len(coeffs["A"]) + len(coeffs["B"])
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_polyphase.py -v`
Expected: 4 passed. If `test_perfs_mode_improves_quadrature_at_high_rate` exceeds 5°, report the actual error and the optimiser result; the polyphase phase model (including the z^-1 relative delay) may need the relative-delay sign checked before loosening anything.

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/topo_polyphase.py plugins/x2uhj/study-topologies/tools/tests/test_topo_polyphase.py
git commit -m "feat(topology-study): Niemitalo first-order polyphase topology

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Half-band → Hilbert topology (Harris-Berdahl-Abel)

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/topo_halfband.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_topo_halfband.py`

Background: an elliptic half-band low-pass decomposes into two parallel all-pass branches; rotating to a Hilbert transformer yields a quadrature pair realised as first-order polyphase sections (same `section_response` as Task 3, so reuse it). This task derives the branch coefficients from an elliptic prototype with `scipy.signal.ellip`, then reuses the polyphase phase/cost. This topology carries implementation risk (per the spec); if the numeric target is missed, report DONE_WITH_CONCERNS with numbers and the study proceeds with RBJ and polyphase.

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_topo_halfband.py`:
```python
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
    """The half-band design holds quadrature in the mid band at 48 kHz."""
    c = topo_halfband.design(4, 48000.0)
    freqs = np.geomspace(200.0, 8000.0, 256)
    err = phase_error_deg(topo_halfband.phase(c, 48000.0, freqs))
    assert err < 10.0
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_halfband.py -v`
Expected: FAIL (`ModuleNotFoundError: topo_halfband`).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/topo_halfband.py`:
```python
"""Half-band derived Hilbert transformer topology (Harris-Berdahl-Abel).

An elliptic half-band low-pass has poles that split into two parallel all-pass
branches; the branch pole radii become the first-order polyphase section
coefficients. The Hilbert pair reuses the polyphase section model from
topo_polyphase. This is the published elliptic route; the exact pole split is
derived numerically here.
"""
import numpy as np
from scipy.signal import ellip, tf2zpk
from topo_polyphase import phase as _poly_phase, cost as _poly_cost, section_response  # noqa: F401
from topology import band_freqs

name = "halfband-elliptic"

def design(order, fs):
    # Elliptic half-band prototype; order sets the number of all-pass sections.
    n = 2 * order + 1                      # odd order gives a clean allpass split
    b, a = ellip(n, 0.1, 60.0, 0.5)        # half-band cutoff at 0.5*Nyquist
    z, p, k = tf2zpk(b, a)
    # Poles lie inside the unit circle; pair them by imaginary sign into branches.
    pos = sorted([abs(pp) for pp in p if pp.imag >= 0], reverse=True)
    # Map pole radii to section coefficients; split alternately into A and B.
    coeffs = [r for r in pos if r < 0.999999][:2 * order]
    A = coeffs[0::2][:order]
    B = coeffs[1::2][:order]
    return {"A": [round(v, 9) for v in A], "B": [round(v, 9) for v in B]}

def phase(coeffs, fs, freqs):
    return _poly_phase(coeffs, fs, freqs)

def cost(coeffs):
    return _poly_cost(coeffs)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_halfband.py -v`
Expected: 3 passed. If `test_quadrature_reasonable_midband` fails, the elliptic pole split needs adjustment (interlacing order, or rotating the prototype to the Hilbert band). Iterate ONCE on the split; if it still misses, report DONE_WITH_CONCERNS with the achieved mid-band error and proceed — the spec accepts a documented partial result for this topology.

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/topo_halfband.py plugins/x2uhj/study-topologies/tools/tests/test_topo_halfband.py
git commit -m "feat(topology-study): elliptic half-band Hilbert topology

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: Historical Schroeder 1958 illustrative topology

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/topo_historical.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_topo_historical.py`

Background: Schroeder's 1958 artificial-stereo effect uses a differential all-pass whose phase difference is a meander jumping between ±90° at multiples of 1/(2τ), where τ is a delay. This module models that differential all-pass for one illustrative measurement, framed as a different design goal (decorrelation, not a held 90°). One delay-based all-pass: `H(z) = (-g + z^-m)/(1 - g z^-m)`.

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_topo_historical.py`:
```python
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_historical.py -v`
Expected: FAIL (`ModuleNotFoundError: topo_historical`).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/topo_historical.py`:
```python
"""Schroeder 1958 differential all-pass, illustrative historical measurement.

Schroeder produced a stereo effect from one signal using a differential
all-pass whose phase difference meanders between plus and minus 90 degrees.
This module measures that differential all-pass for context; its design goal
is decorrelation across the band, distinct from a held 90 degrees.
"""
import numpy as np
from scipy.signal import freqz

name = "schroeder-1958"

def delay_allpass_response(g, m, fs, freqs):
    """Response of a delay all-pass H(z) = (-g + z^-m)/(1 - g z^-m)."""
    b = np.zeros(m + 1); b[0] = -g; b[m] = 1.0
    a = np.zeros(m + 1); a[0] = 1.0; a[m] = -g
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    _, h = freqz(b, a, worN=w)
    return h

def design(order, fs):
    # One illustrative differential pair: a direct path and a delayed all-pass.
    return {"g": 0.7, "m": 32}

def phase(coeffs, fs, freqs):
    h = delay_allpass_response(coeffs["g"], coeffs["m"], fs, freqs)
    # Differential phase between the all-pass path and a flat (zero-phase) path.
    return np.unwrap(np.angle(h))

def cost(coeffs):
    return 2  # one multiply for g in numerator and denominator
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_topo_historical.py -v`
Expected: 2 passed.

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/topo_historical.py plugins/x2uhj/study-topologies/tools/tests/test_topo_historical.py
git commit -m "feat(topology-study): Schroeder 1958 illustrative historical topology

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Comparison harness

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/compare.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_compare.py`

Background: the harness measures the modern topologies across rates and order, in fixed and per-fs modes, plus cost and group delay, into `results.json` (deterministic).

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_compare.py`:
```python
import json, subprocess, sys, os
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def _run():
    subprocess.run([sys.executable, "compare.py"], cwd=HERE, check=True)
    with open(os.path.join(HERE, "..", "results.json")) as fp:
        return json.load(fp)

def test_has_modern_topologies():
    d = _run()
    assert {"rbj-biquad", "polyphase-1st", "halfband-elliptic"} <= set(d["topologies"].keys())

def test_each_topology_has_per_rate_errors():
    d = _run()
    for t, entry in d["topologies"].items():
        assert set(str(r) for r in entry["per_fs_error"].keys())  # non-empty
        for err in entry["per_fs_error"].values():
            assert isinstance(err, (int, float))

def test_group_delay_present():
    d = _run()
    for entry in d["topologies"].values():
        gd = entry["group_delay"]
        assert len(gd["freqs"]) == len(gd["gd_us"]) > 0

def test_deterministic():
    assert _run() == _run()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_compare.py -v`
Expected: FAIL (`compare.py` missing).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/compare.py`:
```python
"""Harness: measure each topology across sample rates and order into results.json.

For each modern topology we record, at a reference order:
  per_fs_error  : max quadrature error designed at each rate (flat)
  fixed_error   : max quadrature error of one reference-rate design at each rate (drift)
  cost          : multiplies per sample
  error_vs_order: max error at the reference rate for a sweep of order
The reference rate is 48 kHz; the reference order is 3 (RBJ) or 4 (polyphase/halfband).
"""
import json, os
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
        c = mod.design(o, REF_FS, **design_kwargs)
        out[str(o)] = round(phase_error_deg(mod.phase(c, REF_FS, band_freqs())), 4)
    return out

def _group_delay(mod, order, design_kwargs):
    """Differential group delay between the two paths at REF_FS, in microseconds.

    The group delay is -d(phase difference)/d(omega); it shows how the 90-degree
    relationship disperses in time across frequency. Stored on a coarse grid.
    """
    freqs = np.geomspace(20.0, 20000.0, 64)
    c = mod.design(order, REF_FS, **design_kwargs)
    diff = mod.phase(c, REF_FS, freqs)
    omega = 2.0 * np.pi * freqs
    gd_s = -np.gradient(diff, omega)            # seconds
    return {"freqs": [round(float(f), 3) for f in freqs],
            "gd_us": [round(float(g * 1e6), 4) for g in gd_s]}

def _entry(mod, order, orders, design_kwargs):
    c = mod.design(order, REF_FS, **design_kwargs)
    return {
        "ref_order": order,
        "cost": mod.cost(c),
        "per_fs_error": _per_fs(mod, order, design_kwargs),
        "fixed_error": _fixed(mod, order, design_kwargs),
        "error_vs_order": _error_vs_order(mod, orders, design_kwargs),
        "group_delay": _group_delay(mod, order, design_kwargs),
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_compare.py -v`
Expected: 3 passed. Report the resulting `per_fs_error` and `fixed_error` dicts for each topology (the central result).

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/compare.py plugins/x2uhj/study-topologies/tools/tests/test_compare.py plugins/x2uhj/study-topologies/results.json
git commit -m "feat(topology-study): comparison harness -> results.json

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: Plots

**Files:**
- Create: `plugins/x2uhj/study-topologies/tools/plots.py`
- Create: `plugins/x2uhj/study-topologies/tools/tests/test_plots.py`

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/study-topologies/tools/tests/test_plots.py`:
```python
import subprocess, sys, os
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIGDIR = os.path.join(HERE, "..", "doc", "figures")

def test_plots_emit_files():
    subprocess.run([sys.executable, "plots.py"], cwd=HERE, check=True)
    for fn in ("fixed_vs_perfs.png", "pareto_cost_quality.png",
               "error_vs_order.png", "group_delay.png"):
        assert os.path.exists(os.path.join(FIGDIR, fn))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_plots.py -v`
Expected: FAIL (`plots.py` missing).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/study-topologies/tools/plots.py`:
```python
"""Render the study figures from results.json into doc/figures/."""
import json, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "..", "results.json")
FIGDIR = os.path.join(HERE, "..", "doc", "figures")

def _load():
    with open(RESULTS) as fp:
        return json.load(fp)["topologies"]

def fixed_vs_perfs(topos):
    plt.figure(figsize=(9, 5))
    for name, e in topos.items():
        rates = sorted(float(r) for r in e["per_fs_error"])
        per = [e["per_fs_error"][f"{r:.1f}"] for r in rates]
        fix = [e["fixed_error"][f"{r:.1f}"] for r in rates]
        plt.plot([r/1000 for r in rates], per, "s-", label=f"{name} per-fs")
        plt.plot([r/1000 for r in rates], fix, "o--", label=f"{name} fixed")
    plt.xlabel("sample rate (kHz)"); plt.ylabel("max |phase diff - 90| (deg)")
    plt.title("Quadrature error: fixed coefficients versus per-rate design")
    plt.legend(fontsize=8); plt.grid(True, alpha=0.3); plt.tight_layout()
    plt.savefig(os.path.join(FIGDIR, "fixed_vs_perfs.png"), dpi=120); plt.close()

def pareto(topos):
    plt.figure(figsize=(7, 5))
    for name, e in topos.items():
        err48 = e["per_fs_error"]["48000.0"]
        plt.scatter(e["cost"], err48, s=60)
        plt.annotate(name, (e["cost"], err48), fontsize=8,
                     textcoords="offset points", xytext=(5, 5))
    plt.xlabel("multiplies per sample"); plt.ylabel("max error at 48 kHz (deg)")
    plt.title("Quality versus cost"); plt.grid(True, alpha=0.3); plt.tight_layout()
    plt.savefig(os.path.join(FIGDIR, "pareto_cost_quality.png"), dpi=120); plt.close()

def error_vs_order(topos):
    plt.figure(figsize=(8, 5))
    for name, e in topos.items():
        orders = sorted(int(o) for o in e["error_vs_order"])
        plt.plot(orders, [e["error_vs_order"][str(o)] for o in orders], "o-", label=name)
    plt.xlabel("sections per path"); plt.ylabel("max error at 48 kHz (deg)")
    plt.title("Phase error versus order"); plt.legend(); plt.grid(True, alpha=0.3)
    plt.tight_layout(); plt.savefig(os.path.join(FIGDIR, "error_vs_order.png"), dpi=120); plt.close()

def group_delay(topos):
    plt.figure(figsize=(8, 5))
    for name, e in topos.items():
        gd = e["group_delay"]
        plt.semilogx(gd["freqs"], gd["gd_us"], "-", label=name)
    plt.xlabel("Hz"); plt.ylabel("differential group delay (us)")
    plt.title("Differential group delay between the two paths (48 kHz)")
    plt.legend(); plt.grid(True, which="both", alpha=0.3)
    plt.tight_layout(); plt.savefig(os.path.join(FIGDIR, "group_delay.png"), dpi=120); plt.close()

def main():
    os.makedirs(FIGDIR, exist_ok=True)
    topos = _load()
    fixed_vs_perfs(topos); pareto(topos); error_vs_order(topos); group_delay(topos)
    print("wrote figures to", os.path.abspath(FIGDIR))

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest tests/test_plots.py -v`
Expected: 1 passed; four PNGs in `doc/figures/` (`fixed_vs_perfs.png`, `pareto_cost_quality.png`, `error_vs_order.png`, `group_delay.png`).

- [ ] **Step 5: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/tools/plots.py plugins/x2uhj/study-topologies/tools/tests/test_plots.py plugins/x2uhj/study-topologies/doc/figures
git commit -m "feat(topology-study): figures from results.json

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 8: Scope README

**Files:**
- Create: `plugins/x2uhj/study-topologies/README.md`

- [ ] **Step 1: Write the README**

Create `plugins/x2uhj/study-topologies/README.md` (one sentence per line, affirmative voice). Cover: the purpose of the study; a table listing each script in `tools/` with its one-line responsibility (`topology.py`, `topo_rbj.py`, `topo_polyphase.py`, `topo_halfband.py`, `topo_historical.py`, `compare.py`, `plots.py`); the run sequence:
```
cd plugins/x2uhj/study-topologies/tools
<venv>/python compare.py     # writes ../results.json
<venv>/python plots.py       # writes ../doc/figures/*.png
<venv>/python -m pytest -v   # runs the test suite
cd ../doc && latexmk -pdf topology-study.tex
```
where `<venv>` is `../../tools/.venv/bin`. State that `results.json` is the single source of truth, that shared math comes from `../../tools/`, and that the bibliography reuses `../../doc/math/refs.bib`. State the scope boundary: the study informs a future standalone quadrature plugin and changes nothing in the shipped x2uhj plugin.

- [ ] **Step 2: Verify it reads cleanly**

Run: `cat plugins/x2uhj/study-topologies/README.md`
Expected: every script in `tools/` appears with a responsibility; the run sequence is present.

- [ ] **Step 3: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/README.md
git commit -m "docs(topology-study): scope README

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 9: LaTeX scaffold

**Files:**
- Create: `plugins/x2uhj/study-topologies/doc/topology-study.tex`

- [ ] **Step 1: Write the scaffold**

Create `plugins/x2uhj/study-topologies/doc/topology-study.tex`:
```latex
\documentclass[11pt,a4paper]{article}
\usepackage{amsmath,amssymb,graphicx,booktabs,hyperref,siunitx}
\usepackage[margin=2.5cm]{geometry}
\graphicspath{{figures/}}

\title{Quadrature All-Pass Topologies for UHJ\\\large A Multi-Rate Comparative Study}
\author{Giuseppe Silvi --- SEAM}
\date{2026}

\begin{document}
\maketitle
\begin{abstract}
This study compares the all-pass network topologies that realise a wideband 90-degree quadrature for UHJ encoding.
It measures phase accuracy, computational cost, sample-rate behaviour, and group delay on one harness, and it recommends a topology for a standalone quadrature plugin.
\end{abstract}
\tableofcontents

% \section{Introduction}              % Task 10
% \section{Topologies Under Test}     % Task 10
% \section{Method}                    % Task 10
% \section{Results}                   % Task 11
% \section{Discussion and Recommendation} % Task 11
% \section*{References}               % Task 11

\bibliographystyle{IEEEtran}
\bibliography{../../doc/math/refs}
\end{document}
```

- [ ] **Step 2: Verify it compiles**

Run: `cd plugins/x2uhj/study-topologies/doc && latexmk -pdf -interaction=nonstopmode topology-study.tex`
Expected: `topology-study.pdf` produced (a bibtex no-\cite warning is acceptable now).

- [ ] **Step 3: Extend the LaTeX gitignore**

Append to `plugins/x2uhj/.gitignore`:
```
doc/math/study-topologies/*.aux
study-topologies/doc/*.aux
study-topologies/doc/*.log
study-topologies/doc/*.out
study-topologies/doc/*.toc
study-topologies/doc/*.bbl
study-topologies/doc/*.blg
study-topologies/doc/*.fls
study-topologies/doc/*.fdb_latexmk
```

- [ ] **Step 4: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/doc/topology-study.tex plugins/x2uhj/.gitignore plugins/x2uhj/study-topologies/doc/topology-study.pdf
git commit -m "docs(topology-study): LaTeX scaffold

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 10: LaTeX sections 1-3 (introduction, topologies, method)

**Files:**
- Modify: `plugins/x2uhj/study-topologies/doc/topology-study.tex`

- [ ] **Step 1: Write sections 1-3**

Replace the §1-§3 comment lines with real `\section` blocks, one sentence per line, affirmative voice.

§1 Introduction: state the wideband quadrature problem; state the question (best accuracy per cost, and behaviour across sample rates); sketch the lineage from Schroeder's 1958 differential all-pass \cite{} and Gerzon's unitary networks \cite{} through to the modern polyphase and biquad forms. Cite the new bib keys where natural: `Bedrosian1960`, `Gerzon1985UHJ`/`gerzon1983broadcast`.

§2 Topologies Under Test: one subsection per modern topology with its defining equation and reference.
- RBJ biquad cascade \cite{RBJCookbook}: `H(s) = (s^2 - (\omega_0/Q)s + \omega_0^2)/(s^2 + (\omega_0/Q)s + \omega_0^2)`.
- First-order polyphase \cite{Niemitalo2003,Ansari1987Hilbert}: `H(z) = (a^2 - z^{-2})/(1 - a^2 z^{-2})`, with the WigWare fixed coefficients noted \cite{WigginsWigWare}.
- Half-band elliptic \cite{HarrisBerdahlAbel2010,SchusslerSteffen1998}: parallel all-pass branches from an elliptic half-band prototype.
- A short paragraph on the historical Schroeder differential all-pass as the conceptual origin, framed by its decorrelation goal.

§3 Method: describe the uniform interface (design/phase/cost), the fixed and per-fs realisation modes, and the four metrics; state that the harness re-runs the design at each sample rate; cite the minimax basis \cite{Lang1998Allpass}.

- [ ] **Step 2: Compile**

Run: `cd plugins/x2uhj/study-topologies/doc && latexmk -pdf -interaction=nonstopmode topology-study.tex`
Expected: PDF rebuilds with §1-§3 and a populated ToC.

- [ ] **Step 3: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/doc/topology-study.tex plugins/x2uhj/study-topologies/doc/topology-study.pdf
git commit -m "docs(topology-study): sections 1-3 (intro, topologies, method)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 11: LaTeX sections 4-6 (results, discussion, references) and final verification

**Files:**
- Modify: `plugins/x2uhj/study-topologies/doc/topology-study.tex`

- [ ] **Step 1: Write sections 4-6**

Replace the §4-§6 comment lines, one sentence per line, affirmative voice.

§4 Results: embed the three figures and a comparison table.
```latex
\begin{figure}[ht]\centering
\includegraphics[width=0.85\linewidth]{fixed_vs_perfs.png}
\caption{Quadrature error versus sample rate for each topology, fixed coefficients versus per-rate design.}
\end{figure}
\begin{figure}[ht]\centering
\includegraphics[width=0.7\linewidth]{pareto_cost_quality.png}
\caption{Phase accuracy at 48~kHz versus multiplies per sample.}
\end{figure}
\begin{figure}[ht]\centering
\includegraphics[width=0.8\linewidth]{error_vs_order.png}
\caption{Phase error versus the number of sections per path.}
\end{figure}
\begin{figure}[ht]\centering
\includegraphics[width=0.8\linewidth]{group_delay.png}
\caption{Differential group delay between the two paths across frequency at 48~kHz.}
\end{figure}
```
Read `plugins/x2uhj/study-topologies/results.json` and transcribe a `booktabs` table: one row per topology with cost (multiplies/sample), error at 48 kHz (per-fs), and the worst fixed-mode error across rates.

§5 Discussion and Recommendation: state the reading of the figures (the cost/quality trade-off, and that per-rate design holds quadrature while fixed coefficients drift); name the recommended topology for the standalone plugin with its justification from the data.

§6: keep `\bibliography{../../doc/math/refs}`; remove the `% \section*{References}` comment.

Ensure all `\cite` keys used resolve against `../../doc/math/refs.bib`.

- [ ] **Step 2: Compile with references**

Run: `cd plugins/x2uhj/study-topologies/doc && latexmk -pdf -interaction=nonstopmode topology-study.tex`
Expected: PDF rebuilds; References lists the cited entries; no unresolved `[?]`.

- [ ] **Step 3: Final full verification**

Run:
```bash
cd plugins/x2uhj/study-topologies/tools && /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/x2uhj/tools/.venv/bin/python -m pytest -q
cd ../doc && latexmk -pdf -interaction=nonstopmode topology-study.tex
```
Expected: all study tests pass; PDF builds with all sections, figures, table, and citations.

- [ ] **Step 4: Commit**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/x2uhj/study-topologies/doc/topology-study.tex plugins/x2uhj/study-topologies/doc/topology-study.pdf
git commit -m "docs(topology-study): sections 4-6 (results, discussion, references)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** uniform interface (Task 1); three benchmarked topologies (Tasks 2-4); historical illustrative (Task 5); harness + results.json (Task 6); plots (Task 7); README (Task 8); LaTeX study doc (Tasks 9-11); four metrics (per_fs/fixed errors, cost, error_vs_order in Task 6; group delay noted below).
- **Group-delay metric (mandatory):** Task 6 records the differential group delay `-d(phase difference)/d(omega)` per topology at REF_FS in `results.json`, and Task 7 renders `group_delay.png`; §4 (Task 11) embeds it. This covers the spec's group-delay/transient metric on the uniform interface.
- **Niemitalo relative delay:** `topo_polyphase.phase` applies a one-sample delay on path B; Task 3 Step 4 flags checking this sign if the per-fs fit misses target.
- **Half-band risk:** Task 4 carries the spec's documented-partial fallback.
- **Type consistency:** every topology exposes `name`, `design(order, fs, ...)`, `phase(coeffs, fs, freqs)`, `cost(coeffs)`; `compare.py` and `plots.py` consume `per_fs_error`, `fixed_error`, `error_vs_order`, `cost` consistently.
- **Shared venv path:** all run commands use the absolute venv path `plugins/x2uhj/tools/.venv/bin/python`.
```
