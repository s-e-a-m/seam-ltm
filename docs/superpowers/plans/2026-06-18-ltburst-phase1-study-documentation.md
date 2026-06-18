# ltburst Phase 1 — Study & Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the `ltburst` documentation tree and write the first four sections of the Italian study diary (introduction, study of the sources, theory/mathematics, validation targets) from the two Linkwitz papers.

**Architecture:** Phase 1 produces documentation only — no DSP code, no CMake changes, no plugin registration. The deliverable is a compilable LaTeX study document (`doc/study/ltburst-study.tex`) in Italian, its bibliography, the two source papers copied into `doc/references/`, and reference figures cropped from the originals into `doc/study/figures/`. The verification loop for every writing task is: edit → `latexmk -pdf` compiles clean → inspect artifact → commit.

**Tech Stack:** LaTeX (pdflatex via latexmk), BibTeX, poppler (`pdftoppm`, `pdfinfo`) and ImageMagick for figure extraction.

## Global Constraints

- Object name and file prefix: `ltburst` (terse, no hyphens).
- Future Faust library (not this phase): `src/seam.linkwitz.lib`, prefix `slw`.
- `doc/study/` is written in **Italian**; `doc/math/` (later phase) is **English**.
- Document format: LaTeX (`.tex` + `.bib` → `.pdf`). Prose style: **one sentence per line** (clean diffs).
- Phase 1 is documentation only: **no** `source/`, **no** `CMakeLists.txt`, **no** `add_subdirectory(plugins/ltburst)`, **no** `resource/`.
- Source papers copied from `/Users/giuseppe/Documents/gitlab/gs/linkwitz/ref/tone-burst/3012.pdf` (= AES preprint 1342, 1978) and `.../3989.pdf` (= JAES 28(4), 1980) into `doc/references/` with descriptive `year-author-title` names.
- Canonical math model (for the theory section): `x(t) = w(t)·sin(2π f₀ t)`, `w(t) = ½ − ½cos(2π t/T)`, `T = N/f₀`, `N = 5`. Staircase half-cycle amplitudes: `0.081, 0.298, 0.583, 0.845, 1.000` (symmetric over ten half-cycles). Spectral roll-off ≈ 6 dB/oct low, ≈ 12 dB/oct high.
- Commits: end the commit body with the two trailers used by the repo (`Co-Authored-By:` and `Claude-Session:`). Work happens on branch `feat/ltburst-linkwitz-tone-burst` (already created).

---

## File Structure

- `plugins/ltburst/doc/references/1978-linkwitz-narrow-band-impulse-testing-aes-preprint-1342.pdf` — copied source (1978 preprint).
- `plugins/ltburst/doc/references/1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf` — copied source (1980 journal).
- `plugins/ltburst/doc/study/ltburst-study.tex` — the Italian study diary; sole authored document of Phase 1.
- `plugins/ltburst/doc/study/refs.bib` — two bibliography entries for the Linkwitz papers.
- `plugins/ltburst/doc/study/Makefile` — one-command build wrapper (`make` → `latexmk -pdf`).
- `plugins/ltburst/doc/study/.gitignore` — ignore LaTeX aux artifacts, keep the `.pdf`.
- `plugins/ltburst/doc/study/figures/orig-fig1-spectrum-rect.png` — crop of JAES Fig. 1.
- `plugins/ltburst/doc/study/figures/orig-fig3-spectrum-hann.png` — crop of JAES Fig. 3.
- `plugins/ltburst/doc/study/figures/orig-fig8-spectrum-staircase.png` — crop of JAES Fig. 8.
- `plugins/ltburst/doc/math/.gitkeep` — placeholder so the empty English-math dir is tracked.
- `CLAUDE.md` (seam-ltm) — one note recording the Italian-diary exception.

---

## Task 1: Documentation scaffold and source papers

