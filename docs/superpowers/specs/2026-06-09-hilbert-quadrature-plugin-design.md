# hilbert — standalone quadrature/Hilbert plugin (Design Spec)

Date: 2026-06-09
Status: Approved (brainstorming), pending spec review
Owner: Giuseppe Silvi (grammaton)
Related: [UHJ quadrature fs-dependence finding] and the seam_quadrature engine (docs/superpowers/specs/2026-06-08-seam-quadrature-engine-uhj-fix-design.md); the topology study (plugins/x2uhj/study-topologies).

## Purpose

`hilbert` is a pedagogical VST3 plugin that realizes a wideband Hilbert/quadrature transformer and lets the listener A/B different all-pass topologies on the same source.
It is a teaching object for the SEAM-LTM suite: students hear and read how a 90° quadrature pair is built, and compare topologies by accuracy and cost at the host sample rate.
It is the standalone consumer of the reusable `seam_quadrature` engine introduced for x2uhj; this plugin extends that engine rather than duplicating it.

The name drops the suite's `x` (which stands for AmbiX): this plugin is mono → stereo and has nothing to do with ambisonics.

## Audio I/O

One mono input → two outputs.
Output `[0]` is the in-phase branch, output `[1]` is the quadrature branch.
Both outputs are all-pass-filtered versions of the input; their phase difference holds −90° across 20 Hz–20 kHz.
Neither output is the dry signal: the quadrature relationship is a property of the *pair*, as in the x2uhj encoder's internal `H_R`/`H_I` networks.

## Topologies shipped

Two topologies, selectable live, each at a fixed sensible order:

| Topology | Order | Max error @48k | Cost (mult/sample) |
|---|---|---|---|
| RBJ biquad cascade | 3 | 1.36° | 24 |
| Polyphase first-order (Niemitalo) | 4 | 1.67° | 8 |

Both are designed live at the actual sample rate by a minimax phase fit (the same Levenberg-Marquardt core already used for RBJ in x2uhj), so accuracy holds across rates.
The contrast is the pedagogical payload: the polyphase form nearly matches RBJ accuracy at a third of the cost.

The elliptic half-band topology is **deferred** (own future phase): its Cauer synthesis (Jacobi elliptic functions + root finding) is a large, self-contained numerical module, and its design is fs-independent, so it does not fit the "extend the live engine" shape of this iteration.

## Engine changes (plugins/_common/seam_quadrature.h)

Approach: two design types and one configurable processor, sharing the LM core.

- Factor the existing Levenberg-Marquardt loop out of `designQuadrature` into a reusable residual-driven solver core (a function templated/parameterised on a residual callback, the parameter vector, and bounds), so both designers call it.
- Add `PolyphaseDesign { double a[ ]; int nA; int nB; /* path A then path B */ double maxErrorDeg; double sampleRate; bool converged; }` and `designPolyphase(double fs, int order)`.
  - Section model: `H(z) = (a² − z⁻²)/(1 − a² z⁻²)`, one all-pass first-order section per coefficient.
  - Residual: `phase(pathB) − phase(pathA) − w − (−π/2)`, where `w` is the one-sample relative delay on path B (matches `topo_polyphase.py`).
  - Seed: the published Niemitalo order-4 coefficients (`_FIXED_A`, `_FIXED_B`); bounds `[0.01, 0.999999]`.
- Add a runtime `QuadraturePair` processor that can be configured from either design:
  - from a `QuadratureDesign`: two RBJ biquad cascades (the existing `AllpassSection`/network math);
  - from a `PolyphaseDesign`: two paths of first-order all-pass sections, with a one-sample delay on path B;
  - exposes `prepare(fs)`-style configuration and `process(double x, double& inPhase, double& quad)`.
- `designQuadrature` keeps its current signature and behaviour; x2uhj is **not** modified.

## Plugin (plugins/hilbert/)

File layout, following the canonical pattern (ddelay/bamodulex/x2uhj):

