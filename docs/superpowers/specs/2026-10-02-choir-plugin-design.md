# choir plugin — design

Date: 2026-10-02.
Status: approved in conversation (three sections), awaiting review of this document.

## Purpose

The choir (`pitchDetectorChoirMcAdams`, four instances) of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco) listens to the TETRAREC A and makes noise sing where the input has partials.
On each of the four channels, 16 bands listen around f·k and 16 voices of noise sing around f·k^a, each voice as loud as its band of the input.
This plugin is the fourth C++ port of SSCDO#2, hand-written from the specification `sdt.choir` of `seam.tedesco.lib` (faust-libraries 517f0c8), following the suite convention: Faust is the spec, C++ is the deliverable.
It is played in Reaper at 96 kHz.

Success means: the plugin reproduces `sdt.choir` to numerical precision at 96 and 48 kHz, its noise included; the window shows, band by band, what the choir is hearing; the window follows `doc/style/ui-style.md`; every decision is in the report, then the session log.

## Constraints

From Giuseppe, for every SSCDO#2 plugin (2026-10-01):

- State and arithmetic in `double` throughout; conversion only at the bus.
- UI coherent with the suite: `ui-style.md`, started from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Filters and DSP blocks are reusable `_common/` libraries; the plugin's `*_dsp.h` only wires them.
- **SR rule:** every process sounds as it does at 96 kHz.

From the choir's survey (2026-10-02, `doc/study/sscdo2/choir-noise/`, `choir-chain/`; report cards `coro-voci`, `coro-dcblocker`, `coro-livello`, `coro-nyquist`):

- **Noise:** block 3 of the SSCDO#2 noise, one `no.multinoise(72)` whose streams 0–7 are LMO's; the choir reads streams 8–71, one per band, channel c on 8 + 16c to 23 + 16c (`sdt.choirnoise`).
- **No DC blocker.**
- **Voices at their 96 kHz level:** `choirdens = sqrt(fs/96000)` on the voices.
- **Bands at or above 20 kHz are silent**, their filter designed at 19999 Hz.

Decided in this design (Giuseppe, 2026-10-02):

- **f, a, Q and release are constants**, as in the performance (f = 48, 48, 96, 96 Hz; a = 1, 1.01, 1.1, 0.9; Q = 350; release 1.5 s), shown in the window, not exposed as parameters.
- **A 4×16 grid** shows the level of every analysis band: which partials the choir hears, and therefore makes sing.
- **RESET from the GUI only**, as stunedrev's: the bands ring for seconds (16 s to −60 dB at 48 Hz).
- **The grid's data path:** 64 independent `std::atomic<float>`, one per band. A bar grid has no invariant across bands, so each value is valid on its own and no snapshot is needed.
- **Library layout:** `fi.svf.bp` and `an.amp_follower` become `_common` libraries; `ba.tau2pole` moves out of `seam_compressors.h` into `seam_basics.h`; the 20 kHz rule stays in the choir's wiring, as `sdt.choirband` is in the specification.

## Architecture

```
plugins/choir/
├── CMakeLists.txt
├── source/
│   ├── choir_ids.h          # FUID 0x5E4D0013, parameter IDs
│   ├── choir_params.h       # SDK-free ParamBox of atomics (POWER, output)
│   ├── choir_display.h      # 64 std::atomic<float>: block peak of each band / Q
│   ├── choir_dsp.h          # SDK-free engine: wires the _common blocks
│   ├── choir_views.h        # ChoirGrid and the RESET button
│   ├── choir_processor.h    # FAUST REFERENCE (sdt.choir*), IAudioProcessor
│   ├── choir_processor.cpp
│   └── version.h
├── resource/choir.uidesc    # from _template.uidesc, format L
└── doc/README.md
plugins/_common/
├── seam_basics.h            # NEW: tau2pole (moved from seam_compressors.h)
├── seam_svf.h               # NEW: Seam::SvfBandpass = fi.svf.bp
└── seam_analyzers.h         # NEW: Seam::AmpFollower = an.amp_follower
```

Reused: `seam_noise.h` (`MultinoiseBlock`), `seam_ramp.h` (`LinearRamp`), `seam_denormals.h` (`ScopedNoDenormals`), `seam_state.h`.

Each new header cites its Faust source in a comment block, takes its parameters generically (no choir constants inside), and has its own `tests/seam_<lib>_test.cpp` against Faust references.
`seam_svf.h` and `seam_analyzers.h` mirror standard Faust libraries (`filters.lib`, `analyzers.lib`).

### `seam_basics.h` — `tau2pole`

`ba.tau2pole(tau) = 0` when |tau| < ε, otherwise `exp(−1/(tau·fs))`.
Moved out of `seam_compressors.h`, which includes it; `CompressorMono` is unchanged and its tests stay green.

### `seam_svf.h` — `SvfBandpass`

