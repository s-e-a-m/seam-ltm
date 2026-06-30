# ltburst Phase 2 + 2b — Faust Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Author the Faust specification of the Linkwitz shaped tone-burst as a new author-attributed library `seam.linkwitz.lib` (prefix `slw`), covering both the fixed-frequency generator (Phase 2) and the glissando generalisation with sweep + two grain-timing modes (Phase 2b), register it in `seam.lib`, and document the reconstruction in Section 5 of the Italian study diary.

**Architecture:** One Faust library file holds three composable units — a sweep map (`sweepfreq`), a glissando-burst generator (`glissburst`, a structured `loop ~ (_,_)` feedback engine), and a pure composition helper (`linkwitzglide`) — plus the fixed-frequency `shapedburst`. The library compiles on stable Faust; the experimental `ondemand` passo variant is documented in the diary only, never in the library. The deliverable is DSP only: no transport, no plugin.

**Tech Stack:** Faust 2.85.5 (Homebrew, `/usr/local/bin/faust`); `faust-od` (dev binary wrapper) for the `ondemand` listing check only; Python 3 for the runtime recurrence simulation; LaTeX (`latexmk`) for the diary.

## Global Constraints

- The library **must compile on stable Faust 2.85.5**. No `ondemand` token in `seam.linkwitz.lib` — it lives only in the diary as documentation.
- Author-attributed library pattern (model: `seam.gerzon.lib`): `declare name "Siegfried Linkwitz - Library";` + version `"0.1"` + author `"Giuseppe Silvi"` + license `"CC3"`; then `import("seam.lib");`; then re-declare own prefix `slw = library("seam.linkwitz.lib");`.
- Preserve the Linkwitz citations (AES preprint 1342, 1978; JAES 28(4):250–258, 1980).
- No `faust -lang cpp` code generation: Faust is the spec, ported by hand later.
- Two git repositories:
  - `faust-libraries` at `/Users/giuseppe/Documents/github/seam/librerie/faust-libraries`, currently on `master` → create branch `feat/linkwitz-shaped-tone-burst` (Tasks 1–5).
  - `seam-ltm` at `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm`, already on `feat/ltburst-linkwitz-tone-burst` (Task 6).
- Diary prose is Italian, **one sentence per line**, affirmative explanatory voice.
- Commit messages and code comments are English.

## File Structure

- Create: `faust-libraries/src/seam.linkwitz.lib` — the library (header + citation + `shapedburst`, `shapedburst5`, `sweepfreq`, `glissburst`, `glissburst5`, `linkwitzglide`).
- Modify: `faust-libraries/src/seam.lib` — one registration line in the "author specific literature" section.
- Modify: `seam-ltm/plugins/ltburst/doc/study/ltburst-study.tex` — preamble (`listings` package + Faust style) and Section 5 body.

## Verification idiom (Faust has no unit-test runner)

A "failing test" is an inline `process` that references a not-yet-defined function: compiling it fails with an undefined-symbol error. Implementing the function makes the same compile succeed. The gate command, run from the library directory, is:

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```

`-I .` lets `import("seam.linkwitz.lib")` and `import("seam.lib")` (and the other `seam.*.lib` it pulls in) all resolve from `src/`.

---

### Task 1: Library skeleton + fixed-frequency `shapedburst` (Phase 2)

**Files:**
- Create: `faust-libraries/src/seam.linkwitz.lib`
- Test: `/tmp/lwtest.dsp` (scratch, not committed)

**Interfaces:**
- Produces: `shapedburst(f0, N, dwell)` and `shapedburst5(f0, dwell)` — fixed-frequency raised-cosine N-cycle burst, one output, no inputs.

- [ ] **Step 1: Create the feature branch**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git checkout master && git checkout -b feat/linkwitz-shaped-tone-burst
```

- [ ] **Step 2: Write the failing test**

```bash
printf 'import("seam.linkwitz.lib");\nprocess = shapedburst5(1000,0.3);\n' > /tmp/lwtest.dsp
```

