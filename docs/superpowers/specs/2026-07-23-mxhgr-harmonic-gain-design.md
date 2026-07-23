# m2xhgr / lr2xhgr — Harmonic Gain Trims — Design

Status: approved (brainstorming, 2026-07-23).
Scope: one spec, two plugins — `m2xhgr` (base) and `lr2xhgr` (inherits).

## Purpose

Both plugins carry a **G** in their name — M(ono) 2 X(AmbiX) H(aar) **G**(ain)
R(otation) — that neither window has ever grown.
The G is a per-channel gain trim on the first-order Ambisonic components
`A1`, `A2`, `A3`, with the omnidirectional `A0` deliberately excluded.
Holding `A0` fixed makes the three trims read as a **ratio** rather than as
three more volume controls: scaling the first-order components against the
omni one changes the *size* of the encoded image — small trims concentrate the
source toward a point, large ones open it out across the soundfield.
That is a musical decision about the encoding, which is why the controls
belong in the plugin window next to the rotation and not in a calibration
path.

Source notes: [`plugins/m2xhgr/doc/roadmap.md`](../../../plugins/m2xhgr/doc/roadmap.md),
[`plugins/lr2xhgr/doc/roadmap.md`](../../../plugins/lr2xhgr/doc/roadmap.md),
both recorded at the host check of 2026-07-22.

## Why one spec for two plugins

In `seam.ambisonics.lib` (prefix `sam`), `lr2xhgr` is **defined in terms of**
`m2xhgr`:

```faust
m2xhgr = sdw.haarmn(1);
lr2xhgr(divergence, yaw, pitch, roll) = par(i, 2, m2xhgr) : ... ;
```

Adding the gain stage to the mono definition carries it into the stereo one
for free; the stereo "sharing rule" (one control drives both banks) becomes
nothing more than *how `lr2xhgr` forwards its arguments*.
Splitting the work across two specs would re-establish the same Faust context
twice for no gain.
This follows the core convention of the suite: **Faust is the specification,
C++ is the deliverable** (`CLAUDE.md`).

## Section 1 — Faust (`seam.ambisonics.lib`)

A small, composable gain stage — the primitive style the SEAM libraries
prefer — inserted between the Haar bank and its outputs.
Gains are **linear**; the dB→linear conversion lives in the GUI/C++ layer,
per Faust convention (a library function takes linear coefficients).

```faust
// Harmonic gain trim: A0 passes, A1/A2/A3 are scaled. Linear gains.
hgain(g1, g2, g3) = _, *(g1), *(g2), *(g3);

// Mono → Haar → per-harmonic gain. Rotation is applied separately by the
// plugin (as today), so it is not part of this definition.
m2xhgr(g1, g2, g3) = sdw.haarmn(1) : hgain(g1, g2, g3);

// Stereo: the SAME three gains feed both banks — the sharing rule is the
// argument forwarding, nothing more. The summation (:> si.bus(4)) and the
// final yaw rotation are the existing body, unchanged.
lr2xhgr(divergence, yaw, pitch, roll, g1, g2, g3) =
    par(i, 2, m2xhgr(g1, g2, g3)) :
    rotateYPR(divergence,   pitch, roll),
    rotateYPR(0 - divergence, pitch, roll)
    :> si.bus(4) :
    rotateYPR(yaw, 0, 0);
```

**Mechanical consequences to track (not optional):**

- `m2xhgr` changes from a *value* to a *3-argument function*. Every reference
  updates accordingly.
- The inline test comment becomes the identity case:
  `//process = no.noise : m2xhgr(1, 1, 1);`
- `lr2xhgr`'s inline test grows three trailing `1`s:
  `//process = (no.noise, no.noise) : lr2xhgr(ma.PI/2, 0, 0, 0, 1, 1, 1);`
- Academic/structure comments in the file are preserved.

**Verification:** the updated inline tests compile from a bare `faust -`.

## Section 2 — C++ port: `m2xhgr`

### Parameters (`m2xhgr_ids.h`)

Continue the existing block (`kParamYaw = 100 … kParamRoll = 102`):

