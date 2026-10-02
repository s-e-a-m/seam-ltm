# DELRM

DELRM is the delRM of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco), ported from the Pure Data patch for Giuseppe Silvi's performance.
It has four channels, and each channel processes only its own input from the TETRAREC.
Channels 1 and 3 are a feed-forward comb: the input added to itself delayed by D.
Channels 2 and 4 multiply three signals, the input delayed by D, the input and its integral, and pass the product through an 11:1 compressor that works as a limiter near −20 dBFS.
Inputs and outputs 1–4 are in the order of the original.

## Parameters

| Parameter | Range | Default | What it does |
|---|---|---|---|
| POWER | off / on | off | fades the output in or out over 25 ms; the engine keeps running |
| distance | 0–30 m, 1 mm | 7.291 m | the distance of DDELAY, the same for the four channels (see below) |
| output | 0–1 | 0 | linear gain out of the four channels, CC82 in the original, 25 ms ramp |

Six read-only meters sit in the footer.
`in 1`–`in 4` show the peak of each input over −70 to +5 dB, instant attack and 300 ms release, to set the gain of the ASP880.
`GR 2` and `GR 4` show the gain reduction of the compressors over 0 to −48 dB, the deepest value of each block, with the same release.
The GR bars are drawn right to left, opposite to the inputs: the input rising and the compressor descending read as two opposite movements, and the window of rehearsal card `delrm-dinamica` is legible while the ASP880 is set.
GR near 0 dB means the product is still below the threshold and the effect is appearing; GR deep means the compressor works as a limiter and the effect is saturated.

## The distance

D is DDELAY's delay: the distance in metres rounded to the millimetre, at 331.4 m/s, rounded to the sample and moved to the next prime strictly above (`sma.imt2npsamp`).
At 0 m D is 0 and the comb doubles its input, as the specification does.
The footer shows D in milliseconds and in samples at the session's rate, so the prime changing with the rate is visible.
A change of distance recomputes D at the start of the next audio block and the delay jumps to its new value: a click, as in the specification and in the original.
The distance is a setting for the rehearsal, and no cue of the piece moves it.

## Sample rate

DELRM sounds as it does at 96 kHz, the rate SSCDO#2 is played at, whatever the session's rate.
D is a distance, so a time at every rate, each rate with its own prime.
The integrator is normalised in seconds and anchored at 96 kHz (`sdt.delrmint`).
The compressor works in seconds.

## Memory

Each of the four delay lines holds exactly `D(30 m) + 1` samples, in one block allocated and zeroed when the host activates the plugin, never in the audio thread.
The specification sizes its lines at `1 << 15`, which covers 30 m up to 192 kHz (17 383 samples) and stops short at 384 kHz (34 763 samples); the exact size is correct at any rate and the sound is identical.
At 384 kHz the four lines take 1.06 MiB (4 × 34 764 doubles).
The memory of delRM is as long as D, so the plugin has no RESET.

## Against the original

Decided with Davide on 2026-10-02:

- No DC blockers: delRM ends at the compressor, as the specification does, and the rehearsal's listening judges the thinner bass.
- One output fader (CC82) replaces the two volumes in series of the original, the gain stage of Pd and the internal 0.9 of the `.dsp`.
- POWER, 25 ms ramps, input meters and the gain reduction of channels 2 and 4 belong to the plugin.

## Specification

The DSP is `sdt.delrmcomb`, `sdt.delrmint`, `sdt.delrmrm` and `sdt.delrmdyn` of `seam.tedesco.lib` (faust-libraries), written again by hand in `source/delrm_dsp.h`:

```
imt2npsamp(mt) = select2(n < 2, n : sff.np, n)
                 with { mm = floor(mt*1000 + 0.5)/1000;
                        n  = int(floor(mm*ma.SR/isos + 0.5)); };  // isos = 331.4
leakyint(fc)   = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR));
delrmcomb(mt)  = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1);
delrmint       = sfi.leakyint(1) : *(96000);
delrmrm(mt)    = _ <: de.delay(1 << 15, sma.imt2npsamp(mt)), _, delrmint : *, _ : *;
delrmdyn       = *(10) : co.compressor_mono(11, -24, 0.03, 0.04);
process        = delrmcomb(mt), (delrmrm(mt) : delrmdyn),
                 delrmcomb(mt), (delrmrm(mt) : delrmdyn);
```

The engine wires four libraries of `plugins/_common/` written for reuse: `seam_delays.h` (`de.delay`), `seam_filters.h` (`sfi.leakyint`), `seam_compressors.h` (`co.compressor_mono`) and `seam_primes.h` (the sieve behind `sff.np`, with `metresToPrimeSamples`).

## Verification

The references are the specification compiled with `faust -double`; the tests need no `faust` binary.

| check | measured |
|---|---|
| `LeakyIntegrator` against `sfi.leakyint(1)`, 96 and 48 kHz | identical to the bit (relative error 0) |
| `CompressorMono` against `co.compressor_mono`, 11:1 and 4:1 sets | output 3.6e-16 and 2.6e-16 relative; gain 1.1e-14 dB and 3.6e-15 dB |
| engine against the specification, 96 kHz | 5.28e-16 relative |
| engine against the specification, 48 kHz | 5.98e-16 relative |
| change of distance at sample 96000 | 5.34e-16 relative |
| line memory at 384 kHz | 1.06 MiB |

Every test was verified by mutation, 24 mutations in all, two of them equivalent mutants explained in the record; the references, the record and the CPU measurement are in `doc/study/sscdo2/delrm-plugin/`.
The cost is 2.02 % of a core at 96 kHz on sound and 0.46 % on silence, 4.12 % and 0.91 % at 192 kHz.
The integrator and the compressor are poles that decay in silence, so their states would sink into subnormal numbers; the engine flushes subnormals to zero while it runs (`plugins/_common/seam_denormals.h`), and without the guard the silence costs about six times more (2.70 % at 96 kHz, 5.45 % at 192 kHz).

## Out of scope

Aligning DDELAY and ADDELAY on `seam_delays.h` is a separate project, to be proposed.
The cues and the MIDI faders live in Reaper, not in the plugin.
