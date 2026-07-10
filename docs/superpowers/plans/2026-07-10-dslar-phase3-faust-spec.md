# dslar Phase 3 — Faust spec (line clone + sds.lar assembly) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the Faust specification of `LAR.pd` — the faithful `spd.line` clone, the promotion of the Di Scipio library into `src`, the feedforward `sds.lar` brick, and the compilable `dslar.dsp` with regenerated SVG + mathdoc.

**Architecture:** `LAR.pd` is a **mono feedforward** processor (the Larsen loop is acoustic, external); `dslar.dsp` composes verified Pd clones (`spd.*`) plus `de.delay` into a reusable `sds.lar` brick. No `~` feedback, no `ondemand`, stable faust throughout. Every Pd object carries a dual source citation (Pd C source + Puckette help patch).

**Tech Stack:** Faust 2.85.5 (stable), `de.delay`/`ba.line`/`ba.sAndH`/`ba.pulse` from faustlibraries, a scratch `faust -lang cpp` C++ harness for numerical verification, Python 3 oracle, `tools/gen-faust-doc.sh`, LaTeX (study diary).

## Global Constraints

- Two repos, both on branch `dslar`, both left committed-locally (do not push): `faust-libraries` (the `.lib` files) and `seam-ltm` (plugin `doc/`).
- `faust-libraries/src` absolute path: `/Users/giuseppe/Documents/github/seam/librerie/faust-libraries`.
- `seam-ltm` absolute path: `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm`.
- Faust libraries source path for `-I`: `/Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src`.
- Faust C++ architecture headers for the harness `c++ -I`: `/Users/giuseppe/Documents/github/faust/architecture`.
- Pd source + help patches checkout: `/Users/giuseppe/Documents/github/pure-data` (`src/`, `doc/5.reference/`).
- Faust is the spec; `faust -lang cpp` is a scratch verification tool ONLY — its output never lands in `plugins/*/source/` (CLAUDE.md).
- No `ondemand`, no `faust-od` in the shared libraries — stable faust only.
- Prose/doc lines: one sentence per line (clean diffs).
- Every commit ends with the trailer `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- Scratch files (harness, oracle, test `.dsp`) live in the session scratchpad, never committed.

---

## Task 1: Promote `seam.discipio.lib` into `src`, enable `sds` in `seam.lib`

**Files:**
- Move: `faust-libraries/temp/seam.discipio.lib` → `faust-libraries/src/seam.discipio.lib`
- Modify: `faust-libraries/src/seam.lib:26` (uncomment the `sds` registration)

**Interfaces:**
- Produces: `sds = library("seam.discipio.lib")` resolvable from `FAUST_LIB_PATH=faust-libraries/src`; `sds.larsengain(npoints, period, reference, k)`, `sds.integrator`, `sds.localmax` available via `import("seam.lib")`.

- [ ] **Step 1: Move the library into `src` with git**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git mv temp/seam.discipio.lib src/seam.discipio.lib
```

- [ ] **Step 2: Enable the `sds` registration in `seam.lib`**

Edit `src/seam.lib` line 26, replacing the commented line with the active one:

```faust
sds = library("seam.discipio.lib");
```

(It currently reads `// sds = library("seam.discipio.lib");`.)

