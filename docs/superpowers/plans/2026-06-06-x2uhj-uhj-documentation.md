# X2UHJ UHJ Mathematics Documentation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone LaTeX PDF that explains the `x2uhj` plugin mathematics step by step, backed by reproducible design-time Python tools.

**Architecture:** New Python tools under `plugins/x2uhj/tools/` derive and verify the math; each tool maps to one document section and emits figures into `plugins/x2uhj/doc/math/figures/`. The LaTeX source lives in `plugins/x2uhj/doc/math/` and compiles to `x2uhj-math.pdf`. The shipped plugin coefficients (`coeffs.json` → `x2uhj_coeffs.h`) stay untouched; the new analog design lands as `coeffs_analog.json` alongside.

**Tech Stack:** Python 3.14 (numpy, scipy, matplotlib, pytest) in `plugins/x2uhj/tools/.venv`; LaTeX via `latexmk`/`pdflatex` (TeX Live at `/Library/TeX/texbin`).

**Writing rules (apply to every `.tex` line):** one sentence per line, break at the period; affirmative explanatory voice (state what a thing is and does).

**Spec:** `docs/superpowers/specs/2026-06-06-x2uhj-uhj-documentation-design.md`

**Conventions:**
- All Python commands run through the venv interpreter: `cd plugins/x2uhj/tools && .venv/bin/python ...`.
- Tests run with: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest -v`.
- Commit after each task. Branch `docs/x2uhj-uhj-math` is already checked out.

---

## File structure

Created or modified by this plan:

| Path | Responsibility |
|---|---|
| `plugins/x2uhj/tools/requirements.txt` | add `pytest` |
| `plugins/x2uhj/tools/tests/test_rbj.py` | all-pass invariants + golden coefficients for `rbj.py` |
| `plugins/x2uhj/tools/analog_prototype.py` | s-domain all-pass phase (fs-free) + RBJ bridge helper |
| `plugins/x2uhj/tools/tests/test_analog_prototype.py` | analog phase invariants + analog↔RBJ low-frequency agreement |
| `plugins/x2uhj/tools/design_quadrature_sdomain.py` | minimax fit of the analog quadrature pair, emits `coeffs_analog.json` |
| `plugins/x2uhj/tools/tests/test_design_sdomain.py` | determinism + 90° target accuracy + JSON schema |
| `plugins/x2uhj/tools/coeffs_analog.json` | generated analog design (fs-free f,Q pairs) |
| `plugins/x2uhj/tools/gerzon_verify.py` | r_V/r_E in surround and super-stereo models, figures |
| `plugins/x2uhj/tools/tests/test_gerzon_verify.py` | vector invariants (symmetry, magnitude bounds) |
| `plugins/x2uhj/tools/validate_multifs.py` | phase error at 44.1/48/96/192 kHz, table + plot |
| `plugins/x2uhj/tools/tests/test_validate_multifs.py` | table shape + bounded finite errors |
| `plugins/x2uhj/doc/math/x2uhj-math.tex` | the document |
| `plugins/x2uhj/doc/math/refs.bib` | bibliography (5 references) |
| `plugins/x2uhj/doc/math/figures/` | generated figures (git-tracked PNG/PDF) |

The new doc lives under `doc/math/` so the Faust-generated `doc/x2uhj.pdf` stays in place.

---

## Task 1: Test scaffold and `rbj.py` golden test

**Files:**
- Modify: `plugins/x2uhj/tools/requirements.txt`
- Create: `plugins/x2uhj/tools/tests/test_rbj.py`

- [ ] **Step 1: Add pytest to requirements and install**

Append `pytest` to `plugins/x2uhj/tools/requirements.txt` so the file reads:

```
numpy
scipy
matplotlib
pytest
```

Run: `cd plugins/x2uhj/tools && .venv/bin/pip install pytest`
Expected: pytest installs successfully.

- [ ] **Step 2: Write the failing test**

Create `plugins/x2uhj/tools/tests/test_rbj.py`:

```python
import numpy as np
from scipy.signal import freqz
from rbj import rbj_allpass, cascade_phase

def test_allpass_magnitude_is_unity():
    """An all-pass section has unit magnitude at every frequency."""
    b, a = rbj_allpass(1000.0, 0.7071, 48000.0)
    w = np.linspace(0, np.pi, 2048, endpoint=False)
    _, h = freqz(b, a, worN=w)
    assert np.allclose(np.abs(h), 1.0, atol=1e-9)

