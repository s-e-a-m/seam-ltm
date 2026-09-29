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
Faust rounds every `de.delay` buffer to the next power of two; the patch carries a note asking for 32 GB of RAM (the figure first written here, about 1 GB, was wrong: see "stunedrev: memory" below).
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
Decision: the standard `no.multinoise` stays, and no SEAM alternative is written.

The LMO's noise is white; the pink filter plays no part in it.
Within one call, `no.multinoise(N)` gives N decorrelated streams (max |r| 0.002 over 120 pairs, at chance level), which is all a tetrahedral source needs: one `multinoise(4)` feeds LFU, RFD, RBU, LBD with independent noise, as `sno.multipink(4)` already does.

The fixed seed makes two streams identical only in two cases.
Two calls in one DSP: in the LMO the second call feeds a second oscillator that duplicates the first, so the remedy belongs to the oscillator block, not to the generator.
Two plugin instances: a matter of the C++ instance, which the specification cannot see, and which `multipink` already solves with splitmix64 seeds and the pool.

Measured on the way, and kept as a warning for whoever introduces seeds: the generator is `y[n] = 1103515245·y[n−1] + seed` in 32-bit integers from zero, so seeds in a small integer ratio give streams in the same ratio modulo 2³².

| Seed against 12345 | r |
|---|---|
| 12346 (+1) | −0.005 |
| 24690 (×2) | −0.249 |
| 37035 (×3) | +0.337 |
| 61725 (×5) | +0.201 |
| hash(1), hash(2) | +0.001, +0.003 |

