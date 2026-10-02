# delRM plugin — design

Date: 2026-10-02.
Status: approved in conversation (three sections), awaiting review of this document.

## Purpose

delRM (`delRM_duet`) of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco) works on four channels, each processing only its own input from the TETRAREC.
Channels 1 and 3 are a feed-forward comb, x + x[n−D]; channels 2 and 4 multiply x[n−D], x and the integral of x, and pass the product through an 11:1 compressor.
This plugin is the third C++ port of SSCDO#2, hand-written from the specification `sdt.delrmcomb`, `sdt.delrmint`, `sdt.delrmrm` and `sdt.delrmdyn` of `seam.tedesco.lib`, following the suite convention: Faust is the spec, C++ is the deliverable.
It is played in Reaper at 96 kHz.

Success means: the plugin reproduces the specification to numerical precision at 96 and 48 kHz, across a change of distance; its delay lines are sized exactly for 30 m at any rate, outside the audio thread; the window shows where the dynamic window of channels 2 and 4 opens and saturates, as the rehearsal card `delrm-dinamica` asks; the window follows `doc/style/ui-style.md`; every decision is in the report, then the session log.

## Constraints

From Giuseppe, for every SSCDO#2 plugin (2026-10-01):

- State and arithmetic in `double` throughout; conversion only at the bus.
- UI coherent with the suite: `ui-style.md`, started from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Filters and DSP blocks are reusable `_common/` libraries; the plugin's `*_dsp.h` only wires them.
- **SR rule:** every process sounds as it does at 96 kHz.

From the 2026-09-29 decisions: D is DDELAY's delay (distance in metres, rounded to the millimetre, 331.4 m/s, rounded to the sample, next prime strictly above, `sma.imt2npsamp`), 0–30 m, 7.291 m at the start (Davide's 22 ms), set during the setup and left alone in the piece; one distance for the four channels, as in the original; the integrator is `sdt.delrmint = sfi.leakyint(1) : *(96000)`.

Decided in this design (Giuseppe, 2026-10-02, after reading the report with Davide):

- **DC blockers: none.** delRM ends at the compressor, as the specification does; the thinner bass of the performance is judged by listening, not built in (card `delrm-dcblocker`).
- **Volume: one `output` fader, 0–1 linear** (CC82). The original's two stages in series (Pd's gain and the `.dsp`'s internal Master Volume, 0 in the wrapper, 0.9 in the `.dsp`) become one; the internal 0.9 no longer exists (card `delrm-volume`, question `volume-delrm` closed).
- **Meters:** input peak on the four channels, as the original, plus the gain reduction of channels 2 and 4; the gain reduction bars run right to left, opposite the input, so the input rising and the compressor descending read as two opposite movements.
- **One four-channel plugin** with the blocks in `_common/`, rather than two mono plugins (four instances, the distance in four places) or a two-channel unit used twice.

## Architecture

```
plugins/delrm/
├── CMakeLists.txt
├── source/
│   ├── delrm_ids.h          # FUID 0x5E4D0012, parameter IDs
│   ├── delrm_params.h       # SDK-free ParamBox of atomics (as lmo/stunedrev)
│   ├── delrm_dsp.h          # SDK-free engine: wires the _common blocks
│   ├── delrm_processor.h    # FAUST REFERENCE (sdt.delrm*), IAudioProcessor
│   ├── delrm_processor.cpp
│   └── version.h
├── resource/delrm.uidesc    # from _template.uidesc, format S
└── doc/README.md
plugins/_common/
├── seam_delays.h            # NEW: Seam::IntegerDelay    = de.delay
├── seam_filters.h           # NEW: Seam::LeakyIntegrator = sfi.leakyint(fc)
└── seam_compressors.h       # NEW: Seam::CompressorMono  = co.compressor_mono
```

Reused: `seam_primes.h` (`PrimeSieve`), `seam_ramp.h` (`LinearRamp`), `seam_denormals.h` (`ScopedNoDenormals`), `seam_meter.h`, `seam_state.h`.