- [ ] **Step 3: Run it to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```
Expected: FAIL — `ERROR ... undefined symbol : seam.linkwitz.lib` (file does not exist yet).

- [ ] **Step 4: Create the library with the header and the fixed-frequency generator**

Create `faust-libraries/src/seam.linkwitz.lib` with exactly:

```faust
declare name "Siegfried Linkwitz - Library";
declare version "0.1";
declare author "Giuseppe Silvi";
declare license "CC3";
//
import("seam.lib");
//============================================= SIEGFRIED LINKWITZ TESTING ===
//============================================================================
slw = library("seam.linkwitz.lib");
//
// shaped tone-burst loudspeaker testing
// AES preprint 1342 (1978); JAES 28(4):250-258 (1980)
//
//   burst (N cycles)        dwell (M cycles)
//  /\  /\  /\  /\  /\
// /  \/  \/  \/  \/  \____________________________  repeats every P=N+M cycles
//
//--------------------------------------------------------------- shapedburst ---
// fixed-frequency raised-cosine N-cycle burst, repeated every P=N+M carrier cycles
shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win
  with {
    M   = max(1, int(ceil(dwell*f0)));            // dwell quantised to whole cycles
    P   = N + M;                                  // total cycles per period
    c   = os.phasor(1, f0/P);                     // 0..1 over P carrier cycles
    u   = P*c;                                    // cycle position 0..P
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); // Hann over the first N cycles
  };
shapedburst5(f0,dwell) = shapedburst(f0,5,dwell); // canonical N=5 wrapper
//process = slw.shapedburst5(1000, 0.3);
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null && echo OK
```
Expected: prints `OK` (compiles, no output written).

- [ ] **Step 6: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.linkwitz.lib
git commit -m "feat(linkwitz): new seam.linkwitz.lib with fixed-frequency shaped tone-burst"
```

---

### Task 2: `sweepfreq` (exp/lin frequency map)

**Files:**
- Modify: `faust-libraries/src/seam.linkwitz.lib`
- Test: `/tmp/lwtest.dsp`

**Interfaces:**
- Consumes: nothing.
- Produces: `sweepfreq(f0, f1, smode, p)` — maps progress `p ∈ [0,1]` to a frequency; `smode` 0 = linear, 1 = exponential. One output.

- [ ] **Step 1: Write the failing test**

```bash
printf 'import("seam.linkwitz.lib");\nprocess = sweepfreq(20000,20,1, os.phasor(1,1/4));\n' > /tmp/lwtest.dsp
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```
Expected: FAIL — `undefined symbol : sweepfreq`.

- [ ] **Step 3: Add `sweepfreq` to the library**

