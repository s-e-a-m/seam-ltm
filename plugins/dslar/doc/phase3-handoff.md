# dslar — Phase 3 handoff log

Date: 2026-07-10
Branches: `dslar` (seam-ltm) + `dslar` (faust-libraries, commit `4e7e42b`)
Status: **Phase 2 steps 1-2 complete and verified.** Steps 3-5 deferred to a later
session. This log hands off with the central open decision (the `ondemand` jump)
framed for that session.

Predecessor: `plugins/dslar/doc/phase2-handoff.md` (Phase 1 → Phase 2).

## What Phase 2 steps 1-2 produced (DONE, verified, committed)

All in the `faust-libraries` repo, branch `dslar`, commit `4e7e42b` (NOT merged to
master, NOT pushed).

### Step 1 — `src/seam.pdclone.lib` (new, prefix `spd`)
Faithful Faust clones of the Pure Data objects LAR needs, each grounded line-by-line
in the Pd C source and **verified numerically** against a Python oracle that
re-implements the Pd formulas — match to float32 precision.

| Clone | Pd source | Verification | max \|diff\| |
|---|---|---|---|
| `spd.powtodb` | `x_acoustics.c` | 1→100, 0.01→80, 100→120, 0→0 | exact |
| `spd.rmstodb` | `x_acoustics.c` | scalar map | exact |
| `spd.dbtopow` | `x_acoustics.c` | scalar map | exact |
| `spd.dbtorms` | `x_acoustics.c` | 100→1, 80→0.1, 60→0.01, 0→0 | ~1e-9 |
| `spd.hip(f)` | `d_filter.c` `sighip` | impulse response vs closed form | 1.7e-8 |
| `spd.env(np,pd)` | `d_ctl.c` `sigenv` | DC + sine, incl. hold timing | 5e-6 |

`seam.lib` registers `spd` (one line, after `scy`).

### Step 2 — LAR homeostatic brick in `temp/seam.discipio.lib` (`sds`)
Composes the clones (does NOT re-implement Pd math); sits alongside the existing AE2
granular code with no name collision (`sds.integrator`, `sds.localmax` still compile).

