# delRM block 2: the comb

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../delrm-delay/`.

delRM's channels 1 and 3 add the input to itself delayed: `_ <: de.delay(...), _ :> _` in the original.
The standard `fi.ff_comb(maxdel, M, b0, bM)` with both gains at 1 is the same filter, so `seam.tedesco.lib` calls it with DDELAY's prime delay:

```faust
delrmcomb(mt) = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1);
```

## The check
`./run.sh` builds `sdt.delrmcomb` and the original structure (with its swapped `de.delay` arguments) and runs `check.py`, at 48 and 96 kHz, mt = 7.291 m (Davide's initial 22 ms):

| | 48 kHz | 96 kHz |
|---|---|---|
| M = `sma.imt2npsamp(7.291)` | 1061 samples (22.10 ms) | 2113 samples (22.01 ms) |
| impulse response | 1 at 0 and at M, 0 elsewhere | same |
| against the original structure, 4 s of noise | identical | identical |
| mutation: original at M − 1 | caught (max diff 2.0) | caught |
| peak at 2·SR/M | +6.02 dB at 90.48 Hz | +6.02 dB at 90.87 Hz |
| notch at 1.5·SR/M | −284 dB at 67.86 Hz | −288 dB at 68.15 Hz |
| power on white noise | +3.00 dB | +3.01 dB |

The prime rounding moves the delay by a few samples from one rate to another, so the comb's spacing differs by 0.4 % between 48 and 96 kHz (45.24 against 45.43 Hz).
It is the price of incommensurable delays, and far below what the tuning by ear resolves.

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).

## Still to do
Renders on a recording of the contrabass clarinet (Giuseppe will provide one): the comb depends on its input, and its notches at 22.6, 67.9, 113.1 Hz… fall across the instrument's lowest register.
