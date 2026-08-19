# multipink — Calibration Procedure

This document explains how to verify (and, if needed, recalibrate) the
reference levels of the `multipink` plugin — both the total RMS, which is what
`Reference`'s number is defined as, and the per-third-octave band level, which
is what the calibration holds invariant across sample rates.

## Background

`multipink` produces a pink reference signal at a stated level. The pink
IIR attenuates the upstream white noise by an amount that depends on the
filter coefficients (`Seam::multipink::PinkDesign` in `multipink_pink.h`),
so to make the `Reference` setting correspond to a real level, a
compensation offset is applied to the gain.

## What "Reference" means now (2026-08-19)

**`Reference`'s numeric value is defined as the total RMS at 48 kHz. What the
offset holds invariant across sample rates is the per-third-octave BAND
level.**

Both halves matter, and the earlier wording of this section — "`Reference` sets
the BAND level, not the total RMS" — got the first one wrong: `Reference = -23`
gives a band level of −39.489 dBFS and a total RMS of −23.000 dBFS at 48 kHz,
so the number on the menu is the RMS, not the band level. The band level is
39.489 − 23 = 16.489 dB below it, a figure that depends on how many bands the
rate makes room for. What is *calibrated* — held still when the sample rate
changes — is the band level.

As of the matched-Z pinking filter (`doc/study/sessions/2026-08-19-pink-filter-design.md`,
`docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md`), the filter is
anchored in Hz rather than fit in the z-plane. A calibration reference exists
so that a loudspeaker can be equalised band by band, so the band level is the
quantity that must not move with the sample rate — an amplifier calibrated at
48 kHz must stay calibrated at 96 kHz. The total RMS is then free to move, and
does.

The offset is therefore a **function of the sample rate**:

```
offset(fs) = 36.180 + 10*log10(fs / 48000)
```

`PinkDesign::kCalibrationOffsetBaseDb = 36.180` and the accessor
`PinkDesign::calibrationOffsetDb()`, in
`plugins/multipink/source/multipink_pink.h`. It is computed once inside
`design(fs)` and read as a load by `computeGainLin()` on the audio thread.

The band level is the sum of four terms:

| term | value |
|---|---|
| gain | `Reference` + `Trim` + `offset(fs)` |
| source RMS | `20*log10(1/sqrt(3))` = −4.7712 dB, the uniform LCG |
| filter | `10*log10(∫ over the band of \|H(f)\|²)` |
| source density | `−10*log10(fs/2)` |

The last term is why the offset depends on fs. The LCG's total RMS is the same
at every sample rate, but it is spread over 0…fs/2, so its spectral density
halves when fs doubles and every fixed band would lose 3.01 dB per doubling if
the offset did not compensate.

**This corrects an error that shipped between 2026-08-19 and this revision**,
where the offset was a fixed 36.180 dB at every rate. Measured in Reaper,
`multipink` straight into `strx` (76 s and 61 s of integration), the mean
third-octave band level read −38.4 dB at 48 kHz and −41.3 dB at 96 kHz: a
2.9 dB fall where the design promised none. If you calibrated a rig at a rate
other than 48 kHz with a build from that window, **the band levels were low by
`10*log10(fs/48000)` dB** — 3.01 dB at 96 kHz, 6.02 dB at 192 kHz, and 0.37 dB
*high* at 44.1 kHz — and the rig wants re-reading.

The base value, 36.180 dB, is the offset at 48 kHz, where the density term is
zero. It is **derived**, not measured with `sox`: `PinkDesign::rmsGainDb()` at
48 kHz is −31.409 dB, and the LCG noise source's RMS was measured directly
(5×10^8 samples, splitmix64-dispersed seeds) at −4.7712 dB against a
theoretical 1/sqrt(3) = −4.7712 dB, so `36.180 = -(-4.7712 + -31.409)`.

