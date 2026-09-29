# delRM block 5: the DC blockers

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../delrm-rm/` (blocks 3 and 4).

The original ends each delRM channel with `fi.dcblocker`, and its `process` puts a second one on all four channels after the master volume:

```faust
delRM = ... : _,(_*10 : co.compressor_mono(11,-24,0.03,0.04)) : _,_ : par(i, 2, _ : fi.dcblocker);
process = ... : delRM_duet : par(i,4, _*masterVolume) : par(i, 4, _ : fi.dcblocker) : vumeter;
```

The question for this block is whether those two filters make process and timbre, or fix a problem of the original that our chain no longer has.
Everything here runs at 96 kHz, the rate SSCDO#2 is played at.

## What they were for
`fi.dcblocker` is `zero(1) : pole(0.995)`.
The measure below reads the mean of each channel at the point where the first one sits.

| signal | DC re RMS |
|---|---|
| channels 2/4, original `fi.integrator`, on the note | −35.8 dB |
| channels 2/4, original `fi.integrator`, the same note after 5 min of running | **−9.6 dB** |
| channels 2/4, `sdt.delrmint` (leaky), on the note and after 5 min | −66.6 / −66.5 dB |
| channels 1/3, the comb | −75.3 dB |
| the dry note | −72.2 dB |

With the original's unbounded integrator the triple product carries, after a few minutes, a DC less than 10 dB below the signal: the first DC blocker had a real job on channels 2 and 4.
It did it late, after the compressor, whose gain reduction the DC had already moved.
With `sdt.delrmint` the DC stays 66 dB down and does not grow; on the comb it was never there.
The second DC blocker, on all four channels, has nothing to remove.
Both were local answers to a local problem, and `sdt.delrmint` removes the problem at its source.

## What they did to the note
The pole is fixed in samples, so the corner follows the rate.
`fi.dcblockerat(fb)` has the pole `(1 − wn)/(1 + wn)`, `wn = π·fb/SR`; the pole 0.995 at 96 kHz gives fb = 96000/π · 0.005/1.995 = **76.59 Hz**.
At the rate of the piece each DC blocker is a first-order high-pass at 76.59 Hz, above the 29.7 Hz fundamental of the contrabass clarinet.

| partial | 1 DC blocker | 2 DC blockers |
|---|---|---|
| 29.7 Hz (1) | −8.81 dB | −17.63 dB |
| 89.1 Hz (3) | −2.38 dB | −4.76 dB |
| 148.5 Hz (5) | −1.00 dB | −2.01 dB |
| 207.9 Hz (7) | −0.53 dB | −1.06 dB |

On the recording the 25–35 Hz band loses 17.7 dB on both channel pairs, and the comb's RMS drops by 4 dB (`results.md`).
The performance sounded with that thinner low end: it was part of what Davide heard, as a by-product of a remedy.

## The decision
delRM in `seam.tedesco.lib` ends at the compressor, and the library states why.
Whether the thinner low end belongs to the piece is a listening decision for Davide.
If he keeps it, it enters as a declared high-pass, `fi.dcblockerat(76.59)` twice, the same filter at 96 kHz (up to its gain at Nyquist, 0.02 dB) and the same corner at any other rate.

## Files
- `probe.dsp`: the probes (comb and triple product clean and through two DC blockers, the original integrator up to the compressor, `fi.dcblocker` against `fi.dcblockerat(76.59)`).
- `analyze.py`: `check` proves the probes are what this page says, each claim with a mutation that must fail; `measure` writes `results.md`; `render` writes the A/B.
- `run.sh`: builds the probes with the harness of `../lmo-bandfilter/` (compiled with `FAUSTFLOAT=double`, or the sample-for-sample checks would compare float32 roundings) and runs `analyze.py`.

```bash
./run.sh            # check, measure, render
./run.sh check      # only the check
```

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`) and the recording `CCB_petalonio_oriz_DO.wav` (track 1, the microphone at 1 m, 96 kHz).

## The A/B for Davide
`renders/`, at 96 kHz, mt = 7.291 m, with the leaky integrator:

| file | what |
|---|---|
| `note_dry.wav` | the note |
| `ch1_comb_clean.wav` / `ch1_comb_2dcblockers.wav` | channels 1/3, without and with the two DC blockers |
| `ch2_triple_clean.wav` / `ch2_triple_2dcblockers.wav` | channels 2/4, the same |

All five share one RMS level, −31 dBFS, so the pairs compare timbre and not loudness; `renders/render-log.md` gives the gain each file received, and within a pair its difference is the level the DC blockers took away.
The level is low because channels 2 and 4 have a crest factor of 28–30 dB: the compressor's 30 ms attack lets the onset of the triple product through before it closes.