```cpp
kParamTrimA1 = 103,
kParamTrimA2 = 104,
kParamTrimA3 = 105,
```

Controller: three `RangeParameter`s, range **−12 … +12 dB**, default **0**,
unit `"dB"`. `A0` has no control — it is the reference the other three are
weighed against.

### DSP (`processHaarMono`)

Follows the existing rotation-parameter style exactly: read the last point of
each parameter queue per block, apply as a constant across the block, **no
smoothing** (matching Yaw/Pitch/Roll, which are already per-block constants).

The dB→linear conversion happens **once per block, outside the sample loop**.
The gain is applied to Haar outputs 1/2/3 **before** `rotateYPR`:

```cpp
const SampleType g1 = static_cast<SampleType>(dbToGain(fTrimA1)); // pow(10, dB/20)
const SampleType g2 = static_cast<SampleType>(dbToGain(fTrimA2));
const SampleType g3 = static_cast<SampleType>(dbToGain(fTrimA3));
// ... in the sample loop, after haarDecompose:
a1 *= g1;  a2 *= g2;  a3 *= g3;    // a0 untouched
rotateYPR (yaw, pitch, roll, a0, a1, a2, a3,
           out0[i], out1[i], out2[i], out3[i]);
```

**Gain before rotation is load-bearing.** `rotateYPR` mixes `A1`, `A2`, `A3`
among themselves; a trim placed after it would no longer correspond to the
named component the user thinks they are adjusting. Applied before, "the A1
trim" means the same thing at every rotation angle.

`dbToGain` is a shared dB helper. `seam_meter.h` is the natural home for dB
utilities and already holds `lin2db`-style helpers; the plan checks for an
existing `db2lin`/`dbToGain` there and reuses it, adding one there if absent
rather than defining a plugin-local duplicate.

### State (clean-break, 6 doubles)

Matches the precedent set by `multipink`'s POWER state change. `getState`
writes 6 normalized doubles, `setState` reads 6:

```
saved[0..2] = yaw, pitch, roll     (normalized)
saved[3..5] = trimA1, trimA2, trimA3 (normalized)
```

A session saved with the old 3-double format does not reload state (parameters
fall to defaults). This is acceptable: default trims are 0 dB = linear gain
1.0 = identity, so the audible result of a failed reload is a plugin that
behaves exactly as the old build did, minus any non-default rotation the user
had set — the same trade `multipink` accepted.

## Section 3 — C++ port: `lr2xhgr`

Identical mechanics, plus the **sharing rule**.

### Parameters (`lr2xhgr_ids.h`)

Existing block is `kParamDivergence = 99, kParamYaw = 100 … kParamRoll = 102`.
Add the same three trims:

```cpp
kParamTrimA1 = 103,
kParamTrimA2 = 104,
kParamTrimA3 = 105,
```

Three controls, not six: `lr2xhgr` runs two Haar banks, but one `A1` control
scales the `A1` output of **both** banks, and likewise `A2` and `A3`.

### Why shared, not per-bank

The size of the encoded image is a property of the encoding, not of one side
of it. A trim pair that let left and right differ would tilt the image
sideways — and `lr2xhgr` already has **Divergence** for placing the two
channels against each other. Two mechanisms acting on the same perceptual
quantity would leave the user unable to say which produced the result they
hear. Sharing keeps the meanings separate: Divergence says how far apart the
two encodings sit, the trims say how large both are.

### DSP

One set of linear gains, applied to outputs 1/2/3 of both `HaarState` banks,
before the divergence rotation:

```cpp
a1L *= g1;  a2L *= g2;  a3L *= g3;
a1R *= g1;  a2R *= g2;  a3R *= g3;
```

This mirrors the Faust structure from Section 1 (`par(i, 2, m2xhgr(g1,g2,g3))`)
one-to-one — spec and deliverable say the same thing.

### State (clean-break, 7 doubles)

```
saved[0]    = divergence           (normalized)
saved[1..3] = yaw, pitch, roll      (normalized)
saved[4..6] = trimA1, trimA2, trimA3 (normalized)
```

## Section 4 — The two L windows

