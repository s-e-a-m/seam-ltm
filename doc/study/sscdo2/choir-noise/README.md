# Choir: the noise of the voices

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`, "The choir"), the first block of the choir's survey.

Davide and Giuseppe decided (2026-10-02) that the choir takes a third block of noise, decorrelated from LMO's two: one `no.multinoise(12)`, LMO on streams 0–7, the choir on 8–11 (`sdt.lmonoise`, `sdt.choirnoise`, on `sno.multinoiseblock`).
Giuseppe proposed one stream per voice: a choir of four voices, each a breath through its own bank of 16 resonances (source and filter).
The original had one stream per band, 16 per instance, the same 16 in all four instances.
This study measures what one stream per voice changes against one per band, and what the original's shared streams did between channels.

## What a voice is
Each of the four instances of `pitchDetectorChoirMcAdams` has 16 bands: the envelope of the input around `f·k` modulates noise filtered by `fi.svf.bp(f·k^a, 350)`, k = 1…16; f = 48, 48, 96, 96 Hz and a = 1, 1.01, 1.1, 0.9 on channels 0–3.
Q = 350 is set once by the wrapper's initialisation messages and no MIDI control moves it.
The probes keep the 16 noise bands of a voice and leave out the envelopes, so that only the noise is measured and heard.

## The measure is exact
Two bands driven by the same white noise have correlation r = Σh₁h₂ / √(Σh₁² Σh₂²), the inner product of their impulse responses.
`measure.py` computes it from the impulse responses, so it carries no estimation noise: a band 0.14 Hz wide (48 Hz at Q = 350) would need hours of signal for a stable estimate.
`fi.svf.bp` is Simper's SVF, the bilinear transform prewarped at f of H(s) = s/(s² + s/Q + 1); the model matches the Faust probe within 3.8e-08 of the peak (2 s of impulse response, the probe's output is float32).
The variance of a voice is the sum of all the inner products with one stream, the sum of the diagonal with one stream per band: their ratio is the level difference.

## Results
`./run.sh`, 96 kHz.

One stream per voice, the bands of a voice:

| channel | f | a | max \|r\| between two bands | level of the sum, one stream vs 16 |
|---|---|---|---|---|
| 0 | 48 | 1.0 | 1.9e-03 (bands 15 and 16) | +0.0093 dB |
| 1 | 48 | 1.01 | 1.8e-03 (bands 15 and 16) | +0.0090 dB |
| 2 | 96 | 1.1 | 1.4e-03 (bands 15 and 16) | +0.0037 dB |
| 3 | 96 | 0.9 | 2.3e-03 (bands 15 and 16) | +0.0103 dB |

At Q = 350 the bands of a voice barely touch, so one stream per voice and one per band give the same voice: the level differs by 0.01 dB at most.
The closest pair is the highest, bands 15 and 16, whose relative distance is the smallest.

How it depends on Q (the original's slider spans 10 to 1000):

| Q | max \|r\| within a voice (any channel) | level of the sum, one stream vs 16 (worst channel) |
|---|---|---|
| 10 | 7.5e-01 | +4.565 dB |
| 30 | 2.5e-01 | +1.377 dB |
| 100 | 2.8e-02 | +0.155 dB |
| 350 | 2.3e-03 | +0.010 dB |
| 1000 | 2.6e-04 | +0.001 dB |

The equivalence is a property of Q: from Q = 100 up the two choices agree within 0.16 dB, below 30 one stream per voice makes the bands of a voice correlated and louder.
If Q ever becomes a performance control, this table says where the choice must be looked at again.

Between channels, in the original (stream k feeds band k of every channel):

| channels | r in the original | bands that coincide |
|---|---|---|
| 0-1 | +0.0256 | 1 |
| 0-2 | -0.0001 | none |
| 0-3 | -0.0000 | none |
| 1-2 | -0.0001 | none |
| 1-3 | -0.0000 | none |
| 2-3 | +0.0073 | 1 |

The different f and a already kept the original's channels almost uncorrelated as wholes.
What they shared exactly was band 1 of channels 0 and 1 (48 Hz) and of channels 2 and 3 (96 Hz): the same narrow band in two drivers, a phantom image between them.
With the third block every voice has its own stream, and that band is uncorrelated too.

The levels of the probes over 10 s (after 20 s of pre-roll) differ by up to 0.39 dB between the two choices: that is the estimation noise of so few degrees of freedom, and the exact figure is the one above.
The probes sum the bands as they come out of `fi.svf.bp` (peak gain Q), hence levels above 0 dBFS; the original divides by Q·2π after the envelopes.

### Mutation
`measure.py` with Q = 3.5, wide bands that overlap: max |r| 0.95 within a voice and +7.5 dB on the sum, so the measure sees correlation when there is some.
The Q table above is the same check, graded.

## Listening
`render.py` writes 6 s of each version after 20 s of pre-roll, as four channels (LFU, RFD, RBU, LBD) and as channels 0 and 1 in a stereo pair for headphones, one gain for all, −20 dBFS RMS over all files (`renders/render-log.md`).

| files | version | r(0,1) over 6 s |
|---|---|---|
| `choir_vo_4ch.wav`, `choir_vo_ch01_stereo.wav` | the original: the same 16 streams in every channel | +0.018 |
| `choir_v4_4ch.wav`, `choir_v4_ch01_stereo.wav` | one stream per voice, `sdt.choirnoise(4)` (the decision) | −0.009 |
| `choir_v64_4ch.wav`, `choir_v64_ch01_stereo.wav` | one stream per band, 64 streams | −0.003 |

`v4` against `v64` is the question of this study: they should sound the same.
`vo` against `v4` lets one hear what the shared band 1 did between channels 0 and 1.

## Files
| file | what it is |
|---|---|
| `probes.dsp` | the voices in three versions (`vo_c`, `v4_c`, `v64_c`, channel c) and the band filter alone (`bp48`) |
| `run.sh` | builds the probes with the offline harness of `../lmo-bandfilter/` and runs `measure.py` |
| `measure.py` | the model check, the exact correlations, the probe levels and the Q table above |
| `render.py` | the listening files in `renders/` (committed: 30 MB) |

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv` with numpy, scipy and soundfile, a faustlibraries clone in `NEW_LIBS`).