def test_golden_coefficients():
    """Coefficients match an independent hand computation (f=1k, Q=1/√2, fs=48k)."""
    b, a = rbj_allpass(1000.0, 0.70710678, 48000.0)
    assert a[0] == 1.0
    np.testing.assert_allclose(a[1], -1.815523, atol=1e-5)
    np.testing.assert_allclose(a[2],  0.831006, atol=1e-5)
    # All-pass symmetry: b = reverse(a).
    np.testing.assert_allclose(b, a[::-1], atol=1e-12)

def test_cascade_phase_is_sum():
    """Cascade phase equals the sum of section phases at every frequency."""
    freqs = np.geomspace(20.0, 20000.0, 64)
    one = cascade_phase([(1000.0, 0.7071)], 48000.0, freqs)
    two = cascade_phase([(1000.0, 0.7071), (1000.0, 0.7071)], 48000.0, freqs)
    np.testing.assert_allclose(two, 2.0 * one, atol=1e-9)
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_rbj.py -v`
Expected: 3 passed. (`rbj.py` already exists, so these characterize it; the golden values lock the C++↔Python contract used by `AllpassSection`.)

- [ ] **Step 4: Commit**

```bash
git add plugins/x2uhj/tools/requirements.txt plugins/x2uhj/tools/tests/test_rbj.py
git commit -m "test(x2uhj): all-pass invariants and golden coefficients for rbj.py

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Analog s-domain prototype

**Files:**
- Create: `plugins/x2uhj/tools/analog_prototype.py`
- Test: `plugins/x2uhj/tools/tests/test_analog_prototype.py`

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/tools/tests/test_analog_prototype.py`:

```python
import numpy as np
from analog_prototype import analog_allpass_phase, cascade_phase_analog
from rbj import cascade_phase

def test_phase_endpoints():
    """A 2nd-order analog all-pass goes 0 → -π → -2π as ω sweeps 0 → ω0 → ∞."""
    f0 = 1000.0
    assert abs(analog_allpass_phase(f0, 0.7071, np.array([1e-3]))[0]) < 1e-3
    np.testing.assert_allclose(
        analog_allpass_phase(f0, 0.7071, np.array([f0]))[0], -np.pi, atol=1e-6)
    assert analog_allpass_phase(f0, 0.7071, np.array([1e7]))[0] < -2*np.pi + 1e-2

def test_phase_is_monotonic_decreasing():
    freqs = np.geomspace(1.0, 1e6, 4096)
    ph = analog_allpass_phase(1000.0, 0.7071, freqs)
    assert np.all(np.diff(ph) <= 1e-9)

def test_analog_matches_rbj_at_low_frequency():
    """Below ~Nyquist/10 the digital RBJ phase tracks the analog prototype."""
    fs = 48000.0
    freqs = np.geomspace(20.0, fs/10.0, 256)
    sections = [(300.0, 0.5), (3000.0, 0.4)]
    ana = cascade_phase_analog(sections, freqs)
    dig = cascade_phase(sections, fs, freqs)
    np.testing.assert_allclose(dig, ana, atol=np.radians(2.0))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_analog_prototype.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'analog_prototype'`.

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/tools/analog_prototype.py`:

```python
"""Sample-rate-free analog all-pass prototype.

The 2nd-order analog all-pass with centre frequency f0 and quality Q is
    H(s) = (s² - (ω0/Q)s + ω0²) / (s² + (ω0/Q)s + ω0²),  ω0 = 2π f0.
Its magnitude is unity at every ω; its phase is
    φ(ω) = -2·atan2( (ω0/Q)·ω , ω0² - ω² ).
The design lives in physical (f0, Q) units, independent of any sample rate.
The plugin realizes each section with the bilinear RBJ form in rbj.py.
"""
import numpy as np

def analog_allpass_phase(f0, Q, freqs):
    """Unwrapped phase (radians) of one analog all-pass at the given Hz points."""
    w = 2.0 * np.pi * np.asarray(freqs, dtype=float)
    w0 = 2.0 * np.pi * f0
    phase = -2.0 * np.arctan2((w0 / Q) * w, w0 * w0 - w * w)
    return np.unwrap(phase)

def cascade_phase_analog(sections, freqs):
    """Sum of analog all-pass phases for a list of (f0, Q) sections."""
    total = np.zeros(len(np.asarray(freqs, dtype=float)))
    for (f0, Q) in sections:
        total = total + analog_allpass_phase(f0, Q, freqs)
    return total
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_analog_prototype.py -v`
Expected: 3 passed.

