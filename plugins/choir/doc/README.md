# CHOIR

CHOIR is the choir of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco), ported from the Pure Data patch (`pitchDetectorChoirMcAdams`, four instances) for Giuseppe Silvi's performance.
It listens to the TETRAREC A (patch inputs 5–8) and makes noise sing where the input has partials.
On each of the four channels, 16 bands listen around f·k and 16 voices of noise sing around f·k^a, each voice as loud as its band of the input.
Channel c listens to input c and sings on output c.

## The constants

The choir has no tuning controls: these values are the performance's, set once by the patch's initialisation, and the window shows them.

| channel | f | a |
|---|---|---|
| 1 | 48 Hz | 1 |
| 2 | 48 Hz | 1.01 |
| 3 | 96 Hz | 1.1 |
| 4 | 96 Hz | 0.9 |

Every band has Q = 350 (about 0.14 Hz wide at 48 Hz), and every follower releases in 1.5 s.
The analysis bands listen at f·k, the harmonics of f; the voices sing at f·k^a, stretched by a.
The choir answers only where the input has a partial within a fraction of a hertz of f·k, and it keeps answering for seconds: a band at 48 Hz rings 16 s before falling 60 dB.

## Parameters

| Parameter | Range | Default | What it does |
|---|---|---|---|
| POWER | off / on | off | fades the output in or out over 25 ms; the choir keeps listening |
| output | 0–1 | 0 | linear gain out of the four channels, CC86 in the patch, 25 ms ramp |

RESET (beside POWER) silences the bands that are still ringing, at the next audio block.
It is a button of the window, not a parameter: the host cannot automate it and the state does not store it.

## The grid

The footer shows what the choir hears: four rows, one per channel, of 16 bars, one per band.
A bar is the amplitude of the input's partial in that band, the peak of the band's envelope over the last audio block divided by Q, in dBFS from −80 to 0.
A bar that rises is a voice that sings.
The bars fall with the follower's 1.5 s release, which is what the voices do.
Each row is labelled with its f and a; the line below carries Q, the release, the noise block and the session's rate.

## The noise

The voices sing on block 3 of the SSCDO#2 noise: one `no.multinoise(72)` whose streams 0–7 are LMO's and 8–71 the choir's, one stream per band, channel c on 8 + 16c to 23 + 16c.
Each value of the generator goes to one block only, so the choir is decorrelated from LMO and its channels from each other, whatever the instant each plugin starts (`doc/study/sscdo2/choir-noise/`).

## Sample rate

CHOIR sounds as it does at 96 kHz, the rate SSCDO#2 is played at, whatever the session's rate.
The bands are designed in hertz, the followers in seconds, and the voices are held at their 96 kHz level: a band of white noise loses 3.01 dB at every doubling of the rate, and the factor √(fs/96000) gives it back.
A band whose centre is at 20 kHz or above is silent at every rate; with the performance's constants the highest centre is 2017 Hz.

## Against the original

Decided by Giuseppe on 2026-10-02:

- No DC blocker: every product is an envelope times a band-pass with a zero at DC, so there is no DC to remove; the original's `fi.dcblocker` took 5.48 dB off the 48 Hz partial at 96 kHz.
- The voices at their 96 kHz level.
- Bands from 20 kHz silent: above fs/2 the original's filters were unstable.
- The noise is block 3 of the SSCDO#2 noise; in the original the four instances shared one noise, made of LMO's own numbers taken every other sample.
- POWER, RESET, the output fader with a 25 ms ramp and the grid belong to the plugin.

## Specification

The DSP is `sdt.choir(350, 1.5)` of `seam.tedesco.lib` (faust-libraries), written again by hand in `source/choir_dsp.h` on `_common/seam_svf.h` (`fi.svf.bp`), `seam_analyzers.h` (`an.amp_follower`), `seam_basics.h` (`ba.tau2pole`) and `seam_noise.h` (`sno.multinoiseblock`).
The C++ equals the specification within 1e-12 of the peak at 96 and 48 kHz.
The engine costs 4.3 % of a core at 96 kHz (Apple Silicon).

Spec: `docs/superpowers/specs/2026-10-02-choir-plugin-design.md`.
Studies: `doc/study/sscdo2/choir-noise/`, `choir-chain/`, `choir-plugin/`.
Log: `logs/2026-10-02-sscdo2-choir.md`.
