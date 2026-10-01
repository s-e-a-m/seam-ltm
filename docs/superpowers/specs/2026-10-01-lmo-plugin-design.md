# LMO plugin — design

Date: 2026-10-01.
Status: approved in conversation (three sections), awaiting review of this document.

## Purpose

LMO is the generator of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco): two narrow bands of noise per channel on the four drivers of STONED, which can beat at a distance Δ.
This plugin is the first C++ port of SSCDO#2 for Giuseppe's performance at the next CIM, hand-written from the specification `sdt.lmo(N, f, d)` in `seam.tedesco.lib` (faust-libraries), following the suite convention: Faust is the spec, C++ is the deliverable.
It is played in Reaper at 96 kHz.

Success means: the plugin reproduces `sdt.lmo(4, f, d)` to numerical precision at 96 kHz; it sounds as it does at 96 kHz at any other rate; the cue-2 glissando (97.44 → 112.67 Hz over 120 s) runs inside the plugin without steps; the window follows `doc/style/ui-style.md`; every decision is in the session log.

## Constraints (from Giuseppe, 2026-10-01)

- State and arithmetic in `double` throughout.
- UI coherent with the suite: `ui-style.md`, started from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Reuse `_common/` where it exists; write what is new as reusable `_common/` code.
- **SR rule:** every process sounds as it does at 96 kHz whatever the session rate. Time and frequency constants are designed at `setupProcessing(fs)`; constants that a white source makes rate-dependent (band level) are anchored at 96 kHz.
- stunedrev (next plugin, not this spec): the 2026-09-29 reading stands — times invariant, prime sets per rate.

## Architecture

```
plugins/lmo/
├── CMakeLists.txt
├── source/
│   ├── lmo_ids.h            # FUID 0x5E4D0010, parameter IDs
│   ├── lmo_dsp.h            # SDK-free engine: noise → 16 filters → sum → density → volume
│   ├── lmo_processor.h      # FAUST REFERENCE block, IAudioProcessor
│   ├── lmo_processor.cpp
│   └── version.h
├── resource/lmo.uidesc      # from _template.uidesc, format S
└── doc/
plugins/_common/
├── seam_noise.h             # NEW: FaustMultinoise
└── seam_butterworth.h       # NEW: order-N Butterworth HP/LP in trapezoidal SVF form
```

`lmo_dsp.h` is SDK-free (like `addelay_dsp.h`) so doctest can drive the whole engine.

### `seam_noise.h` — `FaustMultinoise`

The standard `no.multinoise(N)` (`_noise_env(seed).multirandom`), replicated bit for bit:
one 32-bit LCG, `x ← (x + seed) · 1103515245` with signed 32-bit wraparound, stepped N times per sample inside one feedback loop; each step divided by `2147483647.0` in double.
The output order and the fed-back value follow `randomize(N) = randomize(1) <: randomize(N-1), _` exactly (the streams come out in reverse step order; the test against Faust is what proves the mapping).
Seed defaults to 12345 and is a constructor argument, so the choir (four instances that in the original share the seed — an open question for Davide) can reuse it with distinct seeds.
Interface: `FaustMultinoise(int n, int32_t seed = 12345)`, `void reset()`, `void tick(double* out)` (writes n values).

### `seam_butterworth.h` — order-N Butterworth, SVF sections

