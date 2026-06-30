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

## Integration gate (Task 5)

### VST3 SDK validator

Run: `build/bin/Release/validator build/VST3/Debug/ltburst.vst3`
Result: **47 tests passed, 0 tests failed** (exit 0).

Plugin enumerated correctly:
- 4 parameters: Reference (id=100), Trim (id=101), Frequency (id=102), Dwell (id=103).
- 1 stereo output bus ("Output", Main-Default Active); 0 input buses (instrument).
- FUID `5E4D000C B2C3D4E5 4C544255 52535400` confirmed.

### Dwell-independence of active-window RMS

Both measurements at f0=1000 Hz, fs=48000, active window = 240 samples (N=5 cycles):

| dwell | P (cycles) | active-window RMS |
|-------|-----------|-------------------|
| 50 ms | 55        | 0.433013          |
| 800 ms | 805      | 0.433013          |

Delta: 0.00e+00. Matches analytical √(3/16) = 0.433013 exactly.
Confirms dwell-independence: the calibration constant `kCalibrationOffsetDb` is
valid for any dwell setting.

### DSP core doctests

`build/tests/Debug/ltburst_dsp_test`: 6 test cases, 540520 assertions — **all passed**.