### Second block: the LMO band filter
Why the standard library changed: faustlibraries `0965ea2` (issue #262) computes the Butterworth sections of `fi.lowpass`/`fi.highpass` as trapezoidal SVFs instead of direct-form biquads.
The transfer function is the same; in float the direct form misplaced its poles near z = 1 at low fc/SR, and at 192 kHz `fi.lowpass(3, 10.1)` became unstable.
Davide's externals are compiled in double, so in Pd his filter was already accurate.

What the filter is: HP24 : LP24 at the same fc gives |H|² = 1/(2 + r⁴⁸ + r⁻⁴⁸), a band-pass peaking at −6.02 dB with a −3 dB band of 7.35 % of fc (Q ≈ 13.6), −72 dB at ±½ octave.
The −0.0001 Hz offset plays no part.

Measured in `doc/study/sscdo2/lmo-bandfilter/` on three candidates, at 48 kHz in double: A (the original on the 2.72.14 libraries), B (the same expression on the current SVF libraries), C (`fi.bandpass(24, fl, fu)` with A's half-power edges, scaled to A's peak).
- A and B match the analytic response to 2·10⁻⁴ dB; on the cue-2 glissando B − A is −74.7 dB RMS, with no transient.
  On a step B has no overshoot, where A overshoots by 1.3 dB.
- C shares A's −3 dB band and has near-vertical skirts, but its slowest poles have Q ≈ 208 against A's 7.6: 4.95 s to decay by 60 dB against 0.31 s, and 0.68 s of group delay against 117 ms (checked analytically: T60 ≈ 4.7 s).
- The leakage the #262 commit warns about is present in both forms at about −124 dBFS, inaudible.
- `Nh` was read correctly: C has as many sections as A.
  The band-pass lays its skirts out in bandwidths (7 Hz), Davide's HP : LP in ratios of fc (97 Hz), so the same order falls about fc/BW ≈ 14 times faster.
  No order reproduces A: its rounded peak, the product of two overlapping skirts, lies outside the Butterworth band-pass family, whose top is flat.
- At Giuseppe's request the orders nearest A were added, as reasoning material for Davide: C3 (`Nh` = 3) and C2 (`Nh` = 2), same edges and peak.
  At ¼ / ½ / 1 octave: A −36 / −72 / −145 dB, C3 −47 / −65 / −85, C2 −33 / −45 / −58.
  Decay to −60 dB (energy): A 0.31 s, C3 0.64 s, C2 0.47 s, C 4.95 s.

Giuseppe listened to all renders.
Decision: B, Davide's own line on the current libraries, and the port goes on with it.
A, C, C3 and C2 stay in the study and in the renders, so that the choice can be argued with Davide before it is called settled.


### First entry of `seam.tedesco.lib`
`faust-libraries/src/seam.tedesco.lib`, prefix `sdt`, registered in `seam.lib` under a new "composer specific literature" heading.
- `sdt.lmoband(f)`: the original's HP24 : LP24 line, unchanged, on the current libraries (SVF sections).
- `sdt.lmo(N, f)`: `no.multinoise(N)` through `lmoband`, channel `i` at `f + i` Hz as in the original.

Verified by compiling: the `compute()` of `sdt.lmoband` is identical to the original line on current faustlibraries, and differs on the libraries bundled with Faust 2.88.0 (Homebrew), which still carry the direct form.
The library therefore requires `-I <faustlibraries clone>`, and says so in its header.

Left out, waiting for Davide: the second oscillator (it doubles the first: the frequency formula `freq + (i+1)·1.03·freq/(i+1)` is 2.03·freq for every `i`, and the seed is fixed), the 1.015 factor, the high-passed direct noise, the output stage.
The per-channel `+ i` Hz offset is kept as in the original, not yet discussed.

Giuseppe's listening test of one against two oscillators (`doc/study/sscdo2/lmo-streams/`): his `lmo(N, M, f)` spreads N noise streams over the channels, and at N = 2 he heard more complex phases and a widening, with no change of intonation.
Read and measured: `<:` repeats the streams cyclically, so the two blocks are identical (difference exactly 0) and `/(N)` restores the level; what changes is that channels 0 and 1 no longer share a stream (r from 0.998 to −0.02).
The widening is the decorrelation of the channels, which the original already has with one `multinoise` stream per channel; the `+ i` Hz offsets separate nothing on their own (1 Hz against a 73 Hz band at 1 kHz).
Recorded in `sdt.lmo`'s comment; the library now also cites Davide's GitLab repository.

### Davide's answers (via Giuseppe, 2026-09-29)
**delRM delay.** 22 ms is the initial value; the delay is calibrated during the performance.
Agreed with Davide: the delay uses DDELAY's prime rounding, so that the tool is ready for *Studio sul Corpo d'Ombra #4*, with two sources, which will use the same system.

**LMO second oscillator.** The intention: two oscillators that can beat.
At 0 Hz apart they share the frequency and are decorrelated; at a distance Δ each moves by Δ/2 from the reference band centre (10 Hz apart: −5 and +5 Hz).
The distance slider stays narrow, in the range of beats up to about the critical band.

**Decisions.**
- delRM delay: DDELAY as it is — distance in metres, conversion at c = 331.4 m/s, rounded up to the next prime, integer samples, with the value shown in ms in the UI.
  The delay is dynamic while the setup is tuned and static during the piece, so the click on a change is acceptable: Davide's standard `de.delay` clicked in the same way.
- LMO second oscillator: 2N streams from one `no.multinoise(2N)` call; streams 0 … N−1 feed the band at fc − Δ/2 + k, streams N … 2N−1 the band at fc + Δ/2 + k, and channel k sums the two.
  The per-channel offset k cancels in the difference, so every channel beats at the same Δ.
  Two independent noises sum in power, so the level compensation is 1/√2.
  The range of the Δ slider is set by a listening study (`doc/study/sscdo2/lmo-beats/`).

**The two-oscillator study** (`doc/study/sscdo2/lmo-beats/`), at f = 97.44 Hz: the design holds (level within 0.2 dB of one oscillator at every d, bands independent, channels uncorrelated).
A band of noise already fluctuates, with an envelope spectrum that falls 10 dB by 11.5 Hz; the beat of two bands is a broad bump at d riding on it.
Excess at d over d = 0: +0.1 dB at 3 Hz, +0.9 at 7, +3.0 at 10, +15.8 at 20, +34.5 at 40.
So the beat emerges from about 10–15 Hz; below, a second band widens the sound without beating.
Renders for listening at d = 0, 3, 7, 10, 20, 40 Hz (mono and four-channel) and a sweep of d from 0 to 40 Hz.
The slider's range is left to that listening.

**The LMO, closed on the Faust side.**
Giuseppe widened the Δ range to 0–50 Hz, for experiment, and agreed with the rest:
- the 1.015 factor, a by-product of the old two-oscillator formula, is dropped: the frequency is the band centre, and the cues become 97.44 Hz and 97.44 → 112.67 Hz, the frequencies Davide actually heard;
- the high-passed direct noise is left out: it started at volume 0 on no fader and never sounded;
- `fi.dcblockerat(20)` is removed, redundant after the 24th-order highpass;
- no +6 dB make-up: the normalisation corrects an anomaly, and the level is set by playing, not by compatibility.

`seam.tedesco.lib` now has `sdt.lmoband`, `sdt.lmoosc` (one oscillator, formerly `sdt.lmo`) and `sdt.lmo(N, f, d)`, the two beating oscillators.
The `lmo-beats` self-test checks that `sdt.lmo` equals the measured prototype sample for sample, and catches a 1 Hz change of d.

### delRM block 1: the delay
DDELAY's Faust specification had drifted from the plugin: `sma.imdelay` truncates `mt·SR/331.4` and has no primes, while the C++ rounds to the millimetre, to the nearest sample, and to the next prime strictly above.
`sma.imt2npsamp(mt)` and `sma.imnpdelay(maxdel, mt)` in `seam.math.lib` now specify what the plugin does, verified value by value against the lines of `updateDelaySamples`: 300 001 distances from 0 to 30 m, 0 mismatches at 44.1, 48, 96 and 192 kHz; a 1 mm mutation gives 5 940 (`doc/study/sscdo2/delrm-delay/`).
DDELAY's `FAUST REFERENCE` now cites them.
Davide's 22 ms is 7.291 m: 1061 samples at 48 kHz, 22.10 ms.
The comb of block 2 is the standard `fi.ff_comb(maxdel, M, 1, 1)`, which is x + x[n−M]: nothing to write in `seam.filters.lib`.

### delRM block 2: the comb
`sdt.delrmcomb(mt) = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1)`, the first delRM entry of `seam.tedesco.lib`.
Checked in `doc/study/sscdo2/delrm-comb/` at 48 and 96 kHz: identical to the original structure on noise, impulse response 1 at 0 and at M, peaks +6.02 dB every SR/M Hz, notches below −280 dB, +3.0 dB on white noise; a one-sample mutation is caught.
The prime rounding makes the spacing differ by 0.4 % between rates (45.24 against 45.43 Hz).
On a recording Giuseppe provided (`CCB_petalonio_oriz_DO.wav`: eight microphones around the contrabass clarinet on its low C, 96 kHz, 3.56 s; track 1 at 1 m, fundamental 29.7 Hz, mainly odd partials) the comb's gain on each partial matches 2·|cos(π·f·M/SR)| within 0.2 dB outside the deepest notches.
The distance decides which partials it lifts: at 7.291 m it reinforces the third (+6.0 dB) and hollows the seventh (−7.3 dB); at 10 m it lifts the fundamental (+5.5) and removes the fifth (−17.3).
Tuning delRM's delay is tuning a timbre.
The renders are published (agreed later the same day).

### delRM block 3: the triple product (`doc/study/sscdo2/delrm-rm/`)
Measured on the clarinet's note and on ten minutes of floor noise at −70 dBFS plus the DC of track 1 (3.22e-6):
- the original `fi.integrator` drifts linearly (state 95 after ten minutes at 48 kHz, 187 at 96 kHz, against 33 for the note's own integral); a note arriving after five minutes finds its triple product 7 dB louder and 0.88 correlated with the plain self-ring-modulation 10·x[n−D]·x: in Pd, channels 2 and 4 changed effect with the time the patch had been running;
- its level before the compressor rises by 6.00 dB from 48 to 96 kHz;
- a DC blocker in front of the integrator stops the drift, because its zero cancels the integrator's pole and leaves a leaky integrator (identical state to a 5 Hz leak), but keeps the 6 dB rate dependence;
- a leaky integrator normalised to time, y = (48000/SR)·x + a·y[n−1] with fc = 1 Hz, is bounded, flat across rates (−0.01 dB), and within 0.3 dB of the original at 48 kHz on a fresh start.
**SSCDO#2 is played at 96 kHz** (Giuseppe, 2026-09-29): open question 2 is closed, and every rate anchor of the port is 96 kHz.
The leaky integrator becomes a general SEAM function, `sfi.leakyint(fc)` in `seam.filters.lib` (the integral in seconds, built on the standard `fi.pole`), reused by `sdt.delrmint = sfi.leakyint(1) : *(96000)` and `sdt.delrmrm(mt)`, the triple product.
The self-test checks that `sdt.delrmrm` equals the measured probe sample for sample, and that at 96 kHz it matches the original's level.
Giuseppe agreed to publish the renders made from the recording (comb and triple product).
Giuseppe asked why not `*(ma.SR)`: it cancels the normalisation and gives back a stable sum of samples, identical at 96 kHz; at 48 kHz it is 6.00 dB lower before the compressor and −0.21 dB after it, so the compressor absorbs the level but works 6 dB less hard.
Kept at 96000: the specification is the sound as Davide heard it, and delRM should behave the same at any session rate.

### delRM block 4: the compressor
`sdt.delrmdyn = *(10) : co.compressor_mono(11, −24, 0.03, 0.04)`, the standard compressor as in the original; `sdt.delrmrm : sdt.delrmdyn` equals the probe chain sample for sample.
On the note at varied input levels (`doc/study/sscdo2/delrm-rm/results.md`, block 4): the cubic product makes channels 2 and 4 an expander below threshold (+30 dB out per +10 dB in) and the 11:1 compressor a limiter above it (under 5 dB out over 18 dB in, 38 dB of gain reduction at the recording's level).
About 15 dB of playing dynamics separate absent from saturated: a fact for the operational table, since the ASP880's gain places the performance inside that window.
Transients pass the 30 ms attack: output peaks 16 dB above the RMS, reaching 0 dBFS at +6 dB of input, before the master volume.

### Open: the delay lines are in the wrong library
Giuseppe, at the end of the day: `sma.imnpdelay` uses `de.delay`, so it is a stateful DSP, not a mathematical function, and does not belong in `seam.math.lib` (nor does `sma.imdelay`, whose comment already called it "the one stateful function in this library").
To do first at the next session:
- create `seam.delays.lib`, prefix `sdl`, extending the standard `delays.lib` (only what upstream lacks), and move `imdelay` and `imnpdelay` there; the conversions `isos`, `imt2samp`, `imt2npsamp` stay in `seam.math.lib`;
- update the callers: `sdt.delrmrm`, `doc/study/sscdo2/delrm-rm/probe.dsp`, the `FAUST REFERENCE` in `plugins/ddelay/source/ddelay_processor.h`, `plugins/ddelay/doc/ddelay.dsp`, `plugins/addelay/doc/addelay.dsp`, `plugins/README.md`;
- `ddelay.dsp` calls itself the canonical DSP but still uses `imdelay`, without primes: move it to `sdl.imnpdelay` and regenerate its documentation with `tools/gen-faust-doc.sh`;
- rerun the `delrm-rm` self-test and the `delrm-delay` check.

Quadrature pair: the SVF realisation of the RBJ all-pass sections is tracked as issue #12, as a step before proposing SEAM work to GRAME.

## Open
Questions for Davide:
1. The double feed into `stunedrev`: intended or residual?
2. The performance sample rate. **Answered: 96 kHz.**
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

### Resolved: no delay library, the delay is written out
Discussed with Giuseppe before moving anything, and the plan above changed.
None of the delays had a structure of its own: `sma.imdelay` and `sma.imnpdelay` were `de.delay` fed by a conversion, and so are delRM's comb and triple product and the canonical DSP of DDELAY and ADDELAY.
A `seam.delays.lib` will exist when a delay has its own structure (interpolation, modulation, non-standard read/write), not for a conversion.
- `sma.imdelay` and `sma.imnpdelay` are removed; `seam.math.lib` is stateless again and keeps the conversions (faust-libraries 6d51be9).
- Every user writes `de.delay(1 << 15, sma.imt2npsamp(mt))`: `sdt.delrmrm`, the `delrm-rm` probe, `ddelay.dsp`, `addelay.dsp`; the `FAUST REFERENCE` of ddelay and addelay and `plugins/README.md` say the same.
- `addelay.dsp` had the same drift as `ddelay.dsp` (no primes), and the `FAUST REFERENCE` in `addelay_dsp.h` and `addelay_processor.h` cited `imt2samp`'s truncation while the C++ rounds to the millimetre and to the nearest sample: all corrected.
- `sff.np` stays a foreign function, a study implementation compiled by the C/C++ backends only; the standard libraries have `ma.primes` (the n-th prime) but no next prime, so no alternative is needed for now.
- Checks rerun: `delrm-delay` 0 mismatches at 44.1/48/96/192 kHz, the +1 mm mutation still 5 940 mismatches; `delrm-rm` tables unchanged and renders byte-identical.
- Block diagrams of ddelay and addelay regenerated. Their mathdoc PDFs are not: Faust 2.88's `-mdoc` hits an assertion (`sigtyperules.cpp:209`) on any DSP with a slider, even `_*hslider(...)`, so the July PDFs remain and still show `imdelay`.

### delRM block 5: the DC blockers leave (`doc/study/sscdo2/delrm-dcblock/`)
Giuseppe's rule for the port: measure at the rate of the piece (96 kHz), and carry over what makes process and timbre, not the original's local answers to local problems.
The original ends each delRM channel with `fi.dcblocker` and puts a second one on all four channels after the master volume.
Measured at 96 kHz on the clarinet: with the original `fi.integrator` the DC of channels 2/4 reaches 9.6 dB below the signal after five minutes, which is what the first one was holding back; with `sdt.delrmint` it stays 66.5 dB below, and the comb's is 75 dB below.
delRM in `seam.tedesco.lib` therefore ends at the compressor, and the library says why.
The side effect was timbral: the pole 0.995 is a high-pass at 76.59 Hz at 96 kHz, and the two in series took 17.6 dB from the 29.7 Hz fundamental.
A/B renders (clean, and with the two DC blockers of the performance) are in the study for Davide; if he keeps the thinner low end it enters as a declared `fi.dcblockerat(76.59)`.
Block 6 (master volume, `si.smoo`) is standard.

### stunedrev: decisions and memory
Giuseppe's decisions: the reference is the version used in Pure Data (`stunedrev.dsp`, 42 all-passes per line), not `stunedrev_online_compile.dsp`; the ms → samples → next prime conversion, which gives a different set of primes at each rate, is a feature of the SEAM system (incommensurable delays), not a dependence to remove; the time sliders keep the original's 1–100 ms.
Memory, measured on the generated code: the Pd external (Faust 2.72.14, `-vec -double`) allocates 15.2 GiB, and the same `.dsp` with Faust 2.88 in scalar mode 3.9 GiB.
Four causes add up: every all-pass of a line gets the buffer of the longest one (`de.delay(SRM*ma.SR, ...)`, SRM = 6, 7, 12, 14 s), sized for 192 kHz (the cap of `ma.SR`), rounded to a power of two, and made resident by `instanceClear`, which zeroes it all.
The i-th all-pass of the line with ratio k needs at most `0.1 · (i+1) · k · fs` samples; sized exactly at 96 kHz the four lines need 588 MiB in double, 294 MiB in float.
The sound does not change, only the space reserved and never read.
Next, block by block at 96 kHz: (1) the all-pass, the original's Moorer `apf` against `fi.allpass_comb`, `sms.apfs`, `sms.apfv`, sized per all-pass; (2) the delays, ms → samples · (i+1) · k (√2, φ, e, π) → `sff.np`; (3) the chain, 42 in series with g = 1/√2 on four independent lines.
