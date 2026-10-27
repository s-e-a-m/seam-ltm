# ALOGOS plugin — design

Date: 2026-10-27.
Status: agreed in conversation (Giuseppe and the agent), reviewed by a builder check, awaiting Giuseppe's approval of this document.
Supersedes: `2026-10-27-inphirev-plugin-design.md` (INPHIREV, released in v0.6.0 and removed from the suite by this plugin).

## Purpose

ALOGOS is a four-face all-pass memory for *canto alla durata* (`dream-cad`): the geometric spiral of INPHIREV, opened to four irrational scales and three densities.
TETRAREC picks up the room in A-format, each capsule feeds one face, and each face of STONE plays one chain of Moorer all-pass sections whose delays follow one geometric spiral.
The first delay is the acoustic distance from the TETRAREC to the STONE; every later delay is a power of the scale's ratio above it, moved to the next prime.

The idea (Giuseppe, 2026-10-27): resonant fields of different character, one per STONE, that talk to each other in the hall.
What the plugins do not separate, the hall and the spaced pickup will.

The name is the Greek *alogos*, the Pythagorean word for the incommensurable: the four scales are all *aloga*, and *a-logos* is also "without word", a field that speaks without words.
It carries no "REV": like stunedrev, this is a long memory more than a reverberation.

It is an exploratory instrument, not a mapping from playing to processing.
The sound is chosen at soundcheck by six settings; the send level is set at the mixer.

Success means: the plugin reproduces its Faust specification to numerical precision at 96 and 48 kHz for every scale and density; its delays equal the delay table of the spec exactly; with the default settings it is INPHIREV, delay for delay; it never allocates or frees memory in the audio thread; its memory can be emptied while it plays without a dropout; the window follows `doc/style/ui-style.md` and prints the decay that the settings produce.

## What INPHIREV taught (2026-10-27)

- Giuseppe's first listening: "works very well"; at 16 sections the tail is still very long, but the controls are enough.
- In a Moorer section the energy leaves on average exactly one delay later, whatever g: Σ m (1−g²)² g^(2(m−1)) = 1.
  So the length of the tail is set by the longest sections (T30 ≈ 10 × the longest delay, estimated), almost independently of the ratio.
  The ratio sets three other things: how much the tail changes per section (×ρ), the density of the echoes, and the cost in memory and CPU.
- Estimated T30 on face A at 3.33 m, against the ascent B = 229 s: φ needs 17 sections (201 s), √φ 32 (209 s), φ^¼ 60 (215 s), √2 with g = 1/√2 22–23 (184/260 s), e 9 (229 s), π 8 (227 s).

## Decisions taken in conversation (2026-10-27)

**Geometry for every scale (Giuseppe).**
The other scales tried in stunedrev (√2, e, π) enter as the *ratio* of a geometric spiral, as φ does in INPHIREV, not as stunedrev's arithmetic ladder t·k·(i+1).

**Two parameters define the spiral: the scale r and the density D.**

```
ρ        = r^(1/D)                       r ∈ {φ, √2, e, π},  D ∈ {1, 2, 4}
d(i, k)  = d₀ · ρ^(i + k/4)              i = 0 … N−1 (section), k = 0 … 3 (face A … D)
grid     = ρ^((j + u_id)/4) metres       u = 0, ½, ¼, ¾, ⅛, ⅝, ⅜, ⅞ for id 1 … 8 (van der Corput)
```

INPHIREV is the case r = φ, D = 1.
Adjacent faces stand ρ^¼ apart and the step from D to A′ is the same ratio, as in INPHIREV.
Each delay is the prime strictly above the exact product, never ρ times the previous prime.
The grid follows the density, and the ids stay apart at every density.
*Agent's call, from the builder check:* the grids of one id do not nest across densities, and cannot while the ids stay apart: a nested grid would turn the phase u into D·u mod 1 and collapse ids 1 and 2 at D = 2, ids 1–4 at D = 4.
The separation of the ids wins; a change of density moves the STONE to the nearest point of the new grid, at most half a step away (about 6 % at D = 1, 3 % at D = 2, 1.5 % at D = 4, for φ).
For φ, D = 4 the grid step is φ^(1/16), about 3 % (9 cm at 3 m).

