# strx — STONE Observation M/S Analyzer Design

Date: 2026-07-16.
Status: design approved (brainstorming), ready for an implementation plan.
Part of: the STONE auto-calibration system (Spec 1 of 4; ambit map in `doc/study/sessions/2026-07-14-stone-dslar.md`).
Depends on: `_common/seam_meter.h` (Phase A, math only). Adds `_common/seam_fft.h` and three functions to `seam.analyzers.lib`.

## Goal

Deliver `strx` (stone receiver): a standalone M/S observation analyzer VST3 plugin.
It is the instrument of the STONE "observation mode" — a receiver on a microphone track that reads the room's response to a STONE without touching the audio.
It stands on its own and is useful today; the calibration bus (Spec 2), transfer-function measurement (Spec 3), and auto-EQ (Spec 4) build on it later without rewriting it.

The plugin builds, loads in a host, passes the VST3 validator, and shows three live views fed from the audio: a decaying L/R goniometer, an M+S spectral curve, and M/S/Width meters.

## What the plugin is

`strx` is a pure analyzer.
Audio passes through unchanged (analysis only, no gain, no processing on the signal path).
The input is a single fixed stereo bus (2 channels).
One microphone feeds L and leaves R silent, so Side is zero and the Mid analysis still works; two microphones give Mid + Side, where Side reports the room's contribution to the STONE's sphericity.
Analysis is always M/S internally, regardless of how many microphones are patched.

The M/S frame is the single mental model for the whole plugin: the goniometer rotates the L/R scatter by 45° so mono (`L=R`, `S=0`) is a vertical line and anti-phase (`L=−R`, `M=0`) is horizontal, which is the same sum-and-difference geometry the meters read.

## Scope

In scope:

- Decaying L/R goniometer with Angle/Panorama readout.
- M and S spectral curves overlaid (long-term average).
- Meter columns: In L, In R, M, S, and a Width bar (mono ↔ 100% ↔ inv).
- The Faust anchoring: cite what exists, add the three new `san` analyzers.

Out of scope (Spec 2–4):

- No reference signal, no peer-aware calibration bus, no transfer-function measurement, no EQ.

Deferred to a separate discussion (after the first development step, GS 2026-07-16):

- Metering refinement — ballistics, true-peak (ITU-R BS.1770 oversampling), EBU R128.
- Spec 1 uses the `seam_meter.h` Phase A math as-is; sample-peak only, no true-peak.

## Architecture — one object, one data channel

`strx` is a single object `Seam::StrxProcessor : SingleComponentEffect, VSTGUI::VST3EditorDelegate`, the same shape as `dslar`.
The whole suite uses `SingleComponentEffect` (processor and controller fused), so there is one shared memory and two threads, not two components.

```
  [audio thread]  process()                       [GUI thread]  CVSTGUITimer ~60 Hz
        │                                                │
        ▼                                                ▼
  strx_dsp.h  ── writes ──►  AnalysisFrame  ◄── reads ── custom CView
   M/S (sdmx) · level                (triple-buffer         (goniometer, spectrum,
   followers · Welch M&S ·            lock-free member        meters)
   goniometer decimation ·            of the processor)
   correlation/width/pan
```

The realtime → GUI boundary is a single **lock-free triple-buffer** member of the processor.
The audio thread fills one slot and publishes it with an atomic index swap; the GUI thread always reads the most recent complete slot.
The audio thread never waits and the GUI thread never sees a half-written frame.
There is no `IMessage` (that is for separated components in different processes) and no output parameter for analysis; everything travels in the shared frame.

`AnalysisFrame` is a plain POD carrying, per publish:

- the decimated goniometer point cloud (`(x=S, y=M)` pairs, ~512–1024 points),
- the Welch magnitude arrays for M and S,
- the scalars: In L, In R, M, S levels; Width; correlation; Angle; Panorama.

Publish cadence is ~UI rate (driven by the FFT hop / a fixed sample count); the triple-buffer decouples exact timing so the audio thread just publishes when a frame is ready.

## Files — canonical layout

```
plugins/strx/
├── CMakeLists.txt              # registered via add_subdirectory in the root CMakeLists.txt
├── source/
│   ├── strx_ids.h             # plugin UID; no automatable parameter IDs
│   ├── strx_dsp.h             # SDK-free DSP core + AnalysisFrame + triple-buffer publisher
│   ├── strx_processor.h       # opens with the FAUST REFERENCE comment block
│   ├── strx_processor.cpp     # SingleComponentEffect wiring, stereo pass-through, createCustomView
│   ├── strx_goniometer.h      # custom CView: decaying L/R scatter + Angle/Panorama
│   ├── strx_spectrum.h        # custom CView: M+S curves via CGraphicsPath
│   ├── strx_meters.h          # custom CView: In L/R · M · S bars + Width bar
│   └── version.h
├── resource/strx.uidesc        # three zones side by side + custom-view tags
└── doc/                        # Faust spec citations, study notes
```

New shared code (outside `plugins/strx/`):

- `_common/seam_fft.h` — a header-only, SDK-free real radix-2 FFT. Built here, reused by Spec 3 (transfer function).
- `seam.analyzers.lib` — three new numeric analyzers (below).

The DSP core is the only unit with real substance; the processor is template wiring; each view is a focused, independently testable `CView`.

## `strx_dsp.h` — the analysis core (SDK-free)

