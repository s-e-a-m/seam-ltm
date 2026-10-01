# LMO plugin — references and verification

This folder holds what proves that the C++ LMO (`plugins/lmo/`) is the Faust specification `sdt.lmo(4, f, d)`.
Nothing here is built with the plugin: the references are rendered once, by hand, into a committed header that the tests read.

## Files

| file | what it is |
|---|---|
| `dsp/noise8.dsp` | `no.multinoise(8)`: the reference for `Seam::FaustMultinoise` |
| `dsp/bw.dsp` | `fi.highpass/lowpass(24, 97.44)` and `(2, 1000)` on an impulse: the reference for `Seam::ButterworthSVF` |
| `dsp/lmo.dsp` | `sdt.lmo(4, 97.44, 20)`: the reference for the engine |
| `refdump.cpp` | renders a Faust DSP offline and prints its outputs as hex-float doubles, exact to the bit, after an optional pre-roll |
| `gen-ref.sh` | compiles the three DSPs with `faust -double`, runs `refdump`, writes `tests/ref/lmo_ref.h` |
| `mutations.md` | every mutation tried on the code, and whether the tests saw it |

## Running

```
FAUSTLIBS=<faustlibraries clone >= 0965ea2> SEAMLIBS=<faust-libraries/src> ./gen-ref.sh
cmake --build build-test --config Release
ctest --test-dir build-test -C Release -R "seam_noise|seam_butterworth|seam_ramp|lmo_dsp"
```

`gen-ref.sh` writes into the header the Faust version and the commits of both libraries it used.
Run it again whenever `seam.tedesco.lib` changes the LMO, and commit the header with the change.

## How it fits

`tests/ref/lmo_ref.h` feeds four tests: `seam_noise_test` (the noise, bit for bit), `seam_butterworth_test` (the filters), `seam_ramp_test` (no reference: the ramp is defined by its own properties), and `lmo_dsp_test` (the engine).
The engine references skip the first second: a band at 97 Hz takes that long to form, and over its onset alone (peak 3.1e-7) the comparison would weigh rounding against a signal that is not there yet.

## Results

| check | measured |
|---|---|
| `FaustMultinoise(8)` against `no.multinoise(8)`, 512 samples | identical, bit for bit |
| `ButterworthSVF<24>` and `<2>`, HP and LP, against Faust on an impulse | below 1e-12 of the peak |
| engine against `sdt.lmo(4, 97.44, 20)`, 96 kHz, after 1 s | 6.7e-16 absolute, 7.9e-14 of the peak |
| engine against `sdt.lmo(4, 97.44, 20)`, 48 kHz, after 1 s | 5.5e-16 absolute, 5.2e-14 of the peak |
| band energy × density², against 96 kHz, at 97.44 Hz | within 0.0001 dB at 44.1, 48 and 192 kHz |
| band energy × density², against 96 kHz, at 1 kHz | 0.0117 dB at 44.1 kHz (the bilinear transform) |
| output level at 1 kHz, 4 channels, 20 s | −38.466 dBFS at 48 kHz, −38.428 at 96 kHz, −38.439 at 44.1 kHz |
| glissando 97.44 → 112.67 Hz, glide 120 s, at 48 kHz | reaches the target within one 480-sample block of 120 s, monotonic |
| VST3 validator | 47 of 47 |
