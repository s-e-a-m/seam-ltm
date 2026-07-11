# dslar — C++ VST3 Plugin Design

Date: 2026-07-11.
Status: design approved (brainstorming), ready for an implementation plan.
Depends on: the metering system Phase A (`2026-07-11-seam-ltm-metering-system-design.md`).
Faust spec (frozen): `sds.lar` in `seam.discipio.lib`, composing `spd.*` clones in `seam.pdclone.lib`; entry `plugins/dslar/doc/dslar.dsp`.

## Goal

Deliver the loadable `dslar` VST3 plugin: a hand-written C++ re-implementation of the frozen Faust spec `sds.lar`, following the project rule that Faust is the specification and readable C++ is the deliverable.
The plugin builds, loads in a host, and shows the Larsen homeostat working through two live meters.
Formal validation (the SR-independence and Pd↔Faust block-kernel A/B axes, and the English `doc/math`) is a later phase and is out of scope here.

## What the plugin is

`LAR.pd` is a mono feedforward processor; the Larsen loop is acoustic and external (`dac~ → room → mic → adc~`).
So `dslar` is a pure feedforward mono chain with no internal `~` feedback, and `tab1` is a feedforward delay, not a recursion.
This finding, established in Phase 3, is the contract the C++ port honors.

## Architecture — files (canonical `ltburst`/`ddelay` layout)

```
plugins/dslar/
├── CMakeLists.txt              # registered via add_subdirectory in the root CMakeLists.txt
├── source/
│   ├── dslar_ids.h            # plugin UID, 8 parameter IDs, 2 read-only meter tags (r, g)
│   ├── dslar_dsp.h            # SDK-free DSP core: the hand port of sds.lar (the substance)
│   ├── dslar_processor.h      # opens with the FAUST REFERENCE comment block
│   ├── dslar_processor.cpp    # IAudioProcessor wiring, mono 1-in/1-out, publishes r and g
│   └── version.h
├── resource/dslar.uidesc      # GUI: two columns by DSP role + two read-only meter bars
└── doc/                       # already present (Faust spec, study diary, svg, mathdoc)
```

The DSP core is the only unit with real substance; the rest is template wiring.

## `dslar_dsp.h` — hand port of `sds.lar`

A header-only, SDK-free, unit-testable `Seam::dslar::Larsen` class, opening with a `FAUST REFERENCE (seam.discipio.lib / seam.pdclone.lib)` block that quotes `sds.lar`, `larsengain`, `spd.env`, `spd.hip`, `spd.line`.
Interface: `prepare(double fs)`, `reset()`, `process(double x)`, plus meter getters `analysisGain()` and `measuredRms()`.
Setters mirror the eight parameters.

The chain is feedforward, no recursion:

- **Input fade** — `spd.line(2000 ms)` driven by `Power` (0/1), 20 ms grain staircase, applied before the audio/analysis split (the `adc~` fade in the patch).
- **Audio branch** — `spd.hip(100)` then `× drive` then `delay(tab1)`.
- **Analysis branch** — `delay(tab2)` then the overlap-add `env` then `dbtorms` (this control-rate RMS is `r`) then the homeostat `|r − ref|^k` then `spd.line(tsmooth)` (this smoothed loop gain is `g`).
- **Output** — `y = audioBranch · g · output`.

### Component translation notes

- **`spd.hip(100)`** — the Pd one-pole highpass `sighip`, topology `fi.pole(coef) : fi.zero(1) : *(normal)` with Pd's literal truncated `3.14159` in `coef`, NOT `fi.highpass` (Butterworth).
  Ported as the exact one-pole difference equation so `coef` is bit-identical to Pd.
- **overlap-add `env`** — Pd's `env~` structure, O(overlap) not O(npoints): full-rate accumulation of Hann-weighted `x²` into a few staggered lanes, read out every `period`.
  The window is SR-adaptive in the C++ deliverable: `N = round(fs · 2048 / 44100)`, `period = N/2`, computed at `prepare(fs)`; the constraint `N % period == 0` always holds (`period = N/2`).
  This keeps temporal behavior SR-invariant (the Di Scipio SR-independence rule).
- **`spd.line`** — the Pd control-rate `line`: linear `v = setval + min(elapsed/R,1)·(target−setval)` with restart-from-current, sampled at the 20 ms grain into a staircase.
  Two instances: the 2000 ms input fade and the `tsmooth` control smoothing.
- **`dbtorms` / homeostat** — `dbtorms` from `x_acoustics.c`; homeostat `|r − ref|^k` with LAR values `ref = 1`, `k = 40`.
- **delays** — feedforward `de.delay`; buffers sized once for the worst case `DELMAXMS = 200 ms` at `SRMAX = 192 kHz`, read offset adapting to `fs` at runtime.

