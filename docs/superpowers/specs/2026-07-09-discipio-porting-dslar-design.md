# Di Scipio → SEAM — porting program charter & `dslar` (sub-project #1)

Date: 2026-07-09
Status: design approved (Sections A + B), pending written-spec review
Author: Giuseppe Silvi, with Claude

## Summary

Open a study chapter that ports Agostino Di Scipio's Pure Data patches into SEAM.
The primary literature is the patches themselves, supported by Di Scipio's writings
in `/Volumes/Aleph/aq-dream/gs/autori/di-scipio-agostino`.
We build/repair the `sds` Faust library one object at a time, and we start from
`LAR.pd` — the self-regulating Larsen ecosystem, the main feedback engine around
which the rest of the vocabulary is stitched.

---

## Section A — Program charter

### Nature

This is a multi-patch program, not a single task.
Each Di Scipio patch is a sub-project that runs the same six-phase lifecycle:

1. Study and analysis of the patch (read as primary literature + supporting texts).
2. Analytical documentation (PDF).
3. Extraction/consolidation into the `sds` Faust library.
4. Working Faust examples.
5. VST3 plugin (seam-ltm).
6. Complete documentation: mathematical + operational.

### Homes in the workspace

We reuse existing conventions and introduce no new structures.

| Artifact | Home | Existing model |
|---|---|---|
| `sds` library | `librerie/faust-libraries/` — promote `temp/seam.discipio.lib` → `src/`, re-enable line 25 of `seam.lib` | the other `seam.*.lib` |
| analysis + docs (study IT + math EN) + Faust examples + plugin | `librerie/seam-ltm/plugins/<name>/` with `doc/study/` (Italian diary) and `doc/math/` (formal English) | `ltburst`, `ltglide` |
| executable composition (if/when a whole work) | `composizioni/` | `fc2003dsaae2` |

### `sds` granularity policy — incremental extraction

We do not design a universal "Di Scipio interface" up front (avoid premature abstraction).
Reusable idioms emerge from real consumers.
We already have two anchors: `LAR` and `fc2003dsaae2` (AE2), which already uses
`sds.integrator`, `sds.localmax`, `sds.map*`, `sds.delayfb`.
An idiom moves into `sds` when a second patch claims it; until then it stays local
to its circuit.
Giuseppe supplies further patches opportunistically as extra validation; no work blocks
waiting for them.

### Naming

Plugin family `ds*` (Di Scipio) parallels the library namespace `sds`.
LAR → plugin `dslar`.
Future works follow the same pattern (`dsae2`, …).

### Library layering — `seam.pdclone.lib`

The porting is organized in layers, mirroring how `seam.cyclone.lib` (`scy`)
clones Max/gen~ objects.
A new **`seam.pdclone.lib`** (proposed prefix `spd`) clones **Pure Data**
objects faithfully against the Pd source (`env~`, `powtodb`/`dbtorms`, Pd's
`hip~`, `line`, …), verified per `reference_external_source_checkouts`.
`seam.discipio.lib` (`sds`) does not re-implement Pd primitives; it **composes**
the pdclone objects — the homeostatic law is an `sds` brick built on
`pdclone.env`.
The check already done for LAR: Pd `env~` has no ready Faust equivalent (the
`an.*` followers use rectangular/one-pole windows and a 0 dB reference, while
`env~` uses a normalized Hann window and the 100 dB `powtodb` reference), so
`pdclone.env` is written new.
See memory `project_pdclone_library_architecture`.

### Cross-cutting rule — sample-rate independence

Di Scipio authors at SR = 44100.
Some constants are sample counts, i.e. implicit time constants relative to 44100.
All SEAM Faust/VST work is sample-rate independent, so every porting defines an explicit
SR-compatibility strategy: express each sample-relative constant as time anchored at
`SR_ref = 44100`, then reconstruct samples at the runtime SR, so temporal behavior stays
invariant across rates.
See memory `project_discipio_sr_independence` and the UHJ precedent
`project_uhj_quadrature_fs_dependence`.

### Cross-cutting rule — Pd↔Faust block-kernel validation