`delrm_dsp.h` is SDK-free, so doctest drives the whole engine.

Each new header cites its Faust source in a comment block, takes its parameters generically (no delRM constants inside), and has its own `tests/seam_<lib>_test.cpp` against Faust references.
`seam_delays.h` and `seam_compressors.h` mirror **standard** Faust libraries (`delays.lib`, `compressors.lib`), not SEAM ones: Faust has no `seam.delays.lib` because `de.delay` already exists (2026-09-29), while C++ has no such block, and the plugin must not define one.
DDELAY and ADDELAY keep their own ring buffers; aligning them on `seam_delays.h` is a separate project, to be proposed, not done here.

### `seam_delays.h` — `IntegerDelay`

`de.delay(maxdel, d)` for an integer d: a ring buffer, storage from the caller (`attach(buf, len)`), as `MoorerAllpass` takes it.
`tick(x)` writes x and returns x[n−d]; d = 0 returns x itself, as `de.delay` does.
`setDelay(d)` takes effect on the next `tick`, d ≤ len − 1 (asserted in debug, clamped in release).
`clear()` zeroes the buffer and the write index.

### `seam_filters.h` — `LeakyIntegrator`

`sfi.leakyint(fc) = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR))`:

```
y[n] = x[n]/fs + a·y[n−1],   a = exp(−2π·fc/fs)
```

`prepare(fs, fc)`, `tick(x)`, `reset()`.
The integral in seconds: gain 1/(2πf) above fc at every rate, levelling off at about 1/(2π·fc) below.
delRM scales it by 96000 in its own wiring (`sdt.delrmint`), not inside the library.

### `seam_compressors.h` — `CompressorMono`

`co.compressor_mono(ratio, thresh, att, rel) = compressor_lad_mono(0)`, J. O. Smith's, the original's compressor with the original's parameters:

```
e[n] = (1 − c)·|x[n]| + c·e[n−1],  c = tau2pole(att) if |x[n]| > e[n−1] else tau2pole(rel)
L    = linear2db(e[n])
g    = max(L − thresh, 0) · (1/ratio − 1)                  // dB, ≤ 0
k[n] = (1 − s)·g + s·k[n−1],       s = tau2pole(att/2)      // the "knee": a second one-pole on the gain
y    = x[n] · db2linear(k[n])
```

with `tau2pole(τ) = exp(−1/(τ·fs))` (0 when |τ| < ε), and `linear2db`/`db2linear` as in `basics.lib` (`20·log10`, `10^(x/20)`).
Two states per instance: the envelope and the knee.
The switch compares |x| with the envelope's **previous** state (`si.onePoleSwitching`), not with the updated one.
`gainDb()` returns k[n], the gain just applied, for the meter.
The exact forms of `linear2db` at 0 (−∞ in Faust) and of the ratio's `max(ma.EPSILON, ratio)` are read from the faustlibraries clone the specification compiles with (0965ea2 or later) and reproduced; a difference found there is recorded in the study.

### The engine

Per sample, with D the prime delay shared by the four channels:

```
ch 1, 3:  y = x + delay_j.tick(x)                                    // sdt.delrmcomb
ch 2, 4:  p = delay_j.tick(x) · x · (96000 · leaky_j.tick(x))        // sdt.delrmrm
          y = comp_j.tick(10 · p)                                    // sdt.delrmdyn
out_j = y · output(smoothed) · power(smoothed)
```

`output` and `power` are linear ramps of 25 ms (`Seam::LinearRamp`), as in LMO and stunedrev; the original's `si.smoo` on the Master Volume is replaced by the suite's ramp.
The engine runs inside `ScopedNoDenormals`: the integrator and the compressor are poles that decay in silence.
POWER off keeps the engine running (the delay lines and integrators stay current) and ramps the output to zero.

**The distance.**
`D = imt2npsamp(mt, fs)`: `mm = round(mt·1000)/1000`, `n = floor(mm·fs/331.4 + 0.5)`, kept as it is when n < 2, otherwise `PrimeSieve::nextPrimeAbove(n)`.
At 0 m, D = 0 and the comb doubles its input, as the specification does.
A change of distance recomputes D at the start of the next block, and the delay jumps: the spec's behaviour (`de.delay` with an integer delay that changes) and the original's; the small click is accepted (card `delrm-distanza`).

