# dslar Phase 3 — complete the Faust spec (`line` clone + `sds.lar` assembly)

Date: 2026-07-10
Status: design approved, pending written-spec review
Author: Giuseppe Silvi, with Claude
Predecessors: `docs/superpowers/specs/2026-07-09-discipio-porting-dslar-design.md` (program charter),
`plugins/dslar/doc/phase2-handoff.md`, `plugins/dslar/doc/phase3-handoff.md`

## Summary

Phase 2 steps 1-2 delivered the verified Pd clones (`spd.powtodb/rmstodb/dbtopow/dbtorms/hip/env`)
and the homeostatic brick `sds` (`hannrms`, `homeostat`, `larsengain`), both grounded in the Pd C
source and verified to float precision against a Python oracle.
This sub-project closes the **Faust specification** of `LAR.pd`: it adds the last missing Pd object
(`line`), assembles the full mono processor as a reusable `sds.lar` brick, and produces the compilable
`plugins/dslar/doc/dslar.dsp` entry with SVG + mathdoc.
It stops before the C++ plugin and before the bit-exact Pd↔Faust and 44.1/96 kHz validation, which are
their own sub-projects.

This documentation is written to be **shared with Agostino Di Scipio** to validate the whole porting
process, so both the formal spec (this file, English) and the Italian study diary
(`plugins/dslar/doc/study`) record not only the sources but the choices made and the changes of
direction.

## Key architectural finding — the loop is feedforward, the Larsen is acoustic

Tracing every `#X connect` in `LAR.pd` shows the patch has **no internal feedback**.
The only signal source is `adc~`; `r~ audioLAR` reaches the `oscilloscope` monitor only, never the input.
`delread~ tab1` reads what `delwrite~ tab1` writes from the audio branch and returns it to the loop
multiplier **without closing back onto the input**.
So `tab1 50 ms` is a **feedforward delay**, not a recursion: the Larsen loop is **acoustic**
(`dac~` → room → mic → `adc~`), external to the plugin — the room is Di Scipio's *oikos*.
The `pd input` subpatch (the two `hip~ 200`) is disconnected dead code, as previously noted.

Consequence: `dslar.dsp` is a **pure mono feedforward chain** — no `~` feedback, and the block-kernel
"one-block feedback delay" concern belongs to the acoustic loop, outside the plugin.

### The two branches (from `LAR.pd`, mirrored in the study Table `tab:flusso`)

```
in ─ fade(2000 ms) ─┬─ AUDIO:    hip(100) ─ ×drive(1/2/4) ─ delay(tab1 50 ms) ─┐
                    │                                                          ×── ×output ─ out
                    └─ ANALYSIS: delay(tab2 20 ms) ─ env(2048) ─ dbtorms ─ |·−1|^40 ─ line(200 ms) ─┘
```

The analysis gain multiplies the audio branch at the loop multiplier; `output` is the final VCA to the host.
Both outputs of the Pd patch are identical duplicates → the Faust processor is **mono 1-in / 1-out**
(memory `feedback_porting_mono_io_normalization`, study §"Verdetto sull'I/O: mono").

## Design decisions

### D1 — `spd.line` is a faithful clone, not a thin bridge

Pd's control `line` (`x_time.c`) ramps linearly toward a target and **emits at a grain interval**
(default `DEFAULTLINEGRAIN = 20 ms`), producing a control-rate staircase; `ba.line` alone is
sample-accurate and would drop that staircase.
We reproduce the staircase, reusing `ba.line` as the underlying linear engine (it already restarts from
the current value on a new target, exactly like Pd):

```faust
// x_time.c line_tick: linear ramp sampled at the grain (default 20 ms), held between ticks.
// ba.line drives the ramp; sAndH on a grain pulse imposes Pd's control-rate step-hold.
// FAUST vs Pd line: reproduces the 20 ms grain staircase; the tick PHASE (Pd aligns ticks to
// message arrival, this aligns to sample 0) is the documented residual — bit-exact scheduler
// emulation lives in the block-kernel validation sub-project, as for spd.env.
line(ms) = ba.line(ms * ma.SR/1000.0) : ba.sAndH(ba.pulse(20.0 * ma.SR/1000.0));
```