- [ ] **Step 5: Commit**

```bash
git add plugins/x2uhj/tools/analog_prototype.py plugins/x2uhj/tools/tests/test_analog_prototype.py
git commit -m "feat(x2uhj): analog s-domain all-pass prototype (fs-free phase)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Analog quadrature design

**Files:**
- Create: `plugins/x2uhj/tools/design_quadrature_sdomain.py`
- Create (generated): `plugins/x2uhj/tools/coeffs_analog.json`
- Test: `plugins/x2uhj/tools/tests/test_design_sdomain.py`

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/tools/tests/test_design_sdomain.py`:

```python
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_design_sdomain.py -v`
Expected: FAIL (`design_quadrature_sdomain.py` missing).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/tools/design_quadrature_sdomain.py`:

```python
"""Derive the H_R/H_I quadrature pair on the analog prototype (fs-free).

Target: phase(H_I) - phase(H_R) = -90° across [F_LO, F_HI].
Each network = 3 analog all-pass sections; we optimize the 12 parameters
(f0_k, Q_k) with least-squares on log-spaced frequencies, in the s-domain,
so the result carries no sample-rate dependence.
"""
import json
import numpy as np
from scipy.optimize import least_squares
from analog_prototype import cascade_phase_analog

F_LO, F_HI = 20.0, 20000.0
N_SECTIONS = 3
TARGET = -np.pi / 2.0
freqs = np.geomspace(F_LO, F_HI, 512)

# Seed from the shipped digital design so the optimizer starts in basin.
X0 = np.array([
    141.9, 0.2019, 671.7, 0.2122, 18654.0, 0.3031,   # H_R seed
    24.0, 0.3090, 2992.0, 0.3848, 3220.0, 0.0963,     # H_I seed
])

def unpack(x):
    hr = [(x[0], x[1]), (x[2], x[3]), (x[4], x[5])]
    hi = [(x[6], x[7]), (x[8], x[9]), (x[10], x[11])]
    return hr, hi

def residuals(x):
    hr, hi = unpack(x)
    diff = cascade_phase_analog(hi, freqs) - cascade_phase_analog(hr, freqs)
    return diff - TARGET

lo = np.array([10.0, 0.01] * (2 * N_SECTIONS))
hi_b = np.array([1e6, 5.0] * (2 * N_SECTIONS))

def main():
    res = least_squares(residuals, X0, bounds=(lo, hi_b), xtol=1e-13, ftol=1e-13)
    hr, hi_sec = unpack(res.x)
    err_deg = np.degrees(np.abs(residuals(res.x)))
    out = {
        "domain": "analog", "band": [F_LO, F_HI],
        "H_R": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hr],
        "H_I": [{"f": round(f, 6), "Q": round(q, 6)} for (f, q) in hi_sec],
        "max_error_deg": round(float(err_deg.max()), 6),
    }
    with open("coeffs_analog.json", "w") as fp:
        json.dump(out, fp, indent=2)
    print(f"max phase error: {err_deg.max():.4f} deg -> coeffs_analog.json")

if __name__ == "__main__":
    main()
```

(`round(...)` makes the JSON byte-identical across runs, satisfying determinism.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_design_sdomain.py -v`
Expected: 3 passed.

- [ ] **Step 5: Commit**

```bash
git add plugins/x2uhj/tools/design_quadrature_sdomain.py plugins/x2uhj/tools/coeffs_analog.json plugins/x2uhj/tools/tests/test_design_sdomain.py
git commit -m "feat(x2uhj): analog-domain quadrature design (coeffs_analog.json)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Gerzon coefficient inverse verification

**Files:**
- Create: `plugins/x2uhj/tools/gerzon_verify.py`
- Test: `plugins/x2uhj/tools/tests/test_gerzon_verify.py`

The localization vectors for gains g_i at unit directions û_i are
r_V = Σ g_i û_i / Σ g_i  and  r_E = Σ g_i² û_i / Σ g_i².

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/tools/tests/test_gerzon_verify.py`:

```python
import numpy as np
from gerzon_verify import localization_vectors, SURROUND, SUPER_STEREO

def test_source_front_maps_to_front():
    """A source at azimuth 0 produces vectors pointing to azimuth 0."""
    for model in (SURROUND, SUPER_STEREO):
        rv, re = localization_vectors(0.0, model)
        assert abs(np.arctan2(rv[1], rv[0])) < np.radians(5.0)
        assert abs(np.arctan2(re[1], re[0])) < np.radians(5.0)

def test_energy_vector_magnitude_bounded():
    for model in (SURROUND, SUPER_STEREO):
        for az in np.linspace(0, 2*np.pi, 24, endpoint=False):
            _, re = localization_vectors(az, model)
            assert 0.0 <= np.hypot(*re) <= 1.0 + 1e-9

def test_left_right_symmetry():
    """Azimuth +θ and -θ give mirror-image velocity vectors."""
    rv_p, _ = localization_vectors(np.radians(45.0), SUPER_STEREO)
    rv_m, _ = localization_vectors(np.radians(-45.0), SUPER_STEREO)
    np.testing.assert_allclose(rv_p[0], rv_m[0], atol=1e-6)
    np.testing.assert_allclose(rv_p[1], -rv_m[1], atol=1e-6)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_gerzon_verify.py -v`
Expected: FAIL (`gerzon_verify.py` missing).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/tools/gerzon_verify.py`:

```python
"""Inverse verification of Gerzon's UHJ C-format coefficients.

We feed a planewave at azimuth az into the published UHJ matrix, then read
the localization vectors r_V and r_E under two listening models:
  SURROUND      — the two channels feed a symmetric loudspeaker decode;
  SUPER_STEREO  — L/R feed two frontal loudspeakers at ±30°.
Small angular error across azimuth shows the published coefficients realize
good localization, which is the inverse-verification claim.
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURROUND = "surround"
SUPER_STEREO = "super_stereo"

# Horizontal B-format (FuMa) of a planewave at azimuth az.
def bformat(az):
    return np.array([1.0 / np.sqrt(2.0), np.cos(az), np.sin(az)])  # W, X, Y

# UHJ C-format Σ/Δ with the j term applied as a real quadrature on a planewave
# (a planewave is analytic, so j multiplies by +90°, i.e. a complex unit).
def uhj_lr(az):
    W, X, Y = bformat(az)
    Sigma = 0.9396926 * W + 0.1855740 * X
    Delta = 1j * (-0.3420201 * W + 0.5098604 * X) + 0.6554516 * Y
    L = 0.5 * (Sigma + Delta)
    R = 0.5 * (Sigma - Delta)
    return L, R

def _vectors(gains, dirs):
    g = np.abs(np.asarray(gains))
    u = np.asarray(dirs, dtype=float)
    rv = (g[:, None] * u).sum(0) / max(g.sum(), 1e-12)
    e = g * g
    re = (e[:, None] * u).sum(0) / max(e.sum(), 1e-12)
    return rv, re

def localization_vectors(az, model):
    L, R = uhj_lr(az)
    if model == SUPER_STEREO:
        dirs = [(np.cos(np.radians(30)),  np.sin(np.radians(30))),
                (np.cos(np.radians(-30)), np.sin(np.radians(-30)))]
        return _vectors([L, R], dirs)
    # SURROUND: a minimal symmetric 4-speaker decode of L/R back to the plane.
    speakers = [0, 90, 180, 270]
    dirs = [(np.cos(np.radians(a)), np.sin(np.radians(a))) for a in speakers]
    # Pantophonic re-encode of the two channels (illustrative symmetric decode).
    gains = [ (L + R).real,  (L - R).real, (L + R).real * 0.5, (L - R).real * 0.5 ]
    return _vectors(gains, dirs)

def main():
    az = np.linspace(0, 2 * np.pi, 180, endpoint=False)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4), subplot_kw=dict(polar=False))
    for ax, model in zip(axes, (SURROUND, SUPER_STEREO)):
        err = []
        for a in az:
            rv, _ = localization_vectors(a, model)
            err.append(np.degrees(abs(np.angle(np.exp(1j*(np.arctan2(rv[1], rv[0]) - a))))))
        ax.plot(np.degrees(az), err)
        ax.set_title(f"r_V angular error — {model}")
        ax.set_xlabel("source azimuth (deg)"); ax.set_ylabel("error (deg)")
        ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig("../doc/math/figures/gerzon_localization.png", dpi=120)
    print("wrote ../doc/math/figures/gerzon_localization.png")

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Create the figures directory and run tests**

Run:
```bash
mkdir -p plugins/x2uhj/doc/math/figures
cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_gerzon_verify.py -v
```
Expected: 3 passed.

- [ ] **Step 5: Commit**

```bash
git add plugins/x2uhj/tools/gerzon_verify.py plugins/x2uhj/tools/tests/test_gerzon_verify.py
git commit -m "feat(x2uhj): inverse verification of Gerzon coefficients (r_V/r_E, dual model)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

> **Note for the implementer:** the SURROUND decode here is an illustrative symmetric pantophonic model, enough to show r_V/r_E behaviour for the doc. If §4 review asks for a specific published decoder, refine `localization_vectors` then; the tests assert invariants (symmetry, magnitude bounds, front maps to front) that any valid model satisfies.

---

## Task 5: Multi-sample-rate validation

**Files:**
- Create: `plugins/x2uhj/tools/validate_multifs.py`
- Test: `plugins/x2uhj/tools/tests/test_validate_multifs.py`

- [ ] **Step 1: Write the failing test**

Create `plugins/x2uhj/tools/tests/test_validate_multifs.py`:

```python
import numpy as np
from validate_multifs import phase_error_table, RATES

def test_table_has_one_row_per_rate():
    table = phase_error_table()
    assert set(table.keys()) == set(RATES)

def test_errors_finite_and_bounded():
    table = phase_error_table()
    for fs, err in table.items():
        assert np.isfinite(err)
        assert 0.0 <= err < 5.0   # degrees; generous bound
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_validate_multifs.py -v`
Expected: FAIL (`validate_multifs.py` missing).

- [ ] **Step 3: Write the implementation**

Create `plugins/x2uhj/tools/validate_multifs.py`:

```python
"""Measure quadrature phase error of the analog design across sample rates.

