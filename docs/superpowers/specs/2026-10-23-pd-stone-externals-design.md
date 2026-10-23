# Pd externals for the STONE chain — design

Date: 2026-10-23
Status: draft, awaiting approval (builder check done, answers folded in)

## Intent

On **30 October 2026** Giuseppe performs the timpani excerpt of Agostino Di Scipio's *UPU – Umano Post-Umano* (2024).
In the 2024 ensemble performance the timpani's electronics went out on a conventional stereo pair; this is the first time the piece goes out on **STONE** (the tetrahedral loudspeaker).

The timpani enter Pd from the SSL Big Six (`adc~ 1`) and are processed by Di Scipio's Pd patch, which produces three stereo pairs.
Each pair must be encoded for STONE inside Pd, without a VST host:

```
pair n (L,R) ──► lr2xhgr~ ──► AmbiX (W,Y,Z,X) ──► bamodulex~ ──► 4 tetrahedral feeds ──► dac~ ──► Big Six
```

Three pairs → three groups of four feeds → computer outputs 1–4, 5–8, 9–12 → Big Six.
On the Big Six Giuseppe balances the three groups **by hand on the faders**, then sends them to the STONE amplifier through the stereo cues 1-2 and 3-4.
If twelve returns turn out to be too many, he will sum some groups in Pd (e.g. down to 4+4); that is a patch-level decision, not an external's.

Why separate groups all the way to the mixer: live balance of the three pairs during the performance.

## Goals

1. **`lr2xhgr~`** and **`bamodulex~`** as Pd externals, stable enough for a concert, producing the same samples as the VST3 plugins' DSP for the same parameters.
2. **`m2xhgr~`** as an external too: it is the building block of `lr2xhgr` (Faust reference: `lr2xhgr = par(i, 2, m2xhgr(g1,g2,g3)) : rotateYPR(...)`), so its DSP is extracted first and `lr2xhgr` reuses it.
3. **Help patches** that double as rehearsal tools (see below).
4. **A concert copy** of Di Scipio's patch wired to the STONE chain, with the original untouched.
5. Build logs and notes that feed the porting documentation, consistent with the suite's "Learning Through Making" purpose.

## Non-goals (for now)

- `abmodulex~` (A-format microphone encoder): not used in this piece; after the concert.
- Pd64 (double-precision Pd) builds: the DSP already computes in `double` internally; 32-bit Pd loses precision only at the I/O boundary, which is irrelevant here.
- Distribution to others (deken, students, Di Scipio): only Giuseppe's Mac for now.
- Fixing the Haar sample-rate dependence (replacing the 1-sample QMF with Linkwitz-Riley crossovers in Hz): a separate, post-concert project.
- Pd's multichannel signals: separate signal inlets/outlets read better in a teaching patch and work everywhere.
- Live parameter control (MIDI, automation): parameters are fixed in rehearsal.

## Anti-goals

- **Any sonic difference from the VST3 DSP** at steady state. The externals are a new wrapper around the same headers, not a re-port.
- **Any change to Di Scipio's original patch** in `~/Downloads/UPU/TIMPnew.pd/`.
  "Original" means the folder **as it is on 23 October 2026**: `TIMP.pd`, `processTR2.pd` and `manualCtlTR2.pd` carry edits Giuseppe made that day in rehearsal with Di Scipio, and they are part of the concert version.
- **Putting Di Scipio's patch (or the concert copy) in the public GPL `seam-ltm` repository.** It is his work; only the externals and their help patches go in the repo.
- An external that fails silently: wrong sample rate or missing channels must be visible in the Pd console.

## Constraints

- **Sample rate: 48 kHz.** Di Scipio's patch runs at 48 kHz, and 48 kHz is where the Haar decomposition was implicitly tuned (splits ≈ fs/4 = 12 kHz and ≈ fs/8 = 6 kHz).
  At other rates the frequency→spatial-component mapping shifts (see the Haar SR-dependence note).
  The externals post a warning to the Pd console when the DSP starts at a rate other than 48 000 Hz; they do not correct anything.
- **Pd 0.56-2**, installed at `/Applications/Pd-0.56-2.app`; vanilla 32-bit float.
- **Di Scipio's patch** (inspected): vanilla objects only, no externals, no `[declare]`, no `block~` (default 64-sample block).
  Input: `adc~ 1` (two places). Outputs: eight `dac~` objects in `processTR2.pd`, two on `5 6`, four on `7 8`, two on `9 10`; Pd sums them.
