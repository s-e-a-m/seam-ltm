# X2UHJ — Quadrature All-Pass Topology Study (Design Spec)

Date: 2026-06-07
Status: Approved (brainstorming), pending spec review
Owner: Giuseppe Silvi (grammaton)
Related: [2026-06-06 UHJ documentation spec](2026-06-06-x2uhj-uhj-documentation-design.md)

## Purpose

Compare the all-pass network topologies that realise a wideband 90° quadrature for UHJ encoding, on one footing, using a single measurement harness.
The study answers two questions: which topology gives the best phase accuracy per computational cost, and how each topology behaves across sample rates.
It produces a standalone document that becomes the direct basis of an integrative paper, and it informs a future standalone quadrature plugin.

## Context and motivation

A research spike on the `x2uhj` plugin established that sample-rate independence is a property of the design procedure, not of a fixed coefficient set.
A literature review (saved in `plugins/x2uhj/doc/references/compass_artifact_*.md`) confirmed that no published work presents a cross-topology, multi-sample-rate comparison of quadrature all-pass networks using one harness.
That absence is the gap this study fills.
The harness already exists in embryo as `plugins/x2uhj/tools/design_quadrature_perfs.py` (one topology, per-rate design); this study generalises it to several topologies behind a uniform interface.

## Scope

### In scope

- A uniform Python topology interface and one module per benchmarked topology.
- A comparison harness that sweeps topologies × sample rates × order and records results to `results.json`.
- Plot generation and a standalone LaTeX study document with its own PDF.
- Three benchmarked modern topologies plus a historical narrative with one illustrative measurement.
- Tests for each topology module and for the harness.

### Out of scope

- The standalone quadrature plugin (a later work with its own spec; this study informs it).
- Any change to the `x2uhj` plugin, its shipped `coeffs.json`, or `x2uhj_coeffs.h`.
- The peer-review paper draft (this study is its basis; the venue-formatted draft is a later step).
- Vendoring WigWare coefficients into shipped code; this study measures and cites them only.

## Location

`plugins/x2uhj/study-topologies/`, on the existing branch `docs/x2uhj-uhj-math`.

```
plugins/x2uhj/study-topologies/
├── tools/
│   ├── topology.py        # the uniform interface (Protocol) + shared helpers
│   ├── topo_rbj.py        # second-order RBJ biquad cascade (reuses ../../tools/rbj.py, analog_prototype.py)
│   ├── topo_polyphase.py  # Niemitalo first-order polyphase: fixed-coefficient and per-fs minimax modes
│   ├── topo_halfband.py   # Harris-Berdahl-Abel half-band -> Hilbert
│   ├── topo_historical.py # illustrative Schroeder 1958 differential all-pass (optional)
│   ├── compare.py         # the harness: topologies x rates x order -> results.json
│   ├── plots.py           # figures from results.json
│   └── tests/
├── doc/
│   ├── topology-study.tex # standalone study (own PDF)
│   └── figures/
└── results.json           # generated, deterministic
```

Shared modules import from `../../tools/` (`rbj.py`, `analog_prototype.py`).
The bibliography reuses `../../doc/math/refs.bib` so one bibliography source serves both documents.

## The topology interface

Each topology is a module implementing one interface, so the harness treats them uniformly.

```python
class Topology(Protocol):
    name: str
    def design(self, order: int, fs: float) -> Coeffs: ...   # design coefficients for this order/fs
    def phase(self, coeffs: Coeffs, fs: float, freqs) -> array: ...  # phase of H_I - H_R
    def cost(self, coeffs: Coeffs) -> int: ...               # multiplies per sample
```

`Coeffs` is per-topology (its sections).
Two realisation modes apply to every topology for the sample-rate-robustness metric:

- **fixed:** `design()` once at a reference rate, `phase()` evaluated at all rates (shows drift).
- **per-fs:** `design()` re-run at each rate (shows flatness).

### Benchmarked topology modules

| Module | Topology | Notes |
|---|---|---|
| `topo_rbj.py` | second-order biquad cascade | reuses `rbj.py` + `analog_prototype.py`; the current x2uhj design |
| `topo_polyphase.py` | first-order polyphase (Niemitalo) | exposes both fixed coefficients (from the literature/WigWare, cited) and per-fs minimax redesign |
| `topo_halfband.py` | half-band -> Hilbert (Harris-Berdahl-Abel) | pole motion + frequency warping; published 2010 method |

