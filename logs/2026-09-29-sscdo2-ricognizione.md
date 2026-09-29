# Session log — 2026-09-29 — SSCDO#2: survey of the Pd performance patch

## Goal
Giuseppe will perform *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco) at the next CIM, in the live-electronics role.
The work moves from Pure Data to Reaper, and the result is offered to Davide as a complete interpretation: code organisation, the operational table the CIM reviewers found missing, and the performance.
The method is the one of the suite: a Faust specification in `faust-libraries` (a new `seam.tedesco.lib`), then a hand-written C++ VST3 in `seam-ltm`, one DSP at a time, from the simplest to the most complex.

Davide notices a timbral degradation between his Faust tests and the compiled Pd externals.
The port is also the occasion to measure it, and to check whether the VST departs from the Pd rendering.

This log records the survey only.
No code was written and no repository other than this one was touched.

## Source
Davide's repository, read-only: `/Users/giuseppe/Documents/gitlab/dt/studio_sul_corpo_dombra_numero_2`, HEAD `94be6e4`.
Pd host `src/Pd/Cortegiani-Tedesco-Studio_sul_corpo_d'ombra_#2.pd` (about 2800 lines), Faust sources in `src/FAUST/targets/<name>/`, score material in `doc/` (live-electronics actions, cue list, whiteboards of 21 and 22 November 2025, TikZ schema draft).
The submodules `src/faust-libraries` and `src/st` are declared but not initialised in this checkout.

The patch was parsed with a script, so that `#X connect` indices were resolved mechanically.
Object indexing counts every `#X obj/msg/floatatom/symbolatom/listbox/text/array` record and every `#X restore`; abstraction iolets are ordered by x position.

## Technical setup
Stated by Giuseppe, cross-checked against the mixer photo `doc/mixer_setup/20251122-LEAP/IMG_7895.JPG`.

The SSL BiG SiX is the heart of the setup, both as analog mixer and as the computer's USB interface.
Two TETRAREC arrays feed it:

- **TETRAREC A**, the digital chain of the first part: its microphones enter an Audient ASP880, whose line outputs reach the BiG SiX inputs 5–8.
- **TETRAREC B**, the analog chain of the second part: it enters the BiG SiX inputs 1–4 directly, the four mono SuperAnalogue channels with EQ, compressor and insert.

This matches the patch.
Pd reads USB inputs 5–8 (`adc~` outlets 4–7), which are the two stereo channels 5/6 and 7/8 carrying TETRAREC A.
Pd returns on USB outputs 9–12 (`dac~ 9 10 11 12`), which come back on the stereo channels 9/10 and 11/12 with their `FROM USB` switches engaged: these are the "faders 9, 10, 11, 12 for live electronics" of the score.
Alice's move at 5'30" from the front array (A) to the rear one (B) is the move from the digital chain to the analog feedback loop.

The four drivers of STONED are fed by the two stereo cue outputs of the BiG SiX.
The Main bus is left to Alice, who controls SOMAFONICO (the bodice) and ANAPNOE (the mute) with a pedal.

## What sounds in the performance
Four Faust units, all inside `pd GRAND_CENTRAL`, all four channels wide and channel-parallel.
Inputs are the physical channels 5–8 (`adc~ -m`, 16 channels, only outlets 4–7 used); everything is thrown to `mix_4ch` and leaves on `dac~ 9 10 11 12`, labelled LFU, RFD, RBU, LBD: the four drivers of STONED.

| Unit | Role | Live control |
|---|---|---|
| `liveMultiOscs_30000_plain` (LMO) | Generator: narrow noise band, Butterworth HP + LP of order 24 at F·1.015 + k Hz per channel | volume ← CC81; frequency ← cue list |
| `delRM_duet` | Channels 1, 3: x + x[n−D]; channels 2, 4: 10·x·x[n−D]·∫x into `co.compressor_mono`; D = 22 ms, fixed | volume ← CC82 |
| `stunedrev` (the "APF" of the score) | 42 Schroeder all-pass sections per channel, g = 1/√2, prime delays scaled by √2, φ, e, π; initial T = 83, 47, 7, 71 ms | input ← CC83, output ← CC84 |
| `pitchDetectorChoirMcAdams` ×4 | 16 bands: the envelope of band k at f·k modulates noise filtered at f·k^a; f = 48, 48, 96, 96; a = 1, 1.01, 1.1, 0.9; Q = 350, release 1.5 s | output ← CC86 |

