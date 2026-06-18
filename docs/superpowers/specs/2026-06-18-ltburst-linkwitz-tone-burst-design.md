# ltburst — Linkwitz Shaped Tone-Burst Generator (design)

Date: 2026-06-18
Status: approved (brainstorming) — Phase 1 to be planned next
Related: issue #6 (test toolkit), issue #5 (hilbert FIR topology), the quadrature/UHJ study

## Summary

`ltburst` is a new seam-ltm object: a **shaped tone-burst generator** after
Siegfried Linkwitz. It is the first object in the suite whose DSP specification
does **not** yet exist in the SEAM Faust libraries — we author that
specification as part of this work. It is also a paradigm shift for the suite:
the first **signal generator** (the suite so far is processors and
spatialisation), and the first object built as a **documented study from
primary literature**, written as a step-by-step research diary that doubles as a
candidate paper / Italian teaching text.

The two source papers are the same work in two stages:

- AES preprint **1342 (F-4)**, *"Narrow Band Impulse Testing of Acoustical
  Systems"*, S. Linkwitz, 60th AES Convention, 1978 — the fuller conference
  version (22 pp., with derivations).
- *"Shaped Tone-Burst Testing"*, S. Linkwitz, J. Audio Eng. Soc. **28**(4),
  1980 — the condensed, refined journal version (9 pp.).

## Motivation

- A shaped tone burst is a **constant-Q, narrow-band** test stimulus: holding a
  constant cycle count (N = 5) makes the spectral width a fixed fraction of the
  centre frequency (~1/3 octave), giving uniform 1/3-octave resolution across
  the band.
- Linkwitz notes it is *"a very sensitive indicator of phase distortion in
  subjective evaluations of all-pass networks"* — directly relevant to
  validating the `hilbert`/`x2uhj` all-pass topologies (−90° phase difference,
  group delay, all-pass magnitude flatness), tying this work to #5 and #6.
- It is a near-ideal SEAM object: the mathematics is fully specified and simple,
  it is literature-anchored (consistent with the suite's citation culture), and
  it carries genuine pedagogical content (exact Hann window vs. Linkwitz's
  quantised staircase approximation and the odd harmonics it produces).
- There is no Italian-language literature of this kind; the study diary fills
  that gap.

## Core paradigm decisions

These decisions were settled during brainstorming and govern the whole arc.

1. **`ltburst`** is the object name and file prefix
   (`ltburst_processor.cpp`, etc.) — terse, C-friendly, consistent with the
   suite (`ddelay`, `bamodulex`, `hilbert`).
2. **Author-attributed Faust library.** The DSP spec lands in a new
   `src/seam.linkwitz.lib` with prefix **`slw`** (seam-LinkWitz), following the
   person-attributed pattern of Gerzon (`smg`), Moorer (`sjm`), Roads (`scr`).
   The tone burst is its first inhabitant; future Linkwitz contributions
   (Linkwitz-Riley crossovers, the Linkwitz transform, etc.) join later.
3. **Two distinct documents, two languages.**
   - `doc/study/` — the narrative research diary / candidate paper, in
     **Italian**. This is a new document type for the suite and the connective
     tissue of the whole project.
   - `doc/math/` — the formal mathematical documentation, in **English**,
     consistent with the other plugins (model: `hilbert/doc/math`). Produced at
     the end, reusing material matured in the diary.
   The seam-ltm convention "documentation in English" is preserved for the
   formal math doc; the Italian diary is an explicit, documented exception (to
   be noted in CLAUDE.md).
4. **Document format: LaTeX** (`.tex` + `.bib` → `.pdf`), consistent with
   `hilbert/doc/math`, submission-ready, figure- and citation-capable. Prose
   style: one sentence per line (clean diffs).
5. **Faust is the spec, C++ is the deliverable** — the standing suite
   convention. We author the Faust spec by hand and port it to C++ by hand; no
   `faust -lang cpp` code lands in `source/`.

## Repository layout

Phase 1 creates only the documentation tree. Source, CMake, and resources are
created in later phases.

```
plugins/ltburst/
└── doc/
    ├── study/                          # narrative diary / paper — ITALIAN
    │   ├── ltburst-study.tex
    │   ├── refs.bib                    # the two Linkwitz papers
    │   ├── figures/                    # cropped originals + our generated plots
    │   └── ltburst-study.pdf           # compiled (pdflatex)
    ├── math/                           # formal math doc — ENGLISH (Phase 4)
    └── references/                     # source literature (copied into the repo)
        ├── 1978-linkwitz-narrow-band-impulse-testing-aes-preprint-1342.pdf
        └── 1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf
```

The source PDFs are copied from
`gitlab/gs/linkwitz/ref/tone-burst/{3012,3989}.pdf` into `doc/references/` with
descriptive `year-author-title` names (the convention already used for the
Puckette extract in `hilbert/doc/references`), so the repo is self-contained.

`doc/math/` is created empty in Phase 1 (or deferred to Phase 4); it is filled
at the end of the arc.

## The study diary structure (`ltburst-study.tex`)

The diary reads like a paper but its order follows the work itself, so narrative
and development advance together. Sections grow across phases; sections 1–4 are
the Phase 1 content.

1. **Introduction / motivation** — why a Linkwitz tone burst, why in SEAM, what
   gap it fills (the Italian teaching text).
2. **Study of the sources** *(core of Phase 1)* — a reasoned reading of the two
   papers: the lineage (preprint 1342, 1978 → JAES, 1980), transcription of the
   urgent and necessary passages, with `\cite` pointers to `refs.bib`.
3. **Theory and mathematics** — the shaped burst `x(t) = w(t)·sin(2πf₀t)` with
   `w(t) = ½ − ½cos(2πt/T)`, `T = N/f₀`; the constant-Q property (constant N →
   bandwidth a fixed fraction of f₀ → ~1/3 octave); the spectrum (asymmetric
   roll-off, ~6 dB/oct low, ~12 dB/oct high); exact Hann window **vs.**
   Linkwitz's staircase approximation (the ten half-cycle amplitudes
   `0.081, 0.298, 0.583, 0.845, 1.000, 1.000, 0.845, 0.583, 0.298, 0.081` and
   the odd harmonics from the zero-crossing slope discontinuities); the
   gating/dwell period for echo discrimination.
