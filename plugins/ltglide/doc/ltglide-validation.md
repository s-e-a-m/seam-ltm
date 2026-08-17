# ltglide — validation record

## DSP core doctests (Tasks 1–2)

`build/tests/Debug/ltglide_dsp_test`: **10 test cases, 1,177,030 assertions — all passed**
(DSP core `ltglide_dsp.h`, `SweepFreq` + `GlissBurst` + `GlideTransport`).

### `SweepFreq` — endpoints and midpoints

Reference: `slw.sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p))`.

- Exponential (`smode=1`, geometric sweep): `p=0 -> f0`, `p=1 -> f1`,
  `p=0.5 -> sqrt(f0*f1)` (geometric mean). Checked with `f0=20000, f1=20`.
- Linear (`smode=0`, arithmetic sweep): `p=0 -> f0`, `p=1 -> f1`,
  `p=0.5 -> (f0+f1)/2` (arithmetic mean). Checked with `f0=100, f1=200`.

### `GlissBurst` — constant-frequency onset spacing (gap mode)

At a *constant* drive frequency the retriggered-grain engine degenerates to a
fixed-period generator, which gives a numerical anchor back to `ltburst`.

With `f=1000 Hz`, `N=5`, `delta=0.3 s`, `dmode=1` (gap):

```
Tg = N/f + delta = 5/1000 + 0.3 = 0.305 s  ->  0.305 * 48000 = 14640 samples
```

The test drives `GlissBurst` at a constant `fsig=1000` and measures the
sample distance between two onsets (detected as a wrap of the grain ramp,
i.e. `grainPhase()` decreasing between two consecutive calls). Measured
spacing: **14640 samples**, matching the closed form exactly.

This is the same 14640-sample figure recorded in
`plugins/ltburst/doc/ltburst-validation.md` for `slw.shapedburst5(1000, 0.3)`
(dwell=0.3 s -> full period `P = f0*dwell + N = 305` cycles at fs=48000,
i.e. `305/1000*48000 = 14640` samples). At a constant carrier frequency,
`GlissBurst` in gap mode and `ShapedBurst` (`ltburst`'s verified DSP core)
produce the identical period. This ties the new grain engine numerically
back to the already-validated fixed-burst generator: `GlissBurst` is a
strict generalization of `ShapedBurst`, and reduces to it when the sweep
is degenerate (constant frequency).

### `GlissBurst` — step-mode fuse threshold (regime boundary)

Step mode's onset stride is `max(delta, N/f)` (`GlissBurst::periodSec()`,
`ltglide_dsp.h`) — a *floor*, not a fixed period like gap mode's `N/f + delta`.
At high frequencies `N/f` is small, so `delta` is the binding term: onset
spacing is constant at `delta`, which is longer than each grain's own duration
(`N/f`), so the grains sound as separated pips with silence between them.

As the swept frequency falls, `N/f` grows. At the crossover `delta == N/f`,
i.e. `f* = N/delta` (`5/delta` for the canonical `N=5`), the binding term
switches: below `f*`, `N/f` exceeds `delta` and becomes the onset stride
itself, so the next grain's onset lands exactly when the current grain's Hann
window closes. Onsets and window closes then coincide, grains touch with no
gap, and the morphology changes from a train of separated pips to a single
fused, continuously micro-stepped glissando — still amplitude-modulated by
the Hann window at each onset, but with no silence left to mark the
individual grains apart (Hann-envelope AM sidebands appear at `+-f/5` around
the carrier once fused).

Measured with `delta=0.02 s`: the morphology visibly changes around
`f ~= 250 Hz`, matching `f* = 5/0.02 = 250 Hz` exactly. Gap mode has no such
floor — its period `N/f + delta` never lets `N/f` alone determine the
spacing, so grains never touch regardless of frequency, and the fuse
threshold is a step-mode-only phenomenon. This is also why the GUI's
step-fuse threshold label (`ltglide_fuse_label.h`) draws nothing in gap mode.

For a morphologically uniform sweep of separated pips all the way down to
the sweep's low endpoint `f1`, either use gap mode, or, if step mode's
onset-spacing-tightens-with-frequency character is wanted, choose
`delta >= N/f1` so the floor never binds below `f1`.

### `GlissBurst` — burst starts on a zero crossing (near-zero, not bit-exact)

