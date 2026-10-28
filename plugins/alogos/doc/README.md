# ALOGOS

ALOGOS is an irrational all-pass memory for *canto alla durata* (Giuseppe Silvi).
TETRAREC picks up the room in A-format, each capsule feeds one face, and each face of STONE plays one chain of Moorer all-pass sections whose delays follow one geometric spiral.
The first delay is the acoustic distance from the TETRAREC to the STONE; every later delay is a power of the scale's ratio above it, moved to the next prime.
Inputs and outputs 1–4 are the faces A, B, C, D.

The name is the Greek *alogos*, the Pythagorean word for the incommensurable: the four scales φ, √2, e and π are all *aloga*, and *a-logos* is also "without word", a field that speaks without words.
Like STUNEDREV, it is a long memory more than a reverberation, so the name carries no "REV".

ALOGOS replaces INPHIREV (v0.6.0), the golden spiral it generalises: with the default settings it is INPHIREV, delay for delay, and a test holds it to INPHIREV's table at 44.1, 48, 96 and 192 kHz.
INPHIREV left the suite in v0.7.0; the v0.6.0 release keeps it.

It is an exploratory instrument, not a mapping from playing to processing.
The sound is chosen at the soundcheck by six settings; the send level is the mixer's.
The idea is resonant fields of different character, one per STONE, that talk to each other in the hall: what the plugins do not separate, the hall and the spaced pickup will.

## Parameters

| Parameter | Zone | Range | Default | What it does |
|---|---|---|---|---|
| STONE | SETUP | ?, 1–8 | ? | which STONE's distance grid (below); "?" is undeclared and uses the phase of id 1 |
| distance | SETUP | 1–6 m, typed | 3.33 m | acoustic path from the TETRAREC centre to the STONE centre, snapped to the grid of the id |
| sections | SETUP | 1–128, typed | 17 | all-pass sections per face, limited to N_max (below) |
| scale | SETUP | φ, √2, e, π | φ | the ratio r of the spiral |
| density | SETUP | 1, 2, 4 | 1 | sections per factor r: the step between sections is ρ = r^(1/D) |
| g | SETUP | 1/φ, 1/√2, 1/e, 1/π, √(√2−1) | 1/φ | the all-pass gain of every section |
| POWER | OPS | off / on | off | fades the output in or out over 25 ms; the faces keep running |
| RESET | OPS | button | — | empties the memory (see below) |

The window is the L format of the suite: SETUP holds the six settings in three columns, STONE, distance and sections in the first row, scale, density and g in the second.
Distance and sections are typed fields, because a continuous distance and 128 counts make no usable menu.
The distance field shows the grid point the engine takes, and the sections field shows the effective count, followed by "(max N)" when the count asked is above N_max.
There is no input or output gain: the send and the return belong to the mixer.
The duration is not a control: the window prints it as the consequence of the settings.

## The spiral

One geometric sequence runs through the four faces in turn, A B C D A′ B′ C′ D′ …

```
ρ        = r^(1/D)                       r ∈ {φ, √2, e, π},  D ∈ {1, 2, 4}
d(i, k)  = d₀ · ρ^(i + k/4)              i = 0 … N−1 (section), k = 0 … 3 (face A … D)
```

Adjacent faces stand ρ^¼ apart, and the step from D to A′ is the same ratio as from A to B.
The scale decides where the echoes return; the density sets how finely the spiral climbs, D sections for every factor r.
INPHIREV is the case r = φ, D = 1.
A cascade of all-pass sections is commutative: what decorrelates two faces is their set of delays, not their order.
Each delay is the prime strictly above the exact product, never ρ times the previous prime, so the rounding does not accumulate along the sequence.
With i = 0 and k = 0 the delay is the acoustic path at 20 °C (343.2 m/s), rounded to the sample and moved to the prime above, as DDELAY does.

## The role of g

A Moorer section is all-pass for every |g| < 1; g decides how the energy is spread in time.
The direct term carries g² of the energy and the echoes the rest: 38 % passes at once with 1/φ, 50 % with 1/√2, 14 % with 1/e, 10 % with 1/π.
g is a menu of its own, independent of the scale, so a scale can take its own g (π with 1/π) or another (π with 1/φ).

The effective number of echoes of a section, 1/Σpₘ² over the energies pₘ of its taps, peaks at g = √(√2 − 1) ≈ 0.6436, at 3.188.
1/φ reaches 99.05 % of that peak, 1/√2 94 %, 1/e 53 % and 1/π 47 %.
The small g send most of their energy into the first echo, so the field they make is sparser.