**g is a menu of its own (Giuseppe).**
g ∈ {1/φ, 1/√2, 1/e, 1/π, √(√2−1)}, independent of the scale, so a scale can be paired with its own g (π with 1/π) or mixed (π with 1/φ).
The scale decides *where* the echoes return, g decides *how much* passes at once: the direct term carries g² (38 % at 1/φ, 50 % at 1/√2, 14 % at 1/e, 10 % at 1/π).
√(√2−1) ≈ 0.6436 is the peak of the effective number of echoes; 1/φ is within 1 % of it.
g is fixed per setting: no adaptivity, a moving g is not all-pass while it moves.

**Contacts between STONEs are accepted (Giuseppe).**
Before rounding, two ids never share a delay at any scale and density: their delays lie on one lattice with different fractional phases.
After rounding to the next prime they can, only where the lattice is denser than the integers: with four STONEs (ids 1–4), φ at 96 kHz, the longest shared delay is 3.2 ms at D = 1, 14 ms at D = 2, 24 ms at D = 4 (40 ms at 48 kHz, D = 4); the share of id-pair × grid combinations with at least one common delay is 0.1 %, 27 %, 79 % at 96 kHz.
Above a few tens of milliseconds the fields are disjoint, so the tails never share a delay.
Two fields of different scales lie on incommensurable lattices and can only touch by rounding (not measured).
The residue-class rule (mod 16) considered for INPHIREV is withdrawn: it piles the short delays and moves them up to 200 % on dense spirals.

**One plugin, INPHIREV removed (Giuseppe).**
INPHIREV was played by Giuseppe alone on the day it was released; ALOGOS replaces it with a new FUID, and v0.6.0 keeps the record.
The suite stays at twenty-one plugins.
Release: **v0.7.0** (the set of plugins changes).

## Constraints

- Hand-written C++, Faust is the specification (suite convention; `faust -lang cpp` only as a scratch tool).
- State and arithmetic in `double`, conversion only at the bus: the reference is compiled with `faust -double`.
- Reuse `_common/`: `seam_moorer.h`, `seam_primes.h`, `seam_ramp.h`, `seam_denormals.h`, `seam_state.h`, and `Seam::speedOfSound` / `kRoomCelsius` (c(20 °C) = 343.21462268256795 m/s, added for INPHIREV).
- UI from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Every plugin ships a GUI.

## Specification (Faust)

In `seam.silvi.lib` (prefix `sgs`), generalising the INPHIREV functions:

- `alphase(id)` — the van der Corput phase, as `ipphase`.
- `algrid(r, D, id, j)` — `pow(r, (j + alphase(id)) / (4*D))` metres.
- `aldel(r, D, d, i, k)` — `n = floor(SR · d · pow(r, (i + k/4)/D) / sma.rsos + 0.5)`, kept when below 2, otherwise `sff.np(n)`.
- `almd(r, D, i, k)` — the buffer size, fixed at compile time for 6 m at 192 kHz plus a margin above the largest prime gap.
- `alline(r, D, g, N, k, d)` — `seq(i, N, sjm.apfv(almd(r,D,i,k), aldel(r,D,d,i,k), g))`.
- `alogos(r, D, g, N, id, j)` — the four lines in parallel at `algrid(r, D, id, j)`, face k on channel k.

`sgs.inphirev(N, id, j)` stays in the library as `alogos(sma.phi(1), 1, sma.rphi(1), N, id, j)`, the first case and the lineage; its helpers `ipgrid`, `ipdel`, `ipmd`, `ipline` are redefined on the general ones or removed if nothing else uses them.
The header's lineage note (apphirev 2020, stunedrev) gains ALOGOS.
r, D, g and N are compile-time constants in Faust, so the reference is generated per combination; the C++ takes them at run time.

## Architecture

```
plugins/alogos/
├── CMakeLists.txt
├── source/
│   ├── alogos_ids.h          # FUID 0x5E4D0015, parameter IDs
│   ├── alogos_params.h       # SDK-free ParamBox of atomics
│   ├── alogos_dsp.h          # SDK-free: scales, grid, delay table, engine, RESET
│   ├── alogos_decay.h        # the printed decay, with g as a parameter
│   ├── alogos_state.h
│   ├── alogos_views.h
│   ├── alogos_processor.h    # FAUST REFERENCE block, IAudioProcessor
│   ├── alogos_processor.cpp
│   └── version.h
├── resource/alogos.uidesc
└── doc/README.md
```

