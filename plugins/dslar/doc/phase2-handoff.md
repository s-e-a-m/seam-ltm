# dslar — Phase 2 handoff log

Date: 2026-07-10
Branch: `dslar` (unmerged, 13+ commits from `main`)
Status: **Phase 1 complete** (documented analysis, merge-ready). This log hands off to a fresh implementation session for Phase 2.

## What Phase 1 produced

- Design spec (program charter + `dslar`): `docs/superpowers/specs/2026-07-09-discipio-porting-dslar-design.md`
- Phase-1 plan: `docs/superpowers/plans/2026-07-09-dslar-phase1-analysis.md`
- Study (analysis, Italian, 9 pp): `plugins/dslar/doc/study/dslar-study.tex` (+ built PDF). Read this first — it is the grounded analysis of `LAR.pd`.
- Source patch + Di Scipio references: `plugins/dslar/doc/references/`

`LAR.pd` = the self-regulating Larsen loop of Di Scipio's *Modes of Interference* (LAC 2006). Verdict: **mono 1-in / 1-out**.

## Architecture (decided)

Layered libraries, mirroring how `seam.cyclone.lib` (`scy`) clones Max/gen~ objects:

- **`seam.pdclone.lib`** (proposed prefix `spd`) — faithful clones of Pure Data objects, verified against the Pd source.
- **`seam.discipio.lib`** (`sds`) — Di Scipio's idioms; **composes** the pdclone objects, does not re-implement Pd primitives. Currently WIP in `faust-libraries/temp/seam.discipio.lib`, commented out at `seam.lib:25`.
- Plugin: `dslar` (family `ds*`), in `plugins/dslar/`, following the seam-ltm convention (Faust = spec, hand-written C++, finished GUI).

Memories: `project_pdclone_library_architecture`, `project_env_upstream_grame`.

## Verified source specs — ready to translate to Faust

Pd source lives in the external checkout `/Users/giuseppe/Documents/github/pure-data/src` (memory `reference_external_source_checkouts`). Each object below is verified; the formula is what the Faust clone must reproduce.

### `pdclone.env` (`env~`, `src/d_ctl.c` `sigenv`)
Hann-weighted mean-square over `N` samples → `powtodb`:
- window `buf[i] = (1 − cos(2π i / N)) / N` (normalized Hann, sums to 1),
- output = `powtodb(Σ buf[i]·x[i]²)`.
- `powtodb(p) = 100 + 10·log10(p)` (clip ≥0), `dbtorms(d) = 10^((d−100)/20)` — both in `src/x_acoustics.c`.
- **In the LAR chain** `env~ → dbtorms` collapses to `r = sqrt(Hann-weighted mean-square) = RMS`. A faithful *object* clone still needs the dB form (`pdclone.env` emits dB; `pdclone.dbtorms` inverts).
- SR rule: express the window in time, `N = ba.sec2samp(2048/44100)` (memory `project_discipio_sr_independence`). `env~`'s `N` is samples in Pd.
- Faust `an.*` followers do NOT match (rectangular/one-pole windows, 0 dB reference) — this is new code. Candidate to offer upstream to GRAME (memory `project_env_upstream_grame`).

