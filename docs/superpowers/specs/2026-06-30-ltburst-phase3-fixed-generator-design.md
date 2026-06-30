# ltburst Phase 3 (slice) — Fixed-Frequency Shaped Tone-Burst Plugin

**Date:** 2026-06-30
**Status:** design approved, ready for implementation plan
**Scope:** the first shippable `ltburst` VST3 plugin — a fixed-frequency `slw.shapedburst` generator with a finished GUI.
Glissando (`glissburst`), advanced transport (one-shot/loop/wait/manual click), per-stone routing, and the reference-microphone inverse-EQ ("fase due") are explicitly **out of scope** here and land in Phase 3b / later projects.

## Goal

Port the fixed-frequency shaped tone-burst from `seam.linkwitz.lib` (`slw.shapedburst` / `shapedburst5`) to hand-written C++ as a complete VST3 plugin in the seam-ltm suite.
The plugin is a continuous generator modelled on `multipink`: it emits repeated bursts while active, the host owns play/stop, and it ships a finished VSTGUI window.

## Faust reference (the spec)

The mathematical specification is the merged library function (`faust-libraries/src/seam.linkwitz.lib`):

```faust
shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win
  with {
    M   = max(1, int(ceil(dwell*f0)));            // dwell quantised to whole cycles
    P   = N + M;                                  // total cycles per period
    c   = os.phasor(1, f0/P);                     // 0..1 over P carrier cycles
    u   = P*c;                                    // cycle position 0..P
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); // Hann over the first N cycles
  };
shapedburst5(f0,dwell) = shapedburst(f0,5,dwell); // canonical N=5 wrapper
```

`N` is locked to 5 (the canonical Linkwitz value) in this slice; it is not exposed as a parameter and becomes one only when a later phase needs it.
No `faust -lang cpp` output lands in `source/` (project convention); the generator is ported by hand and the comment block at the top of `ltburst_processor.h` cites this library.

## Plugin identity and architecture

- **Name:** `ltburst` — "Linkwitz Shaped Tone-Burst Generator".
- **FUID index:** `0x5E4D000C` (12th plugin; `multipink`=0008, `x2uhj`=0009, `abmodulex`=000A, `hilbert`=000B).
- **Base class:** `Steinberg::Vst::SingleComponentEffect` (suite convention).
- **Pattern:** Approach A — mirror `multipink` minus the shared pool.
  No peer awareness, no shared static state, so no `_pool.{h,cpp}` split.
  The burst DSP lives in a small, well-bounded `struct ShapedBurst` inside `ltburst_processor.cpp`.
  If Phase 3b (glissando) wants to share the engine, extracting it into `ltburst_burst.h` is a trivial refactor done then, with the real glissando in view.

### File layout

```
plugins/ltburst/
├── CMakeLists.txt            # smtg_add_vst3plugin; registered in root CMakeLists.txt
├── source/
│   ├── ltburst_ids.h         # FUID, ParamID enum, reference-level table
│   ├── ltburst_processor.h   # FAUST REFERENCE comment block + class declaration
│   ├── ltburst_processor.cpp # IAudioProcessor lifecycle + struct ShapedBurst
│   └── version.h
└── resource/ltburst.uidesc   # VSTGUI layout + assets
```

`doc/study/`, `doc/references/`, and `doc/math/.gitkeep` already exist on the branch and are untouched by this slice (the `doc/math/` formal write-up is a later step).

## Parameters

| ParamID | Name | Type | Range / values | Default |
|---|---|---|---|---|
| 100 | `Reference` | StringList (stepped) | −23 / −20 / −18 dBFS RMS | −23 |
| 101 | `Trim` | Range (continuous) | −6 … +6 dB | 0 |
| 102 | `Frequency` | Range (continuous, log taper) | 20 Hz … 20000 Hz | 1000 Hz |
| 103 | `Dwell` | Range (continuous) | 0 … 1000 ms | 300 ms |

`Reference` and `Trim` reuse the `multipink` pattern verbatim (`StringListParameter` with `kReferenceLevelsDb = {-23,-20,-18}`, plus a continuous trim), so the suite keeps one consistent level idiom.
`Frequency` uses a logarithmic taper so equal slider travel spans equal octaves.
`Dwell` is expressed in milliseconds and maps 1:1 to the Faust `dwell` (seconds) as `M = ceil(dwell_s * f0)`.