This supersedes the old constant, `kCalibrationOffsetDb = 26.45`, which was
measured with a render and `sox` rather than derived. Reapplying the same
derivation to the *old* filter gives 26.06 dB, a 0.39 dB gap against the
measured 26.45 that the LCG distribution does not explain (ruled out to four
decimal places by the same measurement above). The gap is not folded into
the new constant and is left open for a render to settle — see the header
comment in `multipink_processor.h` for the full record.

### What the total RMS does

With every band held still, the broadband RMS **rises** by about 0.27–0.28 dB
per doubling of the sample rate (0.2825 dB from 48 to 96 kHz, 0.2844 from 44.1
to 88.2, 0.2652 from 96 to 192 — it is not one number, because the ladder's top
section moves), because each new octave of pink adds a little total energy on
top of bands that have not moved. For `Reference = -23`, `Trim = 0`, predicted:

| fs | offset | 1 kHz band level | total RMS |
|---|---|---|---|
| 44.1 kHz  | 35.812 dB | −39.4825 dBFS | −23.030 dBFS |
| 48 kHz    | 36.180 dB | −39.4891 dBFS | −23.000 dBFS |
| 88.2 kHz  | 38.822 dB | −39.4847 dBFS | −22.746 dBFS |
| 96 kHz    | 39.190 dB | −39.4909 dBFS | −22.718 dBFS |
| 176.4 kHz | 41.833 dB | −39.4866 dBFS | −22.479 dBFS |
| 192 kHz   | 42.201 dB | −39.4923 dBFS | −22.453 dBFS |

A render measured with `sox` therefore lands on −23.0 dBFS **at 48 kHz only**;
at any other rate the expected value is the one in the last column, and a
reading of exactly −23.0 there would mean the band levels are wrong.

## Measurement convention — IMPORTANT

The plugin's calibration target is **AES17 unweighted RMS** as reported by
`sox stat` (pure `sqrt(mean(x²))`, in dBFS = `20·log10`). This is the
engineering RMS that corresponds 1:1 to acoustic RMS at a loudspeaker, and
therefore the convention that aligns with a true SPL meter at the
listening position.

Many pro audio tools use a different convention — **AES17 alignment** —
that multiplies RMS by `√2` (≈ +3.01 dB) so that a full-scale sine wave
reads 0 dBFS on both Peak and RMS meters. The same signal that `sox`
reports as `-23.0 dBFS RMS` will appear as approximately:

| Tool                       | Reading | Convention                         |
|----------------------------|---------|------------------------------------|
| `sox stat`                 | -23.0   | Pure RMS (AES17 unweighted)        |
| iZotope RX                 | -20.0   | AES17 alignment (+3.01 dB)         |
| Reaper RMS meter (default) | ~-19.0  | AES17 alignment + stereo summing   |
| Real SPL meter at speaker  | (rig)   | Pure acoustic RMS = sox alignment  |

**None of these is "wrong"** — they're three different rulers on the same
signal. Pick the one that matches your downstream tooling, and read this
table every time you compare cross-tool measurements.

For SEAM speaker calibration (Stone arrays etc.), the SPL-meter alignment
is what matters, so we calibrate against `sox`.

This is also why `multipink`'s numbers do not compare directly to
SMPTE ST 2095-1's own scale: the standard reports levels in
**dBFS(AES17)**, i.e. the AES17-aligned convention above, which reads
3.01 dB higher than the pure-RMS `sox` dBFS this plugin and `strx` use for
everything. `tests/multipink_pink_test.cpp` judges the filter's *shape*
(per-band deviation from the mean), which the 3.01 dB offset cancels out of
— it only matters when comparing an absolute level to a number quoted from
the standard.

## The 1.00 dB step against pre-2026-08-19 measurements

At equal total RMS, the new matched-Z filter's mean band level sits
**1.00 dB below** the old filter's, measured at 48 kHz. Any absolute level
recorded before 2026-08-19 — a render, a calibration log, a Stone tuning
session — sits 1.00 dB above where the same nominal `Reference` now
produces.