Approach A advances the grain ramp `phase_` by one increment *before* it is
used by the output stage in the same call — this differs from `ltburst`'s
exact-counter (`c`-as-state) design, which lands exactly on `u=0` at sample 0
(see the "State convention" note in `ltburst-validation.md`).

Consequently the very first output sample of `GlissBurst` is not exactly
zero: it is approximately **3.8e-6** in magnitude (a fraction of a phase
increment away from the true zero crossing). `doctest::Approx(0.0)` uses a
*relative* tolerance, which degenerates to near-zero tolerance against an
expected value of exactly `0.0` and would spuriously fail here. The test
therefore checks an *absolute* bound instead:

```cpp
CHECK(std::fabs(g.process(1000.0)) < 1e-4);
```

This is a faithful, intentional consequence of the Faust reference's
recursive `(pphase, pfg)` definition, not a bug — see
`plugins/ltglide/doc/study/ltglide-study.tex` for the derivation.

### `GlissBurst` — sample-and-hold latching

Feeding `fsig=2000` at an onset latches `heldFrequency() == 2000`; feeding a
different `fsig=500` for the next 100 samples (all inside the same grain,
well short of the next onset) leaves `heldFrequency()` unchanged at `2000`.
The swept frequency only takes effect at the *next* onset — confirms the
sample-and-hold semantics of the retriggered-grain engine (Approach A).

### `GlissBurst` — amplitude bound and robustness

- Peak amplitude over one full gap-mode period (`f=1000`, 14640 samples)
  never exceeds `1.0 + 1e-9` (Hann-windowed unit-amplitude carrier) and
  exceeds `0.5` (the window does reach its intended peak region).
- Swept over `fs in {44100, 48000, 96000}`, `dmode in {step, gap}`,
  `delta in {0.05, 0.3, 1.0}`, driven by a full exponential sweep
  `f0=20000 -> f1=20` across 20000 samples: every output sample is
  `std::isfinite` — no NaN/Inf produced anywhere in the parameter space
  exercised.

### `GlideTransport` — exact pass timeline

`GlideTransport` owns the sweep progress `p` and brackets each pass with a
head and tail Dirac impulse. With `fs=48000` and `setSweepSeconds(1.0)`
(`glideN = 48000` samples):

| step | duration | `Kind` | `p` |
|---|---|---|---|
| sample 0 (post-`trigger()`) | 1 sample | `Dirac` | — |
| lead | 5 s (240000 samples) | `Silence` | — |
| glide | 1 s (48000 samples) | `Glide` | `0.0 -> <1.0` (first sample `p==0.0`, last sample `0.99 < p < 1.0`) |
| tail | 5 s (240000 samples) | `Silence` | — |
| tail | 1 sample | `Dirac` | — |
| idle (no loop) | — | `Silence` | — |

- Before any `trigger()`, and with `setLoop(false)`, the transport emits
  `Silence` indefinitely and `running() == false`.
- `trigger()` while a pass is already running is a no-op: retriggering
  mid-lead does not reset `state_`/`counter_`, does not emit a second head
  Dirac, and the original lead window runs to completion undisturbed before
  the glide starts on schedule with `p == 0.0`.
- With `setLoop(true)`, the first tick out of `Idle` is the head Dirac of
  pass 1; after lead + glide + tail + tail-Dirac, the transport emits
  `Silence` for exactly `kWaitSec = 2 s` (96000 samples at 48 kHz), then
  starts pass 2 with a fresh head Dirac — verified end-to-end.

## Integration gate (Task 3–5)

### VST3 SDK validator

Run: `build/bin/Debug/validator build/VST3/Debug/ltglide.vst3`

Result: **47 tests passed, 0 tests failed** (exit 0).

Plugin enumerated correctly:

- FUID `5E4D000D B2C3D4E5 4C54474C 49444500` confirmed
  ("LTGL"/"IDE\0", 13th plugin in the suite; distinct from `ltburst`'s
  `5E4D000C`).