Around them Pd adds only linear gain stages (`interpolator_4ch`: `line` with a 25 ms ramp, updated per 64-sample block) and a master fader (CC88).
The BCF2000 is read on CC81–88; CC85, CC87, CC94 and CC1–2 have no consumer, and motor-fader feedback is not wired.

Not in the performance: `apfn4`, `apfn4x4` (both the Pd abstraction and the Faust external), `quadprog` (declared on a path, never instantiated), the `_30000` and `_sqrt2` LMO variants, `pitchDetectorChoir`, `pitch_tracker`, and every DSP abstraction in `src/Pd/abstractions/` (`spectral_pedal`, `hadamard_base`, `filters_SSL`, `delay_4chs`, `noise_tri_bp_pitch_activated`, `allpass_filter`).
The TESTER section is silent unless its noise toggle is set.

### Cue system
`le.cue` (adapted from Fiordelmondo's pd-live) reads `sscdo2_cuelist.txt` into a `text define`.
Every cue addresses `LMO_glissando`, which drives `line` into `LMO_frequency`:

| Cue | Result |
|---|---|
| 0 | LMO at 48 Hz (initialisation, silent) |
| 1 | LMO at 96 Hz |
| 2 | Glissando 96 → 111 Hz over 120 s |
| 99 | End |

Keyboard control is gated by a toggle: space advances, `i` re-initialises and runs cue 0, Esc turns DSP off.
Every other action in the score (delRM, APF, choir, and the whole analog section) is manual, on MIDI faders or on the SSL BiG SiX.

## Findings
**Build flags.**
The four externals are compiled with `faust2puredata -double` under Faust 2.72.14, verified in each `Makefile.sh` and in the strings of each binary.
Float precision is therefore not the cause of the Faust → Pd degradation.
The remaining candidates: a library version change between 2.72.14 and the current Faust IDE, initial values in the faust2pd wrappers that differ from the `.dsp` defaults (delRM Master Volume 0 against 0.9, choir frequency 27 against 17), SR-dependent constants, and block-rate control updates.

**Double feed into the reverb.**
`stunedrev.pd` holds an `adc~ 5 6 7 8` wired to inlets 1–4 of `stunedrev~`, verified on the `#X connect` records.
Pd sums signals on an inlet, so the reverb receives the inputs unconditionally and the APF INPUT fader adds at most +6 dB.
It is a leftover of standalone testing: commit `ba2e278` removed the matching `dac~`.
Whether the port reproduces or corrects it is Davide's decision.

**Redundant noise streams.**
The LMO's two "oscillators" share seed and frequency, so they sum to one signal at +6 dB.
The four choir instances share the same `multinoise` seed; channel differences come only from f and a.

**Memory in the reverb.**
Faust rounds every `de.delay` buffer to the next power of two, about 1 GB of doubles at 48 kHz; the patch carries a note asking for 32 GB of RAM.
The longest delay in use is about 71 ms · 42 · π ≈ 9.4 s.

**Sample-rate dependence.**
- `fi.dcblocker` has a fixed pole at 0.995: about 38 Hz at 48 kHz, 76 Hz at 96 kHz, applied twice in series in delRM.
- `fi.integrator` in delRM is an unbounded sum whose level grows with the sample rate and accumulates DC.
- delRM's `nmax = 384000` samples halves in seconds when the sample rate doubles.
- stunedrev converts ms to samples and then to the next prime, so the set of primes differs per rate.
- The performance sample rate is written nowhere in the repository.

**Unconnected wrapper state.**
The faust2pd wrapper of the LMO shows stale slider ranges (0..0.998, 20..30000) against the committed `.dsp` (0..1, 20..1500); values are sent raw, so it does no harm.

## Content audit
Every function the four DSPs use was checked against the Faust standard libraries shipped with 2.72.14 (faustlibraries `d28c51f6`, pinned by the compiler's `libraries` submodule), against the current ones (`9c42142`, pulled today in `/Users/giuseppe/Documents/github/grame`), and against SEAM.
Working files are in the session scratchpad; the conclusions are here.

**What changed since 2.72.14.**
The four DSPs were compiled with the old and with the new libraries and the generated C++ was diffed.
delRM, stunedrev and the choir give byte-identical code.
The LMO is the only one that differs, and the only cause is `fi.lowpass`/`fi.highpass`: faustlibraries `0965ea2` (2026-09-26, Julius Smith, #262) rebuilt their second-order sections as trapezoidal SVFs, accurate in float at low cutoffs.
With a fixed cutoff the two forms agree to −190 dB; with the cutoff moving they part (−21 dB for a 0.2 Hz sweep, −4.4 dB for a 2 Hz sweep).
In float, the old direct form misplaces the LMO's band by −12.5 dB at 20 Hz; the new form stays within about −50 dB of double.

**Candidate causes of the Faust → Pd difference Davide hears**, from the evidence so far:
- The IDE compiles in float by default (to be confirmed), the externals in double: with the old filters this moves the LMO's low bands.
- `stunedrev_online_compile.dsp`, the variant the IDE can compile, is a different reverb: 81 sections instead of 42, no prime rounding, other buffer sizes.
- delRM's `fi.integrator` has unbounded state, so its ring-modulated channels change with how long the external has been running.
- `fi.dcblocker` tracks the sample rate; different rates in the IDE and in Pd give different corners.
- The externals are built with `-ftz 0`; long all-pass and high-Q tails may reach denormals (inferred, not measured).

**Verdicts.**

| Function | Verdict |
|---|---|
| `fi.dcblockerat`, `co.compressor_mono`, `si.smoo`, `an.amp_follower`, `ba.*`, `si.bus`, `ro.interleave`, `ma.*` | standard, as is |
| `fi.highpass`/`fi.lowpass` | standard, current SVF form; decide whether the historical sweep behaviour matters |
| `de.delay` | standard, with the arguments in the right order and the buffer sized in seconds |
| custom `apf` + `srprev`… | standard `fi.allpass_comb` or SEAM `sms.apfs`/`sms.apfv`: identical impulse response, measured |
| `next_pr` (`nextprime.h`) | SEAM `sff.np`: zero mismatches over −1000…6 000 000 |
| `phx` | SEAM `sma.phi` |
| meters, `envelop` | SEAM `san.phmeter`, `seam_meter.h` (GUI, not specification) |
| `no.multinoise` | decision: fixed seed, identical across calls and instances |
| `fi.dcblocker` | decision: sample-rate dependent; `fi.dcblockerat(38.3)` equals it at 48 kHz |
| `fi.integrator` | decision: unbounded; leaky pole or DC block before it |
| `fi.svf.bp` | decision: diverges to inf/NaN at f ≥ SR/2, reachable in the choir (f·n^a) |

**Defects in the code itself.**
- LMO: `freq+(i+1)*1.03*(freq/(i+1))` is 2.03·freq for every i, so the second oscillator is the first one again (+6 dB, difference measured 0); the comment "resulting frequency 2*chosen" is wrong, the centre is 1.015 × slider.
- Choir: `partial_order` and `random_for_partial` are dead code with undefined symbols.
- stunedrev: buffers sized from the SR clamp, about 4.2 GB per instance at build time against about 0.3 GB needed at 48 kHz.

**Side effects on SEAM.**
- `no.multinoise(N)` is one LCG stepped N times per sample with a fixed seed: the comment at `plugins/multipink/source/multipink_processor.h:18` ("per-channel seeds dispersed") misdescribes the Faust reference; the C++ seeding is correct.
- `sno.multipink` keeps the shared-seed `no.multinoise`, unlike its C++ port.
- faustlibraries `4b251bf` (#261) rebuilt `tf2sb`/`tf1sb`, hence `fi.bandpass6e`, as float-accurate SVF sections: `san.thirdoctave_levels` may no longer need double on the Faust side; strx's C++ reflects the old form. Not re-measured.
- `sfi.SVFTPT` now overlaps the standard `fi.SVFTPT` (added 2024-11-14).
- SEAM's `src/h/nextprime.h` has no include guard.

## Decisions
- Work lives in `faust-libraries` (the specification, `seam.tedesco.lib`, prefix `sdt`) and in `seam-ltm` (the plugins), as for the rest of the suite.
  No separate composition repository: it would duplicate Davide's; a SEAM repository of his own is his call.
  At most, plugin families are packaged in separate releases.
- Before any line of `seam.tedesco.lib`, a content audit: every function the four DSPs use is checked against the current Faust standard libraries (pulled locally in `/Users/giuseppe/Documents/github/grame`) and against SEAM.
  What the standard library provides is used as it is, unless a defect is found; where SEAM holds a newer version (as for the matched-Z pink curve of `multipink`), the new SEAM version goes in.
- Proposed order: LMO, delRM, stunedrev, choir; the cue and MIDI layer last, in Reaper.
  Gain stages are left to Reaper's track faders.

### No faithful port
Giuseppe, on reading the audit: SEAM's corrected blocks already modify the original, so a "faithful" version is not the baseline; reasoning is the purpose of the port.
Each DSP is split into functional blocks, and each block is discussed, decided and validated on its own before the next.
The order of preference is the corrected SEAM version, then the standard library when it is sound, then new code only for a detected defect.
An entry of `seam.tedesco.lib` may be a single reference to a SEAM function.
Its comments state briefly when and why the SEAM version was written and what the original used, so that the library speaks both to us and to Davide.

At the end of the session, the SEAM integrations (matched-Z pink filter, Hilbert pair, and whatever this port produces) are to be weighed as proposals to GRAME.

### First block: the LMO noise source
Premise checked before writing: SEAM's correction of multichannel noise exists only in C++ (`multipink`: splitmix64 seeds and the 64-slot pool).
On the Faust side `sno.multipink` still calls the standard `no.multinoise`, and the pink filter plays no part in the LMO, whose noise is white.
So the corrected noise source does not exist yet in `faust-libraries`: the first block is a new SEAM function, and `sdt` refers to it.

The standard generator is `y[n] = 1103515245·y[n−1] + seed` in 32-bit integers, starting from zero, with `seed = 12345` fixed.
Measured over 10⁵ samples, correlation against the stream of seed 12345:

| Seed | r |
|---|---|
| 12346 (+1) | −0.005 |
| 24690 (×2) | −0.249 |
| 37035 (×3) | +0.337 |
| 61725 (×5) | +0.201 |
| hash(1) | +0.001 |
| hash(2) | +0.003 |

With the additive seed, two seeds in a small integer ratio give streams in the same ratio modulo 2³², and the correlation reaches 0.34.
A seed parameter exposed as it is would invite exactly the naive choice `seed·(i+1)`.
The seed must pass through a hash before it reaches the generator: the Faust counterpart of the rule already applied in `multipink`.

## Open
Questions for Davide:
1. The double feed into `stunedrev`: intended or residual?
2. The performance sample rate.
3. Which cue output and side carries each of LFU, RFD, RBU, LBD.
   (Inputs 5–8 and the output busses are now known: see Technical setup.)
4. The shared noise seeds in LMO and choir: a sound to keep, or an oversight?

For us:
- The four "decision" verdicts of the content audit: seeding, `fi.dcblocker`, `fi.integrator`, `fi.svf.bp` at Nyquist.
- Which library version and precision the online Faust IDE uses today.
- Rebuilding the LMO with the 2.72.14 compiler, to close the comparison (only stunedrev was checked against 2.72.14-generated code).
- An A/B protocol: Faust in double as the oracle, the Pd external, the VST, on the same input.
- The operational table, and its single source for both the table and Reaper's markers.

## Who
**Who:** Claude (agent), on Giuseppe's instructions.
