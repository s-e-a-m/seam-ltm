# Development log — SEAM Quadrature Engine + x2uhj UHJ Fix

Date: 2026-06-08.
Branch: `docs/x2uhj-uhj-math`.
Plan: ../plans/2026-06-08-seam-quadrature-engine-uhj-fix.md
Spec: ../specs/2026-06-08-seam-quadrature-engine-uhj-fix-design.md

## Decision: a reusable engine instead of a static coefficient table

We built a reusable internal optimiser in `plugins/_common/seam_quadrature.h` rather than shipping the fixed coefficient table.
The earlier x2uhj design baked one `(f, Q)` table that was accurate mainly at 48 kHz, because a fixed RBJ-derived `(f, Q)` set is not sample-rate independent.
Designing the all-pass pair live at the host sample rate is the correct method, so the engine recomputes the coefficients inside `UHJEncoder::prepare(fs)`.
Placing the engine in `_common` makes it a shared analysis primitive that future plugins (a standalone quadrature plugin, filter studies) can reuse.

## Method

The engine designs the `H_R`/`H_I` all-pass quadrature pair by a minimax phase fit over a 512-point log-spaced grid on [20 Hz, 20 kHz].
Each section is an RBJ all-pass biquad; the cascade phase is the per-section unwrap-then-sum, matching the numpy reference in `design_quadrature_perfs.py`.
The residual is `phase(H_I) - phase(H_R) - (-pi/2)`, and the solver is Levenberg-Marquardt with a forward-difference numerical Jacobian.
For the shipped order (3 sections) the solver is seeded with the proven Python seed; other orders spread `f` geometrically across the band with `Q = 0.3`.
Parameters are clamped to `f in [10, fs/2-1]` and `Q in [0.01, 5]`, and the `converged` flag reports whether the achieved max error stays under 10 degrees.

## Measured outcomes

The C++ engine reproduces the Python per-rate table essentially exactly (max phase-difference error, degrees):

| fs (Hz) | engine | reference table |
|---|---|---|
| 44100  | 2.040 | 2.04 |
| 48000  | 1.362 | 1.36 |
| 88200  | 0.530 | 0.53 |
| 96000  | 0.506 | 0.51 |
| 176400 | 0.430 | 0.43 |
| 192000 | 0.426 | 0.43 |

All six designs converge; every value lands within the cross-validation test's 0.15-degree margin.
The x2uhj per-rate check confirms `converged` and `maxErrorDeg < 2.5` at 44.1, 48, 96, and 192 kHz.

## GUI readout values

The readout view prints the live-designed `(fc, Q)` pairs and the achieved error for the current host sample rate.
Visual confirmation in a host is still pending; the values below are the engine output the view renders.

At 48 kHz (max err 1.36 deg):

```
HR   141.88 Hz  Q 0.2019      HI    24.00 Hz  Q 0.3090
HR   671.73 Hz  Q 0.2122      HI  2991.88 Hz  Q 0.3848
HR 18654.18 Hz  Q 0.3031      HI  3219.93 Hz  Q 0.0963
```

At 96 kHz (max err 0.51 deg):

```
HR   112.78 Hz  Q 0.2227      HI    21.06 Hz  Q 0.3178
HR   478.58 Hz  Q 0.2360      HI  1911.06 Hz  Q 0.3999
HR 18997.52 Hz  Q 0.3173      HI  2043.47 Hz  Q 0.1160
```

## Files added or changed

Added: `plugins/_common/seam_quadrature.h`, `tests/seam_quadrature_test.cpp`, `plugins/x2uhj/source/x2uhj_readout_view.h`, this log.
Changed: `plugins/x2uhj/source/x2uhj_dsp.h` (calls the engine in `prepare`, exposes `design()`), `plugins/x2uhj/source/x2uhj_processor.{h,cpp}` (VST3EditorDelegate + `createCustomView`), `plugins/x2uhj/resource/x2uhj.uidesc` (readout view, container grown to 300x300), `plugins/x2uhj/CMakeLists.txt` (readout header + `_common` include path), `tests/CMakeLists.txt` and `tests/x2uhj_dsp_test.cpp` (engine include path + per-rate design check).

Kept in the tree for reference: `plugins/x2uhj/tools/coeffs_perfs.json`, the Python `emit_header.py`, and `plugins/x2uhj/source/x2uhj_coeffs.h`.
The DSP no longer includes `x2uhj_coeffs.h`, but the file stays as a record of the original static design.

One build fix landed alongside Task 4: the x2uhj plugin target lacked `plugins/_common` on its include path (only the test target had it), so the engine header was unresolved; the path now matches the tests.

## Postscript — readout polish and visual confirmation (2026-06-08)

The readout is now confirmed in the host at 48 and 96 kHz, so the values above are verified, not just engine output.
The view adopts the suite text scheme: `TextDim` labels and `TextLight` data in Source Code Pro Light, all resolved from the uidesc rather than hard-coded.
A font must be set on the draw context explicitly, because outside the UI editor no default font is bound and `drawString` would otherwise paint nothing.
The HR/HI coefficients render as a table with a narrow tag column, and the achieved error plus design sample rate sit on a separate footer line (`max err N deg @ M kHz`).
`QuadratureDesign` gained a `sampleRate` field so the footer can name the rate the design was computed at.
A hot sample-rate change refreshes the readout on its own: `prepare(fs)` already re-runs in `setActive`, and the view now polls `design().sampleRate` in `onIdle` and invalidates itself when it changes.
The info line `L R T Q · quadrature ±90°` was aligned with multipink (InfoFont size 11, row at y=66) for a consistent header across the suite.

## Open follow-ups

A standalone quadrature plugin that exposes the three topologies remains planned.
A `seam.filters.lib` Faust spec should capture the per-rate design as the upstream specification.
Future PD/Max/Csound comparisons of the quadrature topologies stay on the roadmap.
