# Pinking Filter — Calibration-Grade Redesign — Design

Status: approved (brainstorming, 2026-08-19).
Scope: `multipink`'s pinking filter, its acceptance test, and the Faust
specification that describes it.
The white-noise source, the 64-slot pool, the GUI and the parameter set are
untouched.
Measurement log and the reasoning behind every number:
`doc/study/sessions/2026-08-19-pink-filter-design.md`.
Probe sources: `doc/study/probes/2026-08-19-pink/`.

## The defect

`multipink` pinks its noise with the three-pole `invfreqz` fit that GRAME ships
as `no.pink_filter`.
The acceptance test written on 2026-08-18 measures its third-octave uniformity
against SMPTE ST 2095-1's ±0.25 dB and finds it fails at **every** sample rate,
including the one it was fitted at.

| fs | worst deviation |
|---|---|
| 44.1 kHz | 0.41 dB |
| 48 kHz | 0.60 dB |
| 88.2 kHz | 2.40 dB |
| 96 kHz | 2.68 dB |
| 192 kHz | 4.99 dB |

The filter is not a correct design that degrades as fs rises.
It was never calibration-grade, and 96 kHz is merely where the error grows large
enough to show up in a room measurement.
Paul Kellett, whose 1999 music-dsp post is the origin of this coefficient
family, wrote that there is no algorithm behind them — they are hand-tuned for
one sample rate.

## Decisions (from brainstorming)

1. **Clean replacement.**
The old fit leaves the code.
No legacy mode, no filter-selection parameter, no second calibration constant.
Historical measurements stay comparable by reading the study diaries, not by
reproducing the signal.

2. **The judged band is each rate's physical limit, not SMPTE's 16 kHz.**
The criterion is ±0.25 dB per third-octave from 20 Hz to the highest ISO 266
band whose upper edge falls within 0.85·Nyquist — the same `bandMeasurable`
rule `strx` already applies.
Choosing 96 or 192 kHz is itself the declaration of wanting to look above
20 kHz, and a reference that is accurate only to 16 kHz would mislead exactly
where it is being interrogated.
Below 48 kHz this coincides with SMPTE: the 20 kHz band's upper edge is
22.39 kHz and is not measurable at all, so 16 kHz is the real ceiling and the
standard is not being conservative.

| fs | judged bands | ceiling |
|---|---|---|
| 44.1 / 48 kHz | 30 | 16 kHz |
| 88.2 / 96 kHz | 33 | 31.5 kHz |
| 176.4 / 192 kHz | 36 | 63 kHz |

3. **C++ first, Faust immediately after, cross-referenced both ways.**
The suite's convention makes Faust the specification, but the design arithmetic
is done more directly in C++ and the inverse order has precedent here.
The binding requirement is that the two never drift: the C++ header carries a
`FAUST REFERENCE` block naming the library function, and the library carries a
back-reference to the plugin, the acceptance test and the measurement log.

4. **Calibration is anchored to the band level, not to total RMS.**
See *Calibration* below.

## Why `fi.spectral_tilt` cannot be used as it stands

The literature audit recommended `fi.spectral_tilt(N, f0, bw, -0.5)`
(Smith & Smith, arXiv:1606.06154) as the primary method.
Measured against the same acceptance test it fails at every rate, by 1.5 to
2.1 dB.

The interior of the band is not the problem: the ripple is already ±0.02 dB at
1.4 poles per octave.
The error is at the top edge — with fs = 48 kHz it reads −0.45 dB at 10 kHz,
−0.97 dB at 12.5 kHz and −1.92 dB at 16 kHz.
No parameter removes it.
Lower guard from 10 Hz down to 1 Hz, upper guard up to and beyond Nyquist, and
densities from 1 to 4 poles per octave all saturate between 0.8 and 2 dB.

The cause is the bilinear transform, and it is arithmetic rather than opinion:
at 16 kHz with fs = 48 kHz the warp factor is tan(60°)/(π/3) = 1.654, which is
0.73 octaves, and at −3.01 dB/octave that is −2.19 dB.
Faust's prewarping is correct and places every pole at its intended digital
frequency; what deviates is the curve *between* the poles, which is evaluated at
the warped analog frequency.

This is structural, not a matter of parameters, and the audit could not have
seen it: Smith & Smith design in the analog domain, and the warp appears only on
discretisation.
It also explains why the old z-plane fit does comparatively well at its top edge
— it was fitted directly in the z-plane, so the warp is already inside its
coefficients, paid for with its low-frequency error and its dependence on fs.

## Architecture

Three parts.

### 1. A matched-Z ladder of real poles and zeros

Poles are spaced geometrically from f0 = 2 Hz to fs/2, with the zero of each
section half a log-step above its pole (alpha = −1/2).
Each analog pole −a maps to the digital pole e^(−aT), each zero likewise, and
each section is normalised to unity gain at DC.

