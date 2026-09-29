# LMO: one noise stream or several

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), beside `../lmo-bandfilter/`.

Giuseppe tested by ear the difference between one and two LMO oscillators with his own variant:

```faust
lmo(N,M,f) = no.multinoise(N) <: par(i,N, par(i, M, lmoband(f + i))) :> si.bus(M) : par(i,4,/(N));
process = lmo(2,4,1000);
```

At `N = 2` he heard more complex phases and a widening of the signal, and no difference of intonation.
This study reads the routing and measures what changes.

## What the expression does
`<:` repeats the `N` noise streams cyclically over the `N·M` filter inputs, so input `k` receives stream `k mod N`.
The inner `par(i, M, …)` shadows the outer `i`, so each block of `M` filters runs at `f + 0 … f + M−1`.
With `N = 2, M = 4` both blocks receive streams 0, 1, 0, 1 at the same four frequencies: they are identical, the merge doubles them, and `/(N)` restores the level exactly.
The output is therefore one oscillator, whose channels 0 and 2 share stream 0 and channels 1 and 3 share stream 1.
With `N = 1` all four channels share one stream.

The widening is real, and its cause is the decorrelation of the channels, not a second oscillator.

## Results
`./run.sh`, 20 s at 48 kHz, f = 1000 Hz, current faustlibraries and `sdt.lmoband`:

| probe | adjacent channels r(0,1) | r(0,2) | r(1,3) | r(2,3) |
|---|---|---|---|---|
| `gs1`: Giuseppe's `lmo(1,4,1000)` | 0.998 | 0.991 | 0.991 | 0.998 |
| `gs2`: Giuseppe's `lmo(2,4,1000)` | −0.021 | 0.991 | 0.991 | −0.023 |
| `dav`: `sdt.lmoosc(4,1000)`, one stream per channel as in the original | −0.007 | 0.001 | −0.021 | 0.001 |

Before the merge, the two blocks of `gs2` differ by exactly 0.
All probes sit within 0.3 dB of −35.5 dBFS RMS per channel.

The `+ i` Hz offsets do not separate channels fed by the same noise: at 1 kHz the band is 73 Hz wide, and 1 Hz moves it by 1.4 % of its width.
Separation comes from independent streams, which the original already has, one `multinoise(8)` stream per channel.

## Listening
`render.py` writes 5 s of each probe, as four channels (LFU, RFD, RBU, LBD) and as channels 0 and 1 in a stereo pair for headphones, with one gain for all, −20 dBFS RMS (`renders/render-log.md`).

| files | probe | r(0,1) |
|---|---|---|
| `lmo_gs1_4ch.wav`, `lmo_gs1_ch01_stereo.wav` | Giuseppe's `lmo(1,4,1000)`: one stream | +0.998 |
| `lmo_gs2_4ch.wav`, `lmo_gs2_ch01_stereo.wav` | Giuseppe's `lmo(2,4,1000)`: two streams, cyclic | −0.032 |
| `lmo_dav_4ch.wav`, `lmo_dav_ch01_stereo.wav` | `sdt.lmoosc(4,1000)`: one stream per channel, as in the original | +0.017 |

On headphones r(0,1) near 1 gives a centred image and near 0 a wide one, the widening Giuseppe heard at `N = 2`; on four channels only `dav` separates all of them, since `gs2` repeats stream 0 on channels 0 and 2 and stream 1 on 1 and 3 (r = 0.991).
The correlations of 5 s differ a little from the table above, measured on 20 s.

## Files
| file | what it is |
|---|---|
| `probes.dsp` | Giuseppe's expression verbatim (`gs1`, `gs2`), its eight signals before the merge (`pre2`), and `sdt.lmoosc` (`dav`); `ch` selects the output |
| `run.sh` | builds the probes with the offline harness of `../lmo-bandfilter/` and runs `measure.py` |
| `measure.py` | renders every channel and prints the table above |
| `render.py` | the listening files in `renders/` (committed: 12 MB) |

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).
