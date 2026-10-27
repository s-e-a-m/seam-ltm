# INPHIREV

INPHIREV is a golden all-pass memory for *canto alla durata* (Giuseppe Silvi), first for the *Dialogo Angelico* anapnoe · tempo (clarinet and electromagnetic timpani).
TETRAREC picks up the room in A-format, each capsule feeds one face, and each face of STONE plays one chain of Schroeder all-pass sections whose delays follow a single golden spiral.
The first delay is the acoustic distance from STONE to TETRAREC; every later delay is a power of φ above it, moved to the next prime.
The name comes from the old Faust experiments `in-phi-rev`, ancestor of this idea and of STUNEDREV.
Inputs and outputs 1–4 are the faces A, B, C, D, in the order of STUNEDREV.

It is an exploratory instrument, not a mapping from playing to processing.
The sound is chosen at the soundcheck by three settings (STONE id, distance, sections); the send level is the mixer's.

## The Dialogo's setup

Two instruments, timpani and contrabass clarinet with mute, and two STONEs facing each other.
Each STONE is the voice of one instrument's reverberation only, placed behind the other instrument.
There are two instances of the plugin, one per instrument: each is fed by that instrument's TETRAREC and plays into the opposite STONE.
The distance an instance uses runs from the centre of the TETRAREC that feeds it to the centre of the STONE that plays its output; the distance between the two STONEs plays no part.
The two distances are 3–4 m and deliberately different, and the two instances take STONE ids 1 and 2.
The input closes at the end of the descent A = C/φ ≈ 6′11″ (371 s) of the first formal level C = 10′; the ascent B = C/φ² ≈ 3′49″ (229 s) is carried by the two tails, asynchronous and independent.
They must die with the form: still audible through B, below −30 dB at its end.

## Parameters

| Parameter | Zone | Range | Default | What it does |
|---|---|---|---|---|
| STONE | SETUP | ?, 1–8 | ? | which STONE's distance grid (below); "?" is undeclared and uses the phase of id 1 |
| distance | SETUP | 15 grid points, 1–6 m | 3.330 m (point 10) | acoustic path from TETRAREC to the STONE, on the grid of the id |
| sections | SETUP | 1–18 | 17 | all-pass sections per face |
| POWER | OPS | off / on | off | fades the output in or out over 25 ms; the faces keep running |
| RESET | OPS | button | — | empties the memory (see below) |

There is no input or output gain: the send and the return belong to the mixer.
The distance is stored as the grid index j, so changing the STONE id keeps j and moves to the neighbouring point of the new grid; the distance text follows at once.
The duration is not a control: the window prints it as the consequence of the settings.

## The spiral

One geometric sequence runs through the four faces in turn, A B C D A′ B′ C′ D′ …

```
d(i, k) = d₀ · φ^(i + k/4)        i = 0 … N−1 (section), k = 0 … 3 (face A … D)
```

Adjacent faces stand φ^¼ apart (12.8 %), and the step from D to A′ is the same ratio as from A to B.
The four sets of delays are disjoint by construction.
That matters because a cascade of all-pass sections is commutative: what decorrelates two faces is their set of delays, not their order.
Each delay is the prime strictly above the exact product, never φ times the previous prime, so the rounding does not accumulate along the sequence.
With i = 0 and k = 0 the delay is the acoustic path at 20 °C (343.2 m/s), rounded to the sample and moved to the prime above, as DDELAY does.

Each extra section is as long as all the previous ones together (Σφⁱ for i < n = φⁿ⁺¹ − φ), so the total delay roughly multiplies by φ at each step.
The longest delay is about 90 s.

## The distance grid

Two instances whose distances stand in a ratio φ^(j/4) play the same spiral shifted by j faces: the same primes, the same echoes, no longer independent.
What decides it is the phase on the spiral, u = 4·log_φ(d) mod 1: equal phases coincide whatever the distances.
So each STONE id has a fixed phase, and the distance moves only on that id's grid, in steps of φ^¼ anchored at 1 m:

```
d(id, j) = φ^((j + u_id)/4)        u = 0, ½, ¼, ¾, ⅛, ⅝, ⅜, ⅞  for id 1 … 8
```

