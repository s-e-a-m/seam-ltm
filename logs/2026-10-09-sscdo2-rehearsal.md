# 2026-10-09 — SSCDO#2 after the rehearsal with STONED

Giuseppe and Davide played the SSCDO#2 plugins on STONED and asked for three changes.

## choir: too bright → 1/f, Q and release live

The choir climbs 6 dB per octave: a band of white noise at constant Q holds a power proportional to its centre (+3 dB per octave per voice), and the voices at f·k crowd as the octaves rise (+3 dB per octave more).
Three weightings were weighed in conversation: 1/√f (+3 dB per octave left), 1/f in amplitude (pink per octave), a pink filter on the output (+3 dB per octave left, and CPU).
Giuseppe chose 1/f in amplitude, on the voice's own frequency: `w = f/fv = (k+1)^-a`, so the stretched channels are weighted where their voices actually sit.

The switch keeps the level: a fixed make-up per channel, `sqrt(Σ fl·fv / Σ fl·fv·w²)`, keeps the power on white noise (+19.7, +19.9, +21.6, +17.8 dB on the four channels), so 1/F changes colour and not loudness; the switch crossfades in 25 ms.

Q (10–1000, log, score 350) and the followers' release (0.05–10 s, log, score 1.5) became sliders.
Q glides on log Q in 25 ms; in the TPT SVF only k = 1/Q moves, so a redesign is a division per band and no tan, done every 32 samples of the engine's clock.
Off, at 350 and 1.5 s, the plugin is the score's choir bit for bit; a new Faust reference, `sdt.choirw(100, 0.5, 1)`, checks the three controls together against the spec (faust-libraries 9b32384).

## stunedrev: the four spectra in the window

`_common/seam_spectrum.h` is the measuring half of strx's spectrum made generic (Welch per channel, strx's triple buffer); `_common/seam_spectrum_view.h` is its drawing half.
Two choices strx did not need: the FFT grows with the rate (4096 points per 48 kHz, ~11 Hz bins at every rate) and the channels' hops are staggered, so four FFTs never share a sample.
strx itself is untouched; moving it onto the shared library is a separate job, if ever.

Welch lost its integer division per sample (5.4% → 2.6% of a core for four 96 kHz spectra), and gained the test that pins the time order of the windowed frame: no test caught a window laid on the ring in storage order.

## Open

- Giuseppe's look in Reaper: choir's window (1/F between POWER and RESET, Q and release under the output) and stunedrev's spectra (460×710).
- Listening: the 1/F colour on STONED, and whether Q below ~50 still sounds like the choir.