Append to `seam.linkwitz.lib` (after `shapedburst5`'s inline test comment):

```faust
//
//----------------------------------------------------------------- sweepfreq ---
// progress p in [0,1] -> frequency (smode 0 = linear, 1 = exponential)
sweepfreq(f0,f1,smode,p) = select2(smode, f0 + (f1-f0)*p, f0*pow(f1/f0, p));
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.linkwitz.lib
git commit -m "feat(linkwitz): add sweepfreq exp/lin frequency map"
```

---

### Task 3: `glissburst` / `glissburst5` (glissando engine, gap + passo)

**Files:**
- Modify: `faust-libraries/src/seam.linkwitz.lib`
- Test: `/tmp/lwtest.dsp`, `/tmp/lwsim.py` (scratch)

**Interfaces:**
- Consumes: `sweepfreq` (for the test signal).
- Produces: `glissburst(N, delta, dmode, fsig)` — glissando of bursts; `fsig` is a frequency signal (Hz), `dmode` 0 = passo (onset-fixed `Tg = max(delta, N/fg)`), 1 = gap (gap-fixed `Tg = N/fg + delta`). One output. And `glissburst5(delta, dmode, fsig) = glissburst(5, delta, dmode, fsig)`.

- [ ] **Step 1: Write the failing test**

```bash
printf 'import("seam.linkwitz.lib");\nprocess = glissburst5(0.3, 1, os.phasor(1,1/4):sweepfreq(20000,20,1));\n' > /tmp/lwtest.dsp
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```
Expected: FAIL — `undefined symbol : glissburst5`.

- [ ] **Step 3: Add the glissando engine to the library**

Append to `seam.linkwitz.lib`:

```faust
//
//---------------------------------------------------------------- glissburst ---
// glissando of bursts: fsig = frequency signal (Hz), dmode 0 = passo, 1 = gap.
// Each grain latches one frequency (sample-and-hold at onset) and stays a
// single-frequency, constant-Q burst. The grain ramp + held frequency form a
// structured two-signal feedback; max(20,.) guards the period division on init
// and the start pulse (1 - 1') latches the sweep at the first sample.
glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u) * win
  with {
    phase = grain : _,!;                          // grain ramp 0..1
    fg    = grain : !,_;                          // held grain frequency
    grain = loop ~ (_,_)
      with {
        loop(pphase,pfg) = nphase, nfg
          with {
            den    = max(20.0, pfg);              // guard div-by-zero on init
            Tg     = select2(dmode, max(delta, N/den), N/den + delta);
            inc    = 1.0/max(1.0, Tg*ma.SR);
            adv    = pphase + inc;
            start  = 1 - 1';                      // 1 only at the first sample
            onset  = (adv >= 1.0) | start;        // wrap, plus a forced start latch
            nphase = adv - floor(adv);
            nfg    = select2(onset, pfg, fsig);   // hold; latch fsig at onset
          };
      };
    den = max(20.0, fg);
    Tg  = select2(dmode, max(delta, N/den), N/den + delta);
    u   = fg * phase * Tg;                         // cycles since onset
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));  // Hann over the first N cycles
  };
glissburst5(delta,dmode,fsig) = glissburst(5,delta,dmode,fsig); // canonical N=5 wrapper
```

- [ ] **Step 4: Run the compile test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Write the runtime simulation (no NaN, correct latching)**

Create `/tmp/lwsim.py`:

```python
import math
SR=48000; N=5; delta=0.3; dmode=1   # gap mode
def fsig(n):
    t=n/SR; p=(t*0.25)%1.0
    return 20000.0*pow(20.0/20000.0, p)
pphase=0.0; pfg=0.0; onsets=[]; nan=False
for n in range(int(4.5*SR)):
    den=max(20.0,pfg)
    Tg=(N/den)+delta if dmode==1 else max(delta,N/den)
    inc=1.0/max(1.0,Tg*SR)
    adv=pphase+inc
    start=1.0 if n==0 else 0.0
    onset=1.0 if (adv>=1.0 or start>0) else 0.0
    nphase=adv-math.floor(adv)
    nfg=fsig(n) if onset>0 else pfg
    fg=nfg; phase=nphase
    Tgo=(N/max(20.0,fg))+delta if dmode==1 else max(delta,N/max(20.0,fg))
    u=fg*phase*Tgo
    out=math.sin(2*math.pi*u)*((1.0 if u<N else 0.0)*(0.5-0.5*math.cos(2*math.pi*u/N)))
    if math.isnan(out) or math.isinf(out): nan=True; break
    if onset>0: onsets.append(round(fg,1))
    pphase,pfg=nphase,nfg
print("NaN:",nan,"| onsets:",len(onsets),"| first freqs:",onsets[:6])
assert not nan, "NaN/Inf produced"
assert onsets[0]==20000.0 and onsets[1]<onsets[0], "frequency not latched/descending"
print("PASS")
```

- [ ] **Step 6: Run the simulation to verify no NaN and descending latched frequencies**

```bash
python3 /tmp/lwsim.py
```
Expected: `NaN: False ...` then `PASS` (first latched freq 20000, then descending; no NaN).

- [ ] **Step 7: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.linkwitz.lib
git commit -m "feat(linkwitz): add glissburst glissando engine (passo + gap modes)"
```

---

### Task 4: `linkwitzglide` (composition helper)

**Files:**
- Modify: `faust-libraries/src/seam.linkwitz.lib`
- Test: `/tmp/lwtest.dsp`

**Interfaces:**
- Consumes: `glissburst`, `sweepfreq`.
- Produces: `linkwitzglide(f0, f1, smode, N, delta, dmode, p)` — wires a sweep into the glissando burst, `p` supplied externally. One output.

- [ ] **Step 1: Write the failing test**

```bash
printf 'import("seam.linkwitz.lib");\nprocess = linkwitzglide(20000,20,1,5,0.3,1, os.phasor(1,1/4));\n' > /tmp/lwtest.dsp
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```
Expected: FAIL — `undefined symbol : linkwitzglide`.

- [ ] **Step 3: Add `linkwitzglide` to the library**

Append to `seam.linkwitz.lib`:

```faust
//
//-------------------------------------------------------------- linkwitzglide ---
// pure composition for testing (p supplied externally, no transport)
linkwitzglide(f0,f1,smode,N,delta,dmode,p) =
    glissburst(N,delta,dmode, sweepfreq(f0,f1,smode,p));
//process = slw.linkwitzglide(20000, 20, 1, 5, 0.3, 1, os.phasor(1, 1/4));
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.linkwitz.lib
git commit -m "feat(linkwitz): add linkwitzglide sweep+burst composition helper"
```

---

### Task 5: Register `slw` in `seam.lib`

**Files:**
- Modify: `faust-libraries/src/seam.lib`
- Test: `/tmp/lwtest.dsp`

**Interfaces:**
- Consumes: the whole library.
- Produces: `slw.*` access path from any DSP that does `import("seam.lib")`.

- [ ] **Step 1: Write the failing test (slw not yet registered)**

```bash
printf 'import("seam.lib");\nprocess = slw.linkwitzglide(20000,20,1,5,0.3,1, os.phasor(1,1/4));\n' > /tmp/lwtest.dsp
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null
```
Expected: FAIL — `undefined symbol : slw` (not registered in `seam.lib` yet).

- [ ] **Step 2: Add the registration line**

In `faust-libraries/src/seam.lib`, in the `// ### author specific literature` section, immediately after the line `smg = library("seam.gerzon.lib");`, add:

```faust
slw = library("seam.linkwitz.lib");
```

- [ ] **Step 3: Run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . /tmp/lwtest.dsp -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 4: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.lib
git commit -m "feat(linkwitz): register slw = seam.linkwitz.lib in seam.lib"
```

---

### Task 6: Study diary Section 5 (seam-ltm, Italian)

**Files:**
- Modify: `seam-ltm/plugins/ltburst/doc/study/ltburst-study.tex`
- Test: `/tmp/lwod.dsp` (scratch; checks the documented `ondemand` listing on `faust-od`)

**Interfaces:**
- Consumes: the finished `seam.linkwitz.lib` as the cited spec source.
- Produces: a filled Section 5 and a rebuilt `ltburst-study.pdf`.

- [ ] **Step 1: Verify the documented `ondemand` passo listing compiles on `faust-od`**

```bash
cat > /tmp/lwod.dsp <<'DSP'
import("stdfaust.lib");
glissburst_od_passo(N,delta,fsig) = sin(2*ma.PI*u) * win
with {
    inc      = 1.0/max(1.0, delta*ma.SR);
    phase    = (+(inc) : frac1) ~ _;
    frac1(x) = x - floor(x);
    onset    = (phase < phase') | (1 - 1');
    fg       = (onset, fsig) : ondemand(_);
    u        = fg * phase * delta;
    win      = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
};
process = glissburst_od_passo(5, 0.3, 20000*pow(20/20000.0, os.phasor(1,1/4)));
DSP
faust-od /tmp/lwod.dsp -o /dev/null && echo "ondemand passo: OK"
```
Expected: `ondemand passo: OK` (this confirms the listing that Section 5 documents).

- [ ] **Step 2: Add the `listings` package and a Faust style to the preamble**

In `ltburst-study.tex`, replace the line:

```latex
\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,hyperref}
```

with:

```latex
\usepackage{amsmath,amssymb,graphicx,booktabs,siunitx,hyperref}
\usepackage{listings}
\lstdefinestyle{faust}{basicstyle=\ttfamily\small,breaklines=true,
  keepspaces=true,columns=fullflexible,frame=single,framesep=4pt}