The current `fi.lowpass0_highpass1(s, N, fc)` (faustlibraries 0965ea2, J.O. Smith, issue #262): for even N, N/2 sections `svf.lp(fc, 1/a1s)` or `svf.hp(fc, 1/a1s)`, with `a1s = -2·cos(-π + π/(2N) + (S-1)·π/N)` for section S = 1…N/2, in that order; odd N adds the first-order `tf1s` section first (implemented for completeness, tested, unused by LMO).
Each section is the trapezoidal SVF of `filters.lib` `svf`: `k = a1s`, `g = tan(π·fc/fs)`, `v1 = (ic1 + g·(v0 − ic2)) / (1 + g·(g + k))`, `v2 = ic2 + g·v1`, `ic1 ← 2v1 − ic1`, `ic2 ← 2v2 − ic2`; LP output `v2`, HP output `v0 − k·v1 − v2`.
The damping `k` of each section depends only on N and is computed once; a frequency change recomputes only `g` and the shared denominator.
Interface: `template<int N> class ButterworthSVF` with `setType(LP|HP)`, `setFrequency(fc, fs)`, `reset()`, `double tick(double)`.

### The engine (`lmo_dsp.h`)

`sdt.lmo(4, f, d)`, channel i = 0…3:

- streams 0…3 from one `FaustMultinoise(8)` feed bands at `f − d/2 + i`; streams 4…7 bands at `f + d/2 + i`;
- each band is `HP24(fb) : LP24(fb − 0.0001)` (`sdt.lmoband`), 16 filters in all;
- channel i = (band_low_i + band_high_i) / √2;
- × **density** `√(fs / 96000)`;
- × volume (smoothed), × POWER gain (smoothed).

**Density anchor.** White noise of constant RMS spreads its power over fs/2: a band of fixed width receives +3.01 dB at 48 kHz against 96 kHz.
Multiplying by `√(fs/96000)` holds every band at its 96 kHz level (unity at 96 kHz, so the 96 kHz numbers of the studies are unchanged).
The same factor is added to the Faust spec (`sdt.lmoosc` and `sdt.lmo`, as `*(sqrt(ma.SR/96000))`), with a comment stating the rule, so spec and C++ stay identical.

**Ramps.** All are linear and anchored in time:

- `f`: on a new target, a linear ramp in Hz from the current value, lasting `glide` seconds (Pd `line` semantics: a target arriving mid-ramp restarts from where the ramp is). With `glide = 0` the ramp lasts 25 ms, which turns host automation into a continuous curve.
- `Δ`: always the 25 ms ramp. (Change from the in-chat design, where Δ also followed `glide`: Δ is a hand control, and with `glide` at 120 s for cue 2 a hand move of Δ would take two minutes.)
- volume and POWER: 25 ms, the ramp of the original's `interpolator_4ch`.

**Coefficient update cadence.** Filter frequencies are recomputed every `max(1, round(fs/6000))` samples (16 at 96 kHz): a cadence in time, 6 kHz, not in samples.
When nothing ramps, nothing is recomputed.

**POWER** is a gain ramp; the noise and the filters run always (192 SVF sections per sample, negligible), so a power-on finds the bands already formed, with no start-up transient.

**Reset** (`setupProcessing`, `setActive(true)`): noise to seed, filter states to zero, ramps snapped to their targets.

## Parameters, bus, state

| ID | Parameter | Range | Default | Note |
|---|---|---|---|---|
| 100 | POWER | off/on | off | toggle (stable value, not momentary) |
| 101 | f | 20–1500 Hz | 48 Hz | cue 0; the committed `.dsp` range |
| 102 | glide | 0–300 s | 0 s | cue 2 uses 120 s |
| 103 | Δ | 0–50 Hz | 0 Hz | the listening study's range |
| 104 | volume | 0–1, linear | 0 | CC81 in the original |
| 200 | f now | 20–1500 Hz | — | read-only, pushed from the audio thread: the frequency during a glissando |

Parameter mappings are linear, as in the original sliders; the value fields accept typed entry. Whether f needs a finer fader mapping is a rehearsal question.

Bus: one output `kAmbi1stOrderACN` (4 channels, LFU, RFD, RBU, LBD in the order of `dac~ 9 10 11 12`), and one 4-channel input that is declared and never read, so the host does not route audio around the insert; subcategory `Fx|Generator` (reference_vst3_generator_bus_category).
The output replaces the signal, as in multipink.

State through `seam_state.h`, append-only: POWER, f, glide, Δ, volume.

## Window

Format S (300 px, one column), from `_template.uidesc`:

- HEADER: LMO, "Studio sul Corpo d'Ombra #2", tagline "two beating noise bands, four channels".
- OPS: POWER.
- FINE: f, glide, Δ, volume.
- FOOTER: the current frequency (`f now`) and the rate, logo.

No SETUP zone (LMO has no station identity).

## Testing

doctest, `tests/lmo_dsp_test.cpp` and `tests/seam_butterworth_test.cpp`, `tests/seam_noise_test.cpp`.
Faust references are generated by a committed script (`doc/study/sscdo2/lmo-plugin/gen-ref.sh`, `faust -double` with the faustlibraries clone) into committed headers; building and running the tests needs no `faust` binary.

1. `FaustMultinoise(8)` equals `no.multinoise(8)` bit for bit over 4096 samples.
2. `ButterworthSVF<24>` HP and LP equal `fi.highpass/lowpass(24, 97.44)` on an impulse at 96 kHz, |error| < 1e-12 relative to peak; also N = 1, 2, 3 against Faust.
3. The engine equals `sdt.lmo(4, 97.44, 20)` (with the density factor) on all four channels at 96 kHz, |error| < 1e-12, volume 1, POWER on, ramps settled.
4. SR invariance: band level (RMS of channel 0 over 10 s, f = 97.44, d = 0) at 48, 96, 192 kHz within 0.1 dB of the 96 kHz value; centre frequency of the band equal across rates.
5. Glissando: 97.44 → 112.67 Hz with glide = 120 s reaches the target at 120 s ± one update period, monotonic, and the largest sample-to-sample step of the frequency trajectory is one update increment.
6. POWER and volume ramps: 25 ms ± one sample at 48 and 96 kHz.

Every test is verified by mutation (feedback_verify_tests_by_mutation): wrong stream order, wrong section order, missing density factor, ramp in samples instead of seconds — each must turn its test red, recorded in the study.
Then the VST3 validator and a listening check in Reaper at 96 kHz (Giuseppe).

## Documentation and log

- `FAUST REFERENCE (seam.tedesco.lib)` block at the top of `lmo_processor.h`, quoting `sdt.lmoband`, `sdt.lmo`, and stating the density anchor and the ramps.
- `plugins/lmo/doc/`: what LMO is, parameters, SR rule.
- `doc/study/sscdo2/lmo-plugin/`: the reference script, the mutation record, renders at 48/96 kHz if useful; a row in `doc/study/sscdo2/README.md`.
- `doc/plugins.toml`: a new family for the ports of works, "Works — SSCDO#2", with LMO; `make -C doc doc`, `make -C doc publish`, and the "sixteen" count in the generators and `make -C doc test` updated to seventeen. Publishing the page is outward-facing: confirmed with Giuseppe before running.
- faust-libraries: the density factor in `sdt.lmoosc` and `sdt.lmo`, with its comment.
- Session log: a new dated log for the C++ phase, `logs/2026-10-01-sscdo2-plugins.md`, linked from the survey log, with every decision of this spec: SR rule, stunedrev reading confirmed, C chosen for the glissando, Δ on the short ramp, density anchor, name `lmo`.
- The report (`doc/study/sscdo2/report/`): the LMO cards gain the plugin's parameter names and the density rule, then `make check`.

## Out of scope

The cue and MIDI layer in Reaper (last, after all plugins), stunedrev, delRM, the choir, the delRM DC-blocker decision, Davide's choice of band filter (A/B/C/C3/C2: the plugin implements B, the spec).