This is the same idiom as `spd.env` (real math underneath, `sAndH(ba.pulse(period))` for the control-rate
hold on top).
Verification: a Python oracle implementing `line_tick`'s grain sampling; target the same ~1e-6 tolerance as
`env`; the residual is tick-phase alignment (documented, out of scope here).

### D2 — `delread~`/`delwrite~` compose `de.delay` directly

Both LAR named lines are single-writer/single-reader (`tab1 50`, `tab2 20`), so
`de.delay(n,d,x) = x@min(n,max(0,d))` is an exact match; no `spd` re-implementation is written.
They still receive a documentation entry (D5) citing `d_delay.c` + `delay-tilde-objects-help.pd`, so every
Pd object we touch — whether re-implemented or mapped to standard Faust — carries its dual source.
The Pd one-block minimum does not bind (50/20 ms ≫ 64 samples).

### D3 — `sds.lar` feedforward brick in `seam.discipio.lib`

The Di Scipio library is promoted `temp/` → `src/` and registered in `seam.lib` as part of this sub-project
(decided mid-brainstorm — the library is under active curation, "it is the right moment to put it back in
its place"), so `dslar.dsp` resolves `sds` via the standard `FAUST_LIB_PATH`.
The reusable spec of the LAR processor composes verified bricks only (no re-implemented math):

```faust
// tab1 = 50 ms loop delay, tab2 = 20 ms decorrelation tap; ref = 1, k = 40 (LAR values).
// gate = system on/off (0/1); the 2000 ms fade sits BEFORE the fan-out, so it lives inside the brick.
ms2samp(ms) = ms * ma.SR/1000.0;
lar(gate, drive, ref, k, tsmooth, tab1, tab2, output, x) =
    audio(fx) * analysisGain(fx) * output          // loop multiplier, then × output VCA
with {
    fx = x * (gate : spd.line(2000));               // adc~ × fade, before the audio/analysis split
    audio(s)        = s : spd.hip(100) : *(drive) : de.delay(ms2samp(tab1), ms2samp(tab1));
    analysisGain(s) = s : de.delay(ms2samp(tab2), ms2samp(tab2))
                        : sds.larsengain(2048, 1024, ref, k)
                        : spd.line(tsmooth);
};
```

`sds.larsengain` already exists and equals `spd.env : spd.dbtorms : homeostat` = the Pd chain
`env~ → dbtorms → (−1 → abs → pow 40)`, verified end to end (DC=0.5 → `0.5^40`).
The exact signature (argument order, whether `fade`/`gate` is a brick argument or lives at the `.dsp` layer)
is finalized at implementation; the shape above is the contract.

### D4 — `plugins/dslar/doc/dslar.dsp` is a thin entry

Following the `ddelay.dsp → sma.imdelay` model: `import("seam.lib"); process = sds.lar(...);` with `hslider`s
for the core DSP parameters so they appear in the block diagram and mathdoc.
Parameters exposed: gate (system on/off, 2000 ms anti-click fade), drive, target (ref), steepness (k),
control-smoothing time (200 ms), decorrelation time (20/50 ms), output level.
The internal-loop feedback path is **not** a feature: `sds.lar` is feedforward, faithful to the acoustic-loop
patch.
If study objects that close the loop internally are wanted later, they are **compiled separately**, not
folded into `dslar.dsp`.
Then `tools/gen-faust-doc.sh dslar` regenerates `dslar-svg/` + `dslar.pdf`
(memory `reference_faust_doc_generation`).

### D5 — dual-source documentation convention across `seam.pdclone.lib`

Every `spd` clone header cites **both** the Pd C source (file + function) **and** the Pd help patch
(Miller Puckette's standard documentation), so the library is self-documenting and traceable to the
canonical references.

| `spd` object | Pd C source | Pd help patch |
|---|---|---|
| `powtodb` `rmstodb` `dbtopow` `dbtorms` | `x_acoustics.c` | `acoustics-help.pd` |
| `hip` | `d_filter.c` | `hip~-help.pd` |
| `env` | `d_ctl.c` | `env~-help.pd` |
| `line` (new) | `x_time.c` | `line-help.pd` |
| `delread~`/`delwrite~` → `de.delay` | `d_delay.c` | `delay-tilde-objects-help.pd` |

Actions: backfill the help-patch citation into the already-shipped clones (`env`, `hip`, `acoustics` group);
add both citations to `spd.line`.
Copy the relevant help patches into `plugins/dslar/doc/references/pd-help/` so the documentation is
self-contained in the repo (the `pure-data` checkout is an external/local sibling, memory
`reference_external_source_checkouts`, not guaranteed on another machine).

### D6 — the study diary tracks the choices and the changes of direction

`plugins/dslar/doc/study` (Italian LaTeX diary; CLAUDE.md diary exception) is updated to record, beyond the
source references:

- the **feedforward finding** (the internal loop is acoustic, `tab1` is a feedforward delay) — this refines
  the Phase-1/2 "loop" framing and must be visible as a change of direction;
- the decision to clone `line` faithfully (grain staircase) rather than compose `ba.line`, with rationale;
- the dual-source documentation convention (C source + Pd help patch);
- that this documentation is prepared to be shared with Agostino Di Scipio to validate the process.

The formal `doc/math/` (English) stays reserved for the later mathematical documentation sub-project.

## Verification (in scope)

- `spd.line` matches the Pd `line_tick` grain oracle to ~1e-6 (Python oracle; scratch `faust -lang cpp`
  harness as in Phase 2, never landed in `plugins/*/source/`).
- `dslar.dsp` compiles at the full window (`spd.env` overlap-add makes `env(2048,1024)` compile in ~0.16 s).
- `gen-faust-doc.sh dslar` produces `dslar-svg/` + `dslar.pdf`.
- End-to-end sanity: the homeostatic gain regulates toward `ref` (the existing DC=0.5 → `0.5^40` check,
  re-run through the assembled `sds.lar`).

## Out of scope (later sub-projects)

- Bit-exact Pd↔Faust block-kernel A/B and 44.1/96 kHz SR-independence A/B (validation sub-project).
- The C++ `plugins/dslar/source/` plugin (hand-written, overlap-add env, finished GUI) and its optional
  internal-loop toggle.
- Formal English `doc/math/`.

## File / location map

| Artifact | Location |
|---|---|
| `spd.line` + doc-convention backfill | `faust-libraries/src/seam.pdclone.lib` (branch `dslar`) |
| `seam.discipio.lib` promotion (`temp`→`src`) + `seam.lib` registration | `faust-libraries/src/` (branch `dslar`) |
| `sds.lar` brick | `faust-libraries/src/seam.discipio.lib` (branch `dslar`) |
| `dslar.dsp` entry | `seam-ltm/plugins/dslar/doc/dslar.dsp` |
| SVG + mathdoc | `seam-ltm/plugins/dslar/doc/dslar-svg/`, `dslar.pdf` (via `tools/gen-faust-doc.sh dslar`) |
| Pd help patches (copied) | `seam-ltm/plugins/dslar/doc/references/pd-help/` |
| Study diary updates | `seam-ltm/plugins/dslar/doc/study/dslar-study.tex` |

## Open questions carried forward

- Exact `sds`/`spd` names once AE2 (`fc2003dsaae2`) usage is cross-checked (charter open question).
- Whether the decorrelation delay ever graduates to an `sds` brick (only when a second patch claims it).
- Editorial: the reference PDF filename says 2007 but the work is LAC 2006 — consider renaming.