**Files:**
- Create: `plugins/ltburst/doc/references/1978-linkwitz-narrow-band-impulse-testing-aes-preprint-1342.pdf`
- Create: `plugins/ltburst/doc/references/1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf`
- Create: `plugins/ltburst/doc/study/figures/.gitkeep`
- Create: `plugins/ltburst/doc/math/.gitkeep`

**Interfaces:**
- Consumes: nothing.
- Produces: the directory tree and the two source PDFs at the paths above; later tasks crop figures from the 1980 PDF and `\cite` both papers.

- [ ] **Step 1: Create the directory tree**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
mkdir -p plugins/ltburst/doc/study/figures plugins/ltburst/doc/math plugins/ltburst/doc/references
```

- [ ] **Step 2: Copy the two source papers with descriptive names**

```bash
SRC="/Users/giuseppe/Documents/gitlab/gs/linkwitz/ref/tone-burst"
DST="plugins/ltburst/doc/references"
cp "$SRC/3012.pdf" "$DST/1978-linkwitz-narrow-band-impulse-testing-aes-preprint-1342.pdf"
cp "$SRC/3989.pdf" "$DST/1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf"
```

- [ ] **Step 3: Add .gitkeep so empty dirs are tracked**

```bash
touch plugins/ltburst/doc/study/figures/.gitkeep plugins/ltburst/doc/math/.gitkeep
```

- [ ] **Step 4: Verify the files are present and non-empty**

Run:
```bash
ls -la plugins/ltburst/doc/references/ && find plugins/ltburst/doc -type d
```
Expected: both PDFs listed with non-zero size; directories `study`, `study/figures`, `math`, `references` exist.

- [ ] **Step 5: Commit**

```bash
git add plugins/ltburst/doc
git commit -m "docs(ltburst): scaffold doc tree and copy Linkwitz source papers

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 2: LaTeX skeleton, bibliography, and build

**Files:**
- Create: `plugins/ltburst/doc/study/refs.bib`
- Create: `plugins/ltburst/doc/study/ltburst-study.tex`
- Create: `plugins/ltburst/doc/study/Makefile`
- Create: `plugins/ltburst/doc/study/.gitignore`

**Interfaces:**
- Consumes: the directory tree from Task 1.
- Produces: a compilable document with `\section` anchors `sec:intro`, `sec:fonti`, `sec:teoria`, `sec:validazione` (filled in Tasks 4–7) and `\cite` keys `linkwitz1978narrowband`, `linkwitz1980shaped`. Build command: `make` (runs `latexmk -pdf ltburst-study.tex`).

- [ ] **Step 1: Write the bibliography**

Create `plugins/ltburst/doc/study/refs.bib`:
```bibtex
@inproceedings{linkwitz1978narrowband,
  author    = {Linkwitz, Siegfried},
  title     = {Narrow Band Impulse Testing of Acoustical Systems},
  booktitle = {Proc. 60th Convention of the Audio Engineering Society},
  address   = {Los Angeles},
  year      = {1978},
  month     = may,
  note      = {AES preprint 1342 (F-4)}
}
@article{linkwitz1980shaped,
  author  = {Linkwitz, Siegfried},
  title   = {Shaped Tone-Burst Testing},
  journal = {J. Audio Eng. Soc.},
  volume  = {28},
  number  = {4},
  pages   = {250--258},
  year    = {1980},
  month   = apr
}
```

- [ ] **Step 2: Write the LaTeX skeleton**

