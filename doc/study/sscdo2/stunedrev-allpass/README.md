# stunedrev block 1: the all-pass

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`); the chain it builds is in `../stunedrev-chain/`, its delays in `../stunedrev-delays/`.

stunedrev descends from Giuseppe Silvi's in-phi-rev (*Canto alla durata*, `gitlab/gs/canto-alla-durata/src/faust/in-phi-rev/`), an all-pass reverberator in Schroeder's form tuned by φ.
Davide Tedesco's `apf` is Moorer's form instead, `sjm.apf` of `seam.moorer.lib` with the buffer as a parameter:

```faust
// seam.moorer.lib
apf(t,g,x)     = (x+_ : *(-g) <: _+x,_ : de.delay(ma.SR,    t-1),_)~(0-_) : mem+_;
// stunedrev.dsp
apf(SRM,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(SRM*ma.SR,t-1),_)~(0-_) : mem+_;
```

Schroeder's library already had the variable form `sms.apfv(md,t,g)`; Moorer's now has `sjm.apfv(md,t,g)` (faust-libraries dec24d8), and `sjm.apf` is `apfv(ma.SR,t,g)`.
stunedrev uses `sjm.apfv`.

## Two ways of writing the same filter
Both forms compute H(z) = (−g + z^−t) / (1 − g·z^−t).

**Schroeder (1962)** sums a direct path −g and a comb scaled by 1 − g²:

```faust
apf(t,g) = _ <: *(-g) + (dflc(t,g)*(1-(g*g)));
```

It reads as the derivation: a comb has a spectrum of teeth, and the direct path of the right weight and opposite sign makes its magnitude flat.
It needs three coefficients (−g, g, 1 − g²), and the magnitude is flat as long as they cancel exactly.

**Moorer (1979, fig. 1a)** writes one loop with one multiplier: the input enters the line, is multiplied by −g once, and the same quantity feeds both the output and the recirculation.
It reads as the computation.
It is all-pass by structure for whatever value g takes after rounding, costs one multiplication per section instead of three (168 against 504 per sample for stunedrev's four lines), and its delay can be replaced by another all-pass (`sjm.apfo`, nesting).

Both libraries keep the delay at t − 1 followed by a `mem`, so that t is the true delay once `~` has added its sample.

## The check
`./run.sh` builds the five forms as chains of 42 sections with stunedrev's delays (line e at 7 ms, its starting time; md from `sdt.stmd`) and runs `check.py`: impulse responses at 96 kHz, 60 s, the whole energy of the line.

| form | max abs difference from `sjm.apfv` (double) |
|---|---|
| Davide's `apf` | 0 |
| `fi.allpass_comb(md,t,-g)` | 5.82e-11 |
| `sms.apfv` (Schroeder) | 5.82e-11 |
| in-phi-rev `apf` (Schroeder) | 5.82e-11 |
| mutation: `sjm.apfv` with −g | 0.0311 |

Davide's `apf` is `sjm.apfv` sample for sample.
The standard `fi.allpass_comb` is the same filter with the gain of opposite sign: its feed-forward coefficient is +aN, Moorer's −g.
The standard and the Schroeder forms agree to rounding, which grows with the recirculation: `fi.allpass_comb` differs by 2.7e-16 over the first 20 000 samples, all three by 5.8e-11 over 60 s.
Energy of the chain: 0.99993 in 60 s, centroid at 17.18 s.

### Float against double
The VST computes in double; the question matters for Faust compiled in float, the online IDE's default.

| form | float: max abs difference from its double | energy lost in float | max abs spectrum deviation, dB |
|---|---|---|---|
| Moorer | 8.85e-09 | −5.38e-08 | 0.000236 |
| Schroeder | 1.12e-08 | +1.43e-06 | 0.000081 |

In float Schroeder's form loses 26 times more energy than Moorer's, which is the cancellation of its three coefficients; both stay within 0.00025 dB of their double spectrum over 42 sections.
The structural advantage of Moorer's form is real and inaudible here: the choice rests on the lineage of Davide's code, the single multiplier and the nesting.

## Files
| file | what it is |
|---|---|
| `probe.dsp` | the five forms and the mutation, each as a chain of 42 sections (`-pn moorer`, `dav`, `std`, `sch`, `inphi`, `mut`) |
| `run.sh` | builds every probe in double, Moorer and Schroeder also in float, with the harness of `../lmo-bandfilter/`, and runs `check.py` |
| `check.py` | the impulse responses and the two tables above |

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).
