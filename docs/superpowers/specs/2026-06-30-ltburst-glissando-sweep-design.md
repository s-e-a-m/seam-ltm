# ltburst Phase 2b — Glissando Tone-Burst (sweep + grain timing) — design

Date: 2026-06-30
Status: approved (brainstorming) — implementation plan next
Parent: docs/superpowers/specs/2026-06-19-ltburst-phase2-faust-spec-design.md
Project spec: docs/superpowers/specs/2026-06-18-ltburst-linkwitz-tone-burst-design.md

## Summary

Phase 2 specifies the **fixed-frequency** Linkwitz shaped tone-burst as the Faust
function `slw.shapedburst` in `seam.linkwitz.lib`.
This extension (Phase 2b) generalises the generator to a **glissando of tone
bursts**: the carrier frequency follows an external sweep (exponential or linear,
`f0 → f1`), and each burst latches a single frequency for its whole duration.
The result is the calibration signal Linkwitz uses to sweep a loudspeaker's
response one narrow band at a time.

Two grain-timing modes are added.
The Faust deliverable stays **DSP only**: no transport, no loop, no wait, no
manual trigger, no per-source routing.
Those belong to the C++ plugin (Phase 3).

## Scope (settled in brainstorming)

- The Faust library exposes three composable units: a **sweep** map, a
  **glissando-burst** generator, and a pure **composition** helper for testing.
- The sweep receives its time base as an external progress signal `p ∈ [0,1]`.
  The plugin owns one-shot-vs-loop, the wait between cycles, the manual click,
  and routing to one stone at a time.
- Per-grain frequency is **held** (sample-and-hold at the burst onset), so every
  burst stays a single-frequency, constant-Q tone burst (a swept carrier inside
  a grain would turn each burst into a chirp and break constant-Q).
- `ondemand` (experimental Faust primitive) is adopted as a **second, documented
  formulation**, alongside the canonical one that compiles on stable Faust.

## Architecture (Approach A — retriggered grain)

Each burst is an independent unit retriggered at its onset.
A grain ramp resets at each onset; a sample-and-hold latches the sweep frequency
at that instant; the carrier and the Hann window are both derived from the same
reset ramp running at the latched frequency.

Resetting the phase to zero at onset gives the zero-crossing start for free:
`sin(0)=0` and the Hann window is zero at `u=0`, so each burst begins cleanly.
This replaces the Phase 2 "the oscillator never stops" technique, which only
held for a fixed repetition frequency; a glissando changes frequency per grain
and therefore must retrigger.

### Equations

For a grain with latched frequency `fg` and N burst cycles:

- grain period in seconds `Tg`:
  - mode **passo** (onset-fixed): `Tg = max(delta, N/fg)`
  - mode **gap** (gap-fixed):    `Tg = N/fg + delta`
- grain ramp `phase ∈ [0,1)` advances by `inc = 1/(Tg·SR)` per sample and wraps;
  the wrap instant is the **onset**.
- `fg = sampleAndHold(onset, fsig)` where `fsig` is the continuous sweep signal.
- seconds since onset `tau = phase·Tg`; cycles since onset `u = fg·tau`.
- carrier `sin(2π·u)`; window `(u < N)·(½ − ½cos(2π·u/N))`.

The grain ramp, the held frequency and the period form one recursive block: `inc`
depends on `Tg`, `Tg` on `fg`, `fg` on `onset`, `onset` on the ramp.
The loop closes through the ramp's one-sample feedback delay.
This recursive engine is the single compile-time gate (as the Phase 2 spec
already gates `os.phasor` signatures).

### Timing modes

- **passo** (onset-fixed): a grain starts every `delta` seconds measured
  onset→onset; the silent gap `delta − N/fg` shrinks as frequency drops.
  `Tg = max(delta, N/fg)` degrades gracefully to back-to-back bursts when `delta`
  is shorter than the burst (a 20 Hz, N=5 burst lasts 250 ms), avoiding overlap.
- **gap** (gap-fixed): `delta` seconds of silence measured end→onset; the
  onset→onset period `N/fg + delta` grows as frequency drops.
  The gap is non-negative by construction, so this mode is always safe.

Both modes express `delta` in seconds; they differ only in the reference point.

## Library surface (`seam.linkwitz.lib`, prefix `slw`)

```faust
// sweep: progress p in [0,1] -> frequency  (smode 0 = linear, 1 = exponential)
sweepfreq(f0,f1,smode,p) = select2(smode, f0 + (f1-f0)*p, f0*pow(f1/f0, p));

// glissando burst: fsig = frequency signal (Hz), dmode 0 = passo, 1 = gap
glissburst(N,delta,dmode,fsig) = ... ;            // canonical, stable primitives
glissburst5(delta,dmode,fsig)  = glissburst(5,delta,dmode,fsig);

// pure composition for testing (p supplied externally, no transport)
linkwitzglide(f0,f1,smode,N,delta,dmode,p) =
    glissburst(N,delta,dmode, sweepfreq(f0,f1,smode,p));
```

