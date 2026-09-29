# stunedrev block 2: the delays

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../stunedrev-allpass/`.

Section i (0 to 41) of the line of ratio k (√2, φ, e, π) delays by the slider's time multiplied by (i+1)·k, moved to a prime number of samples.
Davide Tedesco's chain:

```
ms (slider 1–100) → /1000 → ba.sec2samp → ·(i+1)·k → int (truncation) → next_pr → apf(t)
```

SEAM's (faust-libraries fa12cae):

```faust
sma.ms2npsamp(ms) = select2(n < 2, n : sff.np, n) with { n = int(floor(ms*ma.SR/1000 + 0.5)); };
sdt.stdel(k,i,ms) = sma.ms2npsamp(ms*(i+1)*k);
```

The product comes before the prime, so every section gets a prime of its own.

## Why primes
Before the prime the 42 delays of a line are a harmonic series, multiples of one step, and share factors: echoes that coincide.
On line √2 at its starting 83 ms, 313 of the 861 pairs share a factor; after the prime none does, and the 42 primes are distinct.
A prime is prime at one rate only, so each rate has its own set, all of them incommensurable: a feature of the SEAM system, kept (Giuseppe).

## Rounding and the next prime
Davide's `nextprime.h` and SEAM's `src/h/nextprime.h` (`sff.np`) give the same prime, strictly above n, for every n from −10 to 3 000 000.
The difference is before them: Davide truncates the float, SEAM rounds it, as `sma.imt2npsamp` and DDELAY do.

That moves 813 of the 16 800 delays of the slider's range (1–100 ms, 42 sections, 4 lines, 96 kHz) to another prime.
A move usually spans a few samples, and reaches a whole prime gap when the rounded value is itself a prime, which `sff.np` steps over:

```
line π, section 30, 92 ms: exact 860142.94 samples
  truncated 860142 → prime > 860142 → 860143
  rounded   860143 → prime > 860143 → 860239     (860143 is prime: stepped over, 96 samples, 1 ms)
```

The alternative, the smallest prime not below the exact value, was weighed and set aside: Giuseppe kept `sff.np` for the coherence of SEAM, and with it the delay is always longer than the exact time, by at least half a sample.

At the starting times of the performance, 7 of the 168 sections change:

| line | ms | sections that change: i (Davide → SEAM) |
|---|---|---|
| √2 | 83 | 10 (123953 → 123973), 23 (270443 → 270451), 27 (315517 → 315521) |
| φ | 47 | none |
| e | 7 | 3 (7307 → 7309), 12 (23747 → 23753), 35 (65761 → 65777) |
| π | 71 | 40 (877937 → 877939) |

## No two sections of a line share a prime, under any rule

| rule | duplicates within a line | slider pairs of two lines sharing a prime |
|---|---|---|
| SEAM: round, then prime > n | 0 | 2977 of 60 000 (4.96 %) |
| Davide: truncate, then prime > n | 0 | 3006 of 60 000 (5.01 %) |
| round, then prime ≥ n | 0 | 2951 of 60 000 (4.92 %) |

Distinctness within a line is geometric: the nearest sections are 135.8 samples apart (1 ms, √2, 96 kHz) and below 1.3 million no gap between primes exceeds 114, so two sections never fall on the same prime.
Across lines no rule prevents a shared prime; the lines are four separate faces.

## The buffer of each section
`sdt.stmd(k,i) = int(100·(i+1)·k·SR/1000) + 150`: the longest time the slider asks, at the current rate, plus the step to the next prime.
The largest gap between primes below the longest delay at 192 kHz (2 533 393 samples) is 148; over every rate from 44.1 to 192 kHz, every time and every section, the delay stays at least 62 samples inside the buffer.

## Files
| file | what it is |
|---|---|
| `dt_nextprime.h` | Davide's `nextprime.h`, functions renamed `dt_*` so that it links beside SEAM's |
| `npcmp.c` | the two `next_pr` for every n in [−10, 3 000 000]; with `-DMUTATE`, SEAM's without its strictness, which must mismatch |
| `probe.dsp` | the 16 800 delays as 16 800 output samples, `-pn seam` (`sdt.stdel`) and `-pn dav` (Davide's chain) |
| `run.sh` | runs both comparisons of `npcmp`, builds and runs the probes at 96 kHz, runs `check.py` |
| `check.py` | the independent reference and every table above |

Requirements as for `../lmo-bandfilter/` (faust, a C and C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).