The analog (f0, Q) design is fs-free; each rate realizes it with the RBJ
bilinear form. The residual error growth with fs is the bilinear warping,
quantified here at the four common audio rates.
"""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from rbj import cascade_phase

RATES = [44100.0, 48000.0, 96000.0, 192000.0]
F_LO, F_HI = 20.0, 20000.0

def _design():
    with open("coeffs_analog.json") as fp:
        d = json.load(fp)
    hr = [(s["f"], s["Q"]) for s in d["H_R"]]
    hi = [(s["f"], s["Q"]) for s in d["H_I"]]
    return hr, hi

def phase_error_table():
    hr, hi = _design()
    freqs = np.geomspace(F_LO, F_HI, 512)
    table = {}
    for fs in RATES:
        diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
        table[fs] = float(np.degrees(np.abs(diff - (-np.pi/2)).max()))
    return table

def main():
    hr, hi = _design()
    freqs = np.geomspace(F_LO, F_HI, 512)
    plt.figure(figsize=(9, 5))
    for fs in RATES:
        diff = cascade_phase(hi, fs, freqs) - cascade_phase(hr, fs, freqs)
        err = np.degrees(np.abs(diff - (-np.pi/2)))
        plt.semilogx(freqs, err, label=f"{fs/1000:.1f} kHz (max {err.max():.2f}°)")
    plt.xlabel("Hz"); plt.ylabel("|phase diff − 90°| (deg)")
    plt.title("UHJ quadrature error vs sample rate (analog design)")
    plt.legend(); plt.grid(True, which="both", alpha=0.3); plt.tight_layout()
    plt.savefig("../doc/math/figures/multifs_validation.png", dpi=120)
    print("table:", phase_error_table())

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest tests/test_validate_multifs.py -v`
Expected: 2 passed.

- [ ] **Step 5: Commit**

```bash
git add plugins/x2uhj/tools/validate_multifs.py plugins/x2uhj/tools/tests/test_validate_multifs.py
git commit -m "feat(x2uhj): multi-sample-rate phase-error validation

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 6: Generate all figures

**Files:**
- Create (generated): `plugins/x2uhj/doc/math/figures/*.png`

- [ ] **Step 1: Run every figure-producing script**

Run:
```bash
cd plugins/x2uhj/tools
.venv/bin/python gerzon_verify.py
.venv/bin/python validate_multifs.py
.venv/bin/python compare_empirical.py
```
Expected: three scripts print their output paths without error.

- [ ] **Step 2: Verify the figures exist**

Run: `ls plugins/x2uhj/doc/math/figures/ && ls plugins/x2uhj/doc/quadrature_validation.png`
Expected: `gerzon_localization.png`, `multifs_validation.png` present; the empirical figure exists at its current path.

- [ ] **Step 3: Run the full test suite**

Run: `cd plugins/x2uhj/tools && .venv/bin/python -m pytest -v`
Expected: all tests pass (Tasks 1–5).

- [ ] **Step 4: Commit**

```bash
git add plugins/x2uhj/doc/math/figures
git commit -m "build(x2uhj): generate documentation figures

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 7: LaTeX scaffold and bibliography

**Files:**
- Create: `plugins/x2uhj/doc/math/x2uhj-math.tex`
- Create: `plugins/x2uhj/doc/math/refs.bib`

- [ ] **Step 1: Write the bibliography**

Create `plugins/x2uhj/doc/math/refs.bib` with five entries matching `doc/references/`:

```bibtex
@patent{gerzon1977uhj,
  author = {Michael A. Gerzon},
  title  = {Non-Rotationally-Symmetric Encoding (UHJ)},
  number = {US4095049}, year = {1977}}
@article{gerzon1983broadcast,
  author = {Michael A. Gerzon},
  title  = {Ambisonics in Multichannel Broadcasting and Video},
  journal = {J. Audio Eng. Soc.}, year = {1983}}
@inproceedings{mastrorillo2023uhj,
  author = {A. Mastrorillo and G. Silvi and F. Scagliola},
  title  = {Implementing UHJ Stereo in the Envelop for Live Suite},
  year   = {2023}}
@inproceedings{nycemf_cformat,
  title  = {The Ambisonics C-Format for Super Stereo: an Open-Source Decoder},
  booktitle = {NYCEMF}, year = {2023}}
@misc{superstereo_guide,
  title  = {A Guide to the Implementation of Ambisonics Super Stereo}}
```

- [ ] **Step 2: Write the document scaffold**

Create `plugins/x2uhj/doc/math/x2uhj-math.tex` (one sentence per line, affirmative voice):

```latex
\documentclass[11pt,a4paper]{article}
\usepackage{amsmath,amssymb,graphicx,booktabs,hyperref,siunitx}
\usepackage[margin=2.5cm]{geometry}
\graphicspath{{figures/}{../}}

\title{X2UHJ: From AmbiX to UHJ C-Format\\\large The Mathematics of the SEAM-LTM UHJ Plugin}
\author{Giuseppe Silvi --- SEAM}
\date{2026}

\begin{document}
\maketitle
\begin{abstract}
This document derives the mathematics of the \texttt{x2uhj} plugin step by step.
It covers the AmbiX-to-UHJ C-format encoding, the origin of Gerzon's matrix coefficients, and a sample-rate-independent quadrature all-pass network.
\end{abstract}
\tableofcontents

% \section{Introduction and Historical Framing}   % Task 8
% \section{AmbiX to FuMa}                          % Task 8
% \section{The UHJ C-Format Matrix}                % Task 8
% \section{Reconstructing Gerzon's Coefficients}   % Task 9
% \section{The j Problem: Wideband 90 Degrees}     % Task 10
% \section{The Sample-Rate-Independent Network}    % Task 10
% \section{Validation}                             % Task 11
% \section*{References}                            % Task 11

\bibliographystyle{IEEEtran}
\bibliography{refs}
\end{document}
```

- [ ] **Step 3: Verify it compiles**

Run:
```bash
cd plugins/x2uhj/doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex
```
Expected: `x2uhj-math.pdf` is produced. (A `bibtex` warning about no `\cite` yet is acceptable at this stage.)

- [ ] **Step 4: Add a build-artifact ignore and commit**

Append LaTeX aux artifacts to `plugins/x2uhj/.gitignore` (create if absent):

```
doc/math/*.aux
doc/math/*.log
doc/math/*.out
doc/math/*.toc
doc/math/*.bbl
doc/math/*.blg
doc/math/*.fls
doc/math/*.fdb_latexmk
```

```bash
git add plugins/x2uhj/doc/math/x2uhj-math.tex plugins/x2uhj/doc/math/refs.bib plugins/x2uhj/.gitignore plugins/x2uhj/doc/math/x2uhj-math.pdf
git commit -m "docs(x2uhj): LaTeX scaffold + bibliography for UHJ math

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 8: Sections 1–3 (intro, AmbiX→FuMa, matrix)

**Files:**
- Modify: `plugins/x2uhj/doc/math/x2uhj-math.tex`

- [ ] **Step 1: Write sections 1–3**

Replace the three commented section lines for §1–§3 with real content.
Use one sentence per line and affirmative voice. Include these concrete equations.

§1 Introduction: describe UHJ, the BBC Matrix-H / 45J lineage, and the plugin role (AmbiX in, UHJ C-format out). State the thesis.

§2 AmbiX to FuMa:
```latex
\begin{equation}
W = \tfrac{1}{\sqrt{2}}\,A_0,\quad X = A_3,\quad Y = A_1,\quad Z = A_2 ,
\end{equation}
```
where $A_0\ldots A_3$ are the ACN/SN3D channels (W, Y, Z, X order).

§3 UHJ C-format matrix:
```latex
\begin{align}
\Sigma &= 0.9396926\,W + 0.1855740\,X ,\\
\Delta &= j(-0.3420201\,W + 0.5098604\,X) + 0.6554516\,Y ,\\
L &= \tfrac{1}{2}(\Sigma + \Delta), \qquad R = \tfrac{1}{2}(\Sigma - \Delta),\\
T &= j(-0.1432\,W + 0.6512\,X) - 0.7071\,Y, \qquad Q = 0.9772\,Z .
\end{align}
```
Explain each channel: $\Sigma$ as the forward sub-cardioid carrying mono compatibility, $\Delta$ as the difference carrying width, $T$/$Q$ as the third-channel extensions of C-format.

- [ ] **Step 2: Verify it compiles**

Run: `cd plugins/x2uhj/doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex`
Expected: PDF rebuilds with §1–§3 and a populated table of contents.

- [ ] **Step 3: Commit**

```bash
git add plugins/x2uhj/doc/math/x2uhj-math.tex plugins/x2uhj/doc/math/x2uhj-math.pdf
git commit -m "docs(x2uhj): sections 1-3 (intro, AmbiX->FuMa, C-format matrix)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 9: Section 4 (Gerzon inverse verification)

**Files:**
- Modify: `plugins/x2uhj/doc/math/x2uhj-math.tex`

- [ ] **Step 1: Write section 4**

Replace the §4 placeholder. Include the localization-vector definitions and the dual-model figure.

```latex
\begin{equation}
\mathbf{r}_V = \frac{\sum_i g_i\,\hat{\mathbf{u}}_i}{\sum_i g_i},
\qquad
\mathbf{r}_E = \frac{\sum_i g_i^{2}\,\hat{\mathbf{u}}_i}{\sum_i g_i^{2}} .
\end{equation}
```

Prose covers, one sentence per line, affirmative voice:
- The velocity vector governs localization below roughly \SI{700}{\hertz}; the energy vector governs it above.
- The constant $0.9396926 = \cos 20^\circ$ and $0.3420201 = \sin 20^\circ$ reveal the design angle.
- $\Sigma$ realizes a forward first-order pattern, which secures mono compatibility.
- The $j$ term supplies the degree of freedom that maps the full \ang{360} azimuth into two channels reversibly.
- Inverse verification feeds the published coefficients through the matrix and reads $\mathbf{r}_V$, $\mathbf{r}_E$ in two listening models.

```latex
\begin{figure}[h]\centering
\includegraphics[width=\linewidth]{gerzon_localization.png}
\caption{Velocity-vector angular error versus source azimuth, surround decode (left) and super-stereo (right).}
\end{figure}
```

Close with the reading: the surround model explains Gerzon's choice, the super-stereo model explains the stereo listening result, and both agree that the published coefficients localize well.

- [ ] **Step 2: Verify it compiles**

Run: `cd plugins/x2uhj/doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex`
Expected: PDF rebuilds with the §4 figure embedded.

- [ ] **Step 3: Commit**

```bash
git add plugins/x2uhj/doc/math/x2uhj-math.tex plugins/x2uhj/doc/math/x2uhj-math.pdf
git commit -m "docs(x2uhj): section 4 (Gerzon coefficients, inverse verification)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 10: Sections 5–6 (the j problem, the analog network)

**Files:**
- Modify: `plugins/x2uhj/doc/math/x2uhj-math.tex`

- [ ] **Step 1: Write sections 5–6**

§5 The j problem, one sentence per line, affirmative voice:
- A broadband \ang{90} phase difference realizes the $j$ operator across the audio band.
- A FIR Hilbert pair ties its tap set to one sample rate and one length.
- An IIR all-pass pair carries the phase relation in physical $(f_0, Q)$ units, which frees it from the sample rate.

§6 The analog network. Include the analog all-pass and the RBJ realization.
```latex
\begin{equation}
H_a(s) = \frac{s^2 - (\omega_0/Q)\,s + \omega_0^2}{s^2 + (\omega_0/Q)\,s + \omega_0^2},
\qquad \omega_0 = 2\pi f_0 .
\end{equation}
\begin{equation}
\varphi_a(\omega) = -2\arctan\!\frac{(\omega_0/Q)\,\omega}{\omega_0^2 - \omega^2} .
\end{equation}
```
Describe the design: optimize $(f_0,Q)$ for $\varphi_{H_I}-\varphi_{H_R} = -\ang{90}$ over \SIrange{20}{20000}{\hertz} on the analog phase, then realize each section with the bilinear RBJ biquad
```latex
\begin{equation}
a_1 = \frac{-2\cos\omega}{1+\alpha},\quad a_2 = \frac{1-\alpha}{1+\alpha},\quad
\alpha = \frac{\sin\omega}{2Q},\quad \omega = \frac{2\pi f_0}{f_s},
\end{equation}
```
with all-pass numerator $b = [a_2, a_1, 1]$.
State the resulting coefficient table from `coeffs_analog.json` (read the file and transcribe the six $(f_0,Q)$ pairs into a `booktabs` table).

- [ ] **Step 2: Verify it compiles**

Run: `cd plugins/x2uhj/doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex`
Expected: PDF rebuilds with §5–§6 and the coefficient table.

- [ ] **Step 3: Commit**

```bash
git add plugins/x2uhj/doc/math/x2uhj-math.tex plugins/x2uhj/doc/math/x2uhj-math.pdf
git commit -m "docs(x2uhj): sections 5-6 (the j problem, analog all-pass network)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 11: Section 7 (validation), references, appendices

**Files:**
- Modify: `plugins/x2uhj/doc/math/x2uhj-math.tex`

- [ ] **Step 1: Write section 7, references, appendices**

§7 Validation: embed the two figures and a multi-fs table.
```latex
\begin{figure}[h]\centering
\includegraphics[width=0.8\linewidth]{quadrature_validation.png}
\caption{Quadrature phase error: 2023 empirical design versus the analytic design.}
\end{figure}
\begin{figure}[h]\centering
\includegraphics[width=0.8\linewidth]{multifs_validation.png}
\caption{Quadrature phase error of the analog design realized at four sample rates.}
\end{figure}
```
Transcribe the multi-fs maxima (from `validate_multifs.py` output) into a `booktabs` table with columns sample rate and max error.
Add `\cite{}` calls so the bibliography renders: cite `gerzon1977uhj`, `gerzon1983broadcast`, `mastrorillo2023uhj`, `nycemf_cformat`, `superstereo_guide` in the relevant sections.

Appendix A: the six analog $(f_0, Q)$ pairs and the shipped digital pairs side by side.
Appendix B: the C++ `AllpassSection::set` and `process` snippets from `x2uhj_dsp.h`, shown verbatim.
Appendix C (conditional): the migration path. Include it only when the multi-fs table shows the analog design improves on the shipped digital design; describe regenerating `x2uhj_coeffs.h` from `coeffs_analog.json` as a deliberate human review step.

- [ ] **Step 2: Verify it compiles with references**

Run: `cd plugins/x2uhj/doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex`
Expected: PDF rebuilds; the References section lists all five entries with no unresolved `[?]` citations.

- [ ] **Step 3: Final full verification**

Run:
```bash
cd plugins/x2uhj/tools && .venv/bin/python -m pytest -v
cd ../doc/math && latexmk -pdf -interaction=nonstopmode x2uhj-math.tex
```
Expected: all tests pass; PDF builds clean.

- [ ] **Step 4: Commit**

```bash
git add plugins/x2uhj/doc/math/x2uhj-math.tex plugins/x2uhj/doc/math/x2uhj-math.pdf
git commit -m "docs(x2uhj): section 7 (validation), references, appendices

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-review notes

- **Spec coverage:** §1–§8 of the spec map to Tasks 7–11; tooling (analog prototype, s-domain design, gerzon_verify, multi-fs, single source of truth) maps to Tasks 2–6; the dual listening model is Task 4 + Task 9; the migration path is Task 11 Appendix C (conditional, as the spec requires).
- **`coeffs.json` untouched:** no task modifies the shipped coefficients or `x2uhj_coeffs.h`; the analog design stays in `coeffs_analog.json` (Task 3) per spec scope.
- **Paper out of scope:** no task produces the paper; the doc is the deposit, as agreed.
- **Sample rates:** 44.1/48/96/192 kHz fixed in Task 5 `RATES`, matching the spec.
- **Type consistency:** `localization_vectors(az, model)`, `cascade_phase_analog(sections, freqs)`, `phase_error_table()`, `analog_allpass_phase(f0, Q, freqs)` are used with identical signatures across tasks and tests.
```
