# inphirev plugin — design

Date: 2026-10-27.
Status: agreed in conversation (Giuseppe and the agent), reviewed by a builder check, awaiting Giuseppe's approval of this document.

## Purpose

inphirev is a four-face golden all-pass memory for *canto alla durata* (`dream-cad`), first for the *Dialogo Angelico* anapnoe · tempo (clarinet and electromagnetic timpani).
TETRAREC picks up the room in A-format, each capsule feeds one face, and each face of STONE plays one chain of Schroeder all-pass sections whose delays follow a single golden spiral.
The first delay is the acoustic distance from STONE to TETRAREC; every later delay is a power of φ above it, moved to the next prime.
The name comes from the old Faust experiments `in-phi-rev`, the ancestor of this idea and of stunedrev.

It is an exploratory instrument, not a mapping from playing to processing.
The sound is chosen at soundcheck by three settings (STONE id, distance on that id's grid, number of sections); the send level is set at the mixer.

**The first use (Giuseppe, 2026-10-27).**
Two instruments, timpani and contrabass clarinet with mute, and two STONEs facing each other: each STONE is the voice of one instrument's reverberation only, placed behind the *other* instrument.
So there are two instances of the plugin, one per instrument, each fed by that instrument's TETRAREC and playing into the opposite STONE.
The distance an instance uses is from the centre of the TETRAREC that feeds it to the centre of the STONE that plays its output; the distance between the two STONEs plays no part.
The two distances are 3–4 m and deliberately different.
The piece's first formal level is golden: C = 10′, descent A = C/φ ≈ 6′11″ (371 s), ascent B = C/φ² ≈ 3′49″ (229 s).
The input closes at the end of A, so the ascent is carried by the two tails, asynchronous and independent, and the tails must die with the form: still audible through B, below −30 dB at its end.

Success means: the plugin reproduces its Faust specification to numerical precision at 96 and 48 kHz; its delays equal the delay table of the spec exactly; two instances with different STONE ids never share a delay; it allocates its memory once, outside the audio thread; its memory can be emptied while it plays without a dropout; the window follows `doc/style/ui-style.md` and prints the decay that the settings produce.

## Decisions taken in conversation (2026-10-27)

**A-format throughout, no B-format detour.**
An all-pass is linear: the same filter on four channels gives the same result in A- and B-format.
With independent filters per channel, filtering W, X, Y, Z differently scatters the direction per frequency, because the four B-format channels are projections of one field, not four sources.
Each A-format channel is one direction, so independent chains per capsule are the classic way to decorrelate, and the chain is placed between TETRAREC's capsules and STONE's faces with no abmodulex/bamodulex around it.

**Fixed g = 1/φ = φ − 1 = 0.6180339887…, no adaptivity.**
A Schroeder section is all-pass for every |g| < 1; g decides only how the energy is distributed in time: g² in the direct term, 1 − g² in the echoes.
At g = 1/φ the split itself is golden (direct 1/φ², tail 1/φ).
The effective number of echoes, 1/Σpₖ², peaks at g = √(√2 − 1) ≈ 0.6436 and 1/φ is within 1 % of the peak, so any level-driven excursion of g could only lower the diffusion; a time-varying g is also not all-pass while it moves.
The input level is the mixer's send.

**The golden spiral.**
One geometric sequence runs through the four faces in turn, A B C D A′ B′ C′ D′ …:

```
d(i, k) = d₀ · φ^(i + k/4)        i = 0 … N−1 (section), k = 0 … 3 (face A … D)
```

Adjacent faces stand φ^¼ apart (12.8 %), and the step from D to A′ is the same ratio as from A to B.
The four sets of delays are disjoint by construction.
This matters because a cascade of all-pass sections is commutative: what decorrelates two faces is their *set* of delays, not their order.
Rejected alternatives, for the record: four neighbouring primes near d₀ growing by the same law (the faces stay 2.6 % apart forever, a tight canon rather than four responses); a linear progression (stays a reverberation below one second); faces spaced linearly inside [d₀, φd₀) (numerically within 3 % of the spiral; Giuseppe prefers the spiral's logarithmic division).

**Each delay is the prime strictly above the exact product**, never φ times the previous prime, so the rounding does not accumulate along the sequence.

**Settings at soundcheck, in SETUP: STONE id, distance, number of sections.**
The duration is not a control; the window prints it as the consequence of the settings.
Each extra section is as long as all the previous ones together (Σφⁱ for i < n = φⁿ⁺¹ − φ), so the total delay roughly multiplies by φ at each step.

**The distance grid: no two STONEs can coincide** (Giuseppe, 2026-10-27: the STONEs are moved onto the grid).
Two instances whose distances stand in a ratio φ^(j/4) play the same spiral shifted by j faces: the same primes, the same echoes, no longer independent.
What decides it is the *phase* on the spiral, u = 4·log_φ(d) mod 1: equal phases coincide whatever the distances.
So each STONE id has a fixed phase and the distance slider moves only on that id's grid, in steps of φ^¼ (12.8 %), anchored at 1 m:

```
d(id, j) = φ^((j + u_id)/4)    u = 0, ½, ¼, ¾, ⅛, ⅝, ⅜, ⅞  for id 1 … 8 (van der Corput)
```

The van der Corput order keeps the first M ids as far apart as possible for any M: with two STONEs (the Dialogo) the phases are 0 and ½ and the eight faces form one spiral of eight voices in steps of φ^⅛; with four (*Una Radura*) the sixteen faces are equidistant.
Ids 1–8 and "?" (undeclared) follow the suite's `StoneId` (multipink, ltglide).
*Agent's call:* an undeclared id uses phase 0 and the footer says "STONE id undeclared".
The grid between 2 and 6 m:

| id | phase | distances (m) |
|---|---|---|
| 1 | 0 | 2.058 · 2.321 · 2.618 · 2.953 · 3.330 · 3.756 · 4.236 · 4.778 · 5.388 |
| 2 | ½ | 2.186 · 2.465 · 2.780 · 3.136 · 3.537 · 3.989 · 4.499 · 5.074 · 5.722 |
| 3 | ¼ | 2.121 · 2.392 · 2.698 · 3.043 · 3.432 · 3.871 · 4.365 · 4.923 · 5.553 |
| 4 | ¾ | 2.253 · 2.540 · 2.865 · 3.232 · 3.645 · 4.111 · 4.636 · 5.229 · 5.897 |

The STONE is placed on a grid point with the laser; the grid value, not a measurement, is the distance the plugin uses.

**Ranges (Giuseppe, 2026-10-27).**
Distance: the grid points of the id between 1 and 6 m.
Sections 1–18; the arena depends on distance × φᴺ, so 6 m with 18 sections costs about what 10 m with 17 did.

**The tail against the form.**
The energy decay after a stationary input closes, estimated by convolving the sections' energy distributions (echoes taken as incoherent; to be measured on the plugin), with 17 sections:

| distance | −30 dB, face A / D | at +229 s (end of B), A / D |
|---|---|---|
| 3.136 m (id 2) | 189 s / 272 s | −38 dB / −24 dB |
| 3.330 m (id 1) | 201 s / 289 s | −35 dB / −22 dB |
| 3.537 m (id 2) | 214 s / 307 s | −33 dB / −20 dB |

17 sections at 3.1–3.5 m cross −30 dB across the four faces around the end of B: the face A dies first, the face D last, so each STONE goes out face by face as the form closes.
16 sections would be at −33 dB on face A halfway through B; hence the default of 17 and the cap of 18 for a margin.

**Spiral step φ in this first prototype.**
Giuseppe wants to try a denser spiral as well (step √φ or φ^¼ per section, which allows more and shorter sections in the same memory).
That becomes a third SETUP parameter in a later version and is out of scope here; the engine should not make it hard to add (the step is one constant in one function).
The distance grid is tied to the face spacing (φ^¼ here): a denser spiral changes the grid too, and the grid test (3b) must follow it.

## Prerequisite: the speed of sound (seam-ltm issue #15)

The distance is converted with c(T) = 331.3 · √(1 + T/273.15) at T = 20 °C, that is 343.2146 m/s.
`seam.math.lib` still has `isos = 331.4` (about 0 °C) and `esos = 344`, and issue #15 records why that is wrong.
*Agent's call:* the fix inphirev needs is **additive**.
A new function of temperature, c(T), goes into `seam.math.lib` and its C++ twin into `_common/` (next to `kSpeedOfSoundInterior` in `seam_primes.h`, or a small header of its own), with 20 °C as the interior reference.
`isos`, `esos`, `imt2npsamp` and every plugin that uses them stay as they are: redefining them moves ddelay, delRM and addelay by about 3.5 % and invalidates their reference headers, with the STONE/UPU concert on 2026-10-30.
Whether the old constants are later replaced is the rest of #15, not this plugin's scope.

## Constraints

- Hand-written C++, Faust is the specification (suite convention; `faust -lang cpp` only as a scratch tool).
- State and arithmetic in `double`, conversion only at the bus, as stunedrev: the reference is compiled with `faust -double` and the comparison is to about 1e-12.
- Reuse `_common/`: `seam_moorer.h` (Moorer's all-pass section), `seam_primes.h` (sieve, `nextPrimeAbove` = `sff.np`), `seam_ramp.h`, `seam_denormals.h`, `seam_state.h`.
- UI from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Every plugin ships a GUI.

## Specification (Faust)

**Library: a new `seam.silvi.lib`, prefix `sgs`** (Giuseppe, 2026-10-27), registered in `seam.lib`.
The functions belong to Giuseppe's own work (*canto alla durata*), as `seam.tedesco.lib` holds Davide Tedesco's.

**Lineage, cited and not moved.**
The ancestor is `apphirev(N,t) = seq(i,N, sapf(int(t*(i+1)*phi),0.708))`, committed 2020-07-20 as `in-phi-verb` and now only in `faust-libraries/temp/seam.reverbs.lib`: linear progression, Schroeder's form, g = 0.708, no primes.
The header of `seam.silvi.lib` cites it (and `seam.tedesco.lib`'s note that stunedrev descends from it); the old function stays where it is, unchanged (Giuseppe, 2026-10-27).

**Reuse from `seam.math.lib`:** `sma.phi(1)` is the ratio and `sma.rphi(1)` = (√5−1)/2 is g; no change to `sma`.
`sma.srphi` is not used: it descends, steps by whole powers and multiplies repeatedly, while the spiral needs φ^(i + k/4) from one `pow`.

**All-pass: Moorer's (`sjm.apfv`), not Schroeder's (`sms.apfv`)** (agent's recommendation, not contested).
The transfer function is the same; Moorer's one multiplier keeps the section all-pass after rounding for any g, which matters in a memory lasting minutes, and its C++ twin `Seam::MoorerAllpass` is already tested by stunedrev.
The README explains Schroeder's form as the equivalent reading, with the golden identity it shows at g = 1/φ: 1 − g² = g, so the comb and the direct path have the same gain.

The spec mirrors `sdt.stdel` / `sdt.stline` / `sdt.stunedrev`:

- `ipgrid(id, j)` — the distance in metres of grid point j for STONE id, `φ^((j + u_id)/4)`, the phases as above.
- `ipdel(d, i, k)` — the delay of section i of face k in samples: `n = floor(SR · d · φ^(i + k/4) / c + 0.5)`, kept when below 2, otherwise `sff.np(n)`.
  With k = 0 and i = 0 this is the acoustic path at c(20 °C), rounded to the sample and moved to the prime above, as ddelay does; no millimetre rounding, because the grid value is exact by definition.
- `ipmd(i, k)` — the buffer size of that section, fixed at compile time for 6 m at 192 kHz plus a margin above the largest prime gap (Faust cannot evaluate `sff.np` when sizing).
- `ipline(N, k, d)` — `seq(i, N, sjm.apfv(ipmd(i,k), ipdel(d,i,k), sma.rphi(1)))`, sections in increasing delay.
- `inphirev(N, id, j)` — the four lines in parallel at `ipgrid(id, j)`, face k on channel k.

N is a compile-time constant in Faust (`seq`), so the reference is generated per N; the C++ takes N at run time.

## Architecture

```
plugins/inphirev/
├── CMakeLists.txt
├── source/
│   ├── inphirev_ids.h          # FUID, parameter IDs
│   ├── inphirev_params.h       # SDK-free ParamBox of atomics (as stunedrev)
│   ├── inphirev_dsp.h          # SDK-free engine: delay table, arena, faces, RESET
│   ├── inphirev_processor.h    # FAUST REFERENCE block, IAudioProcessor
│   ├── inphirev_processor.cpp
│   └── version.h
├── resource/inphirev.uidesc
└── doc/README.md
```

Copy stunedrev's shape; the RESET view is stunedrev's GUI-only button.

### Delay table

`delay(i, k)`: grid distance, nearest sample at c(20 °C), `nextPrimeAbove`; in `inphirev_dsp.h`, citing c(T) from `_common/`.
The table (18 × 4) is recomputed when a setting changes, at the start of a block, from the sieve built in `setActive`.

### The printed decay

When a setting changes, the GUI side (never the audio thread) computes from the table the energy decay curve of each face: the sections' energy distributions (g² at 0, (1−g²)²g^(2(m−1)) at m·t) convolved on a grid of 0.1 s and integrated backwards, as in the design's estimate.
From it the footer prints, per face, the time to −30 dB and to −60 dB after the input closes.
It is an estimate and the README says so; test 10 checks it against the decay measured on the engine.

### Memory

One arena of `double`, allocated in `setActive(true)` for the worst case (the largest grid point ≤ 6 m, 18 sections) at the session's rate, each section sized exactly for its longest delay + 1, as stunedrev.
Approximate totals: 290 MiB at 48 kHz, 579 MiB at 96 kHz, 1.13 GiB at 192 kHz; the longest delay is about 90 s.
The sieve's bound is the largest product plus a margin above one prime gap.
Allocation failure: silent engine, `kResultOk`, footer says "allocation failed" (stunedrev's behaviour).

Sections above N are not processed.

### The engine

Per sample, for each face k = 0…3:

```
x = in_k
for i in 0…N−1: x = section[k][i].tick(x)
out_k = x · powerGain(smoothed)
```

No input or output gain: the send and the return are the mixer's.
Denormals: `ScopedNoDenormals` around the block.

### A change of setting

Distance and N are soundcheck values, not gestures.
*Agent's call:* a new value is applied only once it has been stable for 250 ms, so dragging the distance across grid points or a burst of automation produces one reset at the end, with the latest value, instead of a muted output restarting its clearing for the whole gesture.
The footer shows the pending value at once; the sound follows after the pause.
Applying a change recomputes the table and runs the RESET sequence (fade out 25 ms, clear the arena in slices, fade in if POWER is on), because a new delay or a newly enabled section would otherwise play the stale contents of its buffer.
A host automating these parameters gets a reset at every change; the README says so.

### RESET and POWER

As stunedrev: RESET is a GUI-only button driving a generation counter, the arena is cleared in slices per block, not automatable, not in the state.
POWER is the output ramp; the faces keep running while it is off.

## Parameters and window

| Parameter | Zone | Range | Default | Notes |
|---|---|---|---|---|
| STONE id | SETUP | ?, 1–8 (stepped) | ? | the suite's `StoneId`; sets the grid phase |
| distance | SETUP | grid index j, shown in metres (mm) | the id's point nearest 3.3 m | TETRAREC centre ↔ own STONE centre |
| sections | SETUP | 1–18, integer | 17 | |
| POWER | OPS | off / on | off | output ramp, 25 ms |
| RESET | OPS | GUI-only | — | empties the memory |

No FINE zone (an absent zone is omitted).
The distance is stored as the grid index j: changing the id keeps j, so the STONE jumps to the neighbouring point of the new grid, and the window shows the new metres at once.
*Agent's call:* the default j is the id's grid point nearest 3.3 m.
State: STONE id, j, sections, POWER, under the append-only contract of `seam_state.h`.
`tools/check-uidesc.py` recognises this SETUP by the `StoneId` tag as it stands.

```
HEADER   inphirev · golden all-pass memory
SETUP    STONE 1     distance  3.330 m     sections  17
OPS      [■] POWER   [□] RESET
FOOTER   −30 dB  A 201 s · B … · C … · D 289 s     −60 dB  A 355 s · … · D 509 s
         first  A 9.7 ms · last D …
         arena 579 MiB @ 96 kHz · ready | clearing | allocation failed | STONE id undeclared
```

The footer is the printed solution: the estimated decay per face, the first and the longest delay (exact, from the table), the arena and the status.
The figures for A and D are those of the design's estimate at 3.330 m and 17 sections; the builder fills the rest from the implementation.

Bus: four in, four out, A-format face order as stunedrev (`kAmbi1stOrderACN` as the arrangement, channel k = face k); subcategory `Fx|Reverb`.

## Verification

References from the spec, compiled with `faust -double` and the `sff.np` ffunction, into `tests/ref/inphirev_ref.h`; the tests need no `faust` binary.
A full render is millions of samples, so the reference keeps windows of the response and the energy per second, as stunedrev.

| # | Test | Criterion |
|---|---|---|
| 1 | speed of sound | the C++ c(20 °C) equals the library's to the last digit (from #15) |
| 2 | delay table | every d(i, k) for N = 18, every grid point of every id, at 44.1, 48, 96, 192 kHz, equals `ipdel(ipgrid(id, j), i, k)` exactly |
| 3 | spiral | the four faces' sets of delays are disjoint, and each face is strictly increasing |
| 3b | grid | for every pair of different ids and every pair of their grid points, the two instances' 72 + 72 delays share no value, at every rate; mutation: give two ids the same phase, the test must fail |
| 4 | engine | the four faces against `inphirev(N, 1, j)` at 3.330 m for N = 1, 4, 17 at 96 and 48 kHz, on noise and on a clarinet note, within about 1e-12 of the peak |
| 5 | change of setting | after a change, once the clearing ends, the output equals a fresh engine's with the new setting; a burst of changes inside 250 ms produces one reset, with the last value |
| 6 | arena | every section's length ≥ its longest delay + 1 at all rates; totals reported |
| 7 | RESET | as stunedrev test 7 |
| 8 | parameters | ParamBox and state round-trips |
| 9 | footer | the printed first and longest delays equal the table |
| 10 | decay | the printed −30 dB time is within a stated tolerance of the time measured on the engine (noise in, input closed, backward-integrated output energy), at 3.330 m with N = 12 to keep the render short |

Every test is verified by mutation (break the code, see it fail) and the mutations are recorded in the plugin's `doc/`.
Suite: VST3 validator, `check-uidesc.py`, ctest; CPU measured at 96 kHz (72 sections per sample at most, with reads scattered over 580 MiB).

**Host check (Giuseppe):** the Dialogo's setup, two instances (ids 1 and 2) into two opposed STONEs on grid points; the clarinet and the timpani through the four faces at a few (distance, N) pairs; the input closed and the tails timed against the printed decay; POWER; RESET while sounding; a screenshot for `docs/img/`.

## Documentation

- `plugins/inphirev/doc/README.md`: what it is, the spiral table, the energy argument for 1/φ, the printed durations.
- `doc/plugins.toml`: a new entry; `make -C doc doc` and `make -C doc publish`; `make -C doc test` counts twenty cards today and has to count twenty-one.
- A session log under the suite's usual place, and an entry in `dream-cad`'s `logs/` when the plugin is first used for the Dialogo.

## Target

*Agent's call:* a merge to `main` and Giuseppe's host check, so the spiral can be tried in the hall.
No numbered release in this round; the release procedure follows when the prototype has been played.
The whole test matrix is part of the merge.

## Out of scope

- Adaptive g, envelope followers.
- The denser spiral (step √φ, φ^¼): next version.
- Temperature as a setting.
- B-format processing.
- The Pd external version.