- **Hand-written C++, Faust is the spec** (`CLAUDE.md`). The externals include the same headers as the plugins; no `faust2puredata` code ships.
- **One DSP, two wrappers.** Plugin DSP lives in `plugins/<name>/source/<name>_dsp.h` and `plugins/_common/seam_*.h`; the Pd externals include those headers and add nothing to the arithmetic.
- **Loudspeaker: `stone`** (the small concert loudspeaker of the STONE family, 4 × 5", sealed), mounted **upright**: driver order LFU, RFD, RBU, LBD, the same as the VST `bamodulex`. No dual-tetrahedron option is needed for this concert.
- **Only `TIMPnew.pd` runs.** `simpSamp.pd` and `simpSampNEW.pd` in `~/Downloads/UPU/` also write to `dac~ 9 10` but are not part of the concert; they must not be open alongside `stone-out.pd`.
- **Deadline: ready for rehearsal well before 30 October**, which leaves room for at least one session with the real Big Six and STONE.

## The DSP to reuse (current state)

| Module | Plugin DSP today | I/O | Parameters |
|---|---|---|---|
| m2xhgr | **inline** in `m2xhgr_processor.cpp::processHaarMono` — `haarDecompose` (`seam_haar.h`, double state) then `gainRotateYPR` (`seam_rotation.h`) | 1 → 4 (AmbiX ACN: W,Y,Z,X) | yaw, pitch, roll (−180…180°, default 0); trims A1, A2, A3 (−12…+12 dB, default 0) |
| lr2xhgr | Haar inline in the processor (two `HaarState`s), mix in `lr2xhgr_dsp.h::mix` | 2 → 4 | divergence (0…360°, default 90°, internally halved: L +div/2, R −div/2); yaw, pitch, roll; A1–A3 shared by both banks |
| bamodulex | **inline** in `bamodulex_processor.cpp::processMatrix` — fixed orthogonal matrix, ×½ | 4 (W,Y,Z,X) → 4 (LFU, RFD, RBU, LBD) | none |

Trims are converted with `seam::meter::db2lin` (`seam_meter.h`).
Only the Haar stage computes in `double`; the trims and rotations run in the host's sample type (`float` in a 32-bit host). The extracted headers therefore stay templated on the sample type, so the refactor is bit-identical; the external picks the type for that stage, and the equivalence tolerance below accepts either.

### Refactor in the plugin tree (precedes the externals)

- **`m2xhgr_dsp.h`**: a small stateful struct owning one `HaarState<double>` with a `tick(x, g1,g2,g3, yaw,pitch,roll, out[4])` (exact shape left to the builder), reproducing `processHaarMono` per sample, including the double-internal Haar and the cast back to the sample type.
  `m2xhgr_processor.cpp` calls it; `lr2xhgr_processor.cpp` replaces its two inline Haar banks with what `m2xhgr_dsp.h` offers, keeping `lr2xhgr::mix` as is.
- **`bamodulex_dsp.h`**: the matrix as an inline function; `bamodulex_processor.cpp` calls it.
- The VST3 plugins must be **bit-identical before and after** the refactor; this is what the existing tests plus new `m2xhgr_dsp_test` / `bamodulex_dsp_test` guard.

## The externals

Location: **`pd/`** at the repo root, parallel to `plugins/`.

```
pd/
├── Makefile
├── README.md                # what each external is, how to build and install
├── m2xhgr~.cpp   m2xhgr~-help.pd
├── lr2xhgr~.cpp  lr2xhgr~-help.pd
└── bamodulex~.cpp bamodulex~-help.pd
```

Each `.cpp` is a thin C++ Pd external: `extern "C"` setup function, `t_object` struct holding the DSP state from the plugin headers, a `dsp` method (reads `sp[0]->s_sr`, warns if ≠ 48000, resets state) and a perform routine converting `t_sample` (float) ↔ `double` at the boundary.

### Interfaces

**`[m2xhgr~ yaw pitch roll a1 a2 a3]`** — 1 signal inlet, 4 signal outlets (W, Y, Z, X).
**`[lr2xhgr~ divergence yaw pitch roll a1 a2 a3]`** — 2 signal inlets (L, R), 4 signal outlets (W, Y, Z, X).
**`[bamodulex~]`** — 4 signal inlets (W, Y, Z, X), 4 signal outlets (LFU, RFD, RBU, LBD).

- **Creation arguments** carry the values found in rehearsal and are saved with the patch. Missing arguments take the VST defaults. Units are the VST's: degrees and dB.
- **Messages** to the left inlet for rehearsal: `divergence <deg>`, `yaw <deg>`, `pitch <deg>`, `roll <deg>`, `a1 <dB>`, `a2 <dB>`, `a3 <dB>`. Out-of-range values are clipped to the VST ranges.
- **`print`** message: posts to the Pd console the object line ready to paste, built from the object's **current internal state** (not from GUI widgets), e.g. `lr2xhgr~ 35 0 0 0 -2 0 0`.
- **Parameter changes are smoothed** with a short linear ramp so that rehearsal tweaks don't click.
  This is a deliberate departure from the VST3 plugins, which apply parameters per block without a ramp; at steady state the output is identical.
  *Builder's call:* ramp length (tens of ms) and which helper to use (`seam_ramp.h` if it fits).

### Help patches = rehearsal tools

- **`lr2xhgr~-help.pd`**: stereo test input (Pd's `adc~` and a switchable test signal), one slider + number box per parameter (7) feeding the messages, a `[print(` button, **level meters on the 4 outputs**, short text on the parameters and the 48 kHz constraint.
- **`m2xhgr~-help.pd`**: same, 6 parameters (no divergence), mono input.
- **`bamodulex~-help.pd`**: 4 inputs, 4 outputs with meters, the matrix and the driver order (LFU, RFD, RBU, LBD) written out, an example chain `lr2xhgr~ → bamodulex~`.

### Build and install

- `make` builds fat (x86_64 + arm64) `.d_fat` binaries against the headers of the installed Pd (`/Applications/Pd-0.56-2.app/Contents/Resources/src`), overridable with `PDINCLUDEDIR=`.
  No VST3 SDK, no download, no external build system.
- `make install` copies binaries and help patches into **`~/Library/Pd/seam-ltm/`**, and patches load them with `[declare -stdpath seam-ltm]`.
  *Amended during implementation:* the draft said `~/Documents/Pd/externals/seam-ltm/`, but `-stdpath` only searches Pd's static paths (`~/Library/Pd`, `/Library/Pd`, the app's `extra`), never the preference-set search path, so the externals would not load there; `~/Library/Pd` also keeps the concert patch independent of the preferences.
- *Builder's call:* whether the DSP-equivalence tests run from `pd/` or join the CMake test suite in `tests/` (the latter is the suite's convention).