\lstset{style=faust}
```

- [ ] **Step 3: Replace the Section 5 placeholder with the body**

In `ltburst-study.tex`, replace:

```latex
\section{Ricostruzione in Faust}\label{sec:faust}
Sviluppata nella Fase 2.
```

with (Italian, one sentence per line):

```latex
\section{Ricostruzione in Faust}\label{sec:faust}
La specifica Faust del tone-burst vive nella libreria \texttt{seam.linkwitz.lib} (prefisso \texttt{slw}), seguendo il modello author-attributed di Gerzon, Moorer e Roads.
Questa sezione documenta la ricostruzione; il file della libreria resta la fonte autorevole e non viene duplicato qui.

\subsection{Dal tono fisso al glissando}
Il modello matematico di Linkwitz (1980, \S2) descrive il grano come una sinusoide moltiplicata per una finestra a coseno rialzato su un numero costante di cicli $N=5$, da cui la propriet\`a a $Q$ costante.
La generazione hardware (1980, \S4) ottiene lo stesso risultato modulando in ampiezza al 100\% la portante con una sinusoide a $f_0/5$, seguita da un intervallo di silenzio (\emph{dwell}) che lascia decadere gli echi della sala.
Il generatore a frequenza fissa \texttt{shapedburst} riproduce questo schema con un singolo phasor, mantenendo la sincronia di ciclo.

Il glissando generalizza il generatore facendo seguire alla portante uno sweep esterno da $f_0$ a $f_1$.
Uno sweep esponenziale percorre ottave uguali nel tempo, uno lineare percorre hertz uguali; entrambi sono offerti da \texttt{sweepfreq} tramite un selettore.
Ogni grano cattura una sola frequenza con un campionamento-e-tenuta al proprio inizio, cos\`i resta un tono singolo a $Q$ costante invece di diventare un chirp.

\subsection{I due modi di spaziatura}
Il modo \emph{passo} fa partire un grano ogni $\delta$ secondi misurati inizio-inizio, e il silenzio residuo si accorcia quando la frequenza scende.
Il modo \emph{gap} mantiene $\delta$ secondi di silenzio misurati fine-inizio, e il passo tra gli inizi cresce quando la frequenza scende.
Il modo passo protegge dalla sovrapposizione con \texttt{max(delta, N/fg)}, perch\'e a \SI{20}{\hertz} un grano di cinque cicli dura \SI{250}{\milli\second}.

\subsection{Il motore canonico}
Il generatore deriva la rampa di grano e la frequenza tenuta da un'unica retroazione a due segnali, cos\`i portante e finestra condividono lo stesso riferimento di fase.
Azzerare la fase all'inizio del grano d\`a la partenza sullo zero-crossing in modo gratuito, perch\'e $\sin(0)=0$ e la finestra di Hann vale zero in quel punto.

\begin{lstlisting}
glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u) * win
  with {
    phase = grain : _,!;
    fg    = grain : !,_;
    grain = loop ~ (_,_)
      with {
        loop(pphase,pfg) = nphase, nfg
          with {
            den    = max(20.0, pfg);
            Tg     = select2(dmode, max(delta, N/den), N/den + delta);
            inc    = 1.0/max(1.0, Tg*ma.SR);
            adv    = pphase + inc;
            onset  = (adv >= 1.0) | (1 - 1');
            nphase = adv - floor(adv);
            nfg    = select2(onset, pfg, fsig);
          };
      };
    den = max(20.0, fg);
    Tg  = select2(dmode, max(delta, N/den), N/den + delta);
    u   = fg * phase * Tg;
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
  };
\end{lstlisting}

\subsection{Quando \texttt{ondemand} calza, e quando no}
Il primitivo sperimentale \texttt{ondemand} valuta un sotto-circuito solo quando il suo clock \`e attivo e ne tiene le uscite tra un colpo e l'altro.
In modo passo il periodo \`e fisso, quindi il clock del grano \`e indipendente dalla frequenza: un phasor a periodo fisso genera gli inizi e \texttt{ondemand} cattura in avanti la frequenza del grano.

\begin{lstlisting}
glissburst_od_passo(N,delta,fsig) = sin(2*ma.PI*u) * win
  with {
    inc      = 1.0/max(1.0, delta*ma.SR);
    phase    = (+(inc) : frac1) ~ _;
    frac1(x) = x - floor(x);
    onset    = (phase < phase') | (1 - 1');
    fg       = (onset, fsig) : ondemand(_);
    u        = fg * phase * delta;
    win      = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
  };
\end{lstlisting}

In modo gap il periodo $N/f_g + \delta$ dipende dalla frequenza tenuta, quindi il clock del grano si auto-referenzia.
Far passare quell'anello per \texttt{ondemand} fa inseguire il ciclo al valutatore dei box e fallisce, mentre la retroazione strutturata del motore canonico lo risolve usando la frequenza del grano precedente.
La lezione di progetto \`e che \texttt{ondemand} esprime bene una decisione \emph{in avanti} per grano, mentre un clock auto-referenziale appartiene a una retroazione \texttt{\textasciitilde}; questa mappa corrisponde uno-a-uno al futuro \texttt{if (onset)} del porting in C++.

\subsection{Rinvii}
La validazione contro gli spettri pubblicati e la variante a gradini restano alla fase C++ con il toolkit di test.
```