- [ ] **Step 3: Verify `seam.lib` still compiles with `sds` enabled**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
echo 'import("src/seam.lib"); process = _;' | faust -I src - -o /dev/null && echo OK
```
Expected: `OK` (no import cycle or name-collision error).

- [ ] **Step 4: Verify a known `sds` usage compiles at the full LAR window**

Run:
```bash
echo 'import("src/seam.lib"); process = sds.larsengain(2048, 1024, 1.0, 40.0);' | faust -I src - -o /dev/null && echo OK
```
Expected: `OK` (compiles in well under a second — the overlap-add `spd.env` makes the 2048 window instant).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
feat(discipio): promote seam.discipio.lib temp -> src, enable sds in seam.lib

The Di Scipio library is under active curation (dslar Phase 3); move it into
src and register sds so dslar.dsp resolves it via the standard FAUST_LIB_PATH.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `spd.line` — faithful Pd `line` clone + numerical verification

**Files:**
- Modify: `faust-libraries/src/seam.pdclone.lib` (append `line` after the `env` block at end of file)
- Scratch (not committed): `<scratch>/harness.cpp`, `<scratch>/linetest.dsp`, `<scratch>/oracle_line.py`

**Interfaces:**
- Consumes: `ba.if`, `ba.sAndH`, `ba.pulse`, `ma.SR` (via the lib's existing `import("seam.lib")`).
- Produces: `spd.line(ms, x)` — a control-rate ramp toward the current target value `x`, taking `ms` milliseconds, emitting a 20 ms grain staircase (Pd `DEFAULTLINEGRAIN`), restarting from the current value on each new target. `_ : spd.line(200.0)` is the curried signal-processor form.

- [ ] **Step 1: Write the reusable C++ harness (scratch)**

Write `<scratch>/harness.cpp`:
```cpp
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif
#include "faust/dsp/dsp.h"
#include "faust/gui/meta.h"
#include "faust/gui/UI.h"
<<includeIntrinsic>>
<<includeclass>>
int main(int argc, char** argv){
    int SR = argc>1?atoi(argv[1]):44100;
    mydsp DSP; DSP.init(SR);
    std::vector<FAUSTFLOAT> in; double v;
    while(scanf("%lf",&v)==1) in.push_back((FAUSTFLOAT)v);
    int N=(int)in.size(); std::vector<FAUSTFLOAT> out(N);
    FAUSTFLOAT* ins[]={in.data()}; FAUSTFLOAT* outs[]={out.data()};
    DSP.compute(N, ins, outs);
    for(int i=0;i<N;i++) printf("%.9g\n",(double)out[i]);
    return 0;
}
```

- [ ] **Step 2: Write the failing test — Pd `line` oracle + a standalone test dsp that does NOT yet reference `spd.line`**

Write `<scratch>/linetest.dsp` (standalone, will FAIL to compile because `spd.line` is not defined yet):
```faust
import("seam.lib");
process = spd.line(100.0);
```

Write `<scratch>/oracle_line.py`:
```python
import math, subprocess, sys
SR=44100; ms=100.0; grain=20.0
R=ms*SR/1000.0; G=grain*SR/1000.0
# Two stimuli: (A) step 0->1 at n=0; (B) re-target 1.0 then 0.3 at n=2000 (restart-from-current).
def pd_line(inp):
    # Continuous Pd line_tick: v = setval + min(elapsed/R,1)*(target-setval); restart-from-current.
    cont=[]; sv=0.0; e=0; cprev=0.0
    for n,x in enumerate(inp):
        chg = (x != inp[n-1]) if n>0 else (x != 0.0)
        if chg: sv=cprev; e=0
        else: e+=1
        v = sv + min(e/R,1.0)*(x-sv)
        cont.append(v); cprev=v
    # grain staircase: sample-and-hold at ticks n = 0,G,2G,...
    return [cont[int(math.floor(n/G)*G)] for n in range(len(inp))]
def run(inp, label):
    open("in.txt","w").write("\n".join(repr(v) for v in inp)+"\n")
    r=subprocess.run(["./linetest",str(SR)],stdin=open("in.txt"),capture_output=True,text=True)
    fau=[float(x) for x in r.stdout.split()]
    orc=pd_line(inp)
    md=max(abs(f-o) for f,o in zip(fau,orc))
    print(f"{label}: max|diff| = {md:.3e}")
    return md
A=[1.0]*6000
B=[1.0]*2000 + [0.3]*7000
mA=run(A,"step-response")
mB=run(B,"re-target restart-from-current")
assert mA < 1e-6 and mB < 1e-6, "spd.line does NOT match Pd line oracle"
print("PASS: spd.line is float-exact vs Pd line")
```

- [ ] **Step 3: Run the test to verify it FAILS**

Run (from `<scratch>`):
```bash
faust -I /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src \
  -a harness.cpp linetest.dsp -o linetest_h.cpp