Create `plugins/ltburst/doc/study/ltburst-study.tex`. Sections 1–4 carry a single Italian placeholder line each (replaced in Tasks 4–7); sections 5–8 name the future phase and stay as stubs through Phase 1:
```latex
\documentclass[11pt,a4paper]{article}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage[italian]{babel}
\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,hyperref}
\usepackage[margin=2.5cm]{geometry}
\graphicspath{{figures/}}

\title{ltburst: il tone-burst sagomato di Linkwitz\\\large Diario di studio e ricostruzione per la suite SEAM-LTM}
\author{Giuseppe Silvi --- SEAM}
\date{2026}

\begin{document}
\maketitle
\begin{abstract}
Questo documento \`e un diario di studio passo passo.
Ricostruisce il tone-burst sagomato di Siegfried Linkwitz a partire dalla letteratura originale, dalla teoria fino al plugin, e ne valida ogni stadio con grafici confrontabili con le figure dei lavori originali.
\end{abstract}
\tableofcontents

\section{Introduzione e motivazione}\label{sec:intro}
Da scrivere nel Task 4.

\section{Studio delle fonti}\label{sec:fonti}
Da scrivere nel Task 5.

\section{Teoria e matematica}\label{sec:teoria}
Da scrivere nel Task 6.

\section{Obiettivi di validazione}\label{sec:validazione}
Da scrivere nel Task 7.

\section{Ricostruzione in Faust}\label{sec:faust}
Sviluppata nella Fase 2.

\section{Porting in C++}\label{sec:cpp}
Sviluppata nella Fase 3.

\section{Il plugin}\label{sec:plugin}
Sviluppata nella Fase 4.

\section{Conclusioni e verso un paper}\label{sec:conclusioni}
Sviluppata a fine percorso.

\bibliographystyle{plain}
\bibliography{refs}
\end{document}
```

- [ ] **Step 3: Write the Makefile**

Create `plugins/ltburst/doc/study/Makefile`:
```makefile
DOC = ltburst-study

.PHONY: all clean
all: $(DOC).pdf

$(DOC).pdf: $(DOC).tex refs.bib
	latexmk -pdf -interaction=nonstopmode $(DOC).tex

clean:
	latexmk -C $(DOC).tex
```

- [ ] **Step 4: Write the .gitignore (keep the PDF, drop aux)**

Create `plugins/ltburst/doc/study/.gitignore`:
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

- [ ] **Step 5: Build and verify it compiles**

Run:
```bash
cd plugins/ltburst/doc/study && make
```
Expected: exit 0, `ltburst-study.pdf` produced. (If `latexmk` is missing, install a TeX distribution or run `pdflatex ltburst-study.tex; bibtex ltburst-study; pdflatex ltburst-study.tex; pdflatex ltburst-study.tex`.)

Then confirm the artifact:
```bash
ls -la ltburst-study.pdf
```
Expected: non-zero size.

- [ ] **Step 6: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/refs.bib plugins/ltburst/doc/study/Makefile plugins/ltburst/doc/study/.gitignore plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): LaTeX skeleton, bibliography, and build for the study diary

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 3: Crop reference figures from the 1980 paper

**Files:**
- Create: `plugins/ltburst/doc/study/figures/orig-fig1-spectrum-rect.png`
- Create: `plugins/ltburst/doc/study/figures/orig-fig3-spectrum-hann.png`
- Create: `plugins/ltburst/doc/study/figures/orig-fig8-spectrum-staircase.png`

**Interfaces:**
- Consumes: `doc/references/1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf` from Task 1.
- Produces: three PNGs referenced by Task 7 (validation targets) via `\includegraphics`.

Figure-to-page map in the 1980 PDF (9 pages): Fig. 1 is on PDF page 2 (journal p. 251), Fig. 3 on PDF page 4 (journal p. 253), Fig. 8 on PDF page 6 (journal p. 255).

- [ ] **Step 1: Verify the extraction tools are available**

Run:
```bash
command -v pdftoppm && command -v magick || command -v convert
```
Expected: a path for `pdftoppm` and one for `magick` (or `convert`). If ImageMagick is absent, install it (`brew install imagemagick`) or, as a fallback, crop in Preview.

- [ ] **Step 2: Render the three pages to PNG at 200 DPI**

```bash
cd plugins/ltburst/doc/study/figures
PDF="../../references/1980-linkwitz-shaped-tone-burst-testing-jaes-28-4.pdf"
pdftoppm -png -r 200 -f 2 -l 2 "$PDF" page-fig1
pdftoppm -png -r 200 -f 4 -l 4 "$PDF" page-fig3
pdftoppm -png -r 200 -f 6 -l 6 "$PDF" page-fig8
ls page-fig*.png
```
Expected: `page-fig1-02.png`, `page-fig3-04.png`, `page-fig8-06.png` (suffix may vary by poppler version).

