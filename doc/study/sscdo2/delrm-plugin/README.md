# delRM plugin — references and verification

This folder holds what proves that the C++ DELRM (`plugins/delrm/`) is the Faust specification `sdt.delrmcomb`, `sdt.delrmint`, `sdt.delrmrm` and `sdt.delrmdyn`.
Nothing here is built with the plugin: the references are rendered once, by hand, into committed headers that the tests read.
The port added four things to `plugins/_common/`, the C++ side of the Faust libraries: `seam_delays.h` (`de.delay`), `seam_filters.h` (`sfi.leakyint`), `seam_compressors.h` (`co.compressor_mono`), and `metresToPrimeSamples` in `seam_primes.h` (`sma.imt2npsamp`).

## Files

| file | what it is |
|---|---|
| `dsp/leakyint.dsp` | `sfi.leakyint(1)`: the reference for `Seam::LeakyIntegrator` |
| `dsp/comp.dsp`, `dsp/comp2.dsp` | `co.compressor_mono` with the 11:1 set of delRM and a 4:1 set: the references for `Seam::CompressorMono` |
| `dsp/delrm.dsp` | the four channels of `sdt.delrm*` with the distance as an entry: the reference for the engine |
| `refdump.cpp` | renders a Faust DSP offline and prints hex-float doubles, exact to the bit, in windows, optionally moving the distance at a block boundary |
| `gen-ref.sh` | compiles the DSPs with `faust -double`, runs `refdump`, writes `tests/ref/seam_filters_ref.h`, `tests/ref/seam_compressors_ref.h` and `tests/ref/delrm_ref.h` |
| `mutations.md` | every mutation tried on the code (24 rows), and whether the tests saw it |
| `cpu.cpp` | the cost of the engine at 96 and 192 kHz, on sound and on silence |
| `../../../../tests/delrm_signal.h` | the test inputs, included by `refdump.cpp` and by the tests so that both render the same samples |
| `../../../../tests/ref_windows.h` | the comparison of windows against a reference, with guards for NaN and for the count of compared samples |

## Running

```
FAUSTLIBS=<faustlibraries clone> SEAMLIBS=<faust-libraries/src> ./gen-ref.sh
cmake --build build-test --config Release
ctest --test-dir build-test -C Release -R "seam_primes|seam_delays|seam_filters|seam_compressors|delrm_"
c++ -std=c++17 -O3 -I ../../../../plugins/delrm/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu && ./cpu
```

`gen-ref.sh` writes into the headers the Faust version and the commits of the libraries it used.
Run it again whenever `seam.tedesco.lib` changes, and commit the headers with the change.

## How it fits

The specification (`seam.tedesco.lib`) compiles with `gen-ref.sh` into `tests/ref/*.h`, and the tests read those headers: `seam_filters_test`, `seam_compressors_test` and `delrm_dsp_test` compare the C++ with the Faust samples; `seam_delays_test` and the metres test of `seam_primes_test` compare with `de.delay` and `sma.imt2npsamp`.
`delrm_dsp_test` also holds the memory sizing at seven rates, the meters, the host's edge cases (in-place buffers, a new rate), POWER and the output ramp.
`delrm_params_test` and `delrm_state_test` check the controls and the state codec; the state test pins the on-disk order of the parameters.

## Results

| check | measured |
|---|---|
| `LeakyIntegrator` against `sfi.leakyint(1)`, 96 and 48 kHz | relative error 0 (bit-exact) |
| `CompressorMono`, 11:1 set | output 3.6e-16 relative, gain 1.1e-14 dB |
| `CompressorMono`, 4:1 set | output 2.6e-16 relative, gain 3.6e-15 dB |
| engine against the specification, 96 kHz | 5.28e-16 relative |
| engine against the specification, 48 kHz | 5.98e-16 relative |
| a change of distance at sample 96000 | 5.34e-16 relative |
| line memory at 384 kHz (4 × 34 764 doubles) | 1.06 MiB; `delrm_dsp_test` reports nothing larger |
| cost, 256-sample blocks, 96 kHz | 2.02 % of a core on sound, 0.46 % on silence |
| cost, 192 kHz | 4.12 % on sound, 0.91 % on silence |
| cost without `ScopedNoDenormals`, silence | 2.70 % at 96 kHz, 5.45 % at 192 kHz, about six times more |

Of the 24 mutations of `mutations.md`, 22 turned a test red; the other two are equivalent mutants, each with its explanation.
