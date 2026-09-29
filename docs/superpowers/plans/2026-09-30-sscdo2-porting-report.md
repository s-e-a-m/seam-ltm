# SSCDO#2 porting report ("bibbietta") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An Italian LaTeX report, shared by Giuseppe Silvi, Davide Tedesco and Alice Cortegiani, that organises the SSCDO#2 surveys into principles, one card per control of each machine, and a rehearsal protocol with blank fields for verdicts.

**Architecture:** A LuaLaTeX `report` split into one file per part/chapter, with a small macro layer (state tags, `\scheda`, `\prova`, `\questione`, `\misura`, `\studio`).
Every measured number goes through `\misura{value}{source}`; `check.py` verifies each value literally in the cited study folder (or the session log), that every cited folder exists, that every study in the index is cited, and that every required card, rehearsal card and question is present.
`make check` builds the PDF, fails on undefined references, and runs `check.py`.

**Tech Stack:** LuaLaTeX (TeX Live 2026), latexmk, fontspec + unicode-math with Libertinus Serif / Libertinus Math, siunitx, booktabs, longtable, tikz, xcolor, hyperref; Python 3 standard library for `check.py`.

**Spec:** `docs/superpowers/specs/2026-09-30-sscdo2-porting-report-design.md`

## Global Constraints

- Language of the report: Italian. Code, file names, commit messages: English.
- One sentence per line in every `.tex` file; affirmative explanatory voice; avoid negations and "rather than" phrasing where an affirmative one works.
- Every measured number in the text is written as `\misura{<value as in the source>}{<study folder or log>}`; the value string is copied verbatim from the source (dot as decimal separator; the macro prints the Italian comma).
- Sources: a folder name under `doc/study/sscdo2/` (e.g. `stunedrev-chain`) or `log` (= `logs/2026-09-29-sscdo2-ricognizione.md`). The report never recomputes a number.
- Readers did not attend the sessions: every technical term is explained where first used.
- The compiled `sscdo2-porting.pdf` is committed; build artefacts are gitignored.
- The report prepares, and is not, the public documentation or the final operational score; the choir and the Reaper cue/MIDI layer are placeholders.
- Commit as you go (seam-ltm rule); do not push without Giuseppe's go-ahead.

## Review Focus

1. A value in `\misura` written with the Unicode minus (−) or hyphen (-) differently from its source: `check.py` normalises U+2212 to `-` on both sides and must still find it (test in Task 1).
2. A value that appears nowhere in the cited source (typo, or number taken from the wrong study): `check.py` must fail naming the value and the source (mutation test in Task 1).
3. Unicode symbols in running text (√, φ, π, →, Δ, ″): they must render, not vanish; LuaLaTeX with Libertinus handles them, and `make check` fails on "Missing character" in the build log (Task 1).
4. A card with an empty field: `\scheda` takes 8 mandatory arguments and prints "—" for an argument given as `{}`; `check.py` fails on an empty state argument (Task 1).
5. A study folder listed in the index but never cited, or a `\studio{...}` naming a folder that does not exist: `check.py` fails for both (Task 1).

---

## File Structure

```
doc/study/sscdo2/report/
├── Makefile                 # latexmk -lualatex; `make check` = build + log scan + check.py
├── .gitignore               # latexmk artefacts
├── sscdo2-porting.tex       # main: preamble include, title, abstract, \input of every part
├── preamble.tex             # packages, fonts, macros (the only place macros are defined)
├── check.py                 # source/coverage/required-ids checker, with --selftest
├── parte1-principi.tex      # Part I
├── parte2-intro.tex         # Part II opening: the four units, faders, cues, 96 kHz
├── parte2-lmo.tex           # chapter LMO
├── parte2-delrm.tex         # chapter delRM
├── parte2-stunedrev.tex     # chapter stunedrev
├── parte2-coro-master.tex   # choir placeholder, master
├── parte3-prove.tex         # Part III: preparation, rehearsal cards, questions, operational table draft
├── appendice-audio.tex      # index of audio files
└── sscdo2-porting.pdf       # committed output
```

Modified at the end: `doc/study/sscdo2/README.md` (link to the report), `logs/2026-09-29-sscdo2-ricognizione.md` (entry).

---

### Task 1: Scaffold, macros, checker

**Files:**
- Create: `doc/study/sscdo2/report/Makefile`, `.gitignore`, `sscdo2-porting.tex`, `preamble.tex`, `check.py`, and every part file with a one-line placeholder sentence (so the main file compiles).

**Interfaces:**
- Produces (macros, defined only in `preamble.tex`):
  - `\deciso`, `\daprovare`, `\domanda` — coloured margin tag and text tag; used as the state argument of `\scheda`.
  - `\misura{value}{source}` — prints `\num{value}`; `\misurau{value}{unit}{source}` — prints `\qty{value}{unit}`.
  - `\studio{folder}` — prints `\texttt{doc/study/sscdo2/folder/}`.
  - `\scheda{id}{controllo}{ambito}{iniziale}{conseguenze}{originale}{stato}{fonti}` — a boxed card; `id` becomes a `\label{scheda:id}`.
  - `\prova{id}{cosa ascoltare}{valore}{riferimento}` — a rehearsal card with blank ruled lines *Esito* and *Decisione*; `\label{prova:id}`.
  - `\questione{id}{testo}` — a numbered question for Davide; `\label{q:id}`.