Pure Data runs a 64-sample block kernel (`DEFDACBLKSIZE 64`): control values
update at block/control rate (`env~` at its period, `line` and messages at
control rate) and feedback loops carry a one-block delay, while Faust is
per-sample.
Beyond the SR A/B, every porting needs a final Pd↔Faust behavioral A/B on the
same input, confirming the per-sample jump does not substantially change
behavior (feedback timing, control-update rate); if it does, decide per case
whether to emulate the block-rate control update or the one-block loop delay.
See memory `project_pd_vs_faust_block_validation`.

### Cross-cutting rule — mono I/O normalization

When a Pd patch duplicates one signal onto two identical outputs (the same node
on `dac~` channels 1 and 2), port it as a mono 1-in / 1-out plugin, without
duplicating the output.
If the two outputs genuinely differ (decorrelation, panning, independent
processing), keep them distinct.
See memory `feedback_porting_mono_io_normalization`.

### Cross-cutting rule — next-prime delay lengths

When a control function converts milliseconds → samples as a function of SR,
evaluate case by case whether to snap the resulting sample count to the next
prime, so multiple instances of the same object get coprime (decorrelated)
delays instead of reinforcing the same comb resonances.
The function already exists: Faust `sff.np` (ffunction `next_pr`,
`seam.ffunctions.lib`); the C++ reference is `ddelay`'s hand-written `nextPrime`
(`distance → samples → nextprime → delay`).
It is a per-case choice; a single-instance or phase-critical delay may want the
exact value.
See memory `feedback_next_prime_delay_multiinstance`.

### Boundary with seam-ltm conventions

Faust is the spec; C++ is hand-written (no `faust -lang cpp` in plugin DSP).
Every plugin ships a finished GUI.
Documentation is generated with `tools/gen-faust-doc.sh` (SVG diagrams + faust2mathdoc PDF).

---

## Section B — `dslar` spec (sub-project #1)

### Scope

A VST3 plugin encapsulating the self-regulating Larsen ecosystem of `LAR.pd`, with the
`sds` library as its Faust specification, accompanied by a documented analysis of the
patch and a compilable Faust example.

`LAR.pd` feeds `dac~` with two branches that appear to be one duplicated signal.
Confirm during phase 1; if identical, `dslar` is **mono 1-in / 1-out** (per the
mono I/O normalization rule) and the GUI meter/scope shows that single channel.

### What `LAR.pd` does

A single-channel self-regulating Larsen ecosystem.
Two branches leave a shared input `*~`:

- Audio branch: `adc~ 1` → 2 s fade-in (`$1 2000`→`line`) → `hip~ 100` → manual pre-gain
  1/2/4 → `delwrite~/delread~ tab1 50` (50 ms) → final VCA → `dac~` + `s~ audioLAR`.
- Analysis branch: same signal → `delread~ tab2 20` (20 ms decorrelation) → `env~ 2048`
  (RMS→dB) → `dbtorms` → `- 1` → `abs` → `pow 40` → `line 200` → drives the final VCA.

The `audioLAR` bus is the ecosystem loop; the `$0-scilloscope` monitors it.

### The control law (the heart)

`gain = abs(dbtorms(env) − 1) ^ 40` is a homeostatic negative feedback.
Quiet room (`rms→0`) ⇒ `abs(rms−1)→1` ⇒ gain opens ⇒ more Larsen energy.
Loud room (`rms→1`) ⇒ `abs(rms−1)→0` ⇒ gain closes.
The `^40` makes the curve near-threshold, so the system breathes around an equilibrium
instead of settling — the pulsing/granular texture of the ecosystemic works.
This idiom is new: it is not among the existing `sds.map*` functions.

### DSP decomposition