**Memory.**
In `setActive(true)` the engine computes `Dmax = imt2npsamp(30, fs)`, builds the sieve up to a bound covering it plus one prime gap, and allocates four lines of `Dmax + 1` doubles, zero-filled.
`setActive(false)` frees them; a change of rate passes through here.
The specification sizes with `1 << 15`, which covers 30 m up to 192 kHz (17 383 samples) but not at 384 kHz (34 763): the C++ sizes exactly, the sound is identical.
The memory is small (1.06 MiB at 384 kHz), so no arena and no RESET: the memory of delRM is as long as D.

**SR rule.**
D is a distance, so a time: the same at every rate, with the prime of each rate (a feature of the system, as in stunedrev).
The integrator is normalised in seconds and anchored at 96000 (`sdt.delrmint`).
The compressor is in seconds.
So delRM behaves at 48 kHz as it does at 96 kHz.

## Parameters and window

| Parameter | Zone | Range | Default | Notes |
|---|---|---|---|---|
| POWER | OPS | off / on | off | output ramp, 25 ms |
| distance | FINE | 0–30 m, 1 mm resolution | 7.291 m | DDELAY's range and rounding |
| output | FINE | 0–1 linear | 0 | CC82, 25 ms ramp |

Read-only parameters (meters): `in 1`–`in 4`, `GR 2`, `GR 4`.

The controls live in a `ParamBox` of atomics (`delrm_params.h`): `process()` never touches a `Parameter`.
State: the three automatable parameters, under the append-only contract of `seam_state.h`.

**Format S** (two fine controls, well below about five): 300 px, one column.

```
HEADER   SEAM DELRM · comb and triple product, four channels
OPS      [■] POWER
FINE     distance   7.291 m
         output     0.00
FOOTER   D 22.01 ms 2113 samples @ 96 kHz
         in 1  ▮▮▮▮▮▮▮▯▯▯  −18 dB        ← rises to the right
         in 2  ▮▮▮▮▮▮▯▯▯▯  −21 dB
         in 3  ▮▮▮▮▮▮▮▯▯▯  −17 dB
         in 4  ▮▮▮▮▮▯▯▯▯▯  −24 dB
         GR 2  ▯▯▮▮▮▮▮▮▮▮  −31 dB        ← descends to the left, 0 dB at the right
         GR 4  ▯▯▯▮▮▮▮▮▮▮  −27 dB
```

**distance** shows metres, the value a laser distance meter gives.
The FOOTER shows D in ms and in samples at the session's rate (the 2026-09-29 decision, "value shown in ms"), so the prime changing with the rate is visible; D reaches the GUI in one atomic read by a GUI timer, as stunedrev's centroids.

**Input meters:** the peak of each input per block, with instant attack and a one-pole release of 300 ms across blocks, over −70 to +5 dB as in the original.
They serve the setting of the ASP880 gain.

