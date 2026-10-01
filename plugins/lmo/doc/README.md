# LMO

LMO is the generator of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco), ported from the Pure Data patch for Giuseppe Silvi's performance.
On each of the four drivers of STONED it plays two narrow bands of noise, which beat when they are moved apart.
The four outputs are LFU, RFD, RBU, LBD, the order of the patch's `dac~ 9 10 11 12`.

## Parameters

| Parameter | Range | Default | What it does |
|---|---|---|---|
| POWER | off / on | off | fades the output in or out over 25 ms |
| f | 20–1500 Hz | 48 Hz | centre of the bands of channel 0; channel i is i Hz higher |
| glide | 0–300 s | 0 s | the time of the next move of f, linear in Hz |
| Δ | 0–50 Hz | 0 Hz | distance between the two bands of each channel; each moves Δ/2 from f |
| volume | 0–1 | 0 | linear gain, CC81 in the original, 25 ms ramp |
| f now | read-only | — | the band centre at this moment, for following a glissando |

glide is the time of the next move of f, as Pd's `line` takes its time before its target: a cue sets glide first, then f.
Cue 2 of the piece is glide = 120 s, then f = 112.67 Hz.
With glide at 0, f still moves over 25 ms, so that host automation draws a continuous curve.
A value of f that the host sends again does not restart a glissando under way.
Δ always moves over 25 ms: it is played by hand, and it would otherwise inherit a two-minute glide.

The input bus is declared and never read: a generator with no input would be routed around by the host.

## Sample rate

LMO sounds as it does at 96 kHz, the rate SSCDO#2 is played at, whatever the session's rate.
The filters are designed at the session's rate, and every time is in seconds: the glissando, the 25 ms ramps, and the redesign of the filters during a glissando, 6000 times a second (16 samples at 96 kHz).
White noise of constant RMS spreads its power over half the sample rate, so a band of fixed width would sound 3.01 dB louder at 48 kHz than at 96 kHz.
LMO multiplies its output by √(fs/96000), unity at 96 kHz, and every band keeps its 96 kHz level.
The residual is the bilinear transform's: 0.0001 dB at 97.44 Hz, 0.0117 dB at 1 kHz and 44.1 kHz.

## Specification

The DSP is `sdt.lmo(4, f, d)` of `seam.tedesco.lib` (faust-libraries), written again by hand in `source/lmo_dsp.h`:

```
lmoband(f)  = fi.highpass(24, f) : fi.lowpass(24, f - 0.0001);
lmodens     = sqrt(ma.SR/96000);
lmo(N,f,d)  = no.multinoise(2*N)
            : par(i, N, lmoband(max(1, f - d/2 + i))), par(i, N, lmoband(f + d/2 + i))
            :> par(i, N, /(sqrt(2)) : *(lmodens));
```

The engine rests on three headers of `plugins/_common/`, written for reuse: `seam_noise.h` (the standard `no.multinoise`, bit for bit), `seam_butterworth.h` (Butterworth filters of even order in J.O. Smith's state-variable sections), `seam_ramp.h` (a linear ramp in seconds).
`tests/lmo_dsp_test.cpp` checks the engine against the Faust specification at 96 and 48 kHz, to 7.9e-14 and 5.2e-14 of the peak; the references and the record of the mutations are in `doc/study/sscdo2/lmo-plugin/`.

## Out of scope

The cues and the MIDI faders live in Reaper, not in the plugin.
The choice among the band filters of the study (A, B, C, C3, C2) is Davide's: the plugin implements B, the specification.