The Phase 2 functions `shapedburst` / `shapedburst5` stay unchanged; the
glissando family generalises them (a constant `fsig` in gap mode reproduces the
fixed repeated burst).

### Parameters

- `f0`, `f1` — sweep endpoints in Hz; for stone calibration `f0 = 20000`
  (acuto, start), `f1 = 20` (grave, end); either direction is valid.
- `smode` — sweep shape: 0 linear, 1 exponential (equal octaves per unit `p`).
- `N` — burst cycle count; canonical 5 via the `glissburst5` wrapper.
- `delta` — grain spacing in seconds.
- `dmode` — timing mode: 0 passo (onset-fixed), 1 gap (gap-fixed).
- `p` — progress in `[0,1]`, the external time base.

## The two formulations (both to validate in FaustIDE)

### A. Canonical (stable Faust, mergeable into faust-libraries)

Status: verified — compiles on stable Faust 2.85.5; a sample-accurate simulation
of the recurrence produces no NaN/Inf and latches the swept frequency correctly
(20000 → 11908 → 7088 → … → 100 → 54 Hz in gap mode).

The grain engine uses a two-signal feedback (`loop ~ (_,_)`) carrying the ramp
phase and the held frequency.
Two guards make it safe at startup: `max(20.0, pfg)` floors the frequency in the
period division (`N/pfg` would otherwise diverge while the held frequency is
still zero), and a one-sample start pulse `1 - 1'` forces the first onset so the
sweep is latched immediately instead of after a stale first period.

```faust
import("stdfaust.lib");

sweepfreq(f0,f1,smode,p) = select2(smode, f0 + (f1-f0)*p, f0*pow(f1/f0, p));

glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u) * win
with {
    // recursive grain engine: ramp phase + held frequency fg
    phase = grain : _,!;
    fg    = grain : !,_;
    grain = loop ~ (_,_)
    with {
        loop(pphase,pfg) = nphase, nfg
        with {
            den    = max(20.0, pfg);            // guard against div-by-zero on init
            Tg     = select2(dmode, max(delta, N/den), N/den + delta);
            inc    = 1.0/max(1.0, Tg*ma.SR);
            adv    = pphase + inc;
            start  = 1 - 1';                    // 1 only at the first sample
            onset  = (adv >= 1.0) | start;      // force a latch at startup
            nphase = adv - floor(adv);
            nfg    = select2(onset, pfg, fsig); // hold; latch fsig at onset
        };
    };
    den = max(20.0, fg);
    Tg  = select2(dmode, max(delta, N/den), N/den + delta);
    u   = fg * phase * Tg;
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
};
glissburst5(delta,dmode,fsig) = glissburst(5,delta,dmode,fsig);

linkwitzglide(f0,f1,smode,N,delta,dmode,p) =
    glissburst(N,delta,dmode, sweepfreq(f0,f1,smode,p));

// test: exp sweep 20000 -> 20, N=5, 0.3 s gap, looping progress every 4 s
process = linkwitzglide(20000, 20, 1, 5, 0.3, 1, os.phasor(1, 1/4));
```

### B. Grain-domain (`ondemand`, Faust dev branch)

Status: syntax fixed and de-risked — everything except the `ondemand` token
compiles on stable Faust (projections, the 1→2 `decide` function, the ramp, the
start pulse). The `ondemand` line itself awaits a dev-branch binary to compile.
The original error came from binding two names with a tuple (`fg, Tg = ...`),
which Faust rejects; the fix projects the two outputs of `decided` separately.

`ondemand(circuit)` runs its circuit only while the clock signal is non-zero and
holds the outputs otherwise.
Clocked with the one-sample onset pulse, it computes the per-grain decisions
(frequency and period) exactly once per grain and holds them — the explicit
"once per grain" statement that the C++ port writes as `if (onset) { ... }`.

```faust
import("stdfaust.lib");

glissburst_od(N,delta,dmode,fsig) = sin(2*ma.PI*u) * win
with {
    phase    = (+(inc) : frac1) ~ _;
    frac1(x) = x - floor(x);
    onset    = (phase < phase') | (1 - 1');     // ramp wrap, plus a forced start latch
    decide(f) = f, select2(dmode, max(delta, N/f), N/f + delta);
    decided  = (onset, fsig) : ondemand(decide); // per-grain: compute once, hold between
    fg       = decided : _,!;
    Tg       = decided : !,_;
    inc      = 1.0/max(1.0, Tg*ma.SR);
    u        = fg * phase * Tg;
    win      = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
};

process = glissburst_od(5, 0.3, 1, 20000*pow(20/20000.0, os.phasor(1,1/4)));
```