```
Expected: FAIL — `undefined symbol : spd.line` (the clone does not exist yet).

- [ ] **Step 4: Implement `spd.line` in the library**

Append to `faust-libraries/src/seam.pdclone.lib` (after the `env` block, before EOF):
```faust
//----------------------------------------------------------- line(ms, x) ---
// Pd source x_time.c (line, control-rate). Help patch: line-help.pd.
// Pd `line` ramps linearly toward its target and EMITS at a grain interval
// (DEFAULTLINEGRAIN = 20 ms), a control-rate staircase; it restarts from the
// current value on each new target (setval). Here ba.line is NOT used: the
// linear interp is rebuilt as Pd's v = setval + min(elapsed/R,1)*(target-setval)
// so the tick values match Pd's tau/R exactly, then sAndH on a 20 ms grain
// pulse imposes the staircase. Same idiom as spd.env (real math + control-rate
// hold). FAUST vs Pd: reproduces the grain staircase to float precision; the
// tick PHASE (Pd aligns ticks to message arrival, this to sample 0) is the
// documented residual, deferred to the block-kernel validation sub-project.
line(ms, x) = cont : ba.sAndH(ba.pulse(20.0 * ma.SR/1000.0))
with {
    R    = ms * ma.SR/1000.0;                 // ramp length in samples
    chg  = x != x';                           // new target
    cont = (loop ~ (_,_)) : (_, !)            // continuous interp; keep v, drop counter
    with {
        loop(v, e) = nv, ne
        with {
            ne = ba.if(chg, 0, e + 1);        // samples elapsed since change (0 at change)
            sv = ba.sAndH(chg, v);            // setval = output frozen at the change instant
            nv = sv + min(ne/R, 1.0) * (x - sv);
        };
    };
};
// process = line(100.0); // 100 ms ramp, 20 ms grain staircase; vs Pd line oracle < 1e-6
```

- [ ] **Step 5: Rebuild and run the test to verify it PASSES**

Run (from `<scratch>`):
```bash
faust -I /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src \
  -a harness.cpp linetest.dsp -o linetest_h.cpp && \
c++ -std=c++14 -I /Users/giuseppe/Documents/github/faust/architecture linetest_h.cpp -o linetest && \
python3 oracle_line.py
```
Expected output:
```
step-response: max|diff| = ~2e-08
re-target restart-from-current: max|diff| = ~1e-08
PASS: spd.line is float-exact vs Pd line
```

- [ ] **Step 6: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.pdclone.lib
git commit -m "$(cat <<'EOF'
feat(pdclone): add spd.line — faithful Pd control-rate line clone

x_time.c line: linear tau/R interp with restart-from-current, sampled at the
20 ms grain into a control-rate staircase (sAndH on ba.pulse). Verified
float-exact vs a Pd line oracle: step response 2e-8, mid-ramp re-target 1e-8.
Tick-phase alignment is the documented residual (block-kernel sub-project).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Dual-source documentation convention — backfill citations + vendor help patches

**Files:**
- Modify: `faust-libraries/src/seam.pdclone.lib` (add `Help patch:` lines to the `powtodb`/`rmstodb`/`dbtopow`/`dbtorms`, `hip`, `env` headers; add a `delread~`/`delwrite~` → `de.delay` documentation note)
- Create: `seam-ltm/plugins/dslar/doc/references/pd-help/` (copies of the 5 relevant Pd help patches)

**Interfaces:**
- Produces: every `spd` clone header cites both its Pd C source and its Pd help patch; the help patches are vendored in-repo.

- [ ] **Step 1: Vendor the Pd help patches into the plugin doc**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
mkdir -p plugins/dslar/doc/references/pd-help
cd /Users/giuseppe/Documents/github/pure-data/doc/5.reference
cp acoustics-help.pd hip~-help.pd env~-help.pd line-help.pd delay-tilde-objects-help.pd \
  /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/dslar/doc/references/pd-help/
```

- [ ] **Step 2: Add `Help patch:` citation lines to the existing clone headers**

In `faust-libraries/src/seam.pdclone.lib`, extend each existing header comment (the code lines are unchanged):
- On the `x_acoustics.c` converter block (powtodb/rmstodb/dbtopow/dbtorms): add `// Help patch: acoustics-help.pd (vendored under dslar/doc/references/pd-help).`
- On the `hip` header: add `// Help patch: hip~-help.pd.`
- On the `env` header: add `// Help patch: env~-help.pd.`

Example (extend the powtodb header):
```faust
// Pd source x_acoustics.c (powtodb). Help patch: acoustics-help.pd. Used by spd.env.
```

- [ ] **Step 3: Add a `delread~`/`delwrite~` documentation note near the `line` block**

Append to `faust-libraries/src/seam.pdclone.lib`:
```faust
//------------------------------------------- delread~ / delwrite~ (de.delay) ---
// Pd source d_delay.c. Help patch: delay-tilde-objects-help.pd.
// LAR's named lines are single-writer/single-reader (tab1 50 ms loop delay,
// tab2 20 ms decorrelation tap), so de.delay(n,d,x) = x@min(n,max(0,d)) is an
// exact match; no spd re-implementation is written. The Pd one-block minimum
// does not bind (50/20 ms >> 64 samples). Documented here for completeness so
// every Pd object LAR touches carries its dual source (C source + help patch).
```