- 8 parameters (ids 100–106, 108):

  | id | title | unit | type | default (plain) |
  |---|---|---|---|---|
  | 100 | Level | dBFS | Float (linear) | −20 dBFS |
  | 101 | F0 | Hz | Float (log) | 20000 Hz |
  | 102 | F1 | Hz | Float (log) | 20 Hz |
  | 103 | Sweep | — | choice (linear/exponential) | exponential |
  | 104 | Timing | — | choice (step/gap) | gap |
  | 105 | Delta | s | Float (linear) | 0.3 s |
  | 106 | Sweep Time | s | Float (linear) | 20.0 s |
  | 108 | Loop | — | Toggle | 0 |

  The host transport is the sole sounding switch (design doc
  `2026-07-21-ltglide-host-transport-design.md`): play sounds, stop is
  silent. With Loop off, a play edge fires exactly one pass (Dirac → lead →
  glide → tail → Dirac), then silence until the next play edge; with Loop
  on, passes repeat (…→ wait → repeat) for as long as the transport keeps
  playing. Stopping mid-pass aborts to idle immediately and rings the
  in-flight grain out through the existing declick path, rather than
  clicking. A GUI SHOT button was prototyped for a LOOP-off single pass and
  then removed once the host transport took over that role: a momentary
  button also delivers its whole press/release pulse as one coalesced
  parameter point in some hosts (Reaper), so the rising edge never reliably
  reached the processor anyway. `GlideTransport::trigger()` remains in the
  SDK-free core as a manual/test primitive (exercised by the doctest and
  reserved for the 3b-ii receiver), independent of the host-transport gate,
  without a bound parameter.

- 1 mono output bus ("Output", Main-Default Active); 0 input buses
  (instrument, matching `ltburst`'s bus shape).
- Stereo output arrangement correctly rejected
  (`Plug-in suggests: Mono`) per the `setBusArrangements` guard
  (`getChannelCount(outs[0]) != 1 -> kResultFalse`).
- Double-precision (64-bit) process test suite passes identically to the
  32-bit suite; `Process Format` sweep passes at all tested sample rates
  from 1234.5678 Hz to 384000 Hz.
- No bypass parameter (correct — this is an instrument/generator, same
  convention as `ltburst`).

## Reproduction

```bash
cmake -S . -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target ltglide_dsp_test ltglide
./build/tests/Debug/ltglide_dsp_test   # (or ./build/tests/ltglide_dsp_test)
./build/bin/Debug/validator "$(find build -name ltglide.vst3 -maxdepth 4 | head -1)"
```

## Scope note

`ltglide` shares no code with `ltburst`'s `ShapedBurst` — `GlissBurst` is a
standalone, independently-tested DSP core (`ltglide_dsp.h`). The onset-spacing
parity documented above is a *numerical* anchor (both objects produce the
same period at a matching constant frequency/dwell), not a shared
implementation. No shared header was factored between the two plugins
(YAGNI — flagged in the implementation plan's self-review).

Measurement, spectra and receiver-side averaging analysis (the future
peer-aware receiver, phase 3b-ii) are out of scope for this validation
record; `ltglide` is validated here purely as a standalone generator against
its own Faust specification (`seam.linkwitz.lib`, `slw.sweepfreq` /
`slw.glissburst`).

## Bus shape and category change (2026-08-17)

Supersedes the "0 input buses (instrument)" line recorded above, and the note
that reads "this is an instrument/generator".

The behaviour that motivated the change was observed in Nuendo with
`multipink` and `ltglide` cascaded on the same insert chain: the incoming
signal was audible past the generators. The cause was the bus topology. With
no input bus declared, the host has nothing to hand the plugin and routes the
track signal *around* the insert, so the generator was never in the chain to
block anything — `processBlock` had been overwriting its output buffer all
along. The `Instrument|Synth` registration was the same fact seen from the
host's side, and put the generators in Nuendo's separate Instrument list.

`ltglide` now declares a main mono input bus that it never reads, and
registers as `Fx|Generator`. `setBusArrangements` still accepts zero input
buses, which keeps the plugin usable on an instrument track. `ltburst` and
`multipink` took the same change on the same day.

`process` also clears `data.outputs[0].silenceFlags` — a host that trusted an
inherited silence flag would skip every plugin downstream, `strx` included,
which is the one thing that must never miss a pass.

Re-run: `build/bin/Release/validator build/VST3/Release/ltglide.vst3`
Result: **47 tests passed, 0 tests failed**.

```
subCategories = Fx|Generator
=> Audio Buses: [1 In(s) => 1 Out(s)]
     In [0]: "Input" (Main-Default Active)
     Out[0]: "Output" (Main-Default Active)
```
