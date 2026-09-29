# stunedrev block 3: the chain

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../stunedrev-allpass/` and `../stunedrev-delays/`.

```faust
sdt.stmd(k,i)   = int(100*(i+1)*k*ma.SR/1000) + 150;
sdt.stline(k,ms) = seq(i, 42, sjm.apfv(stmd(k,i), stdel(k,i,ms), 1/sqrt(2)));
sdt.stunedrev(t1,t2,t3,t4) = stline(√2,t1), stline(φ,t2), stline(e,t3), stline(π,t4);
```

Four independent lines, one per face of STONED, each with its own step: the ratios √2, φ, e, π that Davide Tedesco and Giuseppe found together, starting at 83, 47, 7 and 71 ms in the Pd patch.
The lineage is Giuseppe's in-phi-rev (*Canto alla durata*): one ratio, φ, 81 sections in Schroeder's form.

## Identity with the original
`./run.sh` builds one probe per line (SEAM, Davide's `stunedrev.dsp` verbatim but for the slider, and Davide's `apf` fed with SEAM's delays) and runs `check.py` on 220 000 samples of noise at 96 kHz, in double:

| line | structure: `sdt.stline` against Davide's `apf`, same delays, starting time | original: against `stunedrev.dsp`, primes agreeing | original at the starting time |
|---|---|---|---|
| √2 | 0 (83 ms) | 0 (17 ms) | 1.84e-06 (83 ms) |
| φ | 0 (47 ms) | 0 (3 ms) | 0 (47 ms) |
| e | 0 (7 ms) | 0 (19 ms) | 0.000868 (7 ms) |
| π | 0 (71 ms) | 0 (7 ms) | 0 (71 ms) |

The structure is Davide's sample for sample, and so is the whole line wherever SEAM's rounding and his truncation give the same primes.
At the starting times 7 of the 168 sections take another prime (`../stunedrev-delays/`), hence the last column; on π the one section that changes (40, 9.1 s) has not yet spoken within the 2.3 s of the test.
The structural check goes red with g = 0.7 (1.81e-06 on line √2).

## A long memory
An all-pass section returns all the energy it receives, delayed on average by exactly its t: the sum of n·t·(1−g²)²·g^(2n−2) over the recirculations is t, for any g.
In series, the energy of a line arrives on average after the sum of its 42 delays, and the direct path is (−g)^42, −126 dB.
Impulse responses at the starting times, 150 s:

| line | ms | longest section | sum of the delays | measured centroid | energy by 10 s | 30 s | 60 s | 150 s |
|---|---|---|---|---|---|---|---|---|
| √2 | 83 | 4.93 s | 106.0 s | (tail beyond 150 s) | 0.000 | 0.001 | 0.027 | 0.943 |
| φ | 47 | 3.19 s | 68.7 s | 68.63 s | 0.000 | 0.006 | 0.319 | 1.000 |
| e | 7 | 0.80 s | 17.2 s | 17.19 s | 0.032 | 0.995 | 1.000 | 1.000 |
| π | 71 | 9.37 s | 201.4 s | (tail beyond 150 s) | 0.000 | 0.000 | 0.001 | 0.145 |

stunedrev is a memory more than a reverberation: what enters returns diffused after 17 seconds on the e face and after more than three minutes on the π face.
Intended (Giuseppe, 2026-09-29): each face returns the sound on its own time scale.

## Memory
`memory.sh` sums the arrays that the generated C++ declares (Faust 2.88, `-double`):

| DSP | memory |
|---|---|
| Davide's `stunedrev.dsp` | 3.94 GiB |
| `sdt.stunedrev` | 1.66 GiB |
| `sdt.stunedrev`, `-dlt 4096` | 1.15 GiB |
| C++ sized exactly at 96 kHz | 588 MiB |

The original gives each of the 42 sections of a line the buffer of the longest (`SRM·ma.SR`, 6 to 14 s), sized for 192 kHz, rounded to a power of two, and made resident when `instanceClear` zeroes it; the Pd external (Faust 2.72.14, `-vec -double`) allocates 15.2 GiB.
`sdt.stmd` sizes each section on its own longest delay.
Faust still fixes the size at compile time, from ma.SR's bound of 192 kHz and a power of two; `-dlt 4096` removes the power of two, output unchanged: the probes of this study are built with it.
The C++ plugin sizes each section in `prepare` at the session's rate.

## Listening
`render.py` runs the contrabass clarinet's low C (`../delrm-comb/renders/ccb_dry.wav`, committed) through the four lines at the starting times, each until its output holds 99.9 % of the input's energy or 600 s, with one gain for the four so that their levels compare.

```bash
./run.sh                                          # builds the probes, runs the checks (about 2 minutes)
../../../../.venv/bin/python render.py            # about 4 minutes, 330 MB of WAV
../../../../.venv/bin/python render.py --times 83 47 7 71 --max 600 SOURCE.wav
```

The WAVs are not committed; `renders/render-log.md` is, and lists what the script wrote on Giuseppe's machine:

| file | line | ms | length s | input energy returned | energy centroid s |
|---|---|---|---|---|---|
| `stunedrev_sqrt2_83ms.wav` | √2 | 83 | 327.8 | 99.9 % | 106.6 |
| `stunedrev_phi_47ms.wav` | φ | 47 | 171.1 | 99.9 % | 70.5 |
| `stunedrev_e_7ms.wav` | e | 7 | 44.4 | 99.9 % | 19.0 |
| `stunedrev_pi_71ms.wav` | π | 71 | 603.8 | 99.8 % | 202.2 |

The centroids are the sums of the delays plus the note's own centroid, about 1.5 s into the file.

## Files
| file | what it is |
|---|---|
| `probe.dsp` | the four lines in SEAM (`sq ph ex pi`), in Davide's original (`osq …`), Davide's `apf` with SEAM's delays (`dsq …`), and the mutation (`mut`) |
| `run.sh` | builds the probes with the harness of `../lmo-bandfilter/` (Davide's `nextprime.h` from `../stunedrev-delays/`), runs `check.py` and `memory.sh` |
| `check.py` | identity, mutation and energy tables |
| `memory.sh` | memory of the generated C++; `DAVIDE=path` points at `stunedrev.dsp` |
| `render.py` | the listening renders and `renders/render-log.md` |

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`); about 2 GB of free memory for the probes.