This does not change any equalisation decision: `strx` reports each band's
deviation from the *mean* of the judged bands, so a uniform 1.00 dB shift
of the whole reference signal shifts every band and the mean together and
cancels out of the deviation `strx` displays. Only the absolute level of
the reference signal moves, not the shape `strx` reads.

## Re-verifying

The total RMS is the convenient check, but it is not the calibrated quantity.
Read the band level as well whenever the rate is not 48 kHz.

1. Add `multipink` to a stereo track in Reaper.
2. Set `Reference = -23 dBFS RMS`, `Trim = 0.0`, `Power = on`.
3. Render 30 s at 48 kHz, 32-bit float WAV, stereo, to `multipink_cal.wav`.
4. Measure RMS:
   ```
   sox multipink_cal.wav -n stat 2>&1 | grep "RMS amplitude"
   ```
5. Convert to dBFS: `dB = 20 · log10(RMS_amplitude)`.
6. Expected at 48 kHz: -23.0 ± 0.1 dBFS RMS per channel (sox/AES17
   unweighted) — this is the rate at which `Reference`'s number is defined,
   and the only rate at which the total RMS reads it back.
   In AES17-aligned tools this will read as ≈ -20.0.
   **At a rate other than 48 kHz, expect the value in the table above**, not
   -23.0 — the RMS is supposed to move.

### Re-verifying the quantity that is actually calibrated

The band level is what `Reference` fixes, so verify it directly: feed
`multipink` into `strx` on the same track and let the band table integrate for
at least 60 s. The mean of the judged bands must read the same number at every
sample rate — it is this comparison, run at 48 and 96 kHz, that found the
rate-dependence the derivation had missed. A difference of about 3 dB per
doubling of fs means the offset is not carrying its density term.

## Recalibrating

The preferred method is to **derive** the base constant, not to measure it:
call `PinkDesign::design(48000.0)` and read `PinkDesign::rmsGainDb()`, then add
the noise source's known RMS (1/sqrt(3) = -4.7712 dB for the current LCG):

```
kCalibrationOffsetBaseDb = -(whiteRmsDb + rmsGainDb(48000))
```

in `plugins/multipink/source/multipink_pink.h`. This needs no render and no
host. Recompute it whenever the filter's coefficients change — the base value
is filter-intrinsic.

**Do not touch the `10*log10(fs/48000)` term while recalibrating.** It belongs
to the noise source's spectral density, not to the filter, and it is the term
that holds the band level still across sample rates. Changing the filter does
not change it; changing the noise source's spectrum would.

The render-and-measure procedure is the fallback for when the noise source
itself changes and its RMS is not known in closed form:

1. With `kCalibrationOffsetBaseDb = 0.0`, render **at 48 kHz** and measure as
   above. Call the measurement `M` (in dBFS).
2. Set `kCalibrationOffsetBaseDb = -23.0 - M` in
   `plugins/multipink/source/multipink_pink.h`.
3. Rebuild.
4. Re-render at 48 kHz and re-measure. Expect -23.0 ±0.1.
5. Verify the same constant works for Reference = -20 and -18 (the offset
   is filter-intrinsic, not reference-dependent — all three should land
   within ±0.1 dB of their nominal values).
6. Verify at a second sample rate that the *band* level has not moved, per the
   section above. Step 4's number will have moved, and should have.

`tests/multipink_pink_engine_test.cpp` asserts all of this analytically — the
base value at 48 kHz, the density term, the band-level invariance across the
six rates over all 30 bands measurable at every rate, and the total-RMS
behaviour — so a regression fails `ctest` before it reaches a render. The
invariance threshold there is 0.20 dB, derived from the filter's own ripple
below (0.0837 dB worst observed) and the 3.01 dB per doubling above; the
comment on `kBandInvarianceDb` carries the argument.

## Why long-term RMS

The 64 LCG/IIR pairs are mutually independent; per-channel RMS converges
to the same value, but only over long integration times (~1 s and up).
Short windows (< 100 ms) will show channel-to-channel variation that
disappears in the 30-s integral.
