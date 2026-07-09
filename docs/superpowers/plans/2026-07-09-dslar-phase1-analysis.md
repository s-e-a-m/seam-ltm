# dslar — Fase 1: analisi documentata di LAR.pd — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a grounded, PDF-built study document (`dslar-study.tex`, Italian) that analyses Agostino Di Scipio's `LAR.pd` from its own source and primary literature, so it can drive the later `sds`/Faust/plugin phases.

**Architecture:** A LaTeX "diario di studio" under `plugins/dslar/doc/study/`, mirroring `ltburst` exactly (same preamble, Makefile, `latexmk` build). The patch source and the relevant Di Scipio PDFs are ingested into `plugins/dslar/doc/references/`. The document is written section by section; each task adds one grounded section and ends with a clean PDF build and a commit. No code, no Faust, no plugin in this phase.

**Tech Stack:** LaTeX (`article`, `babel italian`, `listings`, `siunitx`, `hyperref`), `latexmk`, BibTeX (`refs.bib`), `make`.

## Global Constraints

- Study language is **Italian**; the doc lives in `doc/study/` (formal English math is Phase 5, `doc/math/`, out of scope here).
- LaTeX prose is written **one sentence per line** (clean git diffs), in an **affirmative explanatory voice** (avoid negations / "rather than") — per memory `feedback_latex_writing_style`.
- Every quantitative claim about the patch **must trace to a line in the real `LAR.pd` source**, not to the screenshot.
- `SR_ref = 44100`. Sample-relative constants are expressed as **time anchored at 44100**; for LAR the only such constant is `env~ 2048` (= 2048/44100 s ≈ 46.4 ms). See memory `project_discipio_sr_independence`.
- Naming: plugin/doc stem is **`dslar`**.
- Follow the `ltburst` doc structure and Makefile verbatim; introduce no new tooling.
- The whole plan targets the branch **`dslar`** (already created).

**Prerequisite (blocking Task 1):** the real `LAR.pd` file is **not yet in the workspace or the literature folder**. Giuseppe must supply its path before execution starts; it is copied into `plugins/dslar/doc/references/LAR.pd` in Task 1.

**Reference literature already available** in `/Volumes/Aleph/aq-dream/gs/autori/di-scipio-agostino/`:
`2007-di-scipio-using-pd-live-interactions-sound-exploratory-modes-of-interference-3.pdf`,
`2011-di-scipio-listening-yourself-otherself-background-noise-study.pdf`,
`2002-di-scipio-audible-ecosystemics-2.pdf`,
`2003-di-scipio-sound-is-the-interface-interactive-ecosystemic.pdf`.

---

### Task 1: Scaffold `dslar/doc/` and ingest sources