- Produces (checker): `python3 check.py` exit 0/1; `python3 check.py --selftest` exit 0 when the checker catches its mutations.

- [ ] **Step 1: Write `check.py` with its self-test first**

```python
#!/usr/bin/env python3
"""check.py -- verify the SSCDO#2 porting report against the studies.

usage (from doc/study/sscdo2/report/):
    python3 check.py              check the report
    python3 check.py --selftest   verify that the checker catches its mutations

Rules:
1. every \\misura{v}{src} / \\misurau{v}{unit}{src}: src is a study folder of
   ../ or `log`; v appears literally (U+2212 normalised to '-') in that
   folder's *.md files (build/ excluded) or in the session log;
2. every \\studio{folder} names an existing folder;
3. every folder listed in ../README.md is cited by \\studio or \\misura;
4. every required \\scheda, \\prova and \\questione id is present, and no
   \\scheda has an empty state argument.
"""
import os, re, sys, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
STUDIES = os.path.dirname(HERE)
LOG = os.path.join(STUDIES, "..", "..", "..", "logs", "2026-09-29-sscdo2-ricognizione.md")

REQUIRED_SCHEDE = [
    "lmo-frequenza", "lmo-delta", "lmo-volume", "lmo-canali", "lmo-filtro",
    "delrm-distanza", "delrm-volume", "delrm-dinamica", "delrm-integratore", "delrm-dcblocker",
    "stunedrev-tempi", "stunedrev-ingresso-uscita", "stunedrev-sezioni", "stunedrev-memoria",
    "coro-uscita", "master",
]
REQUIRED_PROVE = [
    "lmo-cue", "lmo-glissando", "lmo-delta", "lmo-filtro",
    "delrm-distanza", "delrm-dinamica", "delrm-dcblocker",
    "stunedrev-facce", "stunedrev-ingresso",
]
REQUIRED_QUESTIONI = ["doppio-ingresso", "uscite-cue", "semi", "dcblocker", "filtro-lmo"]

def norm(s): return s.replace("−", "-")

def tex_sources(root):
    out = {}
    for f in sorted(os.listdir(root)):
        if f.endswith(".tex"):
            out[f] = open(os.path.join(root, f), encoding="utf-8").read()
    return out

def study_folders(studies):
    return sorted(d for d in os.listdir(studies)
                  if os.path.isdir(os.path.join(studies, d)) and d != "report" and not d.startswith("."))

def source_text(studies, log, src, cache={}):
    key = (studies, src)
    if key not in cache:
        if src == "log":
            cache[key] = norm(open(log, encoding="utf-8").read())
        else:
            parts = []
            for dp, dns, fns in os.walk(os.path.join(studies, src)):
                dns[:] = [d for d in dns if d != "build"]
                for fn in fns:
                    if fn.endswith(".md"):
                        parts.append(open(os.path.join(dp, fn), encoding="utf-8").read())
            cache[key] = norm("\n".join(parts))
    return cache[key]

def args(text, macro, n):
    """All argument tuples of \\macro{a}{b}... with n brace groups (one level of nesting)."""
    grp = r"\{((?:[^{}]|\{[^{}]*\})*)\}"
    return re.findall(r"\\" + macro + r"\s*" + r"\s*".join([grp] * n), text)

def check(root, studies, log, required=True):
    errors, warnings = [], []
    tex = tex_sources(root)
    folders = study_folders(studies)
    cited = set()
    for f, t in tex.items():
        body = re.sub(r"(?<!\\)%.*", "", t)
        for v, src in args(body, "misura", 2) + [(a, c) for a, _, c in args(body, "misurau", 3)]:
            cited.add(src)
            if src != "log" and src not in folders:
                errors.append(f"{f}: \\misura{{{v}}}{{{src}}}: unknown source"); continue
            if norm(v) not in source_text(studies, log, src):
                errors.append(f"{f}: \\misura{{{v}}}{{{src}}}: value not found in source")
            if len(v.strip("+-− ")) < 3:
                warnings.append(f"{f}: \\misura{{{v}}}{{{src}}}: short value, weak check")
        for (d,) in [(x,) for x in args(body, "studio", 1)]:
            cited.add(d)
            if d not in folders:
                errors.append(f"{f}: \\studio{{{d}}}: no such folder")
        for a in args(body, "scheda", 8):
            if not a[6].strip():
                errors.append(f"{f}: \\scheda{{{a[0]}}}: empty state")
    index = open(os.path.join(studies, "README.md"), encoding="utf-8").read()
    for d in re.findall(r"`([a-z0-9-]+)/`", index):
        if d in folders and d not in cited:
            errors.append(f"study {d}/ is in the index and never cited")
    if required:
        alltex = "\n".join(tex.values())
        have = {m: {a[0] for a in args(alltex, m, n)} for m, n in (("scheda", 8), ("prova", 4), ("questione", 2))}
        for m, req in (("scheda", REQUIRED_SCHEDE), ("prova", REQUIRED_PROVE), ("questione", REQUIRED_QUESTIONI)):
            for i in req:
                if i not in have[m]:
                    errors.append(f"missing \\{m}{{{i}}}")
    return errors, warnings

def selftest():
    tmp = tempfile.mkdtemp()
    try:
        studies = os.path.join(tmp, "sscdo2"); root = os.path.join(studies, "report")
        os.makedirs(os.path.join(studies, "demo")); os.makedirs(root)
        open(os.path.join(studies, "demo", "README.md"), "w").write("delay 2113 samples, notch −12.8 dB\n")
        open(os.path.join(studies, "README.md"), "w").write("| x | `demo/` |\n")
        log = os.path.join(tmp, "log.md"); open(log, "w").write("cue at 97.44 Hz\n")
        def run(tex):
            open(os.path.join(root, "a.tex"), "w").write(tex)
            return check(root, studies, log, required=False)[0]
        cases = [
            ("good", r"\misura{2113}{demo} \misura{-12.8}{demo} \misura{97.44}{log} \studio{demo}", 0),
            ("minus normalised", r"\misura{−12.8}{demo}", 0),
            ("MUTATION value", r"\misura{2114}{demo}", 1),
            ("MUTATION source", r"\misura{97.44}{demo} \studio{demo}", 1),
            ("MUTATION folder", r"\misura{2113}{demo} \studio{nodemo}", 1),
            ("MUTATION uncited", r"\misura{97.44}{log}", 1),
            ("MUTATION empty state", r"\misura{2113}{demo}\scheda{x}{a}{b}{c}{d}{e}{}{g}", 1),
        ]
        ok = True
        for name, tex, want in cases:
            got = len(run(tex))
            good = (got == 0) if want == 0 else (got >= 1)
            print(f"  [{'ok' if good else 'FAIL'}] {name}: {got} error(s)")
            ok &= good
        return ok
    finally:
        shutil.rmtree(tmp)

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(0 if selftest() else 1)
    errors, warnings = check(HERE, STUDIES, LOG)
    for w in warnings: print("warning:", w)
    for e in errors: print("ERROR:", e)
    print("CHECK OK" if not errors else f"CHECK FAILED ({len(errors)} errors)")
    sys.exit(1 if errors else 0)
```