| Patch block | Destination | Note |
|---|---|---|
| `env~ 2048` (RMS→dB) | `seam.pdclone.lib` clone — `pdclone.env` | Pd `env~` = Hann-weighted mean-square + `powtodb`; no ready Faust match (`an.*` followers are rectangular/one-pole with 0 dB ref); `sds` composes it. Distinct from AE2's `sds.integrator` (abs rectangular average). |
| `dbtorms → −1 → abs → pow 40` | `sds` brick — the heart | homeostatic law; new idiom |
| fade `$1 2000→line`, ctrl `$1 200→line` | Faust std (`si.smoo`/ramp) | smoothing; no new brick |
| `hip~ 100` | Faust std (`fi.highpass`/`sfi`) | coefficients designed at runtime SR |
| `delwrite~/delread~` 20/50 ms | LAR circuit (`de.delay`) | decorrelation; local until a 2nd patch claims it |
| pre-gain 1/2/4, final VCA | LAR parameters/circuit | — |
| `s~/r~ audioLAR` | plugin I/O | loop closes acoustically (see below) |

### SR-compatibility for LAR

The only sample-relative constant is `env~ 2048` (window = 2048/44100 ≈ 46.4 ms).
Port it as `ba.sec2samp(2048/44100)` in Faust and compute the window in `prepare(fs)` in C++.
The others are already SR-independent: `delread~`/`line` are milliseconds, `hip~` is Hz.
Record `SR_ref = 44100` and this conversion in `doc/study`.
Verify with a 44.1 kHz vs 96 kHz behavioral A/B (same breathing period / envelope).
For the 20/50 ms decorrelation delays, consider the next-prime snap (`sff.np` /
`ddelay` reference) so several `dslar` instances in a session decorrelate.

### Plugin/room boundary

In `LAR.pd` the `audioLAR` bus looks like an internal feedback, but physically the signal
leaves `dac~`, excites the acoustic Larsen, and returns through `adc~`.
`dslar` is therefore genuinely input → output; the self-regulation loop is acoustic.
Document this as a usage constraint (mic/speaker placement).
Provide an optional internal loop (GUI toggle) so the system can be studied on headphones
without a real Larsen.

### Parameters

Input drive · target (the `−1`) · steepness (the exponent `40`) · control smoothing time
(200 ms) · decorrelation time (20/50 ms) · output · internal-loop on/off.
These expose the ecosystem's tuning knobs, not merely a gain.

### GUI

A branded window (every seam-ltm plugin ships a finished GUI) with the knobs plus a live
level-vs-control meter/scope that shows the homeostasis breathing — the pedagogical heir
of the patch's `$0-scilloscope`.

### File layout (`plugins/dslar/`)

```
plugins/dslar/
├── CMakeLists.txt
├── doc/
│   ├── dslar.dsp              # process = sds.<lar circuit>;  → SVG + mathdoc
│   ├── dslar-validation.md
│   ├── references/            # relevant Di Scipio PDFs
│   ├── math/                  # formal English math
│   └── study/                 # dslar-study.tex (Italian analysis) → PDF
├── resource/
│   └── dslar.uidesc
└── source/
    ├── dslar_ids.h
    ├── dslar_processor.h      # opens with FAUST REFERENCE comment block
    ├── dslar_processor.cpp
    ├── dslar_dsp.h
    └── version.h
```

Plus `sds` promoted into `faust-libraries/src/` with the two new bricks.

### Execution order

1. Documented analysis → `dslar-study.tex` + copy references; confirm which Di Scipio work
   `LAR.pd` belongs to. ← first concrete artifact
2. `sds` extraction: envelope follower + homeostatic law, reconciled with AE2, inline Faust tests.
3. Compilable Faust example (`doc/dslar.dsp`) → SVG + mathdoc.
4. Hand-written C++ plugin (Faust spec → C++), GUI.
5. Complete docs: mathematical (`doc/math`) + operational (`validation.md`, safe Larsen use in a DAW).

Each phase gets its own plan via `writing-plans` in a later session; this spec holds them together.

### Deferred decisions

- Which specific Di Scipio work `LAR.pd` belongs to — resolve during phase 1 from the
  literature (candidates: Modes of Interference 2007, Background Noise Study 2011, AE2 2002/03).
- Exact names/signatures of the two new `sds` bricks — settle during phase 2 against AE2 usage.
- Whether the decorrelation delay ever graduates to `sds` — only when a second patch claims it.