```
plugins/hilbert/
├── CMakeLists.txt
├── source/
│   ├── hilbert_ids.h          # UID, the Topology parameter id
│   ├── hilbert_processor.h    # SingleComponentEffect + VST3EditorDelegate
│   ├── hilbert_processor.cpp  # I/O, topology dispatch, createCustomView
│   ├── hilbert_readout_view.h # adaptive coefficient/error readout
│   └── version.h
└── resource/
    └── hilbert.uidesc         # title block, topology menu, readout, logo
```

- Registered in the root `CMakeLists.txt` via `add_subdirectory(plugins/hilbert)`.
- One user parameter: `Topology` (enum, RBJ / Polyphase). Changing it re-runs the matching designer and reconfigures the `QuadraturePair`; the readout refreshes.
- The DSP holds the active design (RBJ or polyphase) and the `QuadraturePair`; `setActive(true)` and topology changes call the right designer with `processSetup.sampleRate`.

## GUI

Reuse the suite text scheme established by x2uhj (Source Code Pro Light; `TextDim` labels, `TextLight` data; fonts/colors resolved from the uidesc).

- Title block: name + subtitle (e.g. "Hilbert transformer", "quadrature ±90° · mono → I/Q") + the suite logo.
- A `COptionMenu` bound to the `Topology` parameter.
- An **adaptive readout view** (generalising `QuadratureReadoutView`):
  - RBJ active: `HR`/`HI` rows of `f Hz  Q` (as in x2uhj);
  - Polyphase active: path `A`/`B` rows of the `a` coefficients;
  - footer line: `max err N deg @ M kHz`.
  - Refreshes on hot sample-rate or topology change via `onIdle` polling a small state signature (sampleRate + topology).

## Testing

SDK-free doctest target under `tests/` (pattern of `seam_quadrature_test`), built and run via ctest:

1. Each polyphase section is all-pass: `|H(e^jw)| == 1` across the band.
2. `designPolyphase(fs, 4)` reproduces the study's order-4 error (≈ 1.67° @48k) within a small margin, and converges across the standard rates (44.1–192k).
3. `QuadraturePair` configured from each topology produces two outputs whose measured phase difference is ≈ −90° across a frequency sweep (within each topology's achieved error).
4. Determinism: repeated designs at the same `(fs, order)` return identical coefficients.

GUI rendering has no unit test: verification is build + visual load (report the readout at 48 kHz for both topologies, and the live topology switch).

## Out of scope (YAGNI)

- User-adjustable order (fixed per topology this iteration).
- Elliptic half-band topology (deferred).
- Refactoring x2uhj to share the `QuadraturePair` processor (no speculative unification; x2uhj already consumes the RBJ designer and stays as-is).
- A Faust `seam.filters.lib` spec for the quadrature pair (separate roadmap item).

## Follow-up: FIR Hilbert (analytic-signal form)

All three topologies in this plugin are IIR all-pass **phase-difference networks**: both outputs are all-pass-filtered, their phase difference holds −90°, and *neither output equals the input*.
This matches Max/Pd `hilbert~` (Pd's is literally an all-pass pair; see the Puckette material in `plugins/x2uhj/_temp/`) and the topologies measured in the study.
The reason is structural: the design constrains only the relative phase (−90°), not the absolute phase of either branch, and a wideband IIR all-pass cannot realize a pure delay, so one branch cannot equal the input.

A complementary teaching object is the **FIR Hilbert with a reference delay**: the real branch is the input delayed to match the FIR group delay, and the imaginary branch is the FIR Hilbert transform — so the real output *is* the (delayed) input, realizing the textbook analytic signal `x(t) + j·x̂(t)`.
Adding this as a fourth menu entry would let the listener compare the phase-difference family against the true analytic-signal form, which is precisely the distinction that makes the "neither output is the dry signal" property concrete.
References for the ideal transform and the analytic signal: Bracewell; Oppenheim–Schafer.
Deferred to a future iteration (its own brainstorm + spec); tracked as GitHub issue #5.