Matched-Z rather than bilinear because it does not warp the frequency axis.
Its own error — the aliased tail of a response that decays only 3 dB per octave
— is **a function of f/fs alone**, verified identical at 48 kHz and 192 kHz to
three decimal places.
That invariance is what makes part 2 possible.

Cascaded first-order real-pole sections are also the topology the audit named as
the pass/fail criterion for numerical conditioning.

### 2. One fixed correction section

The residual is smooth, monotone and approximately proportional to (f/fs)²,
reaching +1.48 dB at 0.425·fs.
A single first-order section with **constant coefficients at every sample rate**
cancels it:

```
zero = -0.250775213
pole = -0.160124183
```

with unity gain at DC, fitted by Nelder-Mead with multiple starts against the
sampled residual.
It reduces the residual from 1.48 dB to 0.029 dB.
Two sections reach 0.012 dB and three do not improve, so one is enough.

The two parts are orthogonal by construction: the ladder owns everything that
scales in Hz, the correction owns the one effect that is a function of f/fs.

### 3. The acceptance test as the judge

`tests/multipink_pink_test.cpp` already integrates |H|² over the ISO 266 bands
analytically, which is the only way to resolve a 0.25 dB tolerance — a
third-octave level taken from noise carries about 0.6 dB of spread at BT = 50.
It is extended to the criterion in decision 2 and to all six rates.

### Measured result

| poles/octave | sections | worst deviation, all rates | verdict |
|---|---|---|---|
| 1.0 | 16–18 | 0.072 – 0.081 dB | pass |
| 1.5 | 23–26 | 0.029 – 0.036 dB | pass |
| 2.0 | 29–34 | 0.028 – 0.030 dB | pass |

Recommended setting: **1.0 pole per octave**, which is three times inside the
tolerance and would also satisfy the tighter ±0.1 dB variant the audit raised.
Density above 1.5 buys nothing: the floor is the quality of the correction fit,
not the pole count.
The final choice between 1.0 and 1.5 is the CPU measurement described under
*Testing*, and is a single constant in the header.

### Precision and state

Single precision was measured on the running filter, not assumed: the worst
difference against double is **0.0008 dB**, at 192 kHz and 20 Hz.
This is the opposite of the third-octave filterbank, where single precision put
every band below 160 Hz on the epsilon floor — same frequencies, same f/fs,
different topology.
Coefficients are computed in double; the per-stream state is float.

Layout follows from the 64 streams sharing one filter.
Coefficients are one shared array with capacity for 32 sections, of which 18 are
live at 192 kHz with the recommended density — 432 bytes in use, hot in L1.
State is stored as `state[section][stream]` and not `[stream][section]`, so that
one SIMD instruction can advance eight streams through the same section.
No SIMD is written now; the layout is chosen now because it is free now and
expensive later.

`prepare(fs)` computes the ladder, the coefficients and the calibration
constant, with fixed capacity and no allocation on the audio thread, following
`_common/seam_quadrature.h`.
The engine stays in `plugins/multipink/source/multipink_pink.h`, SDK-free, so
the plugin and the test cannot describe different filters.
It moves to `_common/` when a second consumer exists, not before.

## Calibration

Anchoring must choose what stays fixed, because the filter is anchored in Hz and
its total RMS therefore moves by 5.8 dB from 44.1 to 192 kHz.

**The band level is what stays fixed.**
A reference exists so that a loudspeaker can be equalised band by band, and a
per-band level that moved with the sample rate would invalidate the amplifier
settings derived from it.
That decision stands.

### Corrected 2026-08-19: the offset is a function of fs

This section originally concluded that a single constant achieves it, and the
implementation shipped `kCalibrationOffsetDb = 36.180` fixed at every rate.
That is wrong, and the derivation below replaces it.

The band level of the generator is the sum of **four** terms, not three:

```
band level (dB) = gainDb                                   ; referenceDb + offset(fs)
                + 20*log10(1/sqrt(3))                      ; the LCG source's RMS, -4.7712 dB
                + 10*log10( ∫_band |H(f)|² df )            ; the filter over the band
                - 10*log10(fs/2)                           ; the source's spectral DENSITY
```

The last term is the one the original derivation forgot.
The probe behind the old conclusion measured the third term, found it invariant
across all six rates — which it genuinely is, the filter being anchored in Hz —
and stopped there.
It counted the filter and forgot the source.
The LCG's total RMS is the same at every sample rate, but that fixed power is
spread over 0…fs/2, so its spectral density halves when fs doubles, and every
fixed band therefore receives 3.01 dB less per doubling however perfectly the
filter is anchored.

**Measured in the host**, `multipink` straight into the `strx` analyser, over
76 s and 61 s of integration:

| | 48 kHz | 96 kHz |
|---|---|---|
| mean third-octave band level | −38.4 dB | −41.3 dB |

A 2.9 dB fall where this section promised none, against 3.01 dB predicted from
the missing term.

The offset therefore carries the density term, and only that:

```
offset(fs) = 36.180 + 10*log10(fs / 48000)
```