Start from INPHIREV's files (the shape, the engine, the settle-and-reset sequence, the footer); the scale, density and g replace the constants `kPhi` and `kG`.
`plugins/inphirev/`, its tests, its reference header and its `doc/study/inphirev/` leave the tree; git history and v0.6.0 keep them.

### The setting

A setting is (STONE id, distance, sections, scale, density, g).
As in INPHIREV, a new setting applies once it has been stable for 250 ms and runs fade out, clear, fade in; an identical delay table skips the clear (a change of g alone still clears: the stored state belongs to the old g).

**Distance (agent's call): stored in metres, snapped to the grid.**
The parameter is the distance in metres, 1–6 m, continuous; the engine uses the nearest grid point of (id, r, D) and the window shows that point.
A VST3 parameter has a fixed number of steps, and the grids have from 6 (π, D = 1) to 82 points (√2, D = 4); storing metres keeps one parameter for all of them, and a change of id, scale or density keeps the STONE where it stands, moving it to the nearest point of the new grid.
Default: 3.33 m (INPHIREV's j = 10 for id 1).

**Sections (agent's call): the cap is a time, not a count.**
The parameter is 1–128; the engine uses min(N, N_max), where N_max is the largest N whose longest delay (face D, section N−1) is at most 90 s at the snapped distance (INPHIREV's longest delay at 6 m with 18 sections).
At 3.33 m this gives φ D=1: 19, φ D=2: 38, φ D=4: 76, π D=1: 8; at 90 s the estimated T30 is about fifteen minutes, far beyond the form.
A 60 s cap was considered and rejected: it would stop π at 7 sections, below the 8 that carry B.
The window shows the effective count and, when it is clamped, the maximum.
Default: 17.

### Memory (agent's call): allocated for the setting, never in the audio thread

Reserving the worst case would cost up to about 3 GiB at 96 kHz and 6 GiB at 192 kHz (√2, D = 4, 1 m, 90 s cap), almost all of it unused by any one setting.
So the arena is sized for the setting in use: each section exactly its delay + 1, as stunedrev.
When a settled setting needs more than the arena holds (or less than half of it), a worker owned by the processor allocates the new arena while the output is already faded out, the audio thread swaps it in through an atomic hand-over, and the worker frees the old one.
Until the swap the output stays silent; the footer says "allocating".
`setActive(true)` allocates for the current setting the same way, synchronously, as INPHIREV does today.
Allocation failure: silent engine, `kResultOk`, footer "allocation failed", as stunedrev; a later smaller setting can recover.

**No memory budget (Giuseppe, 2026-10-27).**
Each instrument has its own electroacoustic chain and its own computer: one ALOGOS per machine, never four on one.
So the heaviest settings are accepted and documented, not refused; the README states the arena of each scale and density at 48, 96 and 192 kHz.
On macOS a large allocation rarely fails outright, so the failure branch is tested through an injected byte limit (INPHIREV's `byteLimit` hook).

### The engine and the decay

As INPHIREV: per sample, four faces of N sections, `ScopedNoDenormals`, output through the POWER ramp; no input or output gain.
`estimateDecay` takes g (it uses `kG` today).
The footer prints T30 and T60 per face, the first and the longest delay, the arena, and the status.

## Parameters and window

| Parameter | Zone | Range | Default | Notes |
|---|---|---|---|---|
| STONE id | SETUP | ?, 1–8 (stepped) | ? | the suite's `StoneId`; sets the grid phase |
| distance | SETUP | 1–6 m, snapped to the grid | 3.33 m | TETRAREC centre ↔ own STONE centre |
| sections | SETUP | 1–128, clamped to N_max | 17 | |
| scale | SETUP | φ, √2, e, π | φ | |
| density | SETUP | 1, 2, 4 | 1 | step per section ρ = r^(1/D) |
| g | SETUP | 1/φ, 1/√2, 1/e, 1/π, √(√2−1) | 1/φ | |
| POWER | OPS | off / on | off | output ramp, 25 ms |
| RESET | OPS | GUI-only | — | empties the memory |

The default setting is INPHIREV's default, delay for delay.
State: the six settings and POWER, under the append-only contract of `seam_state.h`, starting fresh (a new FUID).
The format (S or L) follows the control count as `ui-style.md` prescribes; the builder picks it.

```
HEADER   ALOGOS · irrational all-pass memory
SETUP    STONE 1   distance 3.330 m   sections 17   scale φ   density 1   g 1/φ
OPS      [■] POWER   [□] RESET
FOOTER   −30 dB  A … · B … · C … · D …     −60 dB  A … · … · D …
         first A 9.7 ms · last D …
         arena … MiB @ 96 kHz · ready | clearing | allocating | allocation failed | STONE id undeclared
```

Bus: four in, four out, A-format face order (`kAmbi1stOrderACN`, channel k = face k); subcategory `Fx|Reverb`; infinite tail declared to the host.

## Verification

References from the spec, `faust -double` with the `sff.np` ffunction, into `tests/ref/alogos_ref.h`, windows and energy per second as INPHIREV; the tests need no `faust` binary.

| # | Test | Criterion |
|---|---|---|
| 1 | INPHIREV case | with r = φ, D = 1, g = 1/φ, every delay for every id, grid point and N ≤ 18 equals INPHIREV's table at 44.1, 48, 96, 192 kHz (INPHIREV's values captured into the test before its removal) |
| 2 | delay table | for every scale and density, every delay at N_max, every grid point of ids 1–4 and the undeclared id, at 48 and 96 kHz, equals `aldel` exactly |
| 3 | spiral | per setting, the four faces are strictly increasing and their sets disjoint wherever two lattice values are more than one prime gap apart; where they are not, the collisions are counted and reported, not asserted |
| 3b | grid | the snapped distance is the nearest grid point; ids 1 and 2 at φ, D = 1 share no delay at any rate (INPHIREV's property); for the other cases the longest shared delay between ids 1–4 is reported per scale and density and recorded in the README |
| 4 | engine | the four faces against `alogos` at 3.330 m for (φ,1,1/φ), (φ,4,1/φ), (π,1,1/π), (√2,2,1/√2) with a few N, at 96 and 48 kHz, within about 1e-12 of the peak |
| 5 | change of setting | after a settled change, once clearing and any reallocation end, the output equals a fresh engine's; a burst inside 250 ms gives one reset; a change of g alone clears |
| 6 | memory | every section's length ≥ its delay + 1; growing and shrinking settings swap arenas without the audio thread allocating or freeing; an allocation failure leaves a silent engine that a smaller setting recovers |
| 7 | RESET | as INPHIREV |
| 8 | parameters | ParamBox and state round-trips; sections clamped to N_max; distance snapped |
| 9 | footer | first and longest delays equal the table; the effective sections shown |
| 10 | decay | the printed −30 dB time within a stated tolerance of the engine's, for (φ,1,1/φ) and (π,1,1/π) |

Every test is verified by mutation and the mutations recorded in the plugin's `doc/`.
Suite: VST3 validator, `check-uidesc.py`, ctest; CPU measured at 96 kHz for the heaviest setting the 90 s cap allows (√2, D = 4 at 1 m: about 500 sections per sample), as the first step of the plan (a spike before the window is built).
One instance per machine: the result is documented; only if that setting cannot run in real time at 96 kHz on the i7-8850H does the plan add a ceiling on sections per sample, calibrated on the measurement (agent's call).

**Host check (Giuseppe):** the default reproduces INPHIREV; the golden spiral at densities 2 and 4; then the other scales; two or more instances of different scales into separate STONEs; a screenshot for `docs/img/`.

## Documentation and release

- `plugins/alogos/doc/README.md`: the spiral of any scale, the role of D and of g, the energy argument, the table of the scales (grid points, N_max, memory), the measured contacts, the printed decay as an estimate.
- `doc/plugins.toml`: ALOGOS replaces INPHIREV in "Works — canto alla durata"; `make -C doc doc`, `make -C doc publish`; the count stays twenty-one; the screenshot list follows the suite's pending mechanism.
- `seam.silvi.lib` in faust-libraries, with its docs.
- A session log under `logs/`.
- Release **v0.7.0** with the usual procedure (universal build, ditto, ad-hoc signed `libseamcalbus`); the notes say that ALOGOS replaces INPHIREV and that v0.6.0 keeps it.
  The release and the site push are Giuseppe's to confirm.

## Out of scope

- Arithmetic ladders (stunedrev's t·k·(i+1)).
- Scales or densities beyond the menus; a free ratio.
- Adaptive or moving g.
- Temperature as a setting.
- B-format processing.
- Collision-free assignment of short delays across STONEs (contacts accepted).