`fi.svf.bp(f, q)`, Simper's state-variable filter, band-pass output:

```
g = tan(π·f/fs),  k = 1/q
v1 = (ic1 + g·(x − ic2)) / (1 + g·(g + k))
v2 = ic2 + g·v1
ic1 = 2·v1 − ic1,  ic2 = 2·v2 − ic2
y = v1
```

`design(fs, f, q)`, `tick(x)`, `reset()`.
The bilinear transform, prewarped at f, of H(s) = s/(s² + s/q + 1): peak gain q at f.
The filter makes no promise above fs/2 (tan turns negative and the filter unstable); the caller keeps f below it.

### `seam_analyzers.h` — `AmpFollower`

`an.amp_follower(rel) = abs : env`, `env(x) = x·(1 − p) : (+ : max(x, _)) ~ *(p)`, `p = tau2pole(rel)`:

```
e[n] = max(|x[n]|, (1 − p)·|x[n]| + p·e[n−1])
```

An immediate attack, a release in seconds.
`prepare(fs, rel)`, `tick(x)`, `reset()`, `value()`.

### The engine

Constants of SSCDO#2 (`choir_dsp.h`, injectable through the constructor for testing; the defaults are the performance's): f = {48, 48, 96, 96} Hz, a = {1, 1.01, 1.1, 0.9}, Q = 350, release 1.5 s, 16 bands.

Per sample, channel c, band k = 0…15:

```
noise[0..63] = MultinoiseBlock(72, 8, 64).tick()            // block 3: stream 16c + k
env = follower[c][k].tick( listen[c][k].tick(x_c) )         // band at f_c·(k+1)
v   = sing[c][k].tick( noise[16c + k] ) · choirdens         // band at f_c·(k+1)^a_c
y_c = Σ_k env·v / (Q·2π)
out_c = y_c · output(smoothed) · power(smoothed)
```

**Design once, in `prepare(fs)`:** the centres are constants.
A band whose centre is at or above 20 kHz is marked inactive and not computed, as Faust folds `band · 0` to zero; its filter is still designed at `min(fc, 19999)` Hz, so that it would stay stable if a centre ever became variable.
With the performance's constants no band is inactive: the highest centre is 2017 Hz.
`choirdens = sqrt(fs/96000)` is computed there too.

**Denormals:** the engine runs inside `ScopedNoDenormals`; the 128 bands and the 64 followers decay in silence, stunedrev's case.

**POWER** off keeps the engine running (analysis and voices stay current) and ramps the output to zero.
`output` and `power` are linear ramps of 25 ms (`Seam::LinearRamp`), as in LMO and delRM.

**RESET:** a generation counter in the engine, raised by the GUI view.
At the start of a block the engine compares it with the last one it served, and when it has changed it zeroes every filter and follower state (about 2 KiB, cleared at once).
The noise is not rewound: the blocks of the SSCDO#2 noise are disjoint whatever the instant.

**Display:** at the end of each block the engine writes, for every band, the peak of its envelope within the block divided by Q into `choir_display.h`.
Divided by Q, the envelope is the amplitude of the input's partial in that band, so the grid shows it in dBFS.

**Memory:** no allocation in the audio thread; `MultinoiseBlock` allocates its 72-value buffer in its constructor.
Cost per sample: 128 band-passes, 64 followers, 72 LCG steps; measured at 96 kHz and written in the README.

**SR rule:** centres in Hz, follower in seconds, the voices anchored at 96 kHz by `choirdens`: the choir sounds at 48 kHz as at 96 kHz.

Bus: four channels in and out, `kAmbi1stOrderACN` as LMO, stunedrev and delRM.
Channel c listens to input c (the TETRAREC A, patch inputs 5–8) and sings on output c.
Subcategory `Fx`.

## Parameters and window

| Parameter | Zone | Range | Default | Notes |
|---|---|---|---|---|
| POWER | OPS | off / on | off | output ramp, 25 ms |
| output | FINE | 0–1 linear | 0 | CC86, 25 ms ramp |