A `Mute` boolean is **not** included in this slice (the host owns start/stop; multipink's Mute was tied to its pool semantics).

### Level calibration (the design decision)

`Reference` calibrates the **RMS of the windowed sinusoid measured over the active burst window** — the `N` carrier cycles where `win > 0` — excluding the dwell silence.
This keeps the burst amplitude at a known, stable level independent of the dwell setting, which is what loudspeaker/room calibration needs.
A long-term RMS over the whole period would fall as the dwell grows, and a peak calibration would change the established "dBFS RMS" label; both are rejected.

The linear output gain is:

```
gain_dB  = referenceDb + trimDb + kCalibrationOffsetDb
gain_lin = 10^(gain_dB / 20)
```

`kCalibrationOffsetDb` is a measured constant (the `multipink` method): render a fixed burst at 48 kHz with Reference=−23, Trim=0, measure the RMS over the active window with sox, and set the offset so the active-window RMS lands on −23.0 dBFS.
The constant is determined during implementation and documented inline, exactly as `multipink`'s `kCalibrationOffsetDb` (measured 2026-05-07) is.

## DSP core — `struct ShapedBurst`

A literal port of the Faust formula. State is a single normalised phase accumulator `c ∈ [0,1)` spanning the whole `P`-cycle period.

```cpp
// recomputed at block boundaries from the current f0, dwell:
M   = max(1, ceil(dwell_s * f0));   // silence cycles
P   = N + M;                        // N = 5
inc = (f0 / P) / sampleRate;        // per-sample advance of c

// per sample:
u    = P * c;                                    // 0..P
win  = (u < N) ? 0.5 - 0.5*cos(2*PI*u/N) : 0.0;  // Hann over first N cycles
out  = sin(2*PI*u) * win * gain;                 // sin(2*PI*u) == sin(2*PI*P*c)
c   += inc; if (c >= 1.0) c -= 1.0;              // wrap
```

Porting care:

- **Runtime parameter changes:** `M`, `P`, `inc` are recomputed at block boundaries; `c` stays continuous across the recompute, so there is no phase jump when `Frequency` or `Dwell` moves.
- **Gain smoothing:** the linear gain is interpolated toward its target across the block to avoid zipper noise on `Reference`/`Trim` changes.
- **Zero-crossing start:** free, because `c = 0 ⟹ u = 0 ⟹ sin = 0` and the Hann window is 0 there.
- **Output:** stereo output bus, the same mono signal on both channels; no input bus.
  Per-stone routing is Phase 3b.
- **Numerical safety:** `f0/P` and `inc` are finite for all parameter values in range (`P ≥ 6`, `f0 ≥ 20`); no division guard is required beyond the `max(1, …)` on `M`.

## GUI (`ltburst.uidesc`)

Modelled on `multipink.uidesc`, roughly 300×300:

- Title label "SEAM LTBURST", subtitle "Linkwitz Shaped Tone-Burst".
- `Reference` — `COptionMenu` (3 stepped dBFS values).
- `Trim` — `CSlider` + `CTextEdit` readout (dB).
- `Frequency` — `CSlider` (log) + readout (Hz).
- `Dwell` — `CSlider` + readout (ms).
- SEAM logo (`_common/resource/seam_logo.png`), Source Code Pro fonts (`_common/resource/Fonts`).
- No pool/slot/status displays (no shared state).

`VSTGUI_LIVE_EDITING=0` as for the rest of the suite.

## Validation

- **Faust parity:** generate a reference render of `slw.shapedburst5(1000, 0.3)` using `faust -lang cpp` **as a scratch tool only** (never committed to `source/`), and compare the C++ generator's output sample-stream to it numerically (allowing for the documented seed/branch differences — here the function is deterministic, so a tight tolerance applies).
- **Calibration check:** render with Reference=−23, Trim=0 at two dwell values (e.g. 50 ms and 800 ms) and confirm the active-window RMS is −23.0 dBFS in both, independent of dwell.
- **No-NaN / zero-crossing:** the output is finite for all in-range parameters, and each burst starts on a zero crossing.
- **Build + validate:** `cmake` builds the plugin, it is registered in the root `CMakeLists.txt` (`add_subdirectory(plugins/ltburst)`), and it passes the VST3 validator.

## Out of scope (explicit)

- Glissando generator (`slw.glissburst`), exponential/linear sweep, passo/gap timing — Phase 3b.
- Advanced transport: one-shot vs loop, wait between cycles, manual single-burst click, per-stone routing — Phase 3b.
- The reference-microphone inverse-EQ calibration ("fase due") — a later project.
- Exposing `N` (burst cycle count) as a parameter — only when a later phase needs it.
- The staircase-shaped burst variant and published-spectrum comparison — deferred with the test toolkit (#6).
- The formal English `doc/math/` write-up — a later documentation step.

## Self-review

- **Placeholders:** `kCalibrationOffsetDb` is intentionally measured-during-implementation, matching the established `multipink` workflow; no other TBDs.
- **Consistency:** parameter IDs, ranges, and the calibration definition are stated once and reused; the DSP port matches the cited Faust formula term-for-term.
- **Scope:** one plugin, one mode, one finished GUI — sized for a single implementation plan.
- **Ambiguity:** "distanza onset" is resolved to `Dwell` (silence, ms); `Reference` is resolved to active-window RMS; both are made explicit above.