- [ ] **Step 3: Inspect each render to find the figure bounding box**

View each `page-fig*.png` (open it, or Read the image) and read off the figure's pixel bounding box `WxH+X+Y`. The spectrum figures sit roughly in the lower half of pages 2 and 6 and the upper half of page 4; determine exact pixels by inspection rather than guessing.

- [ ] **Step 4: Crop each figure to its own file**

Using the bounding boxes from Step 3 (replace the example geometry with the inspected values), crop with ImageMagick:
```bash
# example geometry — replace WxH+X+Y with the values read in Step 3
magick page-fig1-02.png -crop 1400x900+150+1400 +repage orig-fig1-spectrum-rect.png
magick page-fig3-04.png -crop 1400x900+150+250  +repage orig-fig3-spectrum-hann.png
magick page-fig8-06.png -crop 1400x900+150+1100 +repage orig-fig8-spectrum-staircase.png
```
(If only `convert` exists, substitute `convert` for `magick`.)

- [ ] **Step 5: Verify the crops, then remove the full-page renders**

View the three `orig-fig*.png` files and confirm each frames the intended spectrum figure (Fig. 1 rectangular-burst spectrum, Fig. 3 Hann spectrum, Fig. 8 staircase spectrum). Re-crop if a box is off. Then drop the intermediates:
```bash
rm page-fig*.png
ls -la orig-fig*.png
```
Expected: three non-zero PNGs, no `page-fig*` left.

- [ ] **Step 6: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/figures/orig-fig1-spectrum-rect.png plugins/ltburst/doc/study/figures/orig-fig3-spectrum-hann.png plugins/ltburst/doc/study/figures/orig-fig8-spectrum-staircase.png
git commit -m "docs(ltburst): crop reference spectra (Figs. 1, 3, 8) from the 1980 paper

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 4: Section 1 — Introduzione e motivazione (Italian)

**Files:**
- Modify: `plugins/ltburst/doc/study/ltburst-study.tex` (replace the body of `\section{Introduzione e motivazione}\label{sec:intro}`)

**Interfaces:**
- Consumes: the skeleton and `\cite` keys from Task 2.
- Produces: a complete Section 1; later sections reference its framing.

- [ ] **Step 1: Write the section content**

Replace the line `Da scrivere nel Task 4.` under `\label{sec:intro}` with Italian prose, one sentence per line, covering exactly these points:
- what a shaped tone burst is and why it is a constant-Q, narrow-band (~1/3 octave) test stimulus;
- why it belongs in SEAM: first signal generator of the suite, first object whose Faust spec does not yet exist and is authored here, first object built as a documented study from primary literature;
- the pedagogical hook: exact Hann window vs. Linkwitz's quantised staircase and the odd harmonics it produces;
- the relevance to validating all-pass networks (`hilbert`/`x2uhj`), citing Linkwitz's own remark, with `\cite{linkwitz1980shaped}`;
- the gap this fills: no Italian-language literature of this kind.

Reference both papers at first mention: `\cite{linkwitz1978narrowband, linkwitz1980shaped}`.

- [ ] **Step 2: Build and verify**

Run:
```bash
cd plugins/ltburst/doc/study && make
```
Expected: exit 0, PDF rebuilt. Confirm the citations resolve (no `[?]`):
```bash
grep -c "Citation" ltburst-study.log || true
```
Expected: no "Citation ... undefined" warnings in the log.

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): write study section 1 — introduction and motivation

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 5: Section 2 — Studio delle fonti (Italian)

**Files:**
- Modify: `plugins/ltburst/doc/study/ltburst-study.tex` (replace the body of `\section{Studio delle fonti}\label{sec:fonti}`)