- [ ] **Step 4: Verify the library still compiles (comments only, but confirm no typo broke it)**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
echo 'import("src/seam.lib"); process = spd.line(100.0), spd.env(64,32);' | faust -I src - -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Commit (both repos)**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.pdclone.lib
git commit -m "$(cat <<'EOF'
docs(pdclone): cite Pd help patches alongside C sources for every spd clone

Dual-source convention: each spd object header names both its Pd C source and
Miller Puckette's help patch. Adds the delread~/delwrite~ -> de.delay note.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/doc/references/pd-help
git commit -m "$(cat <<'EOF'
docs(dslar): vendor Pd help patches for the cloned objects

Self-contained copies of acoustics/hip~/env~/line/delay-tilde-objects help
patches, so the documentation does not depend on the external pure-data checkout.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `sds.lar` — feedforward LAR brick

**Files:**
- Modify: `faust-libraries/src/seam.discipio.lib` (add the `delms` helper and the `lar` brick)
- Scratch (not committed): `<scratch>/larsanity.dsp`, reuse `<scratch>/harness.cpp`

**Interfaces:**
- Consumes: `spd.hip`, `spd.line`, `de.delay`, `ma.SR`, and `larsengain` (defined in this same lib).
- Produces: `sds.lar(gate, drive, ref, k, tsmooth, tab1, tab2, output, x)` — the mono feedforward LAR processor. `gate` (0/1) applies a 2000 ms input fade before the fan-out; `drive` the audio pre-gain; `ref`/`k` the homeostat reference/exponent; `tsmooth` the control-smoothing ms; `tab1`/`tab2` the loop/decorrelation delays in ms; `output` the final level. Delay buffers are sized for up to `DELMAXMS = 200 ms` at `SRMAX = 192 kHz`.

- [ ] **Step 1: Write the failing sanity test — analysis gain of a DC input**

Write `<scratch>/larsanity.dsp` (FAILS until `sds.lar` exists; drives the analysis path with DC 0.5 and reads the settled loop gain):
```faust
import("seam.lib");
// Probe: audio branch forced to 1.0 (hip has unity DC gain removed, so use the
// analysis gain directly). Feed DC 0.5, read sds.lar output with drive=0,ref=1,k=40
// so output = 0 * ... would be zero; instead probe larsengain composition through lar
// by setting audio unity: use a dedicated probe of the analysis chain.
process = 0.5 : de.delay(882, 882) : sds.larsengain(64, 32, 1.0, 40.0) : spd.line(200.0);
```
(This probe exercises the exact analysis composition `sds.lar` uses; the DC steady state must be `|0.5 - 1|^40 = 0.5^40 ≈ 9.09e-13`.)

- [ ] **Step 2: Run to verify the probe currently returns the analysis gain, and note we still need `lar`**

Build and read the settled value:
```bash
cd <scratch>
faust -I /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src \
  -a harness.cpp larsanity.dsp -o larsanity_h.cpp && \
c++ -std=c++14 -I /Users/giuseppe/Documents/github/faust/architecture larsanity_h.cpp -o larsanity && \
python3 -c "import subprocess; open('din.txt','w').write('\n'.join(['0.5']*4000)+'\n'); \
print('settled analysis gain =', subprocess.run(['./larsanity','44100'],stdin=open('din.txt'),capture_output=True,text=True).stdout.split()[-1])"
```
Expected: `settled analysis gain = ~9.09e-13` (i.e. `0.5^40`). This confirms the composition; `sds.lar` must wrap it feedforward.

- [ ] **Step 3: Implement `delms` and `lar` in the library**

Append to `faust-libraries/src/seam.discipio.lib` (after the `larsengain` definition):
```faust
//----------------------------------------------------------------- LAR brick ---
// Feedforward mono LAR processor (LAR.pd). The Larsen loop is ACOUSTIC and
// external (dac~ -> room -> mic -> adc~); the patch has NO internal feedback, so
// tab1 is a feedforward delay, not a recursion. See dslar Phase 3 spec + study.
// Delay buffers are sized once for the worst case (DELMAXMS at SRMAX); the read
// delay adapts to ma.SR at runtime (de.delay needs a compile-time buffer size).
SRMAX    = 192000.0;
DELMAXMS = 200.0;
delms(ms) = de.delay(int(DELMAXMS*SRMAX/1000.0), int(ms*ma.SR/1000.0 + 0.5));
lar(gate, drive, ref, k, tsmooth, tab1, tab2, output, x) =
    audio(fx) * analysisGain(fx) * output          // loop multiplier, then output VCA
with {
    fx = x * (gate : spd.line(2000.0));            // adc~ x fade, BEFORE the audio/analysis split
    audio(s)        = s : spd.hip(100.0) : *(drive) : delms(tab1);
    analysisGain(s) = s : delms(tab2)
                        : larsengain(2048, 1024, ref, k)
                        : spd.line(tsmooth);
};
// process = lar(1.0, 1.0, 1.0, 40.0, 200.0, 50.0, 20.0, 1.0);  // LAR nominal
```

