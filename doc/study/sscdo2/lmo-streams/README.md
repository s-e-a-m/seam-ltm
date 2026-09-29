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
| `dav`: `sdt.lmo(4,1000)`, one stream per channel as in the original | −0.007 | 0.001 | −0.021 | 0.001 |

Before the merge, the two blocks of `gs2` differ by exactly 0.
All probes sit within 0.3 dB of −35.5 dBFS RMS per channel.

The `+ i` Hz offsets do not separate channels fed by the same noise: at 1 kHz the band is 73 Hz wide, and 1 Hz moves it by 1.4 % of its width.
Separation comes from independent streams, which the original already has, one `multinoise(8)` stream per channel.

## Files
| file | what it is |
|---|---|
| `probes.dsp` | Giuseppe's expression verbatim (`gs1`, `gs2`), its eight signals before the merge (`pre2`), and `sdt.lmo` (`dav`); `ch` selects the output |
| `run.sh` | builds the probes with the offline harness of `../lmo-bandfilter/` and runs `measure.py` |
| `measure.py` | renders every channel and prints the table above |

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).
