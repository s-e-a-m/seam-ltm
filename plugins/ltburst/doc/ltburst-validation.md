# ltburst — validation record

## Faust parity (Task 2)

Reference: `slw.shapedburst5(1000, 0.3)`, faust 2.85.5 `-lang cpp -double`, fs=48000.
Compared against `Seam::ltburst::ShapedBurst` over one full period (14640 samples).
Max abs difference: `6.456e-14` (tolerance 1e-9). The hand port is numerically faithful.

**Glue note:** the default `faust -lang cpp` generates single-precision float code
(`-single`); the hand core uses `double` throughout. The meaningful comparison requires
`-double` on the faust invocation so both sides accumulate in the same precision.
With single precision the max-diff is ~4.3e-06 (float rounding noise, not a port error);
with `-double` it collapses to 6.456e-14, confirming the algorithms are identical.
No phase-convention difference was found: both Faust's `os.phasor` and the hand core's
`u_` start at zero on the first sample and advance by `f0/fs` cycles per sample.

## Level calibration (Task 2)

`Reference` calibrates the active-window RMS (the N=5 burst cycles, excluding dwell).
Measured at unit gain: active-window RMS = `0.433013` (`-7.270` dBFS), constant over
f0/dwell because it is the RMS of a Hann-windowed sinusoid over its own support
(equals √(3/16) = √3/4 analytically).
`kCalibrationOffsetDb = -15.730` makes Reference=-23, Trim=0 land at -23.0 dBFS RMS
in the burst.