### Historical material (narrative, not full benchmark)

The historical references enrich the narrative and bibliography without crowding the benchmark.

- Schroeder 1958 (artificial stereo from a single signal) is the conceptual origin of mono-to-stereo via differential all-pass with ±90° phase; `topo_historical.py` provides one illustrative measurement of its differential all-pass, framed honestly as a different design goal (decorrelation, not a held 90°).
- Schroeder & Logan 1961 and Schroeder 1962 (delay-line all-pass reverberators) appear in the narrative as the distinct delay-based all-pass structure, explicitly separated from phase-difference networks.
- Gerzon 1976 (unitary energy-preserving networks) appears as the lossless/unitary all-pass theory and Gerzon's own engagement with 90° networks.

## Measurement harness and metrics

`compare.py` iterates topologies × sample rates × order, measuring the four agreed metrics.

| Metric | Measurement |
|---|---|
| max phase error and error vs order | `max|phase(coeffs, fs, freqs) − (−90°)|` over 20–20000 Hz, swept across order (section count) |
| sample-rate robustness | the same measure in fixed and per-fs modes across 44.1/48/88.2/96/176.4/192 kHz |
| computational cost | `cost()` in multiplies per sample at the given order |
| group delay and transient | group delay from the phase derivative plus an impulse-response snapshot |

### Data flow

```
topo_*.py        ─→ compare.py ─→ results.json (deterministic, rounded)
results.json     ─→ plots.py   ─→ doc/figures/*.png
results.json + figures ─→ topology-study.tex ─→ topology-study.pdf
```

`results.json` is the single source of truth; tables and figures derive from it.
Determinism via rounding, consistent with the existing tools.

### Key figures

1. Max phase error versus sample rate, fixed versus per-fs, one curve per topology (the central thesis applied to all).
2. Quality-versus-cost Pareto: max phase error versus multiplies per sample at matched robustness (answers which topology gives the best accuracy per multiply; Niemitalo's 0.7°/8-multiply figure is the benchmark to beat).
3. Phase error versus order, per topology.
4. Group delay overlaid across topologies.

## Document structure (topology-study.tex)

One sentence per line; affirmative explanatory voice.

1. **Introduction** — the wideband quadrature problem and the question: which topology gives the best accuracy per cost, and how each behaves across sample rates. Historical lineage from Schroeder 1958 and Gerzon's unitary networks.
2. **Topologies under test** — each modern topology with references; the historical material as context.
3. **Method** — the uniform interface, the fixed and per-fs modes, the four metrics; the harness is the methodological contribution.
4. **Results** — the four figures plus comparison tables.
5. **Discussion and recommendation** — the ranking and the choice for the future standalone plugin.
6. **References** — reuse `refs.bib`.

## Testing and verification

- Each `topo_*.py`: all-pass magnitude unity, monotone phase, and per-fs `design()` reaches the quadrature target at its own rate.
- `compare.py`: `results.json` has an entry per topology × rate × order, values finite, deterministic across two runs.
- Regression guard on the known-good numbers (as the per-rate table guard), to catch scipy drift.
- Consistency: the RBJ topology in the study reproduces the numbers in `../../tools/coeffs_perfs.json`, linking the study to the already-validated work.
- `topology-study.tex` compiles to PDF with all citations resolved.

## Success criteria

- A reader sees, on one footing, how each topology trades phase accuracy against computational cost, and how each behaves across sample rates.
- The study yields a clear, evidence-based recommendation of a topology for the standalone quadrature plugin.
- Every figure, table, and number regenerates from one command via `results.json`.

## Risks and mitigations

1. The half-band (Harris-Berdahl-Abel) method needs careful implementation (pole motion plus warping).
   Implement it behind the same interface with its own tests; if it resists, document the partial result and proceed with RBJ and polyphase.
2. The historical Schroeder 1958 differential all-pass targets decorrelation, not a held 90°.
   Present its measurement as illustrative context, framed by its original design goal, separate from the benchmark ranking.
3. Per-rate optimisation runs many least-squares fits (topologies × rates × order).
   Cache results in `results.json`; keep the order sweep bounded to a sensible range.
```
