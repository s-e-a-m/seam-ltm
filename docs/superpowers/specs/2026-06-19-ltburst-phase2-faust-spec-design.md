# ltburst Phase 2 — Faust Spec in seam.linkwitz.lib (design)

Date: 2026-06-19
Status: approved (brainstorming) — Phase 2 to be planned next
Parent: docs/superpowers/specs/2026-06-18-ltburst-linkwitz-tone-burst-design.md (the project spec)
Phase 1 (study & documentation): complete on branch feat/ltburst-linkwitz-tone-burst

## Summary

Phase 2 authors the Faust specification of the Linkwitz shaped tone-burst as a
new author-attributed library `seam.linkwitz.lib` (prefix `slw`) in the
`faust-libraries` repository, registers it in `seam.lib`, and documents the
reconstruction in Section 5 of the Italian study diary. The Faust library
contains **only the generator** (the "useful cycle"). The comparison/validation
against Linkwitz's published spectra (and the staircase approximation) is **not**
done in Faust — it belongs to the C++ phase (Phase 3) alongside the test toolkit
(issue #6).

## Scope decision (settled in brainstorming)

- Faust holds the **generator only**. No validation, no plotting, no spectral
  comparison in Faust. This removes the plotting-toolchain dependency entirely
  (the environment has Faust + base Python only — no numpy/matplotlib/gnuplot/
  octave). Validation moves to C++/test-toolkit in Phase 3.
- The **staircase** approximation is comparison material → Phase 3 (C++). Faust
  ships the exact raised-cosine (Hann) burst, which is the ideal Linkwitz poses
  as the target.

## Fidelity to Linkwitz (why the design is what it is)

The two papers describe the burst at two levels:

- **Mathematical object (1980 §2):** a continuous sine multiplied by a window,
  `x(t) = w(t)·sin(2π f₀ t)`, with `w(t) = ½ − ½cos(2π t/T)`, `T = N/f₀`, and a
  **constant** cycle count **N = 5**. Constant N makes T shrink with frequency,
  so the spectral width stays a fixed fraction of f₀ (~1/3 octave) — the
  constant-Q property.
- **Generation (1980 §4, "Test System"):** the carrier f₀ is 100% amplitude
  modulated by a sine at one-fifth the frequency (f₀/5) — one modulator cycle
  spans exactly five carrier cycles, i.e. the raised-cosine envelope — and after
  each burst the signal is **blanked ("dwell") for several hundred milliseconds**
  so room echoes decay. The blanking/repetition is intrinsic to the method
  (echo discrimination), not an add-on. The Appendix notes the hardware
  synchronises the start of each burst to a carrier zero crossing.

## Core correctness constraint: cycle-synchronous generation

The oscillator never stops; the dwell is silence produced by multiplying the
running carrier by zero, not by halting it. The window onset must coincide with
a carrier zero crossing and span exactly N carrier cycles, and the repetition
period must be a whole number of carrier cycles — otherwise each new burst
starts at a random carrier phase and the Hann window no longer aligns with the
carrier, producing discontinuities and spectral artifacts. Linkwitz's hardware
enforces this with a zero-crossing sync flip-flop.

The design satisfies this by deriving **everything from a single phasor**, so
the carrier and the window cannot drift out of phase.

## The generator

Macro period `P = N + M` carrier cycles: N burst cycles + M dwell cycles. A
phasor at `f0/P` sweeps 0→1 over P carrier cycles; multiplying by P gives the
cycle position `u ∈ [0, P)`. The carrier is derived from the same phasor as
`sin(2π·P·c)`, so carrier and window share one phase reference.

```faust
// raised-cosine N-cycle burst, repeated every P=N+M carrier cycles, cycle-synchronous
shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win
  with {
    M   = max(1, int(ceil(dwell*f0)));        // dwell quantised to whole carrier cycles
    P   = N + M;
    c   = os.phasor(1, f0/P);                 // 0..1 over P carrier cycles (confirm signature at compile)
    u   = P*c;                                // cycle position 0..P
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));  // Hann over the first N cycles, then 0
  };
shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);  // canonical N=5 wrapper
// process = slw.shapedburst5(1000, 0.3);
```

Parameters (settled):
- `f0` — carrier frequency (Hz).
- `N` — burst cycle count, **parametric**; canonical value 5 exposed via the
  `shapedburst5` wrapper (Faust has no default arguments). Parametric N
  generalises the constant-N → constant-Q principle to any N.
- `dwell` — silence between bursts in **seconds**, internally quantised to whole
  carrier cycles `M = max(1, ceil(dwell·f0))`. This reproduces Linkwitz's
  "several hundred ms" dwell while keeping the generator cycle-synchronous.

Properties (verifiable): the window always begins at `u=0` (a carrier zero
crossing) and spans exactly N cycles; `win=0` for the M dwell cycles; the
repetition period is P whole cycles so every burst restarts in phase; constant
N gives constant-Q. The repetition rate is `f0/P`.

Note: `os.phasor` / `os.lf_sawpos` exact signatures are confirmed against
`oscillators.lib` at implementation time; the compile step (below) is the gate.

## Library integration

New file `faust-libraries/src/seam.linkwitz.lib`, following the
author-attributed pattern of `seam.gerzon.lib`:

- `declare name "Siegfried Linkwitz - Library";` + version/author/license.
- `import("seam.lib");` then re-declare the own prefix
  `slw = library("seam.linkwitz.lib");`.
- Linkwitz citation block (AES preprint 1342, 1978; JAES 28(4):250–258, 1980)
  and an ASCII illustration of the burst + dwell, consistent with the repo's
  comment style.
- the `shapedburst` / `shapedburst5` functions and a commented inline
  `//process` test.

Register in `faust-libraries/src/seam.lib`, in the existing
"author specific literature" section (beside `smg`/`sjm`/`scr`):

```faust
slw = library("seam.linkwitz.lib");
```

## Study diary update (seam-ltm)

Fill Section 5 "Ricostruzione in Faust" of `plugins/ltburst/doc/study/ltburst-study.tex`
in Italian, one sentence per line:

- map Linkwitz §2 (raised-cosine, N=5, constant-Q) to §4 (100% AM at f₀/5 +
  dwell);
- the cycle-synchronous choice and why (zero-crossing sync; the oscillator never
  stops);
- a listing of the `shapedburst` function with commentary;
- an explicit sentence deferring validation/comparison (spectra, staircase) to
  the C++ phase, keeping the diary consistent with this decision.

No plots in this phase. Section 5 cites `seam.linkwitz.lib` as the spec source;
it does not duplicate the file.

## Cross-repo logistics

Phase 2 touches two independent git repositories:

| Repo | Change | Branch |
|---|---|---|
| `faust-libraries` | new `src/seam.linkwitz.lib` + one line in `seam.lib` | new branch `feat/linkwitz-shaped-tone-burst` |
| `seam-ltm` | study diary Section 5 (+ rebuilt PDF) | existing `feat/ltburst-linkwitz-tone-burst` |

`faust-libraries` is a separate repo with its own PR flow, so it gets its own
branch and commit/PR. The seam-ltm diary continues on the Phase 1 branch.

## Verification (replaces plots for this phase)

The Faust spec must compile. Concrete gates:

- standalone function compiles:
  `echo 'import("seam.linkwitz.lib"); process = slw.shapedburst5(1000,0.3);' | faust -`
- `seam.lib` with the new line compiles a process that uses `slw`:
  `echo 'import("seam.lib"); process = slw.shapedburst(1000,5,0.3);' | faust -`
- the inline `//process` test in the lib compiles when uncommented.
- the study diary still builds (`make` in doc/study) after the Section 5 edit.

## Out of scope (now)

- Validation/plots, spectral comparison to Figs. 1/3/8, the staircase variant —
  all Phase 3 (C++) with the test toolkit (#6).
- C++ port, plugin, GUI, formal math doc — later phases.