- `sds.hannrms(npoints, period)` = `spd.env : spd.dbtorms` — Hann-weighted RMS at
  control rate. (`dbtorms∘powtodb` collapses to `sqrt(ms)`, but composing the real
  clones keeps env's control-rate step-hold faithful.)
- `sds.homeostat(reference, k, r)` = `|r - reference|^k` — LAR values reference=1, k=40.
- `sds.larsengain(npoints, period, reference, k)` = `hannrms : homeostat`.

Verified: DC=0.5 → r=0.5 → g = 0.5^40 = 9.09e-13 (machine precision).

### Key design decisions (locked in, with rationale)
- **`hip` uses Pd's literal truncated `3.14159`** (not `ma.PI`) so `coef` is
  bit-identical to Pd. It is `fi.pole(coef):fi.zero(1):*(normal)` — the exact Pd
  topology, which DIFFERS from `fi.highpass` (Butterworth/biquad). LAR's active loop
  filter is `hip~ 100` (the `hip~ 200` in `pd input` is disconnected).
- **`env` reproduces the control-rate step-hold with `ba.sAndH(ba.pulse(period))`,
  not `ondemand`.** For env the heavy accumulation must run full rate anyway, so
  ondemand would only save the trivial per-tick `powtodb`; sAndH is behaviourally
  identical and keeps `seam.pdclone.lib` (hence all of `seam.lib`) on **stable faust**.
- **SR-independence lives in the C++ deliverable**, not the Faust spec: C++ sizes the
  window `round(fs*2048/44100)` at `prepare(fs)`. The Faust `.lib` is the math spec.
- **Faust has no arity overloading** → `env(npoints, period)` is two explicit args
  (Pd's one-arg `env~ n` defaults `period = n/2`). Don't name a param `exp` (it is
  Faust's exponential primitive) — the brick uses `k`.

### Verification method (reusable)
Scratch harness in the session scratchpad: `faust -lang cpp` wraps each clone in a
tiny C++ driver (`harness.cpp`) fed a controlled input (impulse / DC / sine); output
compared to `oracle.py` (Pd formulas). CLAUDE.md sanctions `faust -lang cpp` as a
scratch/verification tool only — it never lands in `plugins/*/source/`. Rebuild anytime:
`faust -I <src> -a harness.cpp x.dsp -o x_h.cpp && c++ -std=c++14 -I <faust include> x_h.cpp -o x`.

## THE CENTRAL DECISION FOR PHASE 3 — the `ondemand` jump

Available toolchain: `faust-od` (dev branch `master-dev-ocpp-od-fir-2-FIR20`, memory
`reference_faust_od_binary`) + a local IDE that exercises `ondemand`.

### The problem it would address
`spd.env(npoints, period)` is a sliding **FIR**: `sum(i, npoints, w(i)*(x@i)^2)`, which
unrolls to `npoints` taps at compile time. It is spec-clear and correct, but:
- `env(2048, 1024)` takes **>2 min to compile** (measured: 115 s user), on BOTH stable
  faust and `faust-od` — the FIR rework in the dev branch does not turn a `sum` of
  tapped delays into a loop.
- So the flagship `dslar.dsp` at the real window can't be compiled to generate the
  step-3 SVG / mathdoc, and the inline test / demo must use a modest `npoints`.

### Why Pd doesn't have this problem
`env_tilde_perform` is NOT a 2048-tap-per-sample FIR. It is **overlap-add**: each block
accumulates Hann-weighted `x²` into `MAXOVERLAP` `sumbuf[]` slots and emits `sumbuf[0]`
every `realperiod` (default `npoints/2`). Cost is O(overlap) ≈ 2 per sample, not
O(npoints). This overlap-add structure is exactly what `ondemand` expresses naturally:
full-rate accumulation of a few active windows, readout/`powtodb` on demand at control
rate, held between ticks.

### The `ondemand` idiom (validated in ltburst)
`(onset, signal) : ondemand(_)` — a forward per-tick decision that holds between ticks,
mapping **1:1 to `if (onset)` in the C++ port** (ltburst study §"Quando ondemand calza").
`onset = (phase < phase') | (1 - 1')` from a fixed-period phasor. `ondemand` earns its
keep when the gated subgraph is costly AND idle between ticks.

### The jump — two things it could unlock
1. **A full-window-compilable `env`**: structured as Pd's overlap-add (~2 active windows,
   O(overlap)), it compiles instantly even at `env(2048, 1024)`, using `ondemand` for the
   per-window clock. Enables step-3 SVG/mathdoc at the real window and a faithful
   block-kernel match. Testable directly in the local ondemand IDE.
2. **SR-adaptive window** natively (runtime N via `de.delay`), which the compile-time
   `sum` FIR cannot express — could fold SR-independence back into the Faust spec.

### The cost / tradeoff
- Overlap-add-in-Faust is more code and needs its own numerical verification (against the
  same Pd oracle) — the phase indexing of `sumbuf` and the window clock are fiddly.
- Any `ondemand` code requires `faust-od`. Keep it **out of the shared `seam.pdclone.lib`**
  (or a clearly gated variant) so the rest of `seam.lib` still builds on stable faust.
  Candidate homes: a `spd.envc` variant, or keep it at the `dslar.dsp` assembly layer.
- Recommendation to evaluate next session: prototype the overlap-add `env` in the local
  ondemand IDE, verify it against `oracle.py`, and only then decide whether it replaces or
  sits beside the sAndH `env`. The current sAndH `env` is correct and sufficient for the
  plugin (the C++ does overlap-add regardless) — the jump is about the Faust SPEC/doc,
  not about plugin correctness.

## Remaining Phase 3+ work (from phase2-handoff, updated)
- **`line` clone** (`x_time.c`) — control-rate ramp; LAR uses two: control ramp 200 ms,
  input ramp 2000 ms. Ties to the block-kernel check (`project_pd_vs_faust_block_validation`).
- **`delread~` / `delwrite~`** (`d_delay.c`) — confirm vs `de.delay`. LAR: `tab1 50` (loop
  delay), `tab2 20` (analysis tap).
- **`dslar.dsp` assembly** — wire the mono Larsen loop: input drive · `line 2000` · `hip 100`
  · pre-gain · `delwrite/delread tab1 50` · loop `*` (× `larsengain` via `line 200`) · output.
  Then `tools/gen-faust-doc.sh dslar` (memory `reference_faust_doc_generation`).
- **C++ plugin** (`plugins/dslar/source/`, hand-written, overlap-add env, finished GUI),
  following `ddelay`/`ltburst`; reuse `ddelay`'s `nextPrime` if decorrelation snapping.
- **Validation** — the two A/B axes (44.1 vs 96 kHz for SR-independence; Pd patch vs Faust
  for the block kernel), then `doc/math` (English) + `doc/dslar-validation.md`.

## Still-open questions carried forward
- Exact `sds`/`spd` names once AE2 (`fc2003dsaae2`) usage is cross-checked — additions so
  far collide with nothing.
- Whether the decorrelation delay ever graduates to `sds` (only when a 2nd patch claims it).
- Editorial: the reference PDF filename says 2007 but the work is LAC 2006 — consider renaming.

## Build / locations
- faust-libraries branch `dslar`, commit `4e7e42b`. Study: `plugins/dslar/doc/study && make`.
- VST3 SDK: `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` (`-DSEAM_VST3SDK_DIR=...`).
- Memories updated: `project_pdclone_library_architecture` (steps 1-2 status + decisions).
