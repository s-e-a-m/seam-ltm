# stunedrev plugin — design

Date: 2026-10-01.
Status: approved in conversation (three sections), awaiting review of this document.

## Purpose

stunedrev, the APF of the score of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco), is four independent lines of 42 all-pass sections in series, one line per face of STONED, each tuned by an irrational ratio k: √2, φ, e, π.
It is a long memory more than a reverberation: the energy of a line arrives on average after the sum of its 42 delays (106, 69, 17 and 201 s at the starting times).
This plugin is the second C++ port of SSCDO#2, hand-written from the specification `sdt.stunedrev(t1, t2, t3, t4)` of `seam.tedesco.lib` (faust-libraries 9765bb4), following the suite convention: Faust is the spec, C++ is the deliverable.
It is played in Reaper at 96 kHz.

Success means: the plugin reproduces `sdt.stunedrev` to numerical precision at 96 and 48 kHz, across a change of time; it allocates exactly the memory its delays need, at any rate, outside the audio thread; its memory can be emptied while it plays without a dropout; the window follows `doc/style/ui-style.md`; every decision is in the session log.

## Constraints

From Giuseppe, for every SSCDO#2 plugin (2026-10-01):

- State and arithmetic in `double` throughout; conversion only at the bus.
- UI coherent with the suite: `ui-style.md`, started from `plugins/_template/resource/_template.uidesc`, `tools/check-uidesc.py` clean.
- Reuse `_common/` where it exists; write what is new as reusable `_common/` code.
- **SR rule:** every process sounds as it does at 96 kHz.
  For stunedrev the reading of 2026-09-29 stands: the delays are milliseconds converted at the session's rate, so times and memories are those of 96 kHz to within the step to the next prime, and each rate has its own set of primes (a feature of the system, not an SR-dependence to fix).

From the 2026-09-29 decisions: the reference is the version played in Pd (`stunedrev.dsp`, 42 sections), the time sliders keep 1–100 ms, the starting times are 83, 47, 7, 71 ms, and the delay rule is `sma.ms2npsamp` (round, then `sff.np`, the prime strictly above).

## Architecture

```
plugins/stunedrev/
├── CMakeLists.txt
├── source/
│   ├── stunedrev_ids.h          # FUID 0x5E4D0011, parameter IDs
│   ├── stunedrev_params.h       # SDK-free ParamBox of atomics (as lmo_params.h)
│   ├── stunedrev_dsp.h          # SDK-free engine: arena, sections, lines, RESET
│   ├── stunedrev_reset_button.h # GUI-only RESET view (as DslarResetButton)
│   ├── stunedrev_processor.h    # FAUST REFERENCE block, IAudioProcessor
│   ├── stunedrev_processor.cpp
│   └── version.h
├── resource/stunedrev.uidesc    # from _template.uidesc, format L
└── doc/README.md
plugins/_common/
└── seam_primes.h                # NEW: prime sieve, nextPrimeAbove = sff.np
```

`stunedrev_dsp.h` is SDK-free, so doctest drives the whole engine.

### `seam_primes.h` — `PrimeSieve`

A sieve of Eratosthenes over the odd numbers, as a bitset, built up to a bound given at construction.
`nextPrimeAbove(n)` returns the smallest prime strictly greater than n: the semantics of `sff.np` (`nextprime.h`), which the 2026-09-29 study compared with Davide's `next_pr` for every n up to 3 000 000.
`msToPrimeSamples(ms, fs)` is `sma.ms2npsamp`: `n = floor(ms·fs/1000 + 0.5)`, kept as it is when below 2, otherwise `nextPrimeAbove(n)`.
The sieve is built outside the audio thread; a lookup is a scan of at most one prime gap (≤ 154 below 5 million), so a change of time can recompute its 42 delays inside `process()`.
Reusable by every plugin with prime delays (ddelay, delRM).
Interface: `explicit PrimeSieve(uint32_t bound)`, `bool isPrime(uint32_t)`, `uint32_t nextPrimeAbove(uint32_t)`, `uint32_t bound()`.

### The all-pass section

Moorer's form, `sjm.apfv(md, t, g)`:
`(x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_`.
Unrolled per sample, with `w` the value written to the buffer and `wd = w[n−t]`:

```
wd = buf[n − t]
a  = −g · (x − wd)        // the only multiplier
w  = a + x   → buf
y  = wd + a
```

One multiplier: all-pass by structure for any g and any rounding.
g = 1/√2 in every section, as in the original.
A section is an offset and a length into the arena, a write index, and its current delay t.

### Memory: one arena, sized exactly