## The concert patch (outside the repo)

In `~/Downloads/UPU/`, next to the original:

- **`TIMPnew-stone/`** — a copy of `TIMPnew.pd/` in which the eight `dac~` in `processTR2.pd` are replaced:
  `dac~ 5 6` → `throw~ upu1L` + `throw~ upu1R`; `dac~ 7 8` → `upu2L/R`; `dac~ 9 10` → `upu3L/R`.
  Several `throw~` into one `catch~` are summed, exactly as several `dac~` on the same channels are, so the patch's sound is unchanged.
  Nothing else in the copy changes.
- **`stone-out.pd`** — `[declare -stdpath seam-ltm]`; three `catch~` pairs → three `lr2xhgr~` → three `bamodulex~` → `dac~ 1 2 3 4`, `dac~ 5 6 7 8`, `dac~ 9 10 11 12`.
  Within every group the outputs keep the same order (LFU, RFD, RBU, LBD on the group's first to fourth channel), so that each driver of the stone receives the same feed from all three groups once the Big Six cues are patched. Which cue channel reaches which driver is settled at the Big Six in rehearsal.
  The `dac~` channel numbers are visible and easy to change in rehearsal; the `lr2xhgr~` arguments are where the rehearsal values go.
  Opened together with (or included as an abstraction in) the copy of `TIMP.pd`. *Builder's call:* which of the two.

## Verification

1. **Refactor**: VST DSP tests green; new `m2xhgr_dsp_test` / `bamodulex_dsp_test` compare against the pre-refactor inline code on noise and impulses.
2. **Externals vs plugin DSP**: for each external, feed the same input and parameters through the external's perform routine and through the plugin DSP header; outputs equal to float precision at steady state. Check by mutation (break the wrapper, confirm the test fails).
3. **In Pd**: the help patches open without errors in Pd 0.56-2, meters move, `print` emits a line that recreates the same state when pasted, the 48 kHz warning appears at 44.1 kHz.
4. **Concert patch**: with the externals bypassed, `TIMPnew-stone` + a stereo sum produces the same signal as the original on 5-6 / 7-8 / 9-10 (null test, or at least by ear). Then the full chain at the Big Six with STONE.

## Order of work

1. Extract `m2xhgr_dsp.h` and `bamodulex_dsp.h`; VST3 bit-identical.
2. `pd/` skeleton + Makefile + `m2xhgr~` with help patch (sets the pattern).
3. `lr2xhgr~` with help patch (reuses m2xhgr's DSP).
4. `bamodulex~` with help patch.
5. `make install`, `TIMPnew-stone/` and `stone-out.pd` in `~/Downloads/UPU/`.
6. Rehearsal with the Big Six and STONE.