Header-only, unit-testable, opening with a `FAUST REFERENCE (seam.stereophony.lib / seam.analyzers.lib)` block quoting `sst.sdmx`, `san.correlation`, `san.width`, `san.panorama`, `an.mth_octave_spectral_level`.
Interface: `prepare(double fs)`, `reset()`, `process(const float* L, const float* R, int n)`, and `bool tryReadFrame(AnalysisFrame&)` for the GUI side (triple-buffer read).

Analysis stages, each a hand port of its Faust anchor:

- **M/S matrix** — `M=(L+R)/√2`, `S=(L−R)/√2` (energy-preserving). Port of `sst.sdmx`.
- **Levels** — `seam::meter::LevelFollower` (RMS ~300 ms) on In L, In R, M, S for the bars, plus a sample-peak hold for the numeric readout. Reuses `seam_meter` math, the C++ echo of `an.amp_follower` / `an.rms_envelope_tau`.
- **Correlation / Width / Panorama / Angle** — running means of `L·R`, `L²`, `R²` → correlation; energy S/M → Width on the mono↔100%↔inv scale (negative correlation reads "inv"); balance and vector angle for the goniometer readout. Ports of the new `san.correlation` / `san.width` / `san.panorama`.
- **Spectrum** — Welch: Hann window 4096, 50% overlap, magnitude in dB, live exponential average (τ ≈ 2 s), computed for both M and S. Uses `seam_fft.h`. FAUST REFERENCE cites `an.mth_octave_spectral_level` as the idiomatic SR-independent equivalent; the C++ deliberately chooses FFT for a finer display curve.
- **Goniometer decimation** — collect `(x=S, y=M)` per block, decimate to ~512–1024 points by stride; aging/decay is done on the GUI side.

Note on SR: the FFT bin spacing scales with sample rate, but it maps onto a fixed log-Hz display axis, so this is cosmetic resolution, not a behavioral SR-dependence like the Haar crossovers.

## Faust anchoring — new `seam.analyzers.lib` functions

Following the seam-ltm rule (Faust is the spec, C++ is the deliverable) and the Faust-first order used by `ltburst` (Phase 2) and `dslar` (Phase 3).

Already present, cited only:

- M/S rotation → `sst.sdmx` (the goniometer's numeric process; the scatter and decay are GUI-only, nothing to specify in Faust).
- Levels / RMS → `an.amp_follower`, `an.rms_envelope_tau`, `san.pvmeter`.
- Spectrum → `an.mth_octave_spectral_level` (idiomatic, filterbank, SR-independent) cited as the equivalent of the C++ Welch-FFT.

New — written and verified in `seam.analyzers.lib` before the C++ port, each with an inline `// process = ...` test (SEAM convention), numeric only, no GUI:

- `san.correlation(l, r)` — running L/R correlation coefficient from moving means of `l·r`, `l²`, `r²`.
- `san.width(l, r)` — Side/Mid energy ratio mapped to the mono ↔ 100% ↔ inv scale.
- `san.panorama(l, r)` (and `san.vectorangle(l, r)`) — L/R balance and vector angle.

These fill a genuine gap in the official Faust libraries (only `ho.fxDecorrelation` is nearby, and it is unrelated), so they are candidates for an upstream contribution to GRAME, like the `env~` idea.

## GUI — three zones side by side

Layout in `strx.uidesc`: goniometer │ spectrum │ meters, each large and legible; custom views are built through `VST3EditorDelegate::createCustomView`, the same wiring as `dslar_reset_button.h`.
All three read the same `AnalysisFrame`.

- **`StrxGoniometer`** — a square view keeping a decaying point history (a ring of recent frames with a per-frame alpha, the Melda-style trail). Draws the circle, L/R axes, a diagonal grid, and the point cloud via `CGraphicsPath`; overlays `ANGLE x° · PANORAMA y%` at the bottom.
- **`StrxSpectrum`** — draws the M and S curves via `CGraphicsPath` on a log-frequency x-axis (20 Hz–20 kHz) and a dB y-axis, with a grid and an M/S colour legend.
- **`StrxMeters`** — simple bars for In L, In R, M, S plus the Width bar (mono/100%/inv), drawn minimally from the frame. This is the view the later metering discussion may refine or replace (ballistics, true-peak, R128).

Decay time and averaging τ are code constants, tunable in source like `kRampMs` in `dslar`.

## Parameters

None.
`strx` is pure observation, like the suite's stateless plugins (e.g. `sdmx`), which still ship a finished GUI.
Live EMA and goniometer decay are fixed constants in code; there is no automation and no state to persist.
`setState`/`getState` are no-ops.

## Build & verification

- CMake with `-G Xcode`, VST3 SDK at `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- New `san` functions verified first with inline `faust` compilation of their `process = ...` tests.
- VST3 validator clean (target parity with `dslar` at 47/47).
- Functional checks in Reaper: decorrelated pink → wide cloud, high Side energy, Width near 100%; mono → vertical line, Side at floor, "mono"; anti-phase (`L=−R`) → horizontal line, "inv".
- Lock-free review: `process()` takes no lock and allocates nothing; the triple-buffer invariant holds (the GUI never reads a partial frame).

## Deliverables

- `plugins/strx/` — the loadable VST3 analyzer with three live views.
- `_common/seam_fft.h` — reusable real radix-2 FFT.
- `seam.analyzers.lib` — `san.correlation`, `san.width`, `san.panorama` (+ `san.vectorangle`), each with an inline test.

## Out of scope (later specs)

- Calibration bus / peer-awareness (Spec 2).
- Transfer-function measurement `H = Y/X` (Spec 3).
- Auto-EQ synthesis (Spec 4).
- Metering refinement (ballistics, true-peak, EBU R128) — separate discussion.