**Interfaces:**
- Consumes: Section 1 framing; `\cite` keys from Task 2; the source PDFs in `doc/references/`.
- Produces: the reasoned reading that Section 3 (theory) builds its derivations on.

- [ ] **Step 1: Write the section content**

Replace `Da scrivere nel Task 5.` with Italian prose, one sentence per line, covering exactly:
- the lineage of the two papers — preprint 1342 (1978, conference, fuller, with derivations) `\cite{linkwitz1978narrowband}` refined into JAES 28(4) (1980, condensed) `\cite{linkwitz1980shaped}` — stated as the same work in two stages;
- a transcription/paraphrase of the urgent and necessary passages: the network-analysis view (a continuous sine `g(t)` multiplied by a window `w(t)` gives the burst `x(t)`, whose output is the convolution with the network impulse response, so `Y(f) = X(f)·H(f)`);
- why the burst length is a compromise between frequency-response meaning and transient information, and why five cycles gives a one-third-octave main lobe;
- Linkwitz's hardware approximation (the programmable attenuator switching amplitude at each zero crossing) as the historical origin of the staircase window, to be analysed mathematically in Section 3.

Keep direct quotations short and attributed; paraphrase the rest.

- [ ] **Step 2: Build and verify**

Run:
```bash
cd plugins/ltburst/doc/study && make
```
Expected: exit 0, PDF rebuilt, no undefined-citation warnings in `ltburst-study.log`.

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): write study section 2 — study of the sources

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 6: Section 3 — Teoria e matematica (Italian)

**Files:**
- Modify: `plugins/ltburst/doc/study/ltburst-study.tex` (replace the body of `\section{Teoria e matematica}\label{sec:teoria}`)

**Interfaces:**
- Consumes: Section 2's reading; the canonical math model from Global Constraints.
- Produces: the equations and the staircase table that Phase 2 (Faust) reconstructs and Section 4 validates against.

- [ ] **Step 1: Write the section content with the equations verbatim**

Replace `Da scrivere nel Task 6.` with Italian prose (one sentence per line) interleaved with these LaTeX blocks, kept exactly:

```latex
\begin{equation}
x(t) = w(t)\,\sin(2\pi f_0 t), \qquad
w(t) = \tfrac{1}{2} - \tfrac{1}{2}\cos\!\frac{2\pi t}{T}, \qquad
T = \frac{N}{f_0}, \quad N = 5 .
\end{equation}
```

```latex
\begin{equation}
Y(f) = X(f)\,H(f),
\end{equation}
```

The prose must cover:
- the raised-cosine (Hann) envelope and that it premultiplies the sine exactly as a DFT window does;
- the constant-Q property: holding `N` constant makes the burst duration `T = N/f₀` shrink with frequency, so the spectral width stays a fixed fraction of `f₀` (~1/3 octave);
- the spectrum shape: asymmetric roll-off, \SI{6}{dB/oct} toward low frequencies and \SI{12}{dB/oct} toward high (cite Fig. 1 of the original);
- the staircase approximation, presented with this table:

```latex
\begin{table}[h]\centering
\begin{tabular}{lccccc}
\toprule
mezzo-ciclo & 1 & 2 & 3 & 4 & 5 \\
\midrule
ampiezza & 0.081 & 0.298 & 0.583 & 0.845 & 1.000 \\
\bottomrule
\end{tabular}
\caption{Ampiezze di mezzo-ciclo dell'approssimazione a gradini di Linkwitz, simmetriche sui dieci mezzi-cicli del burst di cinque cicli.}
\end{table}
```
- why the zero-crossing slope discontinuities of the staircase generate odd-order harmonics, and why this matters for dynamic range — contrasted with the exact Hann window;
- the gating/dwell repetition period and how it discriminates against echoes (the short burst is analysed before the first reflection arrives).

- [ ] **Step 2: Build and verify**

Run:
```bash
cd plugins/ltburst/doc/study && make
```
Expected: exit 0, PDF rebuilt, equations and the table render, no errors in `ltburst-study.log`.

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): write study section 3 — theory and mathematics

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 7: Section 4 — Obiettivi di validazione (Italian)