The forced start latch `(1 - 1')` handles initialisation in both forms: it fires
the first onset at sample 0 so `fg` and `Tg` take valid values immediately,
instead of dividing by a held-zero frequency.

## Study diary update (Section 5, Italian)

Fill Section 5 "Ricostruzione in Faust" of
`plugins/ltburst/doc/study/ltburst-study.tex`, one sentence per line:

- map Linkwitz §2 (raised cosine, N=5, constant-Q) to §4 (100% AM at f₀/5 + dwell);
- present the glissando as the generalisation: an external sweep feeds a
  per-grain sample-and-hold, so each burst stays single-frequency;
- explain the two timing modes (passo / gap) and the overlap guard;
- show the canonical `glissburst` listing with commentary;
- present the `ondemand` grain-domain formulation as the pedagogical centrepiece,
  noting it expresses "decided once per grain" and maps to the C++ `if (onset)`;
- defer validation/comparison (spectra, staircase) to the C++ phase.

No plots in this phase.
Section 5 cites `seam.linkwitz.lib` as the spec source.

## Cross-repo logistics

| Repo | Change | Branch |
|---|---|---|
| `faust-libraries` | `glissburst` family + `sweepfreq` in `seam.linkwitz.lib` | `feat/linkwitz-shaped-tone-burst` |
| `seam-ltm` | study diary Section 5 (+ rebuilt PDF), this spec | `feat/ltburst-linkwitz-tone-burst` |

`seam.linkwitz.lib` is created in Phase 2 and extended here, on the same
`faust-libraries` branch.

## Collaboration / verification workflow

The two formulations split cleanly by who can run them:

- **A (stable Faust):** Claude owns the loop. Stable Faust 2.85.5 is installed at
  `/usr/local/bin/faust`, and runtime behaviour (NaN, onset timing, latched
  frequencies) is checked with a sample-accurate Python simulation of the
  recurrence. No round-trip needed.
- **B (`ondemand`, dev branch):** needs a dev-branch Faust binary, which is not
  built yet. Two ways to close this loop:
  1. Giuseppe builds a dev `faust` from `/Users/giuseppe/Documents/github/faust`
     and shares the binary path; Claude then compiles and tests B locally, same
     as A.
  2. Otherwise Giuseppe pastes B into FaustIDE (dev branch) and reports the exact
     error line; Claude fixes against the dev grammar and the `tests/impulse-tests/od`
     examples.

Reference material available to Claude: the SEAM `faust-libraries` (for `seam.lib`
integration) and the Faust dev source tree (grammar + `od` test corpus).

## Verification (gates)

- canonical functions compile on **stable** Faust (Homebrew) — confirmed:
  `echo 'import("seam.lib"); process = slw.linkwitzglide(20000,20,1,5,0.3,1, os.phasor(1,1/4));' | faust -`
- the `glissburst` inline test compiles when uncommented — confirmed.
- runtime: no NaN/Inf and correct frequency latching — confirmed by simulation.
- the `ondemand` formulation compiles on the **dev** Faust at
  `/Users/giuseppe/Documents/github/faust` — pending a dev binary.
- the study diary rebuilds (`make` in `doc/study`).

Giuseppe validates both formulations interactively in FaustIDE (the dev branch is
checked out locally), which confirms the recursive engine and the `ondemand`
wiring beyond a pure compile check.

## Out of scope (now)

- Transport: one-shot vs loop, wait between cycles, manual click, per-stone
  routing — all Phase 3 (C++ plugin).
- Validation, plots, spectral comparison, the staircase variant — Phase 3 with
  the test toolkit (issue #6).
- The reference-microphone inverse-EQ calibration ("fase due") — a later project,
  revisited once the tone-burst generator and plugin are proven.

# in libreria rimane valida l'idea di consolidare anche la versione statica semplice?

```faust
import("stdfaust.lib");
 // raised-cosine N-cycle burst, repeated every P=N+M carrier cycles, cycle-synchronous
freq = hslider("freq", 1000,10,20000,1);
delta = hslider("delta", 0.3, 0.1, 1, 0.01);
shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win
with {
  M = max(1, int(ceil(dwell*f0)));              // dwell quantizzato a cicli interi
  P = N + M;                                    // cicli totali per periodo
  c = os.phasor(1, f0/P);                       // 0..1 su P cicli di carrier
  u = P*c;                                      // posizione in cicli: 0..P
  win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); // Hann sui primi N cicli, poi 0
};
shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);   // wrapper canonico N=5
process = shapedburst5(freq, delta);
```

# per aiutarmi a comprendere cosa fai ed essere utile nel debuggare faust

```faust
// cosa sono le variabili di funzione?
sweepfreq(f0,f1,smode,p) = select2(smode, f0 + (f1-f0)*p, f0*pow(f1/f0, p));
//process = sweepfreq(20000,20,1,5);
```

avrei bisogno di codice commentato con le variabili dichiarate e un esempio funzionante anche se commentato. 

