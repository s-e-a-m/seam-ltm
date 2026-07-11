# seam-ltm Metering System — Design & Roadmap

Date: 2026-07-11.
Status: design approved (brainstorming), roadmap for a suite-wide subsystem.
First consumer: the `dslar` C++ plugin (see `2026-07-11-dslar-cpp-plugin-design.md`).

## Purpose

The seam-ltm suite has no shared way to show a DSP-derived value on screen.
Today only `multipink` publishes read-only values, and it draws them as bare numeric `CParamDisplay` fields.
This document plans a coherent metering subsystem for the whole suite, so every plugin that needs to surface a level, a gain, an attenuation, or a loudness reading does it the same way — same transport, same scale, same visual language.

The subsystem is planned in full here; only the slice `dslar` needs now (Phase A) is built now.
`dslar` is the reference consumer, exactly as `multipink` was the reference consumer for the peer-aware `multipink_pool` pattern.

## Design principle — coherence across three layers

A metering value travels through three layers, and coherence is imposed on all three, not only on the drawing.

1. **Measure** — in the SDK-free `<plugin>_dsp.h`: a follower with ballistics plus a `linear → dB → normalized` conversion at a fixed floor.
2. **Transport** — in `<plugin>_processor.cpp`: the `multipink` idiom (a `kIsReadOnly` `RangeParameter` pushed from `process()` via `data.outputParameterChanges`), turned into one standard call with one standard normalization so the GUI mapping is identical everywhere.
3. **Render** — in `<plugin>.uidesc`: a read-only slider bar plus a numeric dB label, house colors, with a dedicated `MeterFill` color that visually separates a meter from an interactive control.

## Architecture — two headers under `plugins/_common/`

The subsystem lives in `plugins/_common/`, the established home of header-only shared helpers (`seam_quadrature.h`, `seam_rotation.h`, `seam_haar.h`, `seam_btox.h`) and shared resources (logo, font).
Plugins already include `_common` via `target_include_directories(<target> PRIVATE .../_common)`.

### M0 — `_common/seam_meter.h` (core, lightweight, header-only)

The everyday meter facility, kept pure so it is usable inside the SDK-free `_dsp.h`.

- dB helpers: `lin2db(x, floorDb)`, `db2norm(db, floorDb)` returning `[0,1]`, `norm2db(...)` for label formatting.
- `LevelFollower` — a small one-pole follower with a mode (`Peak` / `Rms`) and a time-constant window; `prepare(fs, mode, windowMs)`, `feed(x)`, `value()` (linear amplitude; take dB via `lin2db(value())`). True peak-hold ballistics are deferred to the first consumer that needs them.
- Meter-type vocabulary expressed through these primitives:
  - **Level (dBFS)** — RMS or peak of an audio signal, floor default −60 dBFS.
  - **Gain** — a linear control multiplier `[0..1]` (or beyond) shown in dB; `dslar`'s `analysisGain` is this.
  - **Gain reduction** — attenuation shown growing downward, `db2norm` fed the negative reduction.
  - **Generic `[0..1]`** — a normalized quantity with no dB semantics (position, activity, correlation), bypassing the dB stage.

The VST3 transport touches SDK types, so it does not live in the pure header.
The three-line publish idiom is documented here, and MAY be factored into a thin `seam_meter_vst.h` wrapper (included only from the processor `.cpp`, which already links the SDK) once a second consumer exists.
Phase A keeps the idiom inline in `dslar_processor.cpp` and does not create the wrapper (YAGNI).

### M1 — `_common/seam_loudness.h` (EBU R128 / ITU-R BS.1770, heavier, separate)

A dedicated module, deliberately outside `seam_meter.h`.

- K-weighting (a high-shelf plus a highpass biquad, ITU-R BS.1770).
- Momentary (400 ms), short-term (3 s), and gated integrated LUFS (−70 and relative −10 LUFS gates).
- True-peak (dBTP) via 4× oversampling.
- Loudness range (LRA) over the short-term distribution.