- [ ] **Step 2: Run the self-test**

Run: `cd doc/study/sscdo2/report && python3 check.py --selftest`
Expected: seven `[ok]` lines, exit 0. If a MUTATION line prints `[FAIL]`, the checker is broken: fix `check.py` before going on.

- [ ] **Step 3: Write `preamble.tex`**

```latex
% preamble.tex -- packages and macros of the SSCDO#2 porting report.
% Macros are defined here only; check.py parses \misura, \misurau, \studio,
% \scheda, \prova, \questione by name and arity.
\usepackage{fontspec}
\usepackage{unicode-math}
\setmainfont{Libertinus Serif}
\setsansfont{Libertinus Sans}
\setmonofont[Scale=MatchLowercase]{Libertinus Mono}
\setmathfont{Libertinus Math}
\usepackage[italian]{babel}
\usepackage[a4paper,margin=2.3cm,marginparwidth=1.8cm,marginparsep=3mm]{geometry}
\usepackage{amsmath,booktabs,longtable,array,tabularx,enumitem}
\usepackage{siunitx}
\sisetup{output-decimal-marker={,},group-separator={\,},group-minimum-digits=5,
  retain-explicit-plus=true,range-phrase={--},per-mode=symbol}
\usepackage{xcolor}
\definecolor{sdeciso}{HTML}{2E7D32}
\definecolor{sdaprovare}{HTML}{EF6C00}
\definecolor{sdomanda}{HTML}{1565C0}
\usepackage{tikz}
\usetikzlibrary{positioning,arrows.meta}
\usepackage{listings}
\lstdefinestyle{faust}{basicstyle=\ttfamily\small,breaklines=true,
  keepspaces=true,columns=fullflexible,frame=single,framesep=4pt}
\lstset{style=faust}
\usepackage{tcolorbox}
\tcbuselibrary{breakable}
\usepackage[hidelinks]{hyperref}

% ---- state tags
\newcommand{\statotag}[2]{\textcolor{#1}{\textsf{\small\bfseries #2}}}
\newcommand{\deciso}{\statotag{sdeciso}{DECISO}}
\newcommand{\daprovare}{\statotag{sdaprovare}{DA PROVARE}}
\newcommand{\domanda}{\statotag{sdomanda}{DOMANDA}}

% ---- numbers and sources
\newcommand{\misura}[2]{\num{#1}}
\newcommand{\misurau}[3]{\qty{#1}{#2}}
\newcommand{\studio}[1]{\texttt{doc/study/sscdo2/#1/}}
\newcommand{\vuoto}[1]{\ifx\relax#1\relax ---\else #1\fi}

% ---- a card per control
\newcommand{\scheda}[8]{%
  \begin{tcolorbox}[breakable,colback=white,colframe=black!40,boxrule=0.4pt,
    title={\textsf{\bfseries #2}\hfill #7},coltitle=black,colbacktitle=black!6]
  \label{scheda:#1}%
  \begin{tabularx}{\linewidth}{@{}>{\bfseries\small}p{3.2cm}X@{}}
  Ambito & \vuoto{#3}\\
  Valore iniziale & \vuoto{#4}\\
  Conseguenze & \vuoto{#5}\\
  Rispetto all'originale & \vuoto{#6}\\
  Studio e audio & \vuoto{#8}\\
  \end{tabularx}
  \end{tcolorbox}}

% ---- a rehearsal card, with blank fields to fill by hand
\newcounter{prova}
\newcommand{\campo}[1]{\par\noindent\textbf{#1:}\ \hrulefill\par\vspace{2mm}\noindent\hrulefill\par}
\newcommand{\prova}[4]{%
  \refstepcounter{prova}\label{prova:#1}%
  \begin{tcolorbox}[breakable,colback=white,colframe=sdaprovare!60,boxrule=0.4pt,
    title={\textsf{\bfseries Prova \theprova}},coltitle=black,colbacktitle=sdaprovare!8]
  \textbf{Ascoltare:} #2\par
  \textbf{Valore:} #3\par
  \textbf{Riferimento:} #4\par\medskip
  \campo{Esito}\campo{Decisione}
  \end{tcolorbox}}

% ---- a numbered question for Davide
\newcounter{questione}
\newcommand{\questione}[2]{%
  \refstepcounter{questione}\label{q:#1}%
  \par\noindent\domanda\ \textbf{Q\thequestione.}\ #2\par\medskip}
```

