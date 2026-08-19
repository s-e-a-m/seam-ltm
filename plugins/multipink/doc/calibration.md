# multipink — Calibration Procedure

This document explains how to verify (and, if needed, recalibrate) the
RMS reference levels of the `multipink` plugin.

## Background

`multipink` claims that with `Reference = -23 dBFS RMS` and `Trim = 0`, its
long-term per-channel output RMS is exactly -23.0 dBFS. The pink IIR
attenuates the upstream white noise by an amount that depends on the
filter coefficients (see `multipink_processor.h`, `Seam::multipink::PinkDesign`
in `multipink_pink.h`). To make the knob "0 dB" position correspond to a
real reference RMS, a hard-coded compensation `kCalibrationOffsetDb` is
applied.

## What "Reference" means now (2026-08-19)

As of the matched-Z pinking filter (`doc/study/sessions/2026-08-19-pink-filter-design.md`,
`docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md`), the filter is
anchored in Hz rather than fit in the z-plane, so its total RMS is no longer
sample-rate-invariant: it falls 5.8 dB from 44.1 to 192 kHz while its BAND
levels stay put (within 0.01 dB, measured at 1 kHz). `kCalibrationOffsetDb`
therefore anchors the BAND level, not the total RMS, and is deliberately
**not** recomputed per sample rate — recomputing it per rate is exactly what
would make the band level move.

The constant is defined once, at 48 kHz:
`kCalibrationOffsetDb = 36.180`, in `plugins/multipink/source/multipink_processor.h`.
It is **derived**, not measured with `sox` — `PinkDesign::rmsGainDb()` at
48 kHz is -31.409 dB, and the LCG noise source's RMS was measured directly
(5×10^8 samples, splitmix64-dispersed seeds) at -4.7712 dB against a
theoretical 1/sqrt(3) = -4.7712 dB, so `offset = -(-4.7712 + -31.409) = 36.180`.

This supersedes the old constant, `kCalibrationOffsetDb = 26.45`, which was
measured with a render and `sox` rather than derived. Reapplying the same
derivation to the *old* filter gives 26.06 dB, a 0.39 dB gap against the
measured 26.45 that the LCG distribution does not explain (ruled out to four
decimal places by the same measurement above). The gap is not folded into
the new constant and is left open for a render to settle — see the header
comment in `multipink_processor.h` for the full record.

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

1. Add `multipink` to a stereo track in Reaper.
2. Set `Reference = -23 dBFS RMS`, `Trim = 0.0`, `Power = on`.
3. Render 30 s at 48 kHz, 32-bit float WAV, stereo, to `multipink_cal.wav`.
4. Measure RMS:
   ```
   sox multipink_cal.wav -n stat 2>&1 | grep "RMS amplitude"
   ```
5. Convert to dBFS: `dB = 20 · log10(RMS_amplitude)`.
6. Expected: -23.0 ± 0.1 dBFS RMS per channel (sox/AES17 unweighted).
   In AES17-aligned tools this will read as ≈ -20.0.

## Recalibrating

Since 2026-08-19 the preferred method is to **derive** the constant, the way
`kCalibrationOffsetDb = 36.180` was obtained above: call
`PinkDesign::design(fs)` and then read `PinkDesign::rmsGainDb()`, which
takes no argument and reports the gain at the rate the design was given,
and add the noise source's known RMS (1/sqrt(3) = -4.7712 dB for the current LCG). This needs
no render and no host. The render-and-measure procedure below is the
fallback for when the noise source itself changes and its RMS is not known
in closed form:

If the measured value drifts outside ±0.1 dB (e.g., after a coefficient
change in the pink filter), update the constant:

1. With `kCalibrationOffsetDb = 0.0`, render and measure as above. Call
   the measurement `M` (in dBFS).
2. Set `kCalibrationOffsetDb = -23.0 - M` in
   `plugins/multipink/source/multipink_processor.h`.
3. Rebuild.
4. Re-render and re-measure. Expect -23.0 ±0.1.
5. Verify the same constant works for Reference = -20 and -18 (the offset
   is filter-intrinsic, not reference-dependent — all three should land
   within ±0.1 dB of their nominal values).

## Why long-term RMS

The 64 LCG/IIR pairs are mutually independent; per-channel RMS converges
to the same value, but only over long integration times (~1 s and up).
Short windows (< 100 ms) will show channel-to-channel variation that
disappears in the 30-s integral.