**GR 2, GR 4:** the deepest `gainDb()` of the compressor within each block, so short peaks are not lost, over 0 to −48 dB (about −38 dB at the recording's level, so the scale shows the strongest transients without hitting the floor), with the same 300 ms release toward 0 dB.
They are drawn with `draw-value-inverted="true"`, the flag `CSlider::draw` reads (`kDrawInverted`).
Corrected 2026-10-02 after the host check: the first version used `reverse-orientation="true"`, which VSTGUI reads but applies to the mouse and the handle only, so the bars still grew from the left; `tools/check-uidesc.py` now rejects that combination.
They are the instrument of rehearsal card `delrm-dinamica`: GR near 0 while the cubic product is below threshold, the effect appearing; GR deep when the compressor works as a limiter, the effect saturated.

**Transport:** the dslar and `seam_meter` idiom, read-only output parameters drawn as `CSlider` in `MeterFill`; no custom CView.

Bus: four channels in and out, `kAmbi1stOrderACN` as LMO and stunedrev; outputs 1–4 in the original's order.
Subcategory `Fx|Delay`.

## Verification

The references come from the specification, compiled with `faust -double` and the `sff.np` ffunction against the faustlibraries clone, by `doc/study/sscdo2/delrm-plugin/gen-ref.sh` and `refdump.cpp`, into `tests/ref/delrm_ref.h`; the tests need no `faust` binary.
Test signals: noise, and the clarinet note (`delrm-comb/renders/ccb_dry.wav`), in windows, at 96 and 48 kHz.

| # | Test | Criterion |
|---|---|---|
| 1 | `seam_delays_test` | `IntegerDelay` equals `de.delay` for d = 0, 1, 2 and the maximum; a delay shifted by one sample must fail |
| 2 | `seam_filters_test` | `LeakyIntegrator` equals `sfi.leakyint(1)`; gain 1/(2πf) above fc, equal at 48 and 96 kHz; bounded on DC |
| 3 | `seam_compressors_test` | `CompressorMono` equals `co.compressor_mono(11, −24, 0.03, 0.04)` on steps and on the note; `gainDb()` is the gain applied; mutations: without the knee pole, switch compared with the updated state |
| 4 | delays | D equals `sma.imt2npsamp` over 0–30 m in 1 mm steps at 44.1, 48, 96 and 192 kHz |
| 5 | engine | the four channels against `sdt.delrmcomb` and `sdt.delrmrm : sdt.delrmdyn` at 96 and 48 kHz, within about 1e-12 of the peak |
| 6 | change of distance | applied at the same block boundary in the C++ and in the refdump; the outputs agree after the jump |
| 7 | memory | every line holds `Dmax(30 m) + 1` at 44.1, 48, 88.2, 96, 176.4, 192 and 384 kHz, and Dmax is the longest D the slider can ask |
| 8 | meters | the block peak and the deepest block GR equal what the engine computed in that block |
| 9 | parameters | the ParamBox round-trip; the processor's state round-trip |

Every test is verified by mutation, recorded in `doc/study/sscdo2/delrm-plugin/mutations.md`.
Test 6 needs the refdump to move the Faust slider at the exact block boundary where the C++ applies the new D, or it compares two different things.

Suite: VST3 validator, `tools/check-uidesc.py`, ctest.
`minos_lint` runs on a tree configured with `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.
CPU is measured at 96 and 192 kHz and written in the README.

**Host check (Giuseppe, Reaper, 96 kHz):** the clarinet note through the four channels, by ear against the `delrm-comb` and `delrm-rm` renders; the distance moved by hand; the GR meters while the level rises; the same session at 48 kHz; a screenshot of the window for `docs/img/delrm.png`.

## Documentation

In this order, as the report rule asks (rehearsal decisions go into the report first, then the log, then `seam.tedesco.lib`):

1. **Report** (`doc/study/sscdo2/report/`): card `delrm-dcblocker` → DA PROVARE: without in the port, the rehearsal's listening (prova `delrm-dcblocker`) judges it; card `delrm-volume` → DECISO, one output 0–1, the internal 0.9 no longer exists; questions `dcblocker` and `volume-delrm` closed; `make check` passes.
2. **Log** `logs/2026-10-02-sscdo2-delrm.md`: the decisions of this design and the work that follows.
3. **`seam.tedesco.lib`** (faust-libraries): the delRM header comment records the dcblocker decision as taken; the volume is a plugin matter and stays out of the library.
4. `plugins/delrm/doc/README.md`; the study `doc/study/sscdo2/delrm-plugin/` and its row in the index; the registry `doc/plugins.toml` (nineteen plugins, family "Works — SSCDO#2"), then `make -C doc doc`, `make -C doc test`, and `make -C doc publish` after Giuseppe's confirmation.

## Out of scope

- Aligning DDELAY and ADDELAY on `seam_delays.h`.
- The choir, which needs its own survey first.
- Any DC blocker or declared high-pass.