`r` and `g` are control-rate values (updated each `period` / grain, held between ticks), faithful to Pd's block kernel and cheap to publish to the GUI.

## Parameters

The eight of `dslar.dsp`, read as `std::atomic<double>` (the `ltburst` pattern):

| Parameter | Range / default | Role |
|---|---|---|
| Power | checkbox, off | system on/off, drives the 2000 ms anti-click fade |
| Drive | 1 .. 4, def 1 | audio pre-gain (Pd presets 1/2/4) |
| Target | 0 .. 1, def 1 | homeostat reference (`ref`) |
| Steepness | 1 .. 80, def 40 | homeostat exponent (`k`) |
| Control smoothing | 1 .. 1000 ms, def 200 | `tsmooth` line on the loop gain |
| Loop delay | 1 .. 200 ms, def 50 | `tab1` feedforward delay |
| Decorrelation | 1 .. 200 ms, def 20 | `tab2` analysis tap |
| Output | 0 .. 1, def 1 | final VCA to host |

Musical smoothing already lives inside the spec (the internal `line` ramps), so no extra per-block ramp is added beyond a light anti-zipper on Drive and Output at the VST3 boundary.

## Metering (consumes Phase A of the metering system)

Two read-only meters, both driven through the three-layer facility of `seam_meter.h`:

- **`r` — Level (dBFS)** — the Hann RMS, the homeostat input, tapped at `dbtorms` output.
- **`g` — Gain** — the loop gain at the `spd.line(tsmooth)` output, faithful to the slider Di Scipio uses in `LAR.pd` to show the attenuation at the line output.

Measure in `dslar_dsp.h` (the getters), transport via the `multipink` idiom in `dslar_processor.cpp` (two `kIsReadOnly` params pushed from `process()`), render in `dslar.uidesc` as two read-only slider bars with dB labels.
Because the dB→normalized conversion happens in transport, these read-only slider bars are already dB-scaled, which matters because `g` spans a very wide dB range.
This is the suite's first graphical bar meter.

**Deferred render (todo).**
A VSTGUI reconnaissance (recorded in the metering roadmap) confirmed the SDK's `CVuMeter` is bitmap-based and off-style for the suite; the richer render is a reusable custom `CView` meter (dB scale, ticks, peak-hold, `MeterFill`) plus a transfer-curve view plotting the homeostat law `g = |r − ref|^k` with a live operating point.
Phase A ships the baseline read-only slider render; the custom meter and the transfer curve are a later cycle and do not gate this plugin.

## GUI

House style unchanged: dark `#292c2f`, Source Code Pro Light, cyan accent, plus a new `MeterFill` color for the meters.
Layout is two columns by DSP role (AUDIO PATH: Drive, Loop delay, Decorrelation · ANALYSIS: Target, Steepness, Control smoothing, Output), `Power` at the top, and the two `r`/`g` meter bars across the bottom.

```
┌────────────────────────────────────────────┐
│                SEAM DSLAR                   │
│        Agostino Di Scipio · LAR             │
│  [x] Power                                  │
│  ── AUDIO PATH ──      ── ANALYSIS ──       │
│  Drive                 Target               │
│  Loop delay (ms)       Steepness            │
│  Decorrelation (ms)    Control smooth (ms)  │
│                        Output               │
│  r (Hann RMS)  ▓▓▓▓▓▓▓░░░  −12 dB           │
│  g (loop gain) ▓▓░░░░░░░░  −34 dB           │
└────────────────────────────────────────────┘
```

## Build & verification

- Register `add_subdirectory(plugins/dslar)` in the root `CMakeLists.txt`.
- Build against the VST3 SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` (`-DSEAM_VST3SDK_DIR=...`).
- TDD the DSP core: a small scratch driver compares `dslar_dsp.h` against the existing oracle (the Faust `sds.lar` via a scratch `faust -lang cpp`, and the Pd formulas), for example DC 0.5 → `g = 0.5^40 ≈ 9.09e-13`, and the two `line` ramps against the Phase 3 `line` oracle.
- Final check: a clean build plus a successful load in a host, with both meters moving.

## Deliverables

- The plugin (all files above), building and loading.
- `plugins/_common/seam_meter.h` (Phase A of the metering system).
- An Italian `doc/study` diary entry on the C++ porting phase, consistent with the previous phases.

## Out of scope (later phases)

- The formal validation A/B axes (SR-independence 44.1↔96 kHz; Pd↔Faust block kernel) and the English `doc/math`.
- Any retrofit of past plugins to the metering facility.
- The EBU R128 loudness module (metering Phase C).
