# delRM block 3: the triple product and its integrator

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../delrm-delay/` and `../delrm-comb/`.

delRM's channels 2 and 4 compute 10 · x[n−D] · x · Σx and compress it (`co.compressor_mono(11, −24, 0.03, 0.04)`, then `fi.dcblocker`).
The running sum is `fi.integrator`, a pole at z = 1: unbounded state, and a gain SR/(2πf) that doubles with the sample rate.
This study measures what that does, on a recording of the contrabass clarinet and on the floor noise and DC of the real chain, and compares two alternatives.

| | integrator |
|---|---|
| I0 | `fi.integrator`, the original |
| I1 | leaky and normalised: y = (48000/SR)·x + a·y[n−1], a = exp(−2π·fc/SR) |
| I2 | `fi.dcblockerat(5)` then `fi.integrator`, the obvious remedy |

## Files
| file | what it is |
|---|---|
| `probe.dsp` | the three integrators, the triple product before (`pre`) and after (`post`) the original's compressor stage, and the product without the integral (`selfrm`) |
| `build.sh` | compiles every probe with the harness of `../lmo-bandfilter/` |
| `analyze.py` | `selftest`, `measure` (writes `results.md`), `render` (writes `renders/`, local) |
| `results.md` | the tables, regenerated, and the hand-written observations below them |

## Running it
Requirements as for `../lmo-bandfilter/`, plus the recording (`CCB_petalonio_oriz_DO.wav`, track 1: the microphone at 1 m), passed as the second argument or found on the MDAGIFT volume.

```bash
./build.sh
../../../../.venv/bin/python analyze.py all [SOURCE.wav]
```

The drift inputs are ten minutes of Gaussian noise at −70 dBFS plus the DC measured on track 1 (3.22·10⁻⁶), written once into `build/work/` (about 0.3 GB at 96 kHz).
