# stunedrev plugin — references and verification

This folder holds what proves that the C++ STUNEDREV (`plugins/stunedrev/`) is the Faust specification `sdt.stunedrev(t1, t2, t3, t4)`.
Nothing here is built with the plugin: the references are rendered once, by hand, into committed headers that the tests read.
The port added two libraries to `plugins/_common/`, the C++ side of SEAM's Faust libraries: `seam_moorer.h` (Moorer's all-pass, `sjm.apfv`) and `seam_primes.h` (a sieve behind `sff.np` and `sma.ms2npsamp`); and a helper for any recursive plugin, `seam_denormals.h` (flush-to-zero for one scope).

## Files

| file | what it is |
|---|---|
| `dsp/stdel.dsp` | the 168 delays of `sdt.stdel` at the time of an entry: the reference for the delays, every ms from 1 to 100 |
| `dsp/apfv.dsp`, `dsp/apfv07.dsp` | `sjm.apfv(1024, 37, g)` with g = 1/√2 and 0.7: the references for `Seam::MoorerAllpass` |
| `dsp/stunedrev.dsp` | `sdt.stunedrev` with the four times as entries: the reference for the engine |
| `../../../../tests/stunedrev_burst.h` | the test input, 50 ms of white noise on each line then silence, included by `refdump.cpp` and by the tests so that both render the same samples |
| `refdump.cpp` | renders a Faust DSP offline and prints hex-float doubles, exact to the bit: the delay table, an impulse response, or windows of the four lines (the first 512 samples of every second, and the energy of every second), optionally moving an entry at a block boundary |
| `gen-ref.sh` | compiles the DSPs with `faust -double`, runs `refdump`, writes `tests/ref/stunedrev_ref.h` and `tests/ref/seam_moorer_ref.h` |
| `mutate.py` | applies each mutation of `mutations.md`, rebuilds and runs its test, restores the source and the binary |
| `mutations.md` | every mutation tried on the code, and whether the tests saw it |
| `cpu.cpp` | the cost of the engine at 96 and 192 kHz |

## Running

```
FAUSTLIBS=<faustlibraries clone> SEAMLIBS=<faust-libraries/src> ./gen-ref.sh
cmake --build build-test --config Release
ctest --test-dir build-test -C Release -R "seam_primes|seam_moorer|stunedrev_"
python3 doc/study/sscdo2/stunedrev-plugin/mutate.py          # from the repository root
c++ -std=c++17 -O3 -I ../../../../plugins/stunedrev/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu && ./cpu
```

`gen-ref.sh` writes into the headers the Faust version and the commits of the libraries it used.
The spec sizes its buffers at the 192 kHz bound of `ma.SR`, about 1.7 GiB, so `refdump` allocates the DSP on the heap.
Run it again whenever `seam.tedesco.lib` or `seam.moorer.lib` changes, and commit the headers with the change.

## How it fits

`tests/ref/seam_moorer_ref.h` feeds `seam_moorer_test` (the all-pass against `sjm.apfv`, its unit energy, its `clear()`).
`tests/ref/stunedrev_ref.h` feeds `stunedrev_dsp_test`: the delays, the four lines at 96 and 48 kHz over 30 s, a change of time (t e from 7 to 9 ms at sample 48128, the block at which the C++ test moves it), the arena, the centroids, the edge cases of the host (in-place buffers, odd block sizes, a new rate), POWER and RESET.
`seam_primes_test` needs no reference: it compares the sieve with trial division, the method of `nextprime.h`, for every number up to the 192 kHz bound.
`stunedrev_params_test` and `stunedrev_state_test` check the controls and the state codec.

## Results

| check | measured |
|---|---|
| `nextPrimeAbove` and `isPrime` against trial division, every n up to 2 534 298 | identical |
| the 16 800 delays against `sdt.stdel`, 96 and 48 kHz | identical |
| `MoorerAllpass` against `sjm.apfv`, g = 1/√2 and 0.7, 512 samples | below 1e-15 of the peak |
| engine against `sdt.stunedrev(83, 47, 7, 71)`, 96 kHz, 30 s | 1.83e-15 of the peak (windows), 5.5e-16 (energy per second) |
| engine against `sdt.stunedrev(83, 47, 7, 71)`, 48 kHz, 30 s | 1.62e-15 of the peak (windows), 5.4e-16 (energy per second) |
| a change of time, t e 7 → 9 ms at 96 kHz | 8.7e-16 of the peak |
| arena at 96 kHz | 77 085 940 doubles, 588.119 MiB |
| RESET at 96 kHz, fade and clearing | 0.418 s |
| cost, Intel i7-8850H 2.6 GHz, 256-sample blocks | 9.4 % of a core at 96 kHz, 18.8 % at 192 kHz |
| cost at 96 kHz after 10 minutes of silence, subnormals flushed | 9.4 % of a core (without the flush: 4.7 % to 15 % in 30 minutes at 48 kHz, final review) |

Three mutants survived the first run of `mutate.py`; each was a hole in a test, closed and run again (`mutations.md`).