In `setActive(true)` the engine allocates **one** block of `double` for the 168 sections, zero-filled, and builds the prime sieve.
`setActive(false)` frees both; a change of rate passes through here, as VST3 requires.

Section i of the line of ratio k has length

```
len(k, i) = msToPrimeSamples(100·(i+1)·k, fs) + 1
```

`round` and `nextPrimeAbove` are both non-decreasing, so the longest delay the slider can ask is exactly the one at 100 ms, and `len` holds it.
**Difference from the spec, intended:** `sdt.stmd` sizes with a fixed margin, `int(100·(i+1)·k·SR/1000) + 150`, because Faust sizes a buffer at compile time and cannot evaluate `sff.np` there.
The C++ sizes each section exactly, which is correct at any rate: at 384 kHz the margin of 150 would no longer cover the prime gaps (154 below 5 million).
The sound is identical; only the allocation differs.
At 96 kHz the arena is about 588 MiB (the exact figure is measured by test 6 and updates the report's card `stunedrev-memoria`).

The sieve's bound is the largest `round(100·42·π·fs/1000)` plus a margin covering one prime gap, checked at construction by finding the prime.

**Allocation failure** (1.2 GiB at 192 kHz on a loaded machine): the engine stays silent, `setActive` returns `kResultOk` so the host keeps running, and the footer says "allocation failed".
No exception crosses the plugin boundary.

### The engine

Per sample, for each line j = 0…3 (√2, φ, e, π, the original's order, outputs 1–4 as `dac~ 9 10 11 12`):

```
x = in_j · inputGain(smoothed)
for i in 0…41: x = section[j][i].tick(x)
out_j = x · outputGain(smoothed) · powerGain(smoothed)
```

All gains are linear ramps of 25 ms (`Seam::LinearRamp`, `seam_ramp.h`), the ramp of the original's `interpolator_4ch`.

**A change of time** recomputes the 42 delays of its line at the start of the next block, with the sieve, and the delays jump.
This is the spec's behaviour (`de.delay` with an integer delay that changes) and the original's.
The buffers hold the whole history up to the longest delay, so a jump reads older or newer history: a click, never garbage.
The times are setting values: no cue of the patch moves them.
Documented in the README.

**POWER** (new against the original, for the suite standard) is the output gain ramp; the lines keep running when it is off, so the memory keeps emptying through the output at its own pace, and a power-on finds it where it is.

### RESET

The memory of a line lasts minutes (π at 71 ms: 99.8 % of a clarinet note's energy back after 600 s), and only a reload empties it in the original.

RESET is a **GUI-only** button, not a parameter: a momentary parameter is lost when the host coalesces 0→1→0 into one point (ltglide, Reaper).
The plugin is a `SingleComponentEffect`, so the view and the processor are one object: a click increments an atomic counter `resetGen`.
At the start of each block the engine compares `resetGen` with the last generation it served; when they differ:

1. the output ramps to zero over 25 ms; the input is ignored from that moment;
2. the arena is zeroed **4 MiB per block** (about 0.4 ms of `memset`; 147 blocks at 96 kHz, 0.4 s at 256-sample blocks), write indices reset;
3. the output ramps back up if POWER is on.

A click during the clearing restarts it from the beginning: a generation counter rather than a flag survives double clicks.
RESET is not automatable and not in the state: it serves rehearsals, it is not an event of the score.
The view shows a filled square until the clearing ends (an atomic `clearing` flag read by a GUI timer).

## Parameters and window

| Parameter | Zone | Range | Default | Notes |
|---|---|---|---|---|
| POWER | OPS | off / on | off | output ramp, 25 ms |
| RESET | OPS | GUI-only | — | empties the memory, see above |
| t √2 | FINE | 1–100 ms, step 1 | 83 | the original slider's integer step |
| t φ | FINE | 1–100 ms, step 1 | 47 | |
| t e | FINE | 1–100 ms, step 1 | 7 | |
| t π | FINE | 1–100 ms, step 1 | 71 | |
| input | FINE | 0–1 linear | 0 | CC83 in the original, 25 ms ramp |
| output | FINE | 0–1 linear | 0 | CC84 in the original, 25 ms ramp |

The integer step is the score's slider (Pd `hsl` + `nbx`, Faust `step 1`), and keeps a continuous automation from recomputing primes every block.

The controls live in a `ParamBox` of atomics (`stunedrev_params.h`), as in LMO: `process()` never touches a `Parameter`.
State: the six automatable parameters, written and read under the append-only contract of `seam_state.h`.

**Format L** (six fine controls, above the threshold of about five): two columns.

```
HEADER   stunedrev · four tuned all-pass memories
OPS      [■] POWER    [□] RESET
FINE     t √2   83 ms        input   0.00
         t φ    47 ms        output  0.00
         t e     7 ms
         t π    71 ms
FOOTER   memory  √2 106 s · φ 69 s · e 17 s · π 201 s
         arena 588 MiB @ 96 kHz · ready | clearing | allocation failed
```

**FOOTER, read-only.** The energy centroid of each line, the sum of its 42 primes divided by fs: how long each face remembers, the number of the report's card `stunedrev-tempi`, live as the times move.
Below it, the arena size and the engine status.
These are independent scalars, passed from the processor in one atomic each and read by a GUI timer; no triple buffer is needed.

Bus: four channels in and out, `kAmbi1stOrderACN` as LMO; subcategory `Fx|Reverb`.

## Verification

The references come from the specification, compiled with `faust -double` and the `sff.np` ffunction, by `doc/study/sscdo2/stunedrev-plugin/gen-ref.sh` and `refdump.cpp`, into `tests/ref/stunedrev_ref.h`; the tests need no `faust` binary.
A full four-line render is millions of samples, so the reference keeps: the whole delay table of `sdt.stdel`; windows of the response (for instance 512 samples per second over 30 s); and the energy per second of each line.

| # | Test | Criterion |
|---|---|---|
| 1 | `seam_primes.h` | `nextPrimeAbove(n)` equals trial division for **every** n up to the 192 kHz bound (2.53 M), and is strictly greater than n |
| 2 | delays | the 16 800 delays (1–100 ms × 42 sections × 4 lines) equal `sdt.stdel` at 96 and 48 kHz, exactly |
| 3 | section | the impulse response of one section equals `sjm.apfv`; flipping the sign of g must fail (the study's 0.0311) |
| 4 | engine | the four lines against `sdt.stunedrev(83, 47, 7, 71)` at 96 and 48 kHz, on noise and on the clarinet note, on the windows, within about 1e-12 of the peak |
| 5 | change of time | a time changes at a block boundary, in the C++ and in the refdump at the same sample; the outputs agree after the jump |
| 6 | arena | every section's length ≥ its longest delay + 1 at 44.1, 48, 88.2, 96, 176.4, 192 and 384 kHz; the total at 96 kHz is reported |
| 7 | RESET | once the clearing ends the state equals a fresh engine's: the same outputs for the same input; skipping one chunk must fail |
| 8 | parameters | the ParamBox round-trip; the processor's state round-trip (missing in LMO) |
| 9 | centroid | at 48 kHz within one prime gap per section of the 96 kHz value, for each line |

Every test is verified by mutation, recorded in `doc/study/sscdo2/stunedrev-plugin/mutations.md`.
Test 5 needs the refdump to move the Faust slider at the exact block boundary where the C++ applies the new time, or it compares two different things.

Suite: VST3 validator, `tools/check-uidesc.py`, ctest.
`minos_lint` runs on a tree configured with `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` (the `build-test` cache still holds 15.7, a known pre-existing failure).

CPU is measured at 96 and 192 kHz and written in the README: 168 sections per sample are cheap in arithmetic, and their scattered reads across the arena make cache misses the real cost.

**Host check (Giuseppe, Reaper, 96 kHz):** the clarinet note through the four faces, by ear against the `stunedrev-chain` renders regenerated with `render.py`; POWER; RESET while sounding; a time moved by hand; the same session at 48 kHz; a screenshot of the window for `docs/img/stunedrev.png`.

## Documentation

- `plugins/stunedrev/doc/README.md`: parameters, the time jump, RESET, the exact sizing against `stmd`, the SR reading, the CPU figure.
- `doc/study/sscdo2/stunedrev-plugin/`: README, `gen-ref.sh`, `refdump.cpp`, `mutations.md`.
- `doc/plugins.toml`: stunedrev in the family "Works — SSCDO#2", the eighteenth plugin; the counts in `doc/scripts/test-doc.sh`, `render-readme.py` and `CLAUDE.md` go from seventeen to eighteen; `make -C doc doc`, `make -C doc test`.
- Session log `logs/2026-10-01-sscdo2-plugins.md`, and the report's cards: `stunedrev-memoria` with the measured arena, POWER and RESET as additions against the original.

## Out of scope

- The direct `adc~ 5 6 7 8` into the reverb in the patch (card `stunedrev-ingresso-uscita`): a question for Davide; the plugin has one input, governed by `input`.
- Smoothing a change of time (a crossfade would break the all-pass during the fade and depart from the spec).
- The cues and the MIDI faders, which live in Reaper.
