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

**State convention (divergence from the spec pseudocode):** the spec keeps the normalized
phasor `c` as state (`c += inc; u = P*c`); the C++ core keeps the carrier phase `u` in
`[0, P)` as state instead. The two agree for static parameters, but differ on a runtime
change of `P` (i.e. of `f0`/`dwell`): the `c`-as-state form rescales `u = P*c` and jumps the
carrier, whereas tracking `u` directly keeps the carrier phase continuous. The `u` form is
the intentional realization of the spec's stated goal ("no phase jump"), so the divergence
from the written pseudocode is an improvement, not a defect.

## Level calibration (Task 2) — SUPERSEDED

> **Superseded by iter2.** The plugin now uses a direct dBFS peak Level control
> (param id=100, -60..0 dBFS, default -20). The carrier amplitude is
> `pow(10, Level/20)` — the Hann window keeps the actual peak at or below this
> value. No calibration constant is needed.

~~`Reference` calibrates the active-window RMS (the N=5 burst cycles, excluding dwell).
Measured at unit gain: active-window RMS = `0.433013` (`-7.270` dBFS), constant over
f0/dwell because it is the RMS of a Hann-windowed sinusoid over its own support
(equals √(3/16) = √3/4 analytically).
`kCalibrationOffsetDb = -15.730` makes Reference=-23, Trim=0 land at -23.0 dBFS RMS
in the burst.~~

## Integration gate (iter2)

### VST3 SDK validator

Run: `build/bin/Release/validator build/VST3/Debug/ltburst.vst3`
Result: **47 tests passed, 0 tests failed** (exit 0).

Plugin enumerated correctly:
- 3 parameters: Level (id=100), Frequency (id=101), Dwell (id=102).
- 1 mono output bus ("Output", Main-Default Active); 0 input buses (instrument).
- FUID `5E4D000C B2C3D4E5 4C544255 52535400` confirmed.

Stereo arrangement correctly rejected (`Plug-in suggests: Mono`) per
`setBusArrangements` guard (`getChannelCount != 1` → kResultFalse).

### DSP core doctests

`build/tests/Debug/ltburst_dsp_test`: 6 test cases, 540520 assertions — **all passed**
(DSP core `ltburst_dsp.h` unchanged).

---

## Integration gate (Task 5) — historical record

### VST3 SDK validator

Run: `build/bin/Release/validator build/VST3/Debug/ltburst.vst3`
Result: **47 tests passed, 0 tests failed** (exit 0).

Plugin enumerated correctly (pre-iter2):
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
valid for any dwell setting (historical; constant removed in iter2).

### DSP core doctests

`build/tests/Debug/ltburst_dsp_test`: 6 test cases, 540520 assertions — **all passed**.
