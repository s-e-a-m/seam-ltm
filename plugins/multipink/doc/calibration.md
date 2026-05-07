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

## Re-verifying

1. Add `multipink` to a stereo track in Reaper.
2. Set `Reference = -23 dBFS RMS`, `Trim = 0.0`, `Mute = off`.
3. Render 30 s at 48 kHz, 32-bit float WAV, stereo, to `multipink_cal.wav`.
4. Measure RMS:
   ```
   sox multipink_cal.wav -n stat 2>&1 | grep "RMS amplitude"
   ```
5. Convert to dBFS: `dB = 20 · log10(RMS_amplitude)`.
6. Expected: -23.0 ± 0.1 dBFS RMS per channel.

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
