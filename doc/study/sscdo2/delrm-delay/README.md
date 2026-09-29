# delRM block 1: the DDELAY delay in Faust

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`).

Agreed with Davide: delRM's delay uses DDELAY's prime rounding, with the distance in metres and the value shown in ms, so that the same tool serves *Studio sul Corpo d'Ombra #4*, with two sources.
The delay is tuned during the setup and left alone during the piece.

The Faust specification of DDELAY had drifted from the plugin.
`sma.imdelay` computed `int(mt·SR/331.4)`, truncating and without primes, while DDELAY's C++ (`plugins/ddelay/source/ddelay_processor.cpp`, `updateDelaySamples`) rounds the distance to the millimetre, rounds to the nearest sample, and moves to the next prime strictly above.
`sma.imt2npsamp` in `seam.math.lib` now specifies what the plugin does, and the delay is `de.delay(1 << 15, sma.imt2npsamp(mt))`.
`sma.imdelay`, and the `sma.imnpdelay` that briefly replaced it, were removed: a conversion is mathematics, a delay line has state, and neither was a delay of its own.

## The check
`probe.dsp` sweeps the distance by 0.1 mm per sample from 0 to 30 m (DDELAY's range) through `sma.imt2npsamp`.
`ddelay_ref.cpp` applies the lines of `updateDelaySamples` to the same distances.
`compare.py` compares the two value by value.

```bash
./run.sh
```

Result on 2026-09-29: 300 001 values, 0 mismatches at 44.1, 48, 96 and 192 kHz; the same run with the Faust side moved by 1 mm gives 5 940 mismatches at 48 kHz.
Davide's initial 22 ms is 7.291 m, which gives 971 samples at 44.1 kHz, 1061 at 48 kHz (22.10 ms), 2113 at 96 kHz and 4229 at 192 kHz.

A first version of the probe read the step from a slider and found about 0.2 % mismatches: the slider holds its value in single precision, so the Faust side received slightly different distances.
The step is now a constant in the code.
In a host the distance arrives in double, and the millimetre rounding absorbs single-precision error except exactly on a half-millimetre boundary.

Requirements: `faust`, a C++17 compiler, Python 3, a [faustlibraries](https://github.com/grame-cncm/faustlibraries) clone in `NEW_LIBS`.
`sff.np` is a foreign function (`src/h/nextprime.h`), so the online Faust IDE cannot compile it.