R128 is a substantial DSP block; keeping it separate keeps `seam_meter.h` header-only and light, which is all `dslar` needs today.
It is built when a plugin genuinely needs loudness (an output/mastering stage, or a suite-wide output meter), each with its own design→plan→implement cycle.

## Render layer — VSTGUI reconnaissance and roadmap

The render choice is independent of the transport: the read-only parameter path is unchanged whatever the widget draws.
A reconnaissance of VSTGUI (`vstgui4/.../lib/controls/`) in the bundled SDK found these options for a level widget.

- `CSlider` read-only — a filled bar in house colors, no assets; reads a bit like a control and offers only a fill (no ticks, no peak-hold).
- `CParamDisplay` — an exact numeric dB readout (the `multipink` idiom); a companion to a bar, not a replacement.
- `CVuMeter` — the SDK's dedicated LED-ladder meter with built-in peak-hold (`setDecreaseStepValue`), but bitmap-based (on/off image strips); rejected because it needs an asset pipeline the suite avoids and its LED look clashes with the vector uidesc style.
- `CXYPad` / a custom `CView` — free vector drawing, no bitmaps; the basis for a dB scale with ticks and peak-hold, or for a transfer-curve view.

Because `db2norm` scales to dB in the transport, even the plain read-only slider shows a dB-scaled bar — which matters for `dslar`, whose loop gain spans a very wide dB range.

**Render decision.**
Phase A ships the baseline render: a read-only `CSlider` bar plus a `CParamDisplay` dB label, with the `MeterFill` color.

**Deferred render (todo, from this reconnaissance).**
Build one reusable custom `CView` meter — dB scale with ticks, peak-hold marker, `MeterFill` theming, no bitmaps — as the canonical render of the metering convention, replacing the repurposed read-only slider across the suite.
Add, as the richer pedagogical option, a transfer-curve view that plots the homeostat law `g = |r − ref|^k` with a live operating point at `(r, g)`, so the GUI shows the control law itself rather than only a level.
This lands in a later cycle; it does not gate Phase A.

## Phased delivery

| Phase | Content | When |
|---|---|---|
| **A** | `seam_meter.h` core (dB helpers + `LevelFollower`) with **Level** and **Gain**; the GUI convention (`MeterFill` color, read-only slider bar + dB label); the suite's first graphical bar meter | now, with `dslar` (r = Level dBFS, g = Gain) |
| **B** | **Gain reduction** and **Generic `[0..1]`** formalized as first-class helpers; the reusable custom `CView` meter (dB scale + ticks + peak-hold) and the `dslar` transfer-curve view; `multipink`'s numeric read-outs may migrate to bars | at the first real need |
| **C** | `seam_loudness.h` EBU R128 module | dedicated effort, when loudness is required |

## Retrofit policy

The facility is designed for reuse, and past plugins adopt it only when each has a real, user-facing need — the same anti-speculative-retrofit discipline CLAUDE.md states for the peer-aware pattern.
No plugin is retrofitted in this cycle; only `dslar` consumes the facility.
The obvious low-risk future candidate is `multipink`, whose existing numeric read-outs would become bars with no DSP change.

## Success criteria

- A new `plugins/_common/seam_meter.h` exists, header-only, usable from an SDK-free `_dsp.h`, and compiles into `dslar`.
- `dslar` shows two live bars (r and g) driven end to end through the three layers, with the dB scale and floor defined here.
- The GUI convention (colors, bar + label, `MeterFill`) is documented and reproducible by the next plugin without reading `dslar`'s internals.
- No past plugin is modified.

## Open questions carried forward

- The exact `MeterFill` color value versus the existing `SliderActive` cyan (a meter should read as distinct from an interactive slider); resolved during the `dslar` GUI build.
- Whether Phase B's generic indicator subsumes `multipink`'s pool-status string display or leaves it as-is.
- The R128 gating and true-peak details are deferred to the Phase C design.