The phases follow the van der Corput order: the first M ids stand as far apart as possible for any M.
With two STONEs (the Dialogo) the phases are 0 and ½ and the eight faces form one spiral of eight voices in steps of φ^⅛; with four the sixteen faces are equidistant.
The grid between 2 and 6 m:

| id | phase | distances (m) |
|---|---|---|
| 1 | 0 | 2.058 · 2.321 · 2.618 · 2.953 · 3.330 · 3.756 · 4.236 · 4.778 · 5.388 |
| 2 | ½ | 2.186 · 2.465 · 2.780 · 3.136 · 3.537 · 3.989 · 4.499 · 5.074 · 5.722 |
| 3 | ¼ | 2.121 · 2.392 · 2.698 · 3.043 · 3.432 · 3.871 · 4.365 · 4.923 · 5.553 |
| 4 | ¾ | 2.253 · 2.540 · 2.865 · 3.232 · 3.645 · 4.111 · 4.636 · 5.229 · 5.897 |

The STONE is placed on a grid point with the laser; the grid value, not a measurement, is the distance the plugin uses.

### Limit of the grid

Ids 1 and 2 never share a delay at 44.1, 48, 96 and 192 kHz, for any pair of their grid points; a test asserts it (225 pairs per rate), and it covers the Dialogo.
Other pairs of ids, whose phases are ⅛ or ¼ apart, can share a few delays after the rounding to the next prime; the test "other id pairs: shared delays are counted" prints the counts, and at some rates they are many (at 48 kHz, ids 2 and 7, 3 and 5, and 4 and 8 share a delay for all 225 pairs of grid points).
That is harmless for two STONEs and matters from four on.
A possible remedy is a proposal for Giuseppe and is not implemented: each id takes its next prime from its own residue class mod 16, so that two ids can never land on the same prime.

## Why g = 1/φ

A Schroeder section is all-pass for every |g| < 1; g only decides how the energy is distributed in time: g² in the direct term, 1 − g² in the echoes.
At g = 1/φ the golden identity 1 − g² = g makes the split itself golden: the direct path carries 1/φ² of the energy and the echoes 1/φ, and the comb and the direct path have the same gain in Schroeder's reading.
The effective number of echoes, 1/Σpₖ², peaks at g = √(√2 − 1) ≈ 0.6436, and 1/φ is within 1 % of that peak, so any level-driven excursion of g could only lower the diffusion; a time-varying g is also not all-pass while it moves.
The code uses Moorer's form (`sjm.apfv`, `Seam::MoorerAllpass`), with the same transfer function: its single multiplier keeps the section all-pass after rounding for any g, which matters in a memory that lasts minutes.

## The printed decay

The footer prints, per face A–D, the time to −30 dB and to −60 dB after the input closes, then the first and the longest delay, then the arena and the status.
The delays are exact, read from the table.
The decay is an estimate: the GUI side (never the audio thread) convolves the energy distributions of the sections on a grid of 0.1 s, taking the echoes as incoherent, and integrates backwards.
At 3.330 m (id 1, j 10) with 17 sections the estimate gives −30 dB at A 201 s and D 289 s, −60 dB at A 355 s and D 509 s.

**Measured: the printed estimate runs about 10 % long.**
Against the decay measured on the engine itself (noise in, input closed, backward-integrated output energy), at (1, 10, 12) and 48 kHz the −30 dB times, estimated against measured, are 18.1/16.5, 20.4/18.4, 23.0/20.7 and 26.0/23.4 s on faces A, B, C, D.
The cause is the incoherent-echo assumption.
The real tails are about 10 % shorter than the footer says: read the printed time as an upper bound and time the tails at the soundcheck.

| distance | −30 dB, face A / D (printed) | at +229 s, A / D |
|---|---|---|
| 3.136 m (id 2) | 189 s / 272 s | −38 dB / −24 dB |
| 3.330 m (id 1) | 201 s / 289 s | −35 dB / −22 dB |
| 3.537 m (id 2) | 214 s / 307 s | −33 dB / −20 dB |

With 17 sections at 3.1–3.5 m the faces cross −30 dB around the end of B: A dies first, D last, so each STONE goes out face by face as the form closes.
Sixteen sections would be at −33 dB on face A halfway through B, hence the default of 17 and the cap of 18.

The footer follows the applied settings (below), not the pending ones; the SETUP menus show the pending value at once.

## A change of setting

