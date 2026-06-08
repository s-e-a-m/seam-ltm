# SEAM Quadrature Engine + x2uhj UHJ Fix (Design Spec)

Date: 2026-06-08
Status: Approved (brainstorming), pending spec review
Owner: Giuseppe Silvi (grammaton)
Related: [UHJ quadrature fs-dependence finding](../../../plugins/x2uhj) and the topology study spec (2026-06-07).

## Purpose

Replace the x2uhj plugin's fixed 48 kHz quadrature coefficients with a live, sample-rate-aware design.
The design comes from a reusable C++ quadrature engine that computes the all-pass (f, Q) pairs at the actual sample rate, so the quadrature holds across every rate the host provides.
The engine is a suite asset: future plugins (a standalone quadrature filter, a Schroeder mono-to-pseudostereo, musical correlators, an adaptive Larsen feedback suppressor) reuse the same analysis function.

## Background

A research spike established that sample-rate independence is a property of the design procedure, rather than a property of a stored coefficient set.
A fixed (f, Q) set realised through the bilinear form holds the 90 degree quadrature only near the rate it was designed for; the shipped x2uhj plugin is accurate mainly at 48 kHz and drifts to tens of degrees at other rates.
The Python harness `design_quadrature_perfs.py` already computes the correct per-rate design; this work ports that procedure to C++ so the plugin performs the design itself at startup.
Giuseppe chose the internal-optimiser approach over a static table, because a runtime analysis function becomes a reusable, composable skill of the suite.

## Architecture

Three units with clear boundaries.

```
plugins/_common/seam_quadrature.h   (engine: designQuadrature(fs, band, n))
        ├──────────► x2uhj UHJEncoder::prepare(fs)        (DSP: live coefficients)
        └──────────► x2uhj GUI readout (fc/Q + max error)  (display)
```

1. **The engine** `plugins/_common/seam_quadrature.h`, header-only, no VST SDK dependency, reusable across plugins.
2. **x2uhj as first consumer**: `UHJEncoder::prepare(fs)` calls the engine instead of including the fixed coefficient header.
3. **GUI readout**: the existing x2uhj editor gains a panel that prints the six computed (fc, Q) pairs and the achieved maximum phase error at the current sample rate.

## Unit 1 — The quadrature engine (`plugins/_common/seam_quadrature.h`)

A header-only port of the Python minimax phase fit.

### Interface

```cpp
namespace seam { namespace quadrature {

struct APSpec { double f; double Q; };

struct QuadratureDesign {
    static constexpr int kMaxSections = 8;
    APSpec hr[kMaxSections];   // real-path sections
    APSpec hi[kMaxSections];   // imaginary-path sections
    int    nSections = 0;
    double maxErrorDeg = 0.0;  // achieved max |phase diff + 90 deg| over the band
    bool   converged = false;  // false when the fit fell back to the safety seed
};

// Designs the H_R / H_I quadrature pair at sample rate fs over [fLo, fHi].
QuadratureDesign designQuadrature(double fs, double fLo, double fHi, int nSections);

}} // namespace
```

### Algorithm

- Objective: minimise the maximum deviation of `phase(H_I) - phase(H_R)` from -90 degrees over a log-spaced grid across `[fLo, fHi]` (default 20 Hz to 20 kHz), using least-squares on the residual vector (the same formulation as the Python harness).
- The phase of one RBJ all-pass section at (f, Q, fs) comes from the biquad response, reusing the same coefficient math as `AllpassSection`.
- Solver: Gauss-Newton / Levenberg-Marquardt with a numerical (finite-difference) Jacobian over the 2 x nSections x 2 parameters, seeded from (f, Q) pairs spread logarithmically across the band, iterated to convergence with parameter clamping to the bounds (f in (10 Hz, fs/2), Q in (0.01, 5)).
- Determinism: identical fs yields identical output; the solver carries no randomness.

### Robustness