g is fixed per setting: a moving g is not all-pass while it moves.
The code uses Moorer's form (`sjm.apfv`, `Seam::MoorerAllpass`), whose single multiplier keeps the section all-pass after rounding for any g, which matters in a memory that lasts minutes.

## The energy argument

In a Moorer section the energy leaves on average exactly one delay later, whatever g: Σ m (1−g²)² g^(2(m−1)) = 1.
So the length of the tail is set by the longest sections, almost independently of the scale.
The scale and the density set three other things: how much the tail changes from one section to the next (×ρ), the density of the echoes, and the cost in memory and CPU.

## The distance grid

Two instances whose distances stand in a ratio ρ^(j/4) play the same spiral shifted by j faces: the same primes, the same echoes, no longer independent.
What decides it is the phase on the spiral, u = 4·D·log_r(d) mod 1: equal phases coincide whatever the distances.
So each STONE id has a fixed phase, and the distance moves only on that id's grid, anchored at 1 m:

```
d(id, j) = r^((j + u_id)/(4·D))     u = 0, ½, ¼, ¾, ⅛, ⅝, ⅜, ⅞  for id 1 … 8
```

The phases follow the van der Corput order: the first M ids stand as far apart as possible for any M.
The grid follows the scale and the density, and the ids stay apart at every density.
The grids of one id do not nest across densities, and cannot while the ids stay apart: a nested grid would turn the phase u into D·u mod 1 and collapse ids 1 and 2 at D = 2, ids 1–4 at D = 4.

The distance parameter is stored in metres, so a change of id, scale or density keeps the STONE where it stands and moves it to the nearest point of the new grid, nearest in ratio, at most half a step away (for φ about 6 % at D = 1, 3 % at D = 2, 1.5 % at D = 4).
The STONE is placed on a grid point with the laser; the grid value, not a measurement, is the distance the plugin uses.
The golden grid at D = 1 between 2 and 6 m, the one of INPHIREV:

| id | phase | distances (m) |
|---|---|---|
| 1 | 0 | 2.058 · 2.321 · 2.618 · 2.953 · 3.330 · 3.756 · 4.236 · 4.778 · 5.388 |
| 2 | ½ | 2.186 · 2.465 · 2.780 · 3.136 · 3.537 · 3.989 · 4.499 · 5.074 · 5.722 |
| 3 | ¼ | 2.121 · 2.392 · 2.698 · 3.043 · 3.432 · 3.871 · 4.365 · 4.923 · 5.553 |
| 4 | ¾ | 2.253 · 2.540 · 2.865 · 3.232 · 3.645 · 4.111 · 4.636 · 5.229 · 5.897 |

## The sections and N_max

The cap is a time, not a count: N_max is the largest N whose longest delay (face D, section N − 1) lasts no more than 90 s at the snapped distance.
At 90 s the estimated T30 is about fifteen minutes, far beyond the form.
The window shows the effective count, min(N, N_max), and the maximum when the count is limited.
The count asked is stored as it is, so a round trip through a scale with a smaller N_max gives it back.

## The table of the scales

| scale | D | grid points | N_max at 3.33 m | heaviest setting | arena 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|---|---|
| φ | 1 | 15 | 19 (3.330 m) | 1.413 m, N 21 | 289 MiB | 577 MiB | 1155 MiB |
| φ | 2 | 29 | 38 (3.330 m) | 1.046 m, N 43 | 561 MiB | 1121 MiB | 2242 MiB |
| φ | 4 | 59 | 76 (3.330 m) | 1.019 m, N 86 | 1109 MiB | 2217 MiB | 4435 MiB |
| √2 | 1 | 20 | 26 (3.364 m) | 1.022 m, N 30 | 395 MiB | 790 MiB | 1579 MiB |
| √2 | 2 | 41 | 52 (3.364 m) | 1.170 m, N 59 | 777 MiB | 1554 MiB | 3108 MiB |
| √2 | 4 | 82 | 105 (3.364 m) | 1.050 m, N 119 | 1537 MiB | 3075 MiB | 6149 MiB |
| e | 1 | 7 | 9 (3.490 m) | 1.755 m, N 10 | 145 MiB | 290 MiB | 581 MiB |
| e | 2 | 14 | 18 (3.490 m) | 1.573 m, N 20 | 278 MiB | 555 MiB | 1111 MiB |
| e | 4 | 28 | 36 (3.279 m) | 1.160 m, N 41 | 543 MiB | 1086 MiB | 2171 MiB |
| π | 1 | 6 | 8 (3.142 m) | 1.331 m, N 9 | 128 MiB | 256 MiB | 511 MiB |
| π | 2 | 12 | 16 (3.142 m) | 1.175 m, N 18 | 243 MiB | 486 MiB | 971 MiB |
| π | 4 | 25 | 32 (3.375 m) | 1.103 m, N 36 | 473 MiB | 946 MiB | 1893 MiB |