- [ ] **Step 4: Verify `sds.lar` compiles at the full window and the analysis steady state holds**

```bash
echo 'import("/Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src/seam.lib"); process = sds.lar(1.0,1.0,1.0,40.0,200.0,50.0,20.0,1.0);' \
  | faust -I /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src - -o /dev/null && echo "LAR COMPILES (full 2048 window)"
```
Expected: `LAR COMPILES (full 2048 window)` in well under a second.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.discipio.lib
git commit -m "$(cat <<'EOF'
feat(discipio): add sds.lar — feedforward mono LAR processor brick

Composes verified bricks only: input fade (spd.line 2000 ms) before the
fan-out, audio branch spd.hip(100) -> drive -> de.delay(tab1), analysis branch
de.delay(tab2) -> larsengain -> spd.line(tsmooth), multiplied at the loop
multiplier then by output. No internal feedback (the Larsen loop is acoustic).
Delay buffers sized for DELMAXMS @ SRMAX; read delay adapts to ma.SR. Compiles
at the full 2048 window.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `dslar.dsp` entry + regenerate SVG + mathdoc

**Files:**
- Create: `seam-ltm/plugins/dslar/doc/dslar.dsp`
- Generated: `seam-ltm/plugins/dslar/doc/dslar-svg/`, `seam-ltm/plugins/dslar/doc/dslar.pdf`

**Interfaces:**
- Consumes: `sds.lar` (Task 4) via `import("seam.lib")`.
- Produces: the compilable per-plugin doc entry consumed by `tools/gen-faust-doc.sh dslar`.

- [ ] **Step 1: Write `dslar.dsp` (thin entry, mono 1-in/1-out, sliders for the seven parameters + gate)**

Write `seam-ltm/plugins/dslar/doc/dslar.dsp`:
```faust
import("seam.lib");
// Canonical DSP: sds.lar in seam.discipio.lib — the feedforward mono LAR
// processor of Di Scipio's LAR.pd (the Larsen loop is acoustic, external).
gate     = checkbox("Power");                             // system on/off, 2000 ms anti-click fade
drive    = hslider("Drive", 1, 1, 4, 0.01);              // audio pre-gain (Pd presets 1/2/4)
target   = hslider("Target", 1, 0, 1, 0.001);            // homeostat reference (Pd: - 1)
steep    = hslider("Steepness", 40, 1, 80, 0.1);         // homeostat exponent (Pd: pow 40)
tsmooth  = hslider("Control smoothing [unit:ms]", 200, 1, 1000, 1);
tab1     = hslider("Loop delay [unit:ms]", 50, 1, 200, 0.1);
tab2     = hslider("Decorrelation [unit:ms]", 20, 1, 200, 0.1);
output   = hslider("Output", 1, 0, 1, 0.001);            // final VCA to host
process  = sds.lar(gate, drive, target, steep, tsmooth, tab1, tab2, output);
```

- [ ] **Step 2: Verify it compiles standalone**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/dslar/doc
faust -I /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src dslar.dsp -o /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 3: Regenerate the block diagram + mathdoc**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
tools/gen-faust-doc.sh dslar
```
Expected: `done dslar: N svg + dslar.pdf` (N ≥ 1). Requires `faust2mathdoc`, `svg2pdf`, `pdflatex` (memory `reference_faust_doc_generation`); if a tool is missing, report which and stop — do not fake the output.

- [ ] **Step 4: Commit**

```bash
git add plugins/dslar/doc/dslar.dsp plugins/dslar/doc/dslar-svg plugins/dslar/doc/dslar.pdf
git commit -m "$(cat <<'EOF'
feat(dslar): dslar.dsp entry (sds.lar) + regenerated SVG and mathdoc

Thin per-plugin doc entry wiring the feedforward LAR processor with sliders
for the seven parameters plus the power gate. Mono 1-in/1-out.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Update the study diary