**Files:**
- Modify: `plugins/ltburst/doc/study/ltburst-study.tex` (replace the body of `\section{Obiettivi di validazione}\label{sec:validazione}`)

**Interfaces:**
- Consumes: the three cropped figures from Task 3; the theory from Section 3.
- Produces: the validation contract Phase 2 must satisfy (which plots, which agreement criteria).

- [ ] **Step 1: Write the section content and embed the reference figures**

Replace `Da scrivere nel Task 7.` with Italian prose (one sentence per line) that states the validation contract and embeds the three cropped originals:

```latex
\begin{figure}[h]\centering
\includegraphics[width=0.7\linewidth]{orig-fig1-spectrum-rect.png}
\caption{Riferimento originale: spettro del tone-burst rettangolare a cinque cicli (Linkwitz, Fig.~1).}
\end{figure}

\begin{figure}[h]\centering
\includegraphics[width=0.7\linewidth]{orig-fig3-spectrum-hann.png}
\caption{Riferimento originale: spettro con finestra di Hann (Linkwitz, Fig.~3).}
\end{figure}

\begin{figure}[h]\centering
\includegraphics[width=0.7\linewidth]{orig-fig8-spectrum-staircase.png}
\caption{Riferimento originale: spettro dell'approssimazione a gradini (Linkwitz, Fig.~8).}
\end{figure}
```

The prose must define exactly the quantities to compare and the qualitative agreement criteria:
- the burst waveform in time (Hann shaping over five cycles);
- the spectrum in dB on a log `f/f₀` axis — the asymmetric 6/12 dB/oct roll-off and the ~1/3-octave main-lobe width;
- exact Hann vs. Linkwitz staircase — the appearance and relative level of the odd harmonics;
- a statement that the plots compared in later phases are generated from the Faust spec (Phase 2), that the comparison is qualitative (shape, roll-off slope, main-lobe width, presence of odd harmonics), and that the offline plotting tool is fixed in the Phase 2 plan.

- [ ] **Step 2: Build and verify**

Run:
```bash
cd plugins/ltburst/doc/study && make
```
Expected: exit 0, PDF rebuilt, the three figures appear, no missing-graphics or undefined-citation errors in `ltburst-study.log`.

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): write study section 4 — validation targets

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Task 8: Record the Italian-diary exception in CLAUDE.md

**Files:**
- Modify: `plugins/../CLAUDE.md` → `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm/CLAUDE.md` (the "Working language" section)

**Interfaces:**
- Consumes: nothing.
- Produces: a documented exception so future sessions know `doc/study/` diaries may be Italian.

- [ ] **Step 1: Read the current Working language section**

Run:
```bash
grep -n "Working language" -A6 /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/CLAUDE.md
```
Expected: shows the paragraph stating code, commits, and documentation are in English.

- [ ] **Step 2: Add the exception note**

Append one sentence to that section, verbatim:
> Exception: narrative *study diaries* under a plugin's `doc/study/` may be written in Italian (e.g. `ltburst`), to fill the gap in Italian-language DSP literature; the formal `doc/math/` documentation stays English.

- [ ] **Step 3: Verify**

Run:
```bash
grep -n "study diaries" /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/CLAUDE.md
```
Expected: the new line is present.

- [ ] **Step 4: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add CLAUDE.md
git commit -m "docs: allow Italian study diaries under doc/study (ltburst convention)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01TortUqjmxD8Jvp1UahCNhj"
```

---

## Phase 1 Done When

- `plugins/ltburst/doc/study/ltburst-study.pdf` compiles clean and contains sections 1–4 written in Italian, with both papers cited and the three reference figures embedded.
- The two source papers are in `doc/references/`; `doc/math/` exists (empty) for the later English math doc.
- `CLAUDE.md` records the Italian-diary exception.
- No DSP code, no CMake change, plugin not registered — those are Phases 2–4, each with its own plan.