Both windows are **S format** today (`m2xhgr` 300×390, `lr2xhgr` 300×458).
Adding a sixth (m2xhgr) / seventh (lr2xhgr) fine control crosses the S→L
threshold of [`doc/style/ui-style.md`](../../../doc/style/ui-style.md): both
become **L** (width ≥ 460, two columns), alongside `dslar` and `ltglide`.

Column sub-labels follow the standard's vocabulary (as `ltglide` uses
`— SWEEP —` / `— TIMING —`):

- **Left column: `— ROTATION —`** — the rotation controls.
- **Right column: `— GAIN —`** — the three trims. `GAIN` is chosen over `SIZE`
  or `HARMONICS`: it is literal and names the **G** the plugin's own name
  already promises.

Individual trims are labelled **`A1` / `A2` / `A3`** (the roadmap's language,
"the A1 trim"), keeping `A0` visibly absent so the three read as a ratio.

```
m2xhgr (L):                          lr2xhgr (L):

        SEAM M2XHGR                          SEAM LR2XHGR
 Mono to AmbiX via Haar Decomposition  Stereo to AmbiX via Haar (Silvi's method)

  — ROTATION —      — GAIN —            — ROTATION —      — GAIN —
    Yaw (Z)           A1                  Divergence        A1
    Pitch (Y)         A2                  Yaw (Z)           A2
    Roll (X)          A3                  Pitch (Y)         A3
                                          Roll (X)
```

Both windows must lint clean under `tools/check-uidesc.py` (title, palette,
`font-color="TextLight"`, zone order). Start each from
`plugins/_template/resource/_template.uidesc` per convention, or grow the
existing window; either way the result is validated by the lint.

## Section 5 — Testing & verification

**Faust:** the updated inline tests (`m2xhgr(1,1,1)`,
`lr2xhgr(ma.PI/2,0,0,0,1,1,1)`) compile from a bare `faust -`.

**C++ unit tests (TDD, verified by mutation):**

- `m2xhgr` — impulse in, rotation at 0, trim A1 = −6 dB (gain 0.5): output
  channel 1 is halved, **channels 0/2/3 unchanged** (A0 never touched).
  Mutation: move the gain to *after* `rotateYPR` → the test must go red
  (rotation mixes A1/A2/A3, so "the A1 trim" stops naming that channel). This
  test is what protects "gain before rotation".
- `lr2xhgr` — the sharing rule: one A1 trim scales the A1 output of **both**
  banks. Mutation: apply to one bank only → red.

Every test is confirmed red-before-green and by deliberate mutation (per the
suite's "verify every test by mutation" practice).

**ctest / lint:** the two new L windows pass `check-uidesc.py`. Note: the
screenshot rule added on 2026-07-22 (`check_screenshots`) will emit a **WARN**
for `m2xhgr` and `lr2xhgr` the moment their windows change, until the shots
are retaken — the guard doing its job, not a regression.

**Build & host:** build with the Xcode generator
(`cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=…`, per the UI-standard plan),
load both in Reaper, confirm the trims behave, and **retake**
`docs/img/m2xhgr.png` and `docs/img/lr2xhgr.png` (which clears the WARN).

## Decisions (as agreed, 2026-07-23)

| Decision | Choice |
|---|---|
| Scope | Both plugins, one spec |
| Faust | Option A — composable `hgain`, `m2xhgr(g1,g2,g3)` |
| Smoothing | None (follows Yaw/Pitch/Roll, per-block constant) |
| State | Clean-break (6 doubles m2xhgr, 7 doubles lr2xhgr) |
| Trims | ±12 dB, default 0, A0 excluded, IDs 103/104/105 |
| Windows | L format, `— ROTATION —` / `— GAIN —`, trims A1/A2/A3 |

## Out of scope

- The **LR → MS → X** second topology for `lr2xhgr` (matrix to mid/side before
  encoding) is recorded in that plugin's roadmap as a *direction, not a
  specification*, and stays out of this spec.
- `ddelay`'s `ADDELAY` air-absorption companion — separate roadmap, separate
  spec.