### `pdclone.hip` (`hip~`, `src/d_filter.c` `sighip`)
1-pole/1-zero high-pass, realizable with minimal Faust elements:
```faust
pdclone.hip(f) = fi.pole(coef) : fi.zero(1) : *((1 + coef) / 2)
with { coef = max(0, min(1, 1 - 2*ma.PI*f/ma.SR)); };
```
(`fi.pole(p) = +~*(p)` gives `w = x + coef·w₋₁`; `fi.zero(1)` gives `w − w₋₁`; `*((1+coef)/2)` is Pd's `normal`.) DIFFERS from `fi.highpass` (Butterworth/biquad) — must be this clone for faithful behavior.

### `pdclone.dbtorms` / `pdclone.powtodb` (`src/x_acoustics.c`)
`powtodb(p)=100+10·log10(p)` (clip ≥0, 0 if p≤0); `dbtorms(d)=10^((d−100)/20)` (clip d≤485).

### Still to study
- `line` — `src/x_time.c`. Control-rate ramp; ties to the block-kernel check below.
- `delread~` / `delwrite~` — `src/d_delay.c`. Confirm vs `de.delay`.

## The `sds` homeostatic brick

The control law is an `sds` brick composing `pdclone.env`:
```
g = | r − 1 |^k ,   r = RMS from pdclone.env (= dbtorms∘env in the LAR chain)
```
with reference (the `−1`) and exponent `k` (the `40`) as explicit parameters. Operating equilibrium sits at LOW `r ∈ [0, 0.12]` (see study §"La legge di controllo omeostatica"). Must stay compatible with AE2's existing `sds.*` signatures (`fc2003dsaae2`); note `sds.integrator(s)` there is a DIFFERENT follower (abs rectangular average, seconds).

## Cross-cutting rules (apply to every porting)

1. **SR-independence** (`project_discipio_sr_independence`): sample-relative constants → time anchored at SR_ref=44100. In LAR only `env~ 2048`.
2. **Pd↔Faust block validation** (`project_pd_vs_faust_block_validation`): Pd runs a 64-sample block kernel (control-rate updates, one-block feedback delay); Faust is per-sample. A final Pd↔Faust behavioral A/B is required.
3. **Mono I/O normalization** (`feedback_porting_mono_io_normalization`): 1-in/2-identical-out → mono 1-in/1-out. `dslar` is mono.
4. **Next-prime delays** (`feedback_next_prime_delay_multiinstance`): snap decorrelation delays with `sff.np` / `ddelay`'s `nextPrime` for multi-instance decorrelation.

## Validation plan (two A/B axes)

- 44.1 kHz vs 96 kHz (SR-independence): same envelope / breathing period.
- Pd patch vs Faust build (block kernel): same input, confirm per-sample ≈ 64-sample-block behavior (feedback timing, control-update rate).

## Exposed plugin parameters (from spec)

input drive · target (the `−1`) · steepness (the exponent `40`) · control smoothing (200 ms) · decorrelation (20/50 ms) · output · internal-loop on/off. Plus a live level-vs-control meter/scope in the GUI.

## Open questions for Phase 2

- Exact `pdclone`/`sds` names and signatures, settled against AE2 usage.
- Whether the decorrelation delay ever graduates to `sds` (only when a 2nd patch claims it).
- Study wording to tighten when specifying the `sds` brick: "riferimento di equilibrio" (the constant `1`) vs the low-`r` operating equilibrium; and the `r>1` re-opening edge (`|r−1|` grows again) — currently covered by the "(circa) [0,1]" hedge.
- Editorial: the reference PDF filename says "2007-…-modes-of-interference-3" but the file is LAC 2006 (`LAC06.doc` metadata); the study cites it correctly as 2006. Consider renaming the file.

## Suggested Phase-2 first steps

1. Write `pdclone.env` and `pdclone.hip` in Faust (new `seam.pdclone.lib` in `faust-libraries/`, or start in `temp/`), each with an inline `//process =` test; verify `pdclone.hip` against Pd `hip~` and `pdclone.env` against `env~`.
2. Write the `sds` homeostatic brick composing `pdclone.env`; keep AE2 compatibility.
3. Assemble `plugins/dslar/doc/dslar.dsp` (`process = <lar circuit using sds/pdclone>;`) → run `tools/gen-faust-doc.sh dslar` for SVG + mathdoc.
4. Hand-write the C++ plugin (`plugins/dslar/source/`), GUI, following `ddelay`/`ltburst`; reuse `ddelay`'s `nextPrime` if decorrelation snapping is wanted.
5. Run both validation A/Bs; write `doc/math` (English) + `doc/dslar-validation.md`.

## Build

Study: `cd plugins/dslar/doc/study && make` (latexmk). VST3 SDK: `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` (memory `reference_vst3sdk_location`); pass `-DSEAM_VST3SDK_DIR=...`.