36.180 dB is the value **at the 48 kHz reference rate**, where the density term
is zero and the corrected definition coincides with the old one.
With the term in place the predicted 1 kHz band level is −39.4825 / −39.4891 /
−39.4847 / −39.4909 / −39.4866 / −39.4923 dBFS at 44.1 / 48 / 88.2 / 96 / 176.4
/ 192 kHz — a spread of 0.0098 dB.
Without it: −39.114 / −39.489 / −42.127 / −42.501 / −45.139 / −45.513.
The total RMS then *rises* 0.28 dB per doubling (−23.030 at 44.1, −23.000 at 48,
−22.718 at 96, −22.453 at 192, for Reference = −23), which is the correct
behaviour and not a defect: every band holds still while each additional octave
of pink adds a little total energy on top.

**Why no review caught it.**
Eight task reviews and a whole-branch review all passed over this.
Every one of them checked the code against the spec, and the code did implement
the spec faithfully — the spec was wrong.
A review that reads cannot catch an error of physics in the document it is
reading against; only a measurement of the real signal path can, and that is
what found it.
Two structural consequences, both now applied:

1. The constant lived in `multipink_processor.h`, which includes the VST3 SDK,
   and the test binary cannot include the SDK — so **no test could read the
   number at all**.
   It now lives in `multipink_pink.h`, SDK-free, as
   `PinkDesign::kCalibrationOffsetBaseDb` plus a
   `calibrationOffsetDb()` accessor computed once in `design(fs)` and read as a
   load on the audio thread.
2. The test suite asserted the *derivation* (`-(whiteRmsDb + rmsGainDb)` equals
   36.180) rather than the *property the design claims*.
   It now asserts the property directly: the predicted band level for a fixed
   ISO band is invariant across all six rates within 0.01 dB.
   Verified by mutation — removing the density term turns it red by 3.010 dB at
   96 kHz and 6.021 dB at 192 kHz.

It is computed rather than measured.
The old constant, 26.45 dB, fused the filter's RMS gain with the white source's
RMS (a uniform LCG contributes 1/√3, or −4.77 dB), and it was that fusion which
forced it to be measured with a render and `sox`.
The engine knows the integral of |H|² and the source's RMS in closed form.

Transition cost, measured: at equal total RMS the new filter's mean band level
sits **1.00 dB** below the old one at 48 kHz.
Because `strx` reports deviations from the mean of the bands, the band table and
every equalisation decision taken from it are unaffected; only the absolute
level of the reference signal moves by 1 dB.

The metering convention is pinned in the documentation: levels are dBFS RMS, and
SMPTE ST 2095-1's dBFS(AES17) scale differs by 3.01 dB.

## The Faust side

A new function in `seam.filters.lib`, not a wrapper:
`spectral_tilt_mz(N, f0, f1, alpha)` for the general matched-Z tilt, plus the
pink specialisation carrying the correction section.
`seam.noises.lib:11` stops composing `no.pink_filter` and uses it.

`no.pink_filter` carries this defect for every user above 48 kHz, so the
function is a candidate to offer upstream to GRAME, as `pdclone.env` is.

## Testing

1. The acceptance test extended to the criterion in decision 2, at 44.1, 48,
   88.2, 96, 176.4 and 192 kHz, with the pinned numbers replaced.
2. A tighter 0.1 dB sentinel alongside the 0.25 dB standard threshold, since the
   margin exists — a regression should fail before it becomes a standards
   failure.
3. Every test verified by mutation: break the design deliberately and confirm
   the red, per the suite rule.
4. Single-versus-double conditioning pinned at 0.001 dB, making the audit's
   pass/fail criterion a number in the build.
5. The computed calibration constant checked against a render measured with
   `sox`, the same method that produced 26.45.
6. A CPU measurement of the block loop at 64 streams and 192 kHz, which decides
   1.0 against 1.5 poles per octave.
7. A magnitude A/B between the Faust function and the C++ engine, which is how
   the cross-reference is enforced rather than asserted.

## Out of scope

- Extending the `strx` band grid above 20 kHz, which the anechoic measurement
  will eventually want; its own scope, its own spec.
- Any legacy or compatibility mode for the old filter.
- Band-limiting the generator to SMPTE's 10 Hz–22.4 kHz profile: the standard
  separates slope from band-limiting, and so do we.
- SIMD, which the state layout enables and no measurement yet requires.

## References

- SMPTE ST 2095-1, *Calibration Reference Wideband Digital Pink Noise Signal*.
- J. O. Smith and H. F. Smith, *Closed Form Fractional Integration and
  Differentiation via Real Exponentially Spaced Pole-Zero Pairs*,
  arXiv:1606.06154.
- IEC 61260-1:2014, band filter acceptance limits, class 1 ±0.4 dB.
- `doc/study/2026-08-18-pink-filter-literature-audit.md` and its result.
- `doc/study/sessions/2026-08-18-strx-band-table.md`.