4. **Validation targets** — which original figures serve as references and which
   quantities we compare.
5. *(Phase 2)* **Reconstruction in Faust** — the spec, design choices, generated
   plots vs. original figures.
6. *(Phase 3)* **Port to C++** — concept-by-concept correspondence with the
   Faust spec, lifecycle/memory choices.
7. *(Phase 4)* **The plugin** — VST3 integration, GUI, use as a test/calibration
   source (link to #5/#6).
8. **Conclusions / toward a paper** — what is reproducible, what remains open.

## Build arc (phased milestones)

This is a single living spec; each phase after Phase 1 gets its own
implementation plan when reached. Only Phase 1 is planned in detail now.

- **Phase 1 — Study & documentation** *(planned now)*
  Scaffold `doc/`, copy the two papers into `references/`, write diary sections
  1–4, crop the reference figures. No code, no CMake changes.

- **Phase 2 — Faust spec → library**
  Author `src/seam.linkwitz.lib` (prefix `slw`) in
  `librerie/faust-libraries`, import it in `seam.lib`, cite Linkwitz in the
  `declare` block. Generate plots from the Faust signal and compare against the
  original figures in the diary.

- **Phase 3 — Hand port to C++**
  `plugins/ltburst/source/` with the canonical layout
  (`ltburst_processor.{h,cpp}`, `ltburst_ids.h`, `version.h`), a
  `// FAUST REFERENCE (seam.linkwitz.lib): ...` block atop the processor header,
  an SDK-free `doctest` DSP core, and registration via
  `add_subdirectory(plugins/ltburst)` in the root CMake.

- **Phase 4 — VST3 plugin + GUI + math doc**
  Branded VSTGUI window (`resource/ltburst.uidesc`), operational hook to #5/#6
  as a test/calibration source, and the formal English `doc/math` document
  (model: hilbert).

## Validation approach

Each reconstruction stage is validated against the source with side-by-side
comparable plots.

- **References from the papers** — crop the original figures into
  `figures/orig-*.png`: **Fig. 1** (rectangular 5-cycle burst spectrum,
  6/12 dB/oct roll-off), **Fig. 3** (Hann-windowed spectrum), **Fig. 8**
  (staircase approximation and the `0.081…1.000` amplitudes).
- **Quantities compared**:
  1. burst waveform in time (Hann shaping, 5 cycles);
  2. spectrum in dB on a log f/f₀ axis — asymmetric roll-off and ~1/3-octave
     main-lobe width;
  3. exact Hann vs. Linkwitz staircase — appearance of odd harmonics.
- **Where our plots come from** — from the **Faust** spec (Phase 2): generate
  the signal, compute its spectrum, emit PNGs to sit beside the originals. The
  plotting tool (offline Python/numpy+matplotlib, outside the plugin build) is
  fixed in the Phase 2 plan; Phase 1 defines only *which* comparisons we want
  and the qualitative agreement criteria.
- **Link to the #6 toolkit** — these comparisons (spectrum, burst shape) are
  building blocks of the test toolkit; the `ltburst` diary becomes its first
  documented use case, without committing the scoping of #6 (which remains a
  face-to-face discussion).

## Out of scope (now)

- The Faust, C++, GUI, and math-doc phases are described but not planned in
  detail here; each gets its own plan.
- The broader #6 generators toolkit architecture is not designed here.
- No peer-aware behaviour (multipink pattern) — not motivated.

## DSP reference (for later phases)

The essential model to port:

```
x(t) = w(t) · sin(2π f₀ t)
w(t) = ½ − ½ cos(2π t / T),  T = N / f₀,  N = 5
```

plus a repetition period with a silent dwell so room echoes decay between
bursts. Two window variants are pedagogically in scope: the exact Hann window
and Linkwitz's quantised half-cycle staircase (Fig. 8). Holding N constant keeps
the spectral width a constant fraction of f₀ (the constant-Q property).
```

## Open items to carry into Phase 1 planning

- Exact qualitative agreement criteria for the plot comparisons.
- Whether `doc/math/` is created empty now or deferred to Phase 4.
- The CLAUDE.md note recording the Italian-diary exception.