- [ ] **Step 4: Write `sscdo2-porting.tex`**

```latex
\documentclass[11pt,a4paper,openany]{report}
\input{preamble}

\title{Studio sul Corpo d'Ombra \#2\\\large Il porting SEAM: principi, macchine, prove}
\author{Giuseppe Silvi --- SEAM\\\small con Davide Tedesco e Alice Cortegiani}
\date{Versione del \today}

\begin{document}
\maketitle
\begin{abstract}
\input{abstract}
\end{abstract}
\tableofcontents

\part{Principi e processi}
\input{parte1-principi}

\part{Le macchine}
\input{parte2-intro}
\input{parte2-lmo}
\input{parte2-delrm}
\input{parte2-stunedrev}
\input{parte2-coro-master}

\part{Protocollo di prova}
\input{parte3-prove}

\appendix
\input{appendice-audio}
\end{document}
```

Create `abstract.tex`, `parte1-principi.tex`, `parte2-intro.tex`, `parte2-lmo.tex`, `parte2-delrm.tex`, `parte2-stunedrev.tex`, `parte2-coro-master.tex`, `parte3-prove.tex`, `appendice-audio.tex`, each with one Italian sentence stating what the file will hold (e.g. `% parte2-lmo.tex` then `Questo capitolo raccoglie le schede di LMO.`); the chapters start with `\chapter{...}`.

- [ ] **Step 5: Write `Makefile` and `.gitignore`**

```make
DOC = sscdo2-porting
TEX = $(wildcard *.tex)

.PHONY: all check clean
all: $(DOC).pdf

$(DOC).pdf: $(TEX)
	latexmk -lualatex -interaction=nonstopmode -halt-on-error $(DOC).tex

check: $(DOC).pdf
	@! grep -E "Missing character|LaTeX Warning: (Reference|There were undefined)" $(DOC).log || (echo "BUILD LOG PROBLEMS"; exit 1)
	python3 check.py --selftest
	python3 check.py

clean:
	latexmk -C $(DOC).tex
```

`.gitignore`:

```
*.aux
*.log
*.out
*.toc
*.fls
*.fdb_latexmk
*.synctex.gz
```

- [ ] **Step 6: Build and check the skeleton**

Run: `cd doc/study/sscdo2/report && make check`
Expected: the PDF builds; the self-test passes; `check.py` FAILS listing every missing `\scheda`, `\prova`, `\questione` id and every uncited study. This is the failing test the next tasks turn green.

