# multipink — Calibration Procedure

This document explains how to verify (and, if needed, recalibrate) the
RMS reference levels of the `multipink` plugin.

## Background

`multipink` claims that with `Reference = -23 dBFS RMS` and `Trim = 0`, its
long-term per-channel output RMS is exactly -23.0 dBFS. The pink IIR
attenuates the upstream white noise by an amount that depends on the
filter coefficients (see `multipink_processor.h`, `kPinkB`/`kPinkA`). To
make the knob "0 dB" position correspond to a real reference RMS, a
hard-coded compensation `kCalibrationOffsetDb` is applied.

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

## Re-verifying

1. Add `multipink` to a stereo track in Reaper.
2. Set `Reference = -23 dBFS RMS`, `Trim = 0.0`, `Mute = off`.
3. Render 30 s at 48 kHz, 32-bit float WAV, stereo, to `multipink_cal.wav`.
4. Measure RMS:
   ```
   sox multipink_cal.wav -n stat 2>&1 | grep "RMS amplitude"
   ```
5. Convert to dBFS: `dB = 20 · log10(RMS_amplitude)`.
6. Expected: -23.0 ± 0.1 dBFS RMS per channel (sox/AES17 unweighted).
   In AES17-aligned tools this will read as ≈ -20.0.

## Recalibrating

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