Distance, STONE id and sections are soundcheck values, not gestures.
A new value is applied only once it has been stable for 250 ms, so dragging the distance across grid points, or a burst of automation, makes one reset at the end with the latest value.
Applying a change recomputes the delay table and runs the RESET sequence, because a new delay or a newly enabled section would otherwise play the stale contents of its buffer.
A host that automates these parameters gets a reset at every change.

### RESET
RESET fades the output out over 25 ms, zeroes the memory a slice per audio block while the output stays silent and the input is ignored, and fades back in if POWER is on; a second click during the clearing restarts it.
It is a GUI-only button, as in STUNEDREV: not a parameter, not automatable, not in a preset.

## Sample rate and memory

The delays are metres converted at the session's rate, so the primes differ from rate to rate, as in every plugin of the family.
One arena of `double`, allocated and zeroed when the host activates the plugin, never in the audio thread, sized for the worst case (the largest grid point, 18 sections), each section exactly for its longest delay plus one: 289 MiB at 48 kHz, 577 MiB at 96 kHz, about 1.1 GiB at 192 kHz.
If the memory cannot be allocated, the plugin stays silent and the footer says "allocation failed"; the host keeps running.
The footer's status reads "STONE id undeclared" while the id is "?".

## State

The state is a bare array of doubles: power, stoneId, grid, sections.
It follows the append-only contract of `seam_state.h`; a stream that stops short (older or truncated) keeps the defaults for what is missing.

## Specification

The DSP is `sgs.inphirev(N, id, j)` of `seam.silvi.lib` (faust-libraries), written again by hand in `source/inphirev_dsp.h`:

```
ipgrid(id, j)  = phi^((j + u_id)/4);                                     // seam.silvi.lib
ipdel(d, i, k) = sff.np(n) with { n = floor(SR*d*phi^(i + k/4)/c + 0.5); };
ipline(N, k, d) = seq(i, N, sjm.apfv(ipmd(i,k), ipdel(d,i,k), sma.rphi(1)));
inphirev(N, id, j) = four lines in parallel at ipgrid(id, j), face k on channel k;
```

The ancestor `apphirev(N,t) = seq(i,N, sapf(int(t*(i+1)*phi),0.708))` (2020, `faust-libraries/temp/seam.reverbs.lib`) stays where it is: linear progression, Schroeder's form, g = 0.708, no primes.
The speed of sound is `Seam::speedOfSound(T)` = 331.3·√(1 + T/273.15) at 20 °C, additive to `seam.math.lib`; `isos` and `esos` are untouched (seam-ltm issue #15).

The tests (`tests/inphirev_dsp_test.cpp`, `inphirev_decay_test.cpp`, `inphirev_params_test.cpp`, `inphirev_state_test.cpp`) check the delay table of every id and grid point at 44.1, 48, 96 and 192 kHz against the Faust reference exactly, the engine against `sgs.inphirev` at 96 and 48 kHz, the change of setting, the arena, RESET, the parameters and state, and the decay estimate against the engine.
Every test was verified by mutation; the record, the reference generator and the CPU measure are in `doc/study/inphirev/`.

## Cost

At 18 sections (72 per sample, the maximum), 256-sample blocks, Intel i7-8850H at 2.6 GHz, `c++ -O2`: 3.6 % of a core at 96 kHz and 1.6 % at 48 kHz (3.330 m), 3.5 % at 96 kHz at the longest grid point (5.388 m, reads scattered over the whole 577 MiB), and 3.3 % at 96 kHz after ten minutes of silence, so the flush of subnormals holds.
The measure is `doc/study/inphirev/cpu.cpp`, the engine alone without the host.

## Building and status

Developed on a machine without Xcode: the sources compile (processor and views as one translation unit) and the four test programs pass when built directly with `c++`.
The bundle build (`cmake -G Xcode`), the VST3 validator and the full `ctest` are to be run by Giuseppe, with the host check of the spec (two instances, ids 1 and 2, opposed STONEs on grid points, tails timed against the footer, a screenshot for `docs/img/inphirev.png`).
`make -C doc publish` waits for that check.

## Out of scope

Adaptive g and envelope followers; the denser spiral (step √φ or φ^¼ per section, the next version, which also changes the grid); temperature as a setting; B-format processing; the Pd external.