Grid points count the points of one id within 1–6 m.
N_max is taken at 3.33 m snapped on the grid of id 1, whose point is shown in brackets.
The arena is the largest of that scale and density: the maximum, over every STONE id and every grid point, of the memory at N = N_max, each section exactly its delay + 1 doubles.
The heaviest setting is the one that reaches that maximum at 96 kHz.
The table is computed by `doc/study/alogos/arena.cpp`, with the engine's own functions.
The default setting (φ, D 1, 3.330 m, N 17) holds 99 MiB at 48 kHz and 198 MiB at 96 kHz.

## Contacts between STONEs

Before rounding, two ids never share a delay at any scale and density: their delays lie on one lattice with different phases.
After rounding to the next prime they can, only where the lattice is denser than the integers, among the short delays.
With four STONEs (ids 1–4) at 96 kHz, the longest delay two of them share is 3.2 ms at φ D = 1, 14.2 ms at φ D = 2, 24.3 ms at φ D = 4, 56 ms at √2 D = 4, and at most 14.2 ms for every density of e and π.
Ids 1 and 2 at φ, D = 1 share no delay at any rate, for every pair of their grid points (INPHIREV's property, asserted by a test).
Above a few tens of milliseconds the fields are disjoint, so the tails never share a delay.
Two fields of different scales lie on incommensurable lattices and can only touch by rounding.

Within one instance, the dense spirals share some delays between their own sections, for the same reason: the short delays saturate the integers.
At 96 kHz, over every grid point of ids 0–4 at N_max, √2 at D = 4 has 344 sections that repeat a delay of their own instance (in 218 of 410 settings, at most 5 in one setting, all at or below 14.2 ms), φ at D = 4 has 20 (at most 2 in one setting, at or below 3.5 ms), and the other scales and densities none.
At 48 kHz the integers are coarser: √2 at D = 2 has 36, φ at D = 4 375 and √2 at D = 4 1699 (at most 16 in one setting, at or below 28.4 ms).
The contacts are accepted: the hall and the spaced pickup will do the rest.
`alogos_dsp_test` prints the figures per scale and density.

## The printed decay

The footer prints, per face A–D, the time to −30 dB and to −60 dB after the input closes; then the first delay (face A) and the longest (face D), with the effective sections and N_max; then the arena and the status.
The delays are exact, read from the engine's table.
The decay is an estimate: the GUI thread, never the audio thread, convolves the energy distributions of the sections with the setting's g on a grid of 0.1 s, taking the echoes as incoherent.
Against the decay measured on the engine (stationary noise in, input closed, the output level averaged over 0.5 s), the printed −30 dB time agrees within 1.4 % for (φ, D 1, 1/φ) at N 12 and (π, D 1, 1/π) at N 8, on all four faces.

INPHIREV's README said its estimate ran about 10 % long.
That came from the measurement, not from the estimator: it integrated the tail energy backwards, which crosses −30 dB early, a second moment of the tail where the footer prints its level.
The estimator is unchanged; the test now measures the level and holds the two within 5 %.

At the defaults (φ, D 1, 1/φ, 3.330 m, N 17) the footer reads −30 dB at A 201 · B 227 · C 256 · D 288 s and −60 dB at A 355 · B 401 · C 452 · D 510 s, first A 9.8 ms, last D 30.7 s.

## A change of setting

The six settings are soundcheck values, not gestures.
A new value is applied only once it has been stable for 250 ms, so a burst of typed or automated changes makes one reset at the end with the latest value.
Applying a change recomputes the delay table and runs the RESET sequence, because a new delay or a newly enabled section would otherwise play the stale contents of its buffer.
An identical table (same delays, same N, same g) skips the clear: "?" to id 1, both of phase 0, moves only the readouts.
A change of g alone clears, because the stored state belongs to the old g.
A host that automates these parameters gets a reset at every change.

### RESET
RESET fades the output out over 25 ms, zeroes the memory a slice per audio block while the output stays silent and the input is ignored, and fades back in if POWER is on; a second click during the clearing restarts it.
It is a GUI-only button, as in STUNEDREV: not a parameter, not automatable, not in a preset.

## Sample rate and memory

The delays are metres converted at the session's rate, so the primes differ from rate to rate, as in every plugin of the family.
The memory is one arena of `double` sized for the setting in use, each section exactly its delay + 1, from a few MiB to about 3 GiB at 96 kHz (the table above).
The audio thread never allocates or frees it.
When the host activates the plugin, the arena of the current setting is allocated and zeroed there, outside the audio thread.
When a settled setting needs more than the arena holds, or less than half of it, a worker thread owned by the plugin allocates and zeroes the new arena while the output is silent, the audio thread takes it over through an atomic hand-over, and the worker frees the old one.
While the new arena is prepared the output stays silent and the footer says "allocating".
During a change the peak memory can reach the old arena plus the new one.

There is no memory budget: each instrument has its own electroacoustic chain and its own computer, so one ALOGOS runs per machine, and the heaviest settings are accepted and documented rather than refused.
If the memory cannot be allocated, the plugin stays silent, the footer says "allocation failed", and the host keeps running; a later setting that fits recovers it.
The footer's status reads "STONE id undeclared" while the id is "?".

The plugin reports an infinite tail: the memory is the second part of the piece, and a host that suspends processing on silent input (Cubase by default, Reaper with auto-bypass) would otherwise freeze it the moment the input closes.

## State

The state is a bare array of doubles: power, STONE id, distance, sections, scale, density, g.
It follows the append-only contract of `seam_state.h`; a stream that stops short keeps the defaults for what is missing.
ALOGOS has a FUID of its own, so a session saved with INPHIREV does not load into it.

## Specification

The DSP is `sgs.alogos(r, D, g, N, id, j)` of `seam.silvi.lib` (faust-libraries), written again by hand in `source/alogos_dsp.h`:

```
algrid(r, D, id, j)  = pow(r, (j + alphase(id))/(4*D));
aldel(r, D, d, i, k) = select2(n < 2, n : sff.np, n)
                       with { n = int(floor(ma.SR*d*pow(r, (i + k/4)/D)/sma.rsos + 0.5)); };
alline(r, D, g, N, k, d) = seq(i, N, sjm.apfv(almd(r, D, i, k), aldel(r, D, d, i, k), g));
alogos(r, D, g, N, id, j) = par(k, 4, alline(r, D, g, N, k, algrid(r, D, id, j)));
```

`sgs.inphirev(N, id, j)` stays in the library as `alogos(sma.phi(1), 1, sma.rphi(1), N, id, j)`.
Faust takes r, D, g and N as compile-time constants and sizes every buffer for the worst case (`almd`); the plugin takes them at run time and sizes one arena for the setting.

The tests (`tests/alogos_dsp_test.cpp`, `alogos_engine_test.cpp`, `alogos_decay_test.cpp`, `alogos_params_test.cpp`, `alogos_state_test.cpp`) check INPHIREV's table, the delay table of every scale and density against `sgs.aldel` exactly, the spiral and the grid, the engine against `sgs.alogos` at 96 and 48 kHz, the change of setting, the arena broker with the audio thread's allocations counted, RESET, the parameters, the state, the footer's strings and the decay estimate against the engine.
Every test was verified by mutation; the record, the reference generator, the arena table and the CPU measure are in `doc/study/alogos/`.

## Cost

256-sample blocks, Intel i7-8850H at 2.6 GHz, `c++ -O2`, the engine alone without the host:

| setting | sections per sample | arena | 48 kHz | 96 kHz |
|---|---|---|---|---|
| default: φ, D 1, 3.330 m, N 17, g 1/φ | 68 | 198.5 MiB at 96 kHz | 1.3 % | 2.8 % |
| default, after ten minutes of silence | 68 | | | 2.7 % |
| CPU measurement setting: √2, D 4, 1 m, N 119, g 1/√2 | 476 | 2928 MiB at 96 kHz | | 21.8 % |

The flush of subnormals holds: the cost after ten minutes of silence is the cost with the input.
The CPU measurement setting runs in real time at 96 kHz with room to spare, so the number of sections per sample has no ceiling beyond N_max.
The measure is `doc/study/alogos/cpu.cpp`, the whole engine as the plugin runs it; the earlier spike (`cpu-spike.cpp`, bare sections without the engine) gave 3.0 % and 29.2 %.

## Building and status

The bundle builds with `cmake -G Xcode`; the VST3 validator and the full `ctest` pass, save the screenshot check, which waits for the host check.
The host check is Giuseppe's: the default against INPHIREV, the golden spiral at densities 2 and 4, the other scales, two or more instances of different scales into separate STONEs, and a screenshot for `docs/img/alogos.png`.
`make -C doc publish` and the release v0.7.0 wait for that check.

## Out of scope

Arithmetic ladders (STUNEDREV's t·k·(i+1)); scales or densities beyond the menus, a free ratio; adaptive or moving g; temperature as a setting; B-format processing; a collision-free assignment of the short delays across STONEs.
