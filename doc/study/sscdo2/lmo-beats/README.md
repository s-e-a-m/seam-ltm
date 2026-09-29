# LMO: two beating oscillators

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../lmo-bandfilter/` and `../lmo-streams/`.

Davide Tedesco's intention for the LMO's second oscillator (2026-09-29): two bands that can beat; at 0 Hz apart they share the frequency and are decorrelated; at a distance d each moves d/2 from the reference centre; the distance slider stays within the range of beats, up to about the critical band.
This study builds that design, checks it, and measures where a beat between two noise bands becomes audible, so that the slider's range can be set on numbers and on listening.

## The design

```faust
lmo2(N, f, d) = no.multinoise(2*N) : bandsA(N, f, d), bandsB(N, f, d) :> par(i, N, /(sqrt(2)));
bandsA(N, f, d) = par(i, N, sdt.lmoband(f - d/2 + i));
bandsB(N, f, d) = par(i, N, sdt.lmoband(f + d/2 + i));
```

One `no.multinoise(2N)` call gives 2N independent streams; two calls would give the same noise twice.
Channel k sums a band at f − d/2 + k and one at f + d/2 + k: the per-channel offset k cancels in the difference, so every channel beats at d.
Two independent noises add in power, hence 1/√2.

## Files

| file | what it is |
|---|---|
| `dsp/probes.dsp` | `lmo2` and the probes of the self-test (`osc`: the eight bands before the sum; `one`: `sdt.lmo`, the level reference; `twocall`: the two-call mutation) |
| `build.sh` | compiles every probe with the offline harness of `../lmo-bandfilter/` |
| `analyze.py` | `selftest`, `measure` (writes `results.md`), `render` (writes `renders/`) |
| `results.md` | the tables, regenerated, and the hand-written observations below them, preserved |
| `renders/` | the listening files, committed, and `render-log.md` |

## Running it

Requirements: `faust` (2.88 was used), a C++17 compiler, the seam-ltm `.venv` (see `../lmo-bandfilter/README.md`), and a clone of [grame-cncm/faustlibraries](https://github.com/grame-cncm/faustlibraries) at `0965ea2` or later in `NEW_LIBS`; `SEAM_LIBS` defaults to the sibling `faust-libraries/src`.

```bash
./build.sh
../../../../.venv/bin/python analyze.py all      # selftest, measure, render
```

## The renders

All at f = 97.44 Hz (cue 1), 48 kHz, 24-bit, normalised to −20 dBFS RMS with 50 ms fades:

- `one_osc_mono.wav`: the single oscillator, `sdt.lmo`, for reference;
- `dNN_mono.wav` (20 s) and `dNN_4ch.wav` (10 s, the four channels for STONED), for d = 0, 3, 7, 10, 20, 40 Hz;
- `sweep_d00-40_mono.wav`: d rising linearly from 0 to 40 Hz over 60 s, to hear where the beat appears.

`d00_mono.wav` and `one_osc_mono.wav` should be indistinguishable: two independent noises through one filter are one noise band.