**Files:**
- Modify: `seam-ltm/plugins/dslar/doc/study/dslar-study.tex` (add a Phase 3 diary section before `\bibliographystyle`)
- Rebuild: `seam-ltm/plugins/dslar/doc/study/dslar-study.pdf`

**Interfaces:**
- Produces: the Italian study diary records the change of direction and the choices, ready to share with Agostino Di Scipio.

- [ ] **Step 1: Add a Phase 3 section to the diary (Italian; one sentence per line)**

Insert before `\bibliographystyle{plain}` in `dslar-study.tex`:
```latex
\section{Fase 3 — completamento della specifica Faust}\label{sec:fase3}
La lettura connessione-per-connessione di \texttt{LAR.pd} corregge l'inquadramento dell'anello.
Il patch non ha retroazione interna: l'unica sorgente è \texttt{adc\textasciitilde{}}, e \texttt{r\textasciitilde{} audioLAR} raggiunge solo l'oscilloscopio.
La linea \texttt{tab1} di \SI{50}{\milli\second} è quindi un ritardo \emph{feedforward}, non una ricorsione: l'anello di Larsen è acustico (\texttt{dac\textasciitilde{}} $\to$ stanza $\to$ microfono $\to$ \texttt{adc\textasciitilde{}}), esterno al plugin, l'\emph{oikos} che alimenta il sistema.
Di conseguenza \texttt{dslar} è una catena mono feedforward pura, senza \texttt{\textasciitilde} di retroazione.

Il clone \texttt{spd.line} riproduce fedelmente il \texttt{line} control-rate di Pure Data.
La rampa lineare segue la formula di \texttt{line\_tick} ($v = \mathit{setval} + \min(\mathit{elapsed}/R,1)\,(\mathit{target}-\mathit{setval})$, con ripartenza dal valore corrente) ed è campionata al passo di grain di \SI{20}{\milli\second} in una scala control-rate.
La verifica contro un oracolo Python della formula Pd dà scarto massimo dell'ordine di \num{1e-8}, sia sul gradino sia sul cambio di target a metà rampa.
La differenza di fase dei tick (Pd allinea al messaggio, Faust al campione zero) è il residuo documentato, rinviato alla validazione block-kernel.

Ogni oggetto Pd replicato porta ora una doppia fonte: il sorgente C e l'help patch di Miller Puckette.
Gli help patch sono copiati nel repository sotto \texttt{doc/references/pd-help} per non dipendere dal checkout esterno di Pure Data.
La libreria \texttt{seam.discipio.lib} è stata rimessa in \texttt{src} e registrata in \texttt{seam.lib}: è in cura ed è il momento giusto per riportarla al suo posto.
Il mattone \texttt{sds.lar} assembla i pezzi verificati nel processore feedforward, e \texttt{dslar.dsp} lo espone con i sette parametri più il gate.

Questa documentazione è preparata per essere condivisa con Agostino Di Scipio, a validazione dell'intero processo di porting.
```

- [ ] **Step 2: Rebuild the study PDF**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/dslar/doc/study
make
```
Expected: `dslar-study.pdf` rebuilt with the new section (check the page count grew).

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/doc/study/dslar-study.tex plugins/dslar/doc/study/dslar-study.pdf
git commit -m "$(cat <<'EOF'
docs(dslar): Phase 3 study diary — feedforward finding, spd.line, promotion

Records the change of direction (LAR loop is acoustic, tab1 is feedforward),
the faithful spd.line clone and its verification, the dual-source doc
convention, and the promotion of seam.discipio.lib. Prepared to share with
Agostino Di Scipio to validate the porting process.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-review notes (already reconciled)

- **Spec coverage:** D1 → Task 2; D2 → Task 3 note; D3 → Task 4; D4 → Task 5; D5 → Task 3; D6 → Task 6; promotion decision (mid-brainstorm) → Task 1.
- **Verified before planning:** the `spd.line` code (Task 2 Step 4), the full-window assembly, and the `de.delay` constant-buffer pattern were prototyped and compiled/checked in the scratchpad; the numbers in the expected-output blocks are measured, not guessed.
- **Type consistency:** `sds.lar(gate, drive, ref, k, tsmooth, tab1, tab2, output, x)` — same argument order in Task 4 (definition), Task 5 (`dslar.dsp` call), and the spec. `spd.line(ms, x)` consistent across Task 2, Task 4, and `dslar.dsp`.
- **Carry-forward open questions:** final `sds`/`spd` names vs AE2 `fc2003dsaae2` usage; whether the decorrelation delay graduates to its own `sds` brick; the 2007→2006 reference PDF filename.