- [ ] **Step 7: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): scaffold, macros and source checker"
```

---

### Task 2: Part I — Principi e processi, and the abstract

**Files:**
- Modify: `doc/study/sscdo2/report/abstract.tex`, `doc/study/sscdo2/report/parte1-principi.tex`

**Interfaces:**
- Consumes: `\misura`, `\misurau`, `\studio`, `\deciso` from Task 1.
- Produces: labels `sec:principio-<n>` (`ragiona`, `blocchi`, `96k`, `primi`, `processo`, `livelli`, `precisione`, `genealogia`) that Parts II and III cite with `\ref`.

- [ ] **Step 1: Write the abstract**

Five to eight sentences: what SSCDO#2 is (Alice Cortegiani, Davide Tedesco; Giuseppe performs the live electronics at the next CIM), what the port is (Faust specification `seam.tedesco.lib`, prefix `sdt`, then hand-written C++ VST3 in seam-ltm), what this report is for (read and judged during rehearsals, towards the documentation of the piece and the operational score), the three state tags and what each means, and that every number cites the study that measured it.

- [ ] **Step 2: Write the eight principles**

One `\chapter{Principi}` with a `\section` per principle; each section has four labelled paragraphs, `\paragraph{Enunciato}`, `\paragraph{Perché}`, `\paragraph{Dove}`, `\paragraph{Un numero}`.
Facts to use (value, source):

1. *Il porting ragiona, non copia* (`sec:principio-ragiona`): SEAM, then standard, then new code only for a detected defect; the reference is the version played in Pd (`stunedrev.dsp`, 42 sections, not the online variant with 81); an entry of `seam.tedesco.lib` may be a single reference to a SEAM function. Number, in words with `\studio{stunedrev-delays}` (no `\misura`: the phrase is not literal in the source): at the starting times 7 of the 168 sections of stunedrev take another prime, and elsewhere the SEAM line equals Davide's sample for sample.
2. *Un blocco alla volta, verificato per mutazione* (`sec:principio-blocchi`): each block discussed, decided, validated before the next; every check is broken on purpose to show it sees the fault. Number: the Moorer all-pass with g flipped moves the response by `\misura{0.0311}{stunedrev-allpass}`.
3. *96 kHz è la frequenza del brano* (`sec:principio-96k`): constants anchored at 96 kHz; `sdt.delrmint = sfi.leakyint(1) : *(96000)`; with `*(ma.SR)` at 48 kHz the level before the compressor would drop by `\misurau{6.00}{\decibel}{log}`; exception: primes follow the rate (a feature of the system).
4. *Primi e incommensurabilità* (`sec:principio-primi`): ms → samples → next prime strictly above (`sff.np`). In words with `\studio{stunedrev-delays}`: on line √2 at 83 ms, 313 of the 861 pairs of delays share a factor before the prime and none after. Wrapped, from `stunedrev-delays/README.md`: nearest sections `\misura{135.8}{stunedrev-delays}` samples apart, largest prime gap below 1.3 million `\misura{114}{stunedrev-delays}`, a step-over of up to `\misura{96}{stunedrev-delays}` samples. Distinctness is geometric; `sff.np` is kept for the coherence of SEAM (the delay is always longer than the exact time).
5. *Processo, non rimedi locali* (`sec:principio-processo`): the DC blockers held back the old integrator's DC (`\misurau{9.6}{\decibel}{delrm-dcblock}` below the signal after five minutes); with `sdt.delrmint` the DC stays `\misurau{66.5}{\decibel}{delrm-dcblock}` below; their side effect was a high-pass at `\misurau{76.59}{\hertz}{delrm-dcblock}` taking `\misurau{17.6}{\decibel}{delrm-dcblock}` from the clarinet's fundamental.
6. *I livelli si fissano suonando* (`sec:principio-livelli`): no +6 dB make-up for LMO; the level is set by playing.
7. *Precisione* (`sec:principio-precisione`): the VST computes in double; float concerns only Faust in the online IDE; in float Moorer and Schroeder stay within `\misurau{0.00025}{\decibel}{stunedrev-allpass}` of double over 42 sections.
8. *Genealogia* (`sec:principio-genealogia`): in-phi-rev (*Canto alla durata*, Giuseppe; Schroeder form, φ, 81 sections) → stunedrev (four faces of STONED, four ratios, Moorer form, ms); DDELAY's prime delay in delRM so that SSCDO#4 (two sources) uses the same tool; `\studio{stunedrev-allpass}`, `\studio{delrm-delay}`.

Every `\misura` value must be checked against the source file before writing it (grep it); if a value is absent from the source, write it in words and cite with `\studio` instead.

- [ ] **Step 3: Build and check**

Run: `make check`
Expected: build clean; `check.py` still fails only on the ids and studies owned by Tasks 3–7; no "value not found" error from `parte1-principi.tex` or `abstract.tex`.

- [ ] **Step 4: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): part I, the principles of the port"
```

---

### Task 3: Part II opening and the LMO chapter

**Files:**
- Modify: `doc/study/sscdo2/report/parte2-intro.tex`, `doc/study/sscdo2/report/parte2-lmo.tex`

**Interfaces:**
- Consumes: `\scheda`, `\misura`, `\misurau`, `\studio`, state tags; labels `sec:principio-*`.
- Produces: `\label{scheda:lmo-frequenza}`, `scheda:lmo-delta`, `scheda:lmo-volume`, `scheda:lmo-canali`, `scheda:lmo-filtro`; `\label{cap:lmo}`.

- [ ] **Step 1: Write `parte2-intro.tex`**

`\chapter{Il sistema}`: the four Faust units inside `pd GRAND_CENTRAL`, four channels each; inputs 5–8 (TETRAREC A through the ASP880), outputs 9–12 labelled LFU, RFD, RBU, LBD, the four drivers of STONED; the BCF2000 faders (CC81 LMO volume, CC82 delRM volume, CC83 APF input, CC84 APF output, CC86 choir output, CC88 master); the cue list (cue 0 initialisation, 1, 2 glissando over 120 s, 99 end), space advances; the performance rate 96 kHz; one TikZ block diagram: inputs → {delRM, stunedrev, choir}, LMO generator → mix → master → four outputs. Source for all of this: `log` (no numbers need `\misura` except `\misura{97.44}{log}` if quoted).
Then one paragraph explaining how to read a card: the seven fields and the three tags.

- [ ] **Step 2: Write `parte2-lmo.tex`**

`\chapter{LMO}\label{cap:lmo}`: the chain in a few lines (white noise `no.multinoise`, one stream per channel → band filter HP24 : LP24 at the same frequency → two beating oscillators), then five `\scheda` calls:

| id | controllo | contents (value, source) | stato |
|---|---|---|---|
| `lmo-frequenza` | Frequenza (cue) | cue 1 `\misurau{97.44}{\hertz}{log}`, cue 2 glissando to `\misurau{112.67}{\hertz}{log}` over 120 s; the 1.015 factor of the original was a by-product of the two-oscillator formula and is dropped, so the cues are the frequencies Davide actually heard; ref. `\ref{sec:principio-ragiona}` | `\deciso` |
| `lmo-delta` | Distanza Δ tra i due oscillatori | range 0–50 Hz (Giuseppe widened it for experiment; source `log`); each band moves Δ/2 from the centre; excess envelope at Δ over Δ = 0: `\misura{+3.0}{lmo-beats}` dB at 10 Hz, `\misura{15.8}{lmo-beats}` at 20, `\misura{34.5}{lmo-beats}` at 40; the beat emerges from 10–15 Hz, below it the second band widens without beating; audio `\studio{lmo-beats}` renders at 0, 3, 7, 10, 20, 40 Hz and a sweep | `\daprovare` |
| `lmo-volume` | Volume (CC81) | a linear gain stage; no +6 dB make-up (`\ref{sec:principio-livelli}`) | `\daprovare` |
| `lmo-canali` | I quattro canali | one noise stream per channel, adjacent channels uncorrelated (r `\misura{-0.007}{lmo-streams}` against `\misura{0.998}{lmo-streams}` with one shared stream); the +i Hz offset separates nothing on its own (1 Hz against a `\misurau{73}{\hertz}{lmo-streams}` band at 1 kHz); audio `\studio{lmo-streams}` A/B | `\deciso` |
| `lmo-filtro` | Il filtro di banda | HP24 : LP24, −3 dB band `\misura{7.35}{lmo-bandfilter}` % of the centre, Q about `\misura{13.6}{lmo-bandfilter}`; version B (Davide's line on the current SVF libraries) chosen by Giuseppe's listening; C (`fi.bandpass`) decays in `\misurau{4.95}{\second}{lmo-bandfilter}` against `\misurau{0.31}{\second}{lmo-bandfilter}`; A, C, C3, C2 kept for Davide; audio `\studio{lmo-bandfilter}` | `\daprovare` |

Before writing each `\misura`, grep its value in the cited folder (`grep -rF -- '<value>' doc/study/sscdo2/<folder> --include='*.md'`); a value not found is written in words with `\studio`.

- [ ] **Step 3: Build and check**

Run: `make check`
Expected: the five `lmo-*` schede no longer reported missing; `lmo-beats`, `lmo-streams`, `lmo-bandfilter` no longer reported uncited; no "value not found".

- [ ] **Step 4: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): the system and the LMO cards"
```

---

### Task 4: The delRM chapter

**Files:**
- Modify: `doc/study/sscdo2/report/parte2-delrm.tex`

**Interfaces:**
- Consumes: macros; `sec:principio-*`.
- Produces: `\label{cap:delrm}`, `scheda:delrm-distanza`, `scheda:delrm-volume`, `scheda:delrm-dinamica`, `scheda:delrm-integratore`, `scheda:delrm-dcblocker`.

- [ ] **Step 1: Write `parte2-delrm.tex`**

`\chapter{delRM}\label{cap:delrm}`: channels 1 and 3 a comb x + x[n−D]; channels 2 and 4 the triple product 10·x[n−D]·x·∫x into the compressor; D is DDELAY's delay in metres (agreed with Davide for SSCDO#4). Then:

| id | controllo | contents (value, source) | stato |
|---|---|---|---|
| `delrm-distanza` | Distanza (ritardo D) | in metres, 0–30 m, converted at 331.4 m/s, rounded to the next prime; start `\misurau{7.291}{\metre}{delrm-comb}` = `\misura{2113}{delrm-comb}` samples at 96 kHz (Davide's 22 ms); tuned during setup, static in the piece, a click on change is accepted; the comb recolours partial by partial: peaks every SR/D Hz, at 7.291 m the third partial +6.0 dB and the seventh −7.3 dB, at 10 m the fundamental `\misura{+5.5}{delrm-comb}` dB and the fifth `\misura{-17.3}{delrm-comb}` dB (tuning the delay is tuning a timbre); audio `\studio{delrm-comb}` at 5, 7.291, 10 m | `\daprovare` |
| `delrm-volume` | Volume (CC82) | linear gain | `\daprovare` |
| `delrm-dinamica` | Canali 2 e 4: la finestra dinamica | the cubic product is an expander below threshold and the 11:1 compressor a limiter above; about 15 dB of playing separate absent from saturated (source `delrm-rm`, write the value in words with `\studio{delrm-rm}` unless "about 15 dB" is wrapped whole); transients reach 0 dBFS at +6 dB of input before the master volume; the ASP880's gain places the performance inside the window | `\daprovare` |
| `delrm-integratore` | L'integratore | the original `fi.integrator` drifted with the time the patch had been running (a note after five minutes 7 dB louder); SEAM: leaky integrator at 1 Hz scaled by 96000 (`\ref{sec:principio-96k}`); audio `\studio{delrm-rm}` fresh / after 5 min | `\deciso` |
| `delrm-dcblocker` | I DC blocker | left out (`\ref{sec:principio-processo}`); the thinner low end they gave (`\misurau{76.59}{\hertz}{delrm-dcblock}`, `\misurau{17.6}{\decibel}{delrm-dcblock}` on the fundamental) is Davide's listening decision; if kept it enters as a declared `fi.dcblockerat(76.59)`; audio `\studio{delrm-dcblock}` clean / with the two DC blockers | `\domanda` |

Also cite `\studio{delrm-delay}` in the chapter's opening (DDELAY's specification verified value by value).
Grep every value before wrapping it.

- [ ] **Step 2: Build and check**

Run: `make check`
Expected: the five `delrm-*` schede present; `delrm-*` studies cited; no "value not found".

- [ ] **Step 3: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): the delRM cards"
```