- **Cross-validation against Python:** at the six canonical rates (44.1/48/88.2/96/176.4/192 kHz) the engine reproduces the numbers in `plugins/x2uhj/tools/coeffs_perfs.json` within tolerance (max-error agreement under 0.1 degrees).
- **Convergence guard and fallback:** when the fit fails to reach a sane error (a degeneracy or an extreme fs), `designQuadrature` returns the band-spread seed with `converged = false`, so the audio path always receives a stable, finite design.

## Unit 2 — x2uhj integration

- `UHJEncoder::prepare(double fs)` calls `seam::quadrature::designQuadrature(fs, 20.0, 20000.0, 3)` and configures the six networks (`hrW, hrX, hrY, hrZ`, `hiW, hiX`) from the returned (f, Q) pairs, replacing the current use of the fixed `kHR`/`kHI` tables.
- `prepare` runs from `setActive`, off the audio thread, so the design cost at startup and at each sample-rate change is acceptable.
- The encoder retains the latest `QuadratureDesign` so the editor can read it for the GUI readout.
- The generated `x2uhj_coeffs.h` and its toolchain (`emit_header.py`, the shipped `coeffs.json`) stop feeding the plugin DSP; they remain in the tree for reference and for the Python harness. The plugin DSP no longer includes the fixed coefficient header.

## Unit 3 — GUI readout

- The existing x2uhj editor gains a read-only panel that displays, for the current sample rate: the six (fc, Q) pairs (H_R and H_I) and the achieved maximum phase error in degrees, plus a flag when the design fell back to the safety seed.
- The values update whenever `prepare` runs (a sample-rate change), reading the encoder's retained `QuadratureDesign`.
- The panel follows the existing branded VSTGUI style of the suite; it presents data and accepts no input.

## Testing and verification

- **Engine cross-check (C++):** a small test target builds `seam_quadrature.h` and asserts that `designQuadrature` at the six canonical rates matches `coeffs_perfs.json` within the stated tolerance. The reference numbers are embedded in the test from the JSON.
- **Allpass-phase agreement:** the engine's single-section phase matches the Python `rbj.cascade_phase` for a known (f, Q, fs) within a tight tolerance, anchoring the C++ phase math to the validated Python math.
- **Fallback path:** a test drives an fs or configuration that trips the guard and confirms `converged == false` with a finite, band-spread design.
- **Build:** the suite builds with CMake including `_common/seam_quadrature.h` and the new test target; x2uhj compiles and loads.
- **Manual audio check:** x2uhj loaded at 44.1/48/96/192 kHz produces the expected quadrature, and the GUI readout shows the computed (fc, Q) and a small max error at each rate.

## Scope

### In scope

- The reusable engine `plugins/_common/seam_quadrature.h`.
- x2uhj DSP wired to the engine.
- The x2uhj GUI numeric readout.
- The engine cross-validation test target.

### Out of scope

- The standalone quadrature plugin (a later work; a future consumer of the engine).
- Other future consumers (Schroeder pseudostereo, correlators, Larsen suppressor).
- Topology choices beyond the RBJ second-order cascade (the engine designs the RBJ form; the topology study covers the alternatives).
- Removing `coeffs.json` / `emit_header.py` / `x2uhj_coeffs.h` from the tree (kept for reference and the Python harness).

## Risks and mitigations

1. The Gauss-Newton / LM fit is moderately non-convex.
   The Python harness proves convergence from a band-spread seed at every canonical rate; the C++ port uses the same seed strategy and the cross-validation test catches regressions.
2. A hand-written solver in C++ adds complexity to a pedagogical suite.
   The numerical Jacobian keeps the code readable (no hand-derived phase derivatives), and the routine runs once per sample-rate change rather than per sample; the engine is documented as a teaching object.
3. An extreme or non-canonical sample rate could fail to converge.
   The convergence guard returns the stable band-spread seed with `converged = false`, so the audio path stays valid and the GUI surfaces the fallback.
4. GUI plumbing in the combined SingleComponentEffect.
   The editor reads the encoder's retained design; the readout is display-only, which keeps the parameter model unchanged.
