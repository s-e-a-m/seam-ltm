# Choir: the chain

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`, "The choir"), block 2 of the choir's survey, after `../choir-noise/`.

## What the original does
`pitchDetectorChoirMcAdams`, four instances, one per channel, on inputs 5–8 (TETRAREC A through the ASP880, as delRM); the output goes through the patch's `interpolator_4ch` (CC86) to the mix.
Per channel, for k = 1…16:

- analysis: the input through `fi.svf.bp(f·k, Q)`, then `an.amp_follower(release)`;
- voice: a noise stream through `fi.svf.bp(f·k^a, Q)` (the third block of the SSCDO#2 noise, `../choir-noise/`);
- the two multiplied; the 16 products summed, divided by Q·2π, then `fi.dcblocker`.

f = 48, 48, 96, 96 Hz and a = 1, 1.01, 1.1, 0.9 on channels 0–3, Q = 350, release 1.5 s.
The wrapper's own initialisation sets Frequency 27, Q 350, Release 1.5, a 1; the four-channel abstraction's sets f and a per channel after it (Pd fires the inner loadbangs first), so f and a are 48/96 and the stretches above.
No MIDI control moves f, a, Q or the release: in the performance they are constants, and only the output (CC86) moves.
`partial_order`, `random_for_partial` and `mcadams_freq` are dead code: only `sma_sc(f, n, a) = f·n^a` is used.

## Results
`./run.sh`, at 48 and 96 kHz:

| SR | `fi.dcblocker` on 48 Hz | noise band at 48 Hz, Q 350 (exact power) |
|---|---|---|
| 48000 | -2.12 dB | -4.36 dBFS |
| 96000 | -5.48 dB | -7.37 dBFS |

| SR | follower, 1 s after the input stops | analysis band, 1 s after | band to -60 dB |
|---|---|---|---|
| 48000 | -5.80 dB (exp(-1/1.5): -5.79) | -3.65 dB | 16.0 s |
| 96000 | -5.80 dB (exp(-1/1.5): -5.79) | -3.65 dB | 16.0 s |

Centres above the Nyquist frequency (white noise, 0.5 s): at 0.55·SR the output reaches 9.05e+04 (48 kHz) and 3.61e+09 (96 kHz), at 0.75·SR 5.36e+15 and 4.16e+30.

### The DC blocker
`fi.dcblocker` is a pole at 0.995 whatever the rate, so its corner is a fraction of SR: 38.2 Hz at 48 kHz, 76.4 Hz at 96 kHz.
At 96 kHz it takes 5.48 dB off the first partial of channels 0 and 1 (48 Hz) and 2.12 dB off that of channels 2 and 3 (96 Hz); at 48 kHz 2.12 and 0.62 dB.
The choir carries no DC to remove: each product is a non-negative envelope times a band of noise, and `fi.svf.bp` has a zero at DC, so every product has zero mean.
It is delRM's case (`../delrm-dcblock/`), where Giuseppe and Davide chose to go without and judge by listening.

### The level of the noise bands
A band of white noise holds the power of its bandwidth over SR/2: 3.01 dB less at every doubling of the rate (-4.36 dBFS at 48 kHz, -7.37 at 96 kHz for 48 Hz, Q 350, from the impulse response and the variance 1/3 of `no.noise`).
LMO holds its bands at their 96 kHz level with `sdt.lmodens = sqrt(SR/96000)`; the choir's voices are the same kind of band.

### The follower and the analysis bands
`an.amp_follower` has an immediate attack and a release in seconds (`ba.tau2pole`): -5.80 dB one second after the input stops at both rates, the exponential exp(-1/1.5) to 0.01 dB.
The analysis band at Q 350 rings far longer than the follower releases: -3.65 dB after a second, -60 dB only after 16 s (its time constant is Q/(π·f) = 2.3 s at 48 Hz).
Both are rate-invariant: the choir listens with a memory of seconds, by design of Q, at any SR.

### Above the Nyquist frequency
`fi.svf.bp` designs with tan(π·f/SR): at f = SR/2 the tangent is infinite and the band goes silent; above it the tangent is negative and the filter is unstable.
With the performance's constants the highest centre is 96·16^1.1 = 2017 Hz, far below; the sliders of the original (f up to 500 Hz, a up to 2) reach 128 kHz.

## The decisions and the spec
Giuseppe, 2026-10-02: no DC blocker, the voices at their 96 kHz level, bands at or above 20 kHz silent.
`sdt.choirband`, `sdt.choirchan`, `sdt.choir(q, rel)` in seam.tedesco.lib (faust-libraries).
`spec.py` checks the spec against the original transcribed in `spec.dsp`, on the same noise streams and without its DC blocker; the input of channel c is 16 sines at f·k, 0.05 each, 20 s, compared after 10 s:

| SR | channel | peak of the original | max \|ours − orig·choirdens\| | max \|ours − sdt.choir\| |
|---|---|---|---|---|
| 96000 | 0 | 0.16 | 4.0e-16 of the peak | 4.0e-16 of the peak |
| 96000 | 1 | 0.206 | 3.8e-16 of the peak | 3.8e-16 of the peak |
| 96000 | 2 | 0.31 | 3.3e-16 of the peak | 3.3e-16 of the peak |
| 96000 | 3 | 0.235 | 3.3e-16 of the peak | 3.3e-16 of the peak |
| 48000 | 0 | 0.253 | 2.9e-16 of the peak | 2.9e-16 of the peak |
| 48000 | 1 | 0.258 | 2.8e-16 of the peak | 2.8e-16 of the peak |
| 48000 | 2 | 0.412 | 2.7e-16 of the peak | 2.7e-16 of the peak |
| 48000 | 3 | 0.367 | 2.0e-16 of the peak | 2.0e-16 of the peak |

The first column proves `choirchan` is the original without the DC blocker, with `choirdens` on the voices (1 at 96 kHz); the second that `sdt.choir` gives each channel its own input and its own 16 streams.
With f = 5000 Hz at 48 kHz (bands from 20 kHz up to 80 kHz) the original is unstable, 3.4e+38 and only 33.5 % of the samples finite in 2 s; the spec stays finite, peak 0.293 (input: the harmonics of 1 kHz below Nyquist, in `spec.py`).

### Mutations
| mutation | check | result |
|---|---|---|
| M1: voices not interleaved with their analysis bands | ours − orig, channel 0, 96 kHz | RED, 1.35 |
| M2: channel c sings on channel c+1's streams | ours − sdt.choir, channel 0 | RED, 0.256 |
| M4: `choirdens` applied twice | ours − orig, channel 0, 48 kHz | RED, 0.0524 (at 96 kHz `choirdens` is 1: the check must run at another rate) |
| M5: the stretch a on the analysis bands too | ours − orig, channel 2 | RED, 0.127 |
| M3: no clamp at 19999 Hz in `choirband` | with constant f | equivalent: Faust folds `band * 0` to 0 and never computes the filter |
| M3: no clamp, centre on a slider at 30 kHz, 48 kHz | finite output | RED: non-finite from 14.54 s (24.2 % finite over 60 s); with the clamp finite and silent |

The clamp matters wherever the centre is a variable, as in C++: the gate alone multiplies an unstable filter by 0, and inf times 0 is NaN.

## Files
| file | what it is |
|---|---|
| `probes.dsp` | the DC blocker on a 48 Hz sine, the band's impulse response, the follower and the analysis band on a gated sine, two centres above Nyquist |
| `run.sh` | builds the probes with the offline harness of `../lmo-bandfilter/` and runs `measure.py` |
| `measure.py` | the tables of the stages; each figure beside the formula it must match (dcblocker response, exp(-1/1.5), 3.01 dB per doubling) |
| `spec.dsp`, `spec.py` | the original transcribed, `sdt.choirchan` and `sdt.choir` against it, and above Nyquist |

No audio yet: the choir answers only where the input has partials within a fraction of a hertz of f·k, so its sound belongs to the plugin in the room, with the TETRAREC.
Requirements as for `../lmo-bandfilter/`.