---

### Task 5: The stunedrev chapter

**Files:**
- Modify: `doc/study/sscdo2/report/parte2-stunedrev.tex`

**Interfaces:**
- Consumes: macros; `sec:principio-*`.
- Produces: `\label{cap:stunedrev}`, `scheda:stunedrev-tempi`, `scheda:stunedrev-ingresso-uscita`, `scheda:stunedrev-sezioni`, `scheda:stunedrev-memoria`.

- [ ] **Step 1: Write `parte2-stunedrev.tex`**

`\chapter{stunedrev}\label{cap:stunedrev}`: four independent lines, one per face of STONED, 42 Moorer all-pass sections each, g = 1/√2, delay of section i = time·(i+1)·k to the next prime, k = √2, φ, e, π; lineage `\ref{sec:principio-genealogia}`; a short explanation of the long memory (each section returns all its energy, delayed on average by its own t; the line's centroid is the sum of its 42 delays; the direct path is −126 dB). Then:

| id | controllo | contents (value, source) | stato |
|---|---|---|---|
| `stunedrev-tempi` | I quattro tempi (√2, φ, e, π) | range 1–100 ms; start 83, 47, 7, 71 ms (Pd patch); energy centroids `\misurau{106.0}{\second}{stunedrev-chain}`, `\misurau{68.7}{\second}{stunedrev-chain}`, `\misurau{17.2}{\second}{stunedrev-chain}`, `\misurau{201.4}{\second}{stunedrev-chain}`; time for 99.9 % of a note's energy to return `\misurau{327.8}{\second}{stunedrev-chain}`, `\misurau{171.1}{\second}{stunedrev-chain}`, `\misurau{44.4}{\second}{stunedrev-chain}`, `\misurau{603.8}{\second}{stunedrev-chain}`; the centroid grows linearly with the time (sum of the delays = time·k·903); intended: each face returns the sound on its own time scale; audio `\studio{stunedrev-chain}` via `render.py` | `\daprovare` |
| `stunedrev-ingresso-uscita` | Ingresso (CC83) e uscita (CC84) | the Pd patch also wires `adc~ 5 6 7 8` straight into the reverb, so it receives the inputs unconditionally and the APF INPUT fader adds at most +6 dB (source `log`) | `\domanda` |
| `stunedrev-sezioni` | Le sezioni che cambiano | SEAM rounds, Davide truncated: `\misura{813}{stunedrev-delays}` of the 16 800 delays of the range move to another prime, by up to `\misura{96}{stunedrev-delays}` samples; at the starting times 7 of 168 sections change (√2: 3, φ: none, e: 3, π: 1 — table from `stunedrev-delays`); elsewhere the line equals Davide's sample for sample | `\deciso` |
| `stunedrev-memoria` | Memoria | original `\misura{3.94}{stunedrev-chain}` GiB in Faust double, the Pd external `\misura{15.2}{stunedrev-chain}` GiB; SEAM Faust `\misura{1.66}{stunedrev-chain}` GiB, `\misura{1.15}{stunedrev-chain}` GiB with `-dlt 4096`; the C++ plugin sized at 96 kHz `\misura{588}{stunedrev-chain}` MiB | `\deciso` |

Cite `\studio{stunedrev-allpass}` for the all-pass form and `\studio{stunedrev-delays}` for the primes in the chapter opening.
Grep every value before wrapping it.

- [ ] **Step 2: Build and check**

Run: `make check`
Expected: the four `stunedrev-*` schede present; the three stunedrev studies cited; no "value not found".

- [ ] **Step 3: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): the stunedrev cards"
```

---

### Task 6: Choir placeholder, master, Part III

**Files:**
- Modify: `doc/study/sscdo2/report/parte2-coro-master.tex`, `doc/study/sscdo2/report/parte3-prove.tex`

**Interfaces:**
- Consumes: macros; every `scheda:*` label from Tasks 3–5.
- Produces: `scheda:coro-uscita`, `scheda:master`; `prova:*` for every id in `REQUIRED_PROVE`; `q:*` for every id in `REQUIRED_QUESTIONI`.

- [ ] **Step 1: Write `parte2-coro-master.tex`**

`\chapter{Coro e master}`: the choir (`pitchDetectorChoirMcAdams` ×4: 16 bands, f = 48, 48, 96, 96 Hz, a = 1, 1.01, 1.1, 0.9, Q 350, release 1.5 s; source `log`) is not yet studied: `\scheda{coro-uscita}{Uscita del coro (CC86)}{}{}{Non ancora studiato: lo studio seguirà quello di stunedrev.}{}{\daprovare}{}` (empty fields print —). Master: `\scheda{master}{Master (CC88)}{...}{...}{...}{...}{\daprovare}{...}` with the master volume `si.smoo` (standard) and the fact that gain stages move to Reaper's track faders (source `log`).

- [ ] **Step 2: Write `parte3-prove.tex`**

Three chapters.

`\chapter{Preparazione}`: 96 kHz; TETRAREC A → ASP880 → inputs 5–8; outputs 9–12 to STONED (LFU, RFD, RBU, LBD); the ASP880's gain sets delRM's dynamic window (`\ref{scheda:delrm-dinamica}`); how to regenerate the stunedrev renders (`render.py`, about 4 minutes, 330 MB).

`\chapter{Schede di prova}` in the order of the piece; each card cites its `\scheda` with `\ref`:

| id | ascoltare | valore | riferimento |
|---|---|---|---|
| `lmo-cue` | the LMO band at cue 1 | 97.44 Hz, Δ = 0 | `lmo-bandfilter/renders/` (B) |
| `lmo-glissando` | the glissando of cue 2 | 97.44 → 112.67 Hz, 120 s | log, cue list |
| `lmo-delta` | where the beat starts | Δ 0, 3, 7, 10, 20, 40 Hz, sweep | `lmo-beats/renders/` |
| `lmo-filtro` | the band filter's shape and tail | A, B, C, C3, C2 | `lmo-bandfilter/renders/` |
| `delrm-distanza` | which partials each distance lifts | 5, 7.291, 10 m | `delrm-comb/renders/` |
| `delrm-dinamica` | where channels 2/4 appear and saturate | ASP880 gain in steps | `delrm-rm/renders/` |
| `delrm-dcblocker` | the low end with and without the two DC blockers | clean / two DC blockers | `delrm-dcblock/renders/` |
| `stunedrev-facce` | the four faces returning the note | 83, 47, 7, 71 ms | `stunedrev-chain/renders/` (regenerate) |
| `stunedrev-ingresso` | the reverb with the APF input fader at zero | CC83 = 0 | live, with the double feed |

`\chapter{Domande per Davide}`: five `\questione` in this order:
`doppio-ingresso` (the double feed into stunedrev: intended or residual?), `uscite-cue` (which cue output and side carries each of LFU, RFD, RBU, LBD), `semi` (the shared noise seeds in LMO and choir: a sound to keep, or an oversight?), `dcblocker` (keep the thinner low end of the two DC blockers?), `filtro-lmo` (B, or one of A, C, C3, C2?).

Then `\section{Bozza della tabella operativa}`: a `longtable` with columns Fader/Cue, Macchina, Valore iniziale, Ambito, Nota, one row per BCF2000 fader in use (CC81, 82, 83, 84, 86, 88) and per cue (0, 1, 2, 99), each note a `\ref` to its card.

- [ ] **Step 3: Build and check**

Run: `make check`
Expected: every required id present; only uncited studies (if any) remain; no "value not found".

- [ ] **Step 4: Commit**

```bash
git add doc/study/sscdo2/report
git commit -m "feat(sscdo2/report): choir and master cards, rehearsal protocol, questions"
```

---

### Task 7: Audio appendix, index, final build

**Files:**
- Modify: `doc/study/sscdo2/report/appendice-audio.tex`, `doc/study/sscdo2/README.md`, `logs/2026-09-29-sscdo2-ricognizione.md`
- Create: `doc/study/sscdo2/report/sscdo2-porting.pdf` (built, committed)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write `appendice-audio.tex`**

`\chapter{File audio}`: one `longtable` per study with sound (lmo-bandfilter, lmo-beats, lmo-streams, delrm-comb, delrm-rm, delrm-dcblock, stunedrev-chain) listing the files as they are in each `renders/` folder (list them with `ls` while writing; do not invent names), marking committed ones and the stunedrev files regenerated by `render.py`; `\studio{...}` per table.

- [ ] **Step 2: Full check**

Run: `make check`
Expected: `CHECK OK`, self-test all `[ok]`, no build-log problems.

- [ ] **Step 3: Read the PDF**

Open `sscdo2-porting.pdf` and check page by page: every card shows all fields and its tag; the rehearsal cards print ruled *Esito*/*Decisione* lines; √, φ, π, →, Δ render; the table of contents lists three parts. Fix and rebuild if not.

- [ ] **Step 4: Link the report**

In `doc/study/sscdo2/README.md`, after the first paragraph, add one line: `The rehearsal report, in Italian, gathers every study into principles, one card per control, and the rehearsal protocol: \`report/sscdo2-porting.pdf\` (\`make check\` in \`report/\`).`
Append to the session log a `### The porting report` entry: what it is, where, `make check`, and that decisions from rehearsals are written there first.

- [ ] **Step 5: Commit**

```bash
git add doc/study/sscdo2/report doc/study/sscdo2/README.md logs/2026-09-29-sscdo2-ricognizione.md
git commit -m "docs(sscdo2): the porting report, built and checked"
```

---

## Self-review notes

- Spec coverage: purpose/audience/language (Global Constraints, Task 2 abstract); location/build/PDF committed (Tasks 1, 7); state markers and `\studio` (Task 1); Part I eight principles (Task 2); Part II cards for LMO, delRM, stunedrev, choir, master (Tasks 3–6); Part III preparation, rehearsal cards with blank fields, A/B sessions, questions, operational table draft (Task 6); sources and maintenance: `\misura` + `check.py`, decisions written here first (Tasks 1, 7); audio appendix (Task 7); checks before delivery (Task 1 `make check`, Task 7 Step 2).
- Numbers that are not literal in a study `.md` (Task 2 items 1 and 4, the "about 15 dB" window in Task 4) are written in words with `\studio`, never wrapped in `\misura`.