RESET is a GUI view with no bound parameter (stunedrev's `StunedrevResetButton` pattern), never in the state, never automatable.
The controls live in a `ParamBox` of atomics (`choir_params.h`): `process()` never touches a `Parameter`.
State: POWER and output, under the append-only contract of `seam_state.h`.

**Format L, 460 px**, for the width the grid needs (strx is L for its views):

```
HEADER   SEAM CHOIR
         sixteen voices on four channels
OPS      [■] POWER            [ ] RESET          (left / right column, as stunedrev)
FINE     output  ─────────────────────  0.00     (full-width block, as dslar's Output)
FOOTER   ┌ ChoirGrid ───────────────────────────────┐
         │ 1  48 Hz  a 1     16 bars                 │
         │ 2  48 Hz  a 1.01  16 bars                 │
         │ 3  96 Hz  a 1.1   16 bars                 │
         │ 4  96 Hz  a 0.9   16 bars                 │
         │       1  2  3  4 … 16   (k)               │
         └──────────────────────────────────────────┘
         Q 350 · release 1.5 s · noise block 3 · @ 96 kHz
         [logo]
```

**ChoirGrid** is FOOTER content (runtime feedback, by the style guide), a custom `CView` created in `createCustomView` as strx's views, about 400 × 170 px.
Each row carries its channel's f and a; the status line carries Q, the release, the noise block and the session's rate, so the constants are readable without opening anything.
Each bar is the block peak of the band's envelope divided by Q, in dBFS, from −80 to 0 dB, filling from the bottom.
No ballistics of its own: the follower's 1.5 s release is what the bar shows.
An inactive band is drawn empty and grey.
A `CVSTGUITimer` at 30 Hz reads the 64 atomics and calls `invalid()` once per frame; drawing allocates nothing.
Colours and fonts from the suite's palette.

## Verification

The references come from the specification, compiled with `faust -double` against the faustlibraries clone, by `doc/study/sscdo2/choir-plugin/gen-ref.sh` and `refdump.cpp` (LMO's), into `tests/ref/choir_ref.h`; the tests need no `faust` binary.

| # | Test | Criterion |
|---|---|---|
| 1 | `seam_basics_test` | `tau2pole` is 0 for τ near 0 and exp(−1/(τ·fs)) otherwise; `seam_compressors_test` stays green after the move |
| 2 | `seam_svf_test` | `SvfBandpass` equals the impulse response of `fi.svf.bp` at 48 Hz, Q 350, 96 kHz and at 1 kHz, Q 0.7, 48 kHz, within 1e-13 of the peak; peak gain q at f |
| 3 | `seam_analyzers_test` | `AmpFollower` equals `an.amp_follower(1.5)` on a gated sine; immediate attack; −5.79 dB one second after the stop |
| 4 | engine | `sdt.choir(350, 1.5)` on an input generated inside the DSP, 16 sines at f·k per channel written as `sin(2π·f·n/SR)` and reproduced by the same formula in C++, compared after 3 s of pre-roll, at 96 and 48 kHz, within 1e-12 of the peak on all four channels; this covers the noise's block 3 and `choirdens`, visible at 48 kHz only |
| 5 | RESET | after a reset, with zero input, the output is exactly 0 (the envelopes start from zero); the noise is not rewound |
| 6 | inactive bands | an engine with f = 5000 Hz at 48 kHz stays finite and its bands from 20 kHz up are silent |
| 7 | denormals | after the signal, 60 s of silence leave no subnormal state |
| 8 | ramps | POWER and output reach their targets in exactly 25 ms at every rate |
| 9 | display | a sine of amplitude A on band k reads 20·log10(A) within 0.1 dB at steady state; the other bands read below −40 dB (the nearest, at 1.5 times the centre, sit near −49 dB: 1/(350·0.83)) |
| 10 | parameters and state | the ParamBox round-trip; the processor's state round-trip; a short read keeps the defaults; RESET is not in the state |

Every test is verified by mutation, recorded in `doc/study/sscdo2/choir-plugin/mutations.md`, at least: channel on another channel's streams; `choirdens` missing or applied twice; the stretch a on the analysis bands; the follower with a wrong release; an inactive band computed without the 19999 Hz design; RESET without zeroing the followers; the display without the division by Q.
Test 4 must run at 48 kHz as well: at 96 kHz `choirdens` is 1, and a mutation on it stays invisible there.

Suite: `tools/check-uidesc.py`, `minos_lint`, ctest, the VST3 validator.
CPU measured at 96 kHz and written in the README.

## Documentation

In this order, as the report rule asks:

1. **Report** (`doc/study/sscdo2/report/`): card `coro-uscita` (CC86) filled; `make check` passes.
2. **Log** `logs/2026-10-02-sscdo2-choir.md`: this design and the work that follows.
3. `plugins/choir/doc/README.md`; the study `doc/study/sscdo2/choir-plugin/` and its row in the index; the registry `doc/plugins.toml` (twenty plugins, family "Works — SSCDO#2"), with the plugin counts in `CLAUDE.md` and in the `make -C doc test` check; `make -C doc doc` and `make -C doc test`.

## Deferred to the end, as Giuseppe asked

- The Release build of LMO and the choir together, and the host check in Reaper at 96 kHz by Giuseppe (the TETRAREC A into the four channels, the grid while the clarinet plays, the same session at 48 kHz), with a screenshot for `docs/img/choir.png`.
- `make -C doc publish`, after Giuseppe's confirmation (it reaches the public site).

## Out of scope

- f, a, Q or the release as parameters.
- Any DC blocker.
- A coherent snapshot of the grid (triple buffer): no invariant across bands asks for it.