- [ ] **Step 4: Build the diary**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/ltburst/doc/study
make
```
Expected: `latexmk` builds `ltburst-study.pdf` with no errors (Section 5 now populated, two Faust listings render).

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ltburst/doc/study/ltburst-study.tex plugins/ltburst/doc/study/ltburst-study.pdf
git commit -m "docs(ltburst): write study Section 5 (Faust reconstruction + ondemand finding)"
```

---

## Self-Review

**Spec coverage** (against `2026-06-30-ltburst-glissando-sweep-design.md` and the Phase 2 spec):
- Library surface `sweepfreq` / `glissburst` / `glissburst5` / `linkwitzglide` → Tasks 2, 3, 4. ✓
- Fixed-frequency `shapedburst` / `shapedburst5` (Phase 2) → Task 1. ✓
- Two timing modes (passo/gap) with overlap guard → Task 3 code + Section 5. ✓
- Per-grain sample-and-hold, zero-crossing start → Task 3 code + Section 5. ✓
- Library integration in `seam.lib` → Task 5. ✓
- `ondemand` dual formulation: stable lib only; passo variant documented in diary → library excludes `ondemand` (Global Constraints), Section 5 documents it (Task 6 Steps 1, 3). ✓
- Cross-repo branches → Global Constraints + Task 1 Step 1 (faust-libraries) and Task 6 (seam-ltm). ✓
- Verification gates (stable compile, runtime no-NaN, ondemand on faust-od, diary build) → Tasks 3, 5, 6. ✓
- Out of scope (transport, plugin, C++, plots, staircase) → not present in any task. ✓

**Placeholder scan:** no TBD/TODO; every code step shows complete code; every command shows expected output. ✓

**Type consistency:** `glissburst(N,delta,dmode,fsig)`, `glissburst5(delta,dmode,fsig)`, `sweepfreq(f0,f1,smode,p)`, `linkwitzglide(f0,f1,smode,N,delta,dmode,p)`, `slw` — names and arities identical across Tasks 2–5 and Section 5 listings. ✓
