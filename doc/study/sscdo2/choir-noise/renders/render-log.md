6 s at 96000 Hz after a 20 s pre-roll; one gain for all files, -35.89 dB, -20 dBFS RMS over all, 20 ms fades, 24-bit.

| files | version | r(0,1) | RMS per channel (dBFS) |
|---|---|---|---|
| `choir_vo_4ch.wav`, `choir_vo_ch01_stereo.wav` | the original: the same 16 streams in every channel | +0.018 | -21.5 / -21.9 / -17.7 / -20.1 |
| `choir_v4_4ch.wav`, `choir_v4_ch01_stereo.wav` | one stream per voice, `sdt.choirnoise(4)` | -0.009 | -22.0 / -22.0 / -18.0 / -19.8 |
| `choir_v64_4ch.wav`, `choir_v64_ch01_stereo.wav` | one stream per band, 64 streams | -0.003 | -21.7 / -21.6 / -17.9 / -19.8 |

Channel 0, mean floor between the bands (median of 0.3-0.7 of each gap) relative to the mean peak:

| version | floor re peaks |
|---|---|
| `vo` | -28.5 dB |
| `v4` | -36.6 dB |
| `v64` | -26.7 dB |

Outside the bank, `v4` minus `v64`:

| channel | 20-30 Hz | 5-7 kHz | 20-30 kHz |
|---|---|---|---|
| 0 | +6.8 dB | +11.0 dB | +11.0 dB |
| 1 | +7.5 dB | +11.0 dB | +10.9 dB |
| 2 | +8.6 dB | +10.7 dB | +10.8 dB |
| 3 | +7.7 dB | +11.1 dB | +11.0 dB |