**Files:**
- Create: `plugins/dslar/doc/study/Makefile`
- Create: `plugins/dslar/doc/study/.gitignore`
- Create: `plugins/dslar/doc/study/refs.bib`
- Create: `plugins/dslar/doc/study/figures/.gitkeep`
- Create: `plugins/dslar/doc/study/dslar-study.tex`
- Create: `plugins/dslar/doc/math/.gitkeep`
- Create: `plugins/dslar/doc/references/LAR.pd` (copied from Giuseppe's path)
- Create: `plugins/dslar/doc/references/` Di Scipio PDFs (copied)

**Interfaces:**
- Produces: the buildable study skeleton `dslar-study.tex` with section stubs `\label{sec:anatomia}`, `\label{sec:contesto}`, `\label{sec:segnale}`, `\label{sec:controllo}`, `\label{sec:sr}`, `\label{sec:decomposizione}`, `\label{sec:sintesi}` consumed by Tasks 2–5. Build entry point `make` in `doc/study/`.

- [ ] **Step 1: Obtain `LAR.pd` and copy it into references**

Ask Giuseppe for the absolute path to `LAR.pd`, then (substitute the real path):

```bash
mkdir -p plugins/dslar/doc/references
cp "<GIUSEPPE_PATH>/LAR.pd" plugins/dslar/doc/references/LAR.pd
head -5 plugins/dslar/doc/references/LAR.pd   # expect Pd "#N canvas ..." header
```
Expected: the file begins with `#N canvas`.

- [ ] **Step 2: Copy the relevant Di Scipio PDFs into references**

```bash
SRC="/Volumes/Aleph/aq-dream/gs/autori/di-scipio-agostino"
cp "$SRC/2007-di-scipio-using-pd-live-interactions-sound-exploratory-modes-of-interference-3.pdf" \
   "$SRC/2011-di-scipio-listening-yourself-otherself-background-noise-study.pdf" \
   "$SRC/2002-di-scipio-audible-ecosystemics-2.pdf" \
   "$SRC/2003-di-scipio-sound-is-the-interface-interactive-ecosystemic.pdf" \
   plugins/dslar/doc/references/
ls plugins/dslar/doc/references/
```
Expected: `LAR.pd` plus four PDFs listed.

- [ ] **Step 3: Create the study scaffold (Makefile, .gitignore, figures, math)**

`plugins/dslar/doc/study/Makefile`:
```make
DOC = dslar-study

.PHONY: all clean
all: $(DOC).pdf

$(DOC).pdf: $(DOC).tex refs.bib
	latexmk -pdf -interaction=nonstopmode $(DOC).tex

clean:
	latexmk -C $(DOC).tex
```

`plugins/dslar/doc/study/.gitignore`:
```gitignore
*.aux
*.log
*.out
*.toc
*.bbl
*.blg
*.fls
*.fdb_latexmk
```

```bash
mkdir -p plugins/dslar/doc/study/figures plugins/dslar/doc/math
touch plugins/dslar/doc/study/figures/.gitkeep plugins/dslar/doc/math/.gitkeep
```

`plugins/dslar/doc/study/refs.bib` (starter, expanded in Task 3):
```bibtex
@article{discipio2003ecosystemics,
  author  = {Di Scipio, Agostino},
  title   = {{Sound is the Interface: From Interactive to Ecosystemic Signal Processing}},
  journal = {Organised Sound},
  volume  = {8},
  number  = {3},
  pages   = {269--277},
  year    = {2003}
}
```

- [ ] **Step 4: Write the study skeleton `dslar-study.tex`**

```latex
\documentclass[11pt,a4paper]{article}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage[italian]{babel}
\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,hyperref}
\usepackage{listings}
\lstdefinestyle{faust}{basicstyle=\ttfamily\small,breaklines=true,
  keepspaces=true,columns=fullflexible,frame=single,framesep=4pt}
\lstset{style=faust}
\usepackage[margin=2.5cm]{geometry}
\graphicspath{{figures/}}

\title{dslar: l'ecosistema di Larsen autoregolato di Di Scipio\\\large Diario di studio e ricostruzione per la suite SEAM-LTM}
\author{Giuseppe Silvi --- SEAM}
\date{2026}

\begin{document}
\maketitle
\begin{abstract}
Questo documento è un diario di studio passo passo.
Analizza la patch \texttt{LAR.pd} di Agostino Di Scipio a partire dal suo sorgente e dalla letteratura primaria dell'autore.
Ne ricostruisce il motore di autoregolazione del feedback acustico, la legge di controllo omeostatica e la strategia di indipendenza dalla frequenza di campionamento, preparando le fasi successive: libreria Faust \texttt{sds}, esempio compilabile e plugin.
\end{abstract}
\tableofcontents

\section{Introduzione e motivazione}\label{sec:intro}
Segnaposto: sostituito in Task 2.

\section{Anatomia della patch}\label{sec:anatomia}
Segnaposto: scritto in Task 2.

\section{Contesto e fonti}\label{sec:contesto}
Segnaposto: scritto in Task 3.

\section{Analisi del segnale}\label{sec:segnale}
Segnaposto: scritto in Task 4.

\section{La legge di controllo omeostatica}\label{sec:controllo}
Segnaposto: scritto in Task 4.

\section{Compatibilità con la frequenza di campionamento}\label{sec:sr}
Segnaposto: scritto in Task 4.

\section{Mappa di decomposizione}\label{sec:decomposizione}
Segnaposto: scritto in Task 5.

\section{Sintesi e ponte alla Fase 2}\label{sec:sintesi}
Segnaposto: scritto in Task 5.

\bibliographystyle{plain}
\bibliography{refs}
\end{document}
```

- [ ] **Step 5: Build the skeleton PDF**

Run:
```bash
cd plugins/dslar/doc/study && make
```
Expected: `dslar-study.pdf` is produced; `latexmk` exits 0; the log shows title, abstract, and eight section headings in the table of contents. (A first-pass `bibtex` "I found no \citation commands" warning is acceptable at this stage.)

- [ ] **Step 6: Commit**

```bash
git add plugins/dslar/doc/study/Makefile plugins/dslar/doc/study/.gitignore \
        plugins/dslar/doc/study/refs.bib plugins/dslar/doc/study/figures/.gitkeep \
        plugins/dslar/doc/study/dslar-study.tex plugins/dslar/doc/math/.gitkeep \
        plugins/dslar/doc/references/
git commit -m "docs(dslar): scaffold doc/study + ingest LAR.pd and Di Scipio references"
```

---

### Task 2: Section "Anatomia della patch" (grounded transcription)

**Files:**
- Modify: `plugins/dslar/doc/study/dslar-study.tex` (replace `\label{sec:intro}` and `\label{sec:anatomia}` stubs)
- Read: `plugins/dslar/doc/references/LAR.pd`

**Interfaces:**
- Consumes: the buildable skeleton from Task 1.
- Produces: a verified object/parameter inventory table and the mono-I/O determination, cited by Tasks 4 and 5.

- [ ] **Step 1: Read the real `LAR.pd` source and extract the object/connection list**

Read `plugins/dslar/doc/references/LAR.pd` in full. From the `#X obj`/`#X msg`/`#X connect` records, build the authoritative inventory. Confirm each of these against the source (values from the screenshot, to be verified in text): `adc~ 1`; `line` ramps `$1 2000` and `$1 200`; `hip~ 100`; pre-gain message set `1/2/4`; `delwrite~/delread~ tab1 50`; `delwrite~/delread~ tab2 20`; `env~ 2048`; `dbtorms`; `- 1`; `abs`; `pow 40`; final VCA `*~`; `s~ audioLAR` / `r~ audioLAR`.

- [ ] **Step 2: Determine mono I/O**

From the `#X connect` records, trace the `dac~` feeds. Decide whether both output channels resolve to the **same** node (duplicated mono) or differ. Record the verdict explicitly; per memory `feedback_porting_mono_io_normalization`, identical outputs ⇒ `dslar` is mono 1-in/1-out.

- [ ] **Step 3: Write the sections**

Replace the `sec:intro` stub with a short motivation (why LAR is the entry point to the Di Scipio chapter: one loop, one control law, fully readable).
Replace the `sec:anatomia` stub with:
- prose describing the two branches (audio / analysis) that leave the shared input `*~`, one sentence per line;
- a `booktabs` inventory table with columns **Oggetto | Argomenti | Ruolo | Ramo**;
- an explicit paragraph stating the mono-I/O verdict from Step 2, with the connection evidence.

- [ ] **Step 4: Build and verify grounding**

Run:
```bash
cd plugins/dslar/doc/study && make
```
Expected: PDF builds clean. Manual checklist: every value in the inventory table appears verbatim in `LAR.pd`; the mono-I/O paragraph cites specific `#X connect` lines.

- [ ] **Step 5: Commit**

```bash
git add plugins/dslar/doc/study/dslar-study.tex
git commit -m "docs(dslar): anatomia della patch — inventario oggetti e verdetto I/O mono"
```

---

### Task 3: Section "Contesto e fonti" (literature grounding)

**Files:**
- Modify: `plugins/dslar/doc/study/dslar-study.tex` (`\label{sec:contesto}` stub)
- Modify: `plugins/dslar/doc/study/refs.bib`
- Read: the four Di Scipio PDFs in `plugins/dslar/doc/references/`

**Interfaces:**
- Consumes: the anatomy from Task 2.
- Produces: the identification of the parent work and BibTeX keys cited elsewhere in the study.

- [ ] **Step 1: Identify the parent work**

Skim the candidate PDFs (`modes-of-interference-3` 2007, `background-noise-study` 2011, `audible-ecosystemics-2` 2002, `sound-is-the-interface` 2003). Match LAR's structure — acoustic Larsen as source, room-level homeostasis — to the work that describes it. Record the identification and the textual evidence (quote + page).

- [ ] **Step 2: Add BibTeX entries**

Add one `@article`/`@inproceedings` entry per cited source to `refs.bib`, with correct author/title/venue/year (verified from each PDF's front matter).

- [ ] **Step 3: Write the section**

Replace the `sec:contesto` stub with prose (one sentence per line) placing LAR in Di Scipio's ecosystemic poetics: the room as a resource, feedback as material, listening to the room listening to itself. Cite with `\cite{...}`. State which work LAR belongs to and why.

- [ ] **Step 4: Build and verify**

Run:
```bash
cd plugins/dslar/doc/study && make
```
Expected: PDF builds; the bibliography renders; all `\cite` keys resolve (no "undefined citation" in the log).

- [ ] **Step 5: Commit**

```bash
git add plugins/dslar/doc/study/dslar-study.tex plugins/dslar/doc/study/refs.bib
git commit -m "docs(dslar): contesto e fonti — opera di appartenenza e bibliografia"
```

---

### Task 4: Sections "Analisi del segnale", "Legge di controllo", "Compatibilità SR"

**Files:**
- Modify: `plugins/dslar/doc/study/dslar-study.tex` (`sec:segnale`, `sec:controllo`, `sec:sr` stubs)

**Interfaces:**
- Consumes: the inventory (Task 2) and the parent-work framing (Task 3).
- Produces: the homeostatic-law derivation and the SR-conversion result reused by the Phase 2 `sds` extraction.

- [ ] **Step 1: Write "Analisi del segnale"**

Describe the two branches signal-by-signal (one sentence per line): the audio path (`adc~` → fade-in → `hip~ 100` → pre-gain → `tab1` 50 ms delay → VCA → out), and the analysis path (`tab2` 20 ms decorrelation → `env~ 2048` → control chain → VCA). Explain the `audioLAR` bus as the ecosystem loop and the `$0-scilloscope` as its monitor. Add a `booktabs` signal-flow table or a simple TikZ/`\fbox` block chain (figures optional; a table is sufficient).

- [ ] **Step 2: Write "La legge di controllo omeostatica" with the derivation**

State the control law verbatim and derive its behaviour. Include these equations:
```latex
\begin{equation}
g(t) = \bigl|\, \mathrm{dbtorms}(\text{env}(t)) - 1 \,\bigr|^{\,40}
\end{equation}
```
Explain, one sentence per line: `env` is the RMS level; quiet room drives the base toward $1$ so $g \to 1$ (gain opens), loud room drives it toward $0$ so $g \to 0$ (gain closes); the exponent $40$ turns the mapping into a near-threshold curve so the system breathes around an equilibrium instead of settling. Note this idiom is absent from the existing `sds.map*` family, so it becomes a new `sds` brick in Phase 2.

- [ ] **Step 3: Write "Compatibilità con la frequenza di campionamento"**

State `SR_ref = 44100`. Show the single sample-relative constant and its conversion:
```latex
T_{\text{env}} = \frac{2048}{44100}\,\text{s} \approx \SI{46.4}{\milli\second},
\qquad N(\text{SR}) = \mathrm{ba.sec2samp}(T_{\text{env}}) = \left\lfloor T_{\text{env}}\cdot \text{SR} \right\rfloor .
\end{equation*}
```
(Use a displayed equation; the exact LaTeX wrapper is the writer's choice.) Explain, one sentence per line: keeping the literal `2048` would shrink the window to ≈21 ms at 96 kHz and change the breathing; anchoring the window in time keeps behaviour invariant. State that `delread~`/`line` (ms) and `hip~` (Hz) are already SR-independent. Add the next-prime note: for the 20/50 ms decorrelation delays, the next-prime snap (`sff.np` / `ddelay`'s `nextPrime`) is available so several `dslar` instances decorrelate; reference memories `project_discipio_sr_independence` and `feedback_next_prime_delay_multiinstance`. State the validation plan: 44.1 kHz vs 96 kHz behavioural A/B (same breathing period).

- [ ] **Step 4: Build and verify**

Run:
```bash
cd plugins/dslar/doc/study && make
```
Expected: PDF builds clean; the equations render; no LaTeX math errors in the log.

- [ ] **Step 5: Commit**

```bash
git add plugins/dslar/doc/study/dslar-study.tex
git commit -m "docs(dslar): analisi del segnale, legge omeostatica e compatibilità SR"
```

---

### Task 5: Sections "Mappa di decomposizione", "Sintesi", and self-review

**Files:**
- Modify: `plugins/dslar/doc/study/dslar-study.tex` (`sec:decomposizione`, `sec:sintesi` stubs)

**Interfaces:**
- Consumes: all prior sections.
- Produces: the brick-vs-local decomposition and the open-questions list that seed the Phase 2 plan.

- [ ] **Step 1: Write "Mappa di decomposizione"**

Reproduce the decomposition decision as a `booktabs` table with columns **Blocco patch | Destino (sds brick / circuito LAR / Faust std) | Nota**, matching the spec:
- `env~ 2048` → `sds` brick (follower), reconcile with `sds.integrator` used by AE2;
- `dbtorms→-1→abs→pow 40` → `sds` brick (legge omeostatica, nuovo idioma);
- fade/ctrl ramps → Faust std smoothing;
- `hip~ 100` → Faust std;
- delays 20/50 ms → circuito LAR (con next-prime opzionale);
- pre-gain / VCA → parametri/circuito;
- `s~/r~ audioLAR` → I/O del plugin.
Add a paragraph on the plugin/room boundary (acoustic loop) and the optional internal-loop toggle for headphone study.

- [ ] **Step 2: Write "Sintesi e ponte alla Fase 2"**

List, one sentence per line: the confirmed parameters to expose (drive, target, steepness, smoothing, decorrelation, output, internal-loop on/off); the two `sds` bricks to extract next and the AE2 signatures they must stay compatible with; the open questions (exact brick names/signatures; whether the decorrelation delay ever graduates to `sds`).

- [ ] **Step 3: Self-review against the spec and build final PDF**

Re-read the study against `docs/superpowers/specs/2026-07-09-discipio-porting-dslar-design.md`. Checklist: no remaining "Segnaposto"; every spec claim about LAR appears; mono-I/O verdict present; SR conversion present; decomposition table matches the spec; prose is one-sentence-per-line and affirmative.
Run:
```bash
cd plugins/dslar/doc/study && make clean && make
```
Expected: clean build from scratch; no undefined references/citations in the log; `dslar-study.pdf` complete.

- [ ] **Step 4: Commit**

```bash
git add plugins/dslar/doc/study/dslar-study.tex
git commit -m "docs(dslar): mappa di decomposizione, sintesi e ponte alla Fase 2"
```

---

## Self-Review (plan vs spec)

- **Spec coverage:** LAR analysis (Tasks 2–4) ✓; parent-work identification (Task 3) ✓; homeostatic law (Task 4) ✓; SR-independence for `env~ 2048` + next-prime note (Task 4) ✓; mono-I/O rule (Task 2) ✓; decomposition map + parameters + plugin/room boundary + internal loop (Task 5) ✓; doc structure mirrors `ltburst` (Task 1) ✓. Phases 2–5 (sds extraction, Faust example, C++ plugin, math/operational docs) are intentionally out of scope for this Phase-1 plan.
- **Placeholder scan:** the `.tex` skeleton uses "Segnaposto" markers that each task explicitly replaces; Task 5 Step 3 verifies none remain. No "TBD/TODO" in task steps.
- **Type/name consistency:** section labels (`sec:anatomia`, `sec:contesto`, `sec:segnale`, `sec:controllo`, `sec:sr`, `sec:decomposizione`, `sec:sintesi`) are defined in Task 1 and referenced by the same names in Tasks 2–5; doc stem `dslar-study` and `DOC` in the Makefile agree.
