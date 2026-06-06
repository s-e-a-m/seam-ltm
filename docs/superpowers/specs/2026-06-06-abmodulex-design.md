# abmodulex — A-format → AmbiX encoder

**Date:** 2026-06-06
**Status:** Design approved, pending spec review
**Context:** First stage of the TETRAREC → STEREO-LR chain
(`abmodulex → x2uhj → L,R`). Companion to the existing `bamodulex`.

## 1. Purpose

`abmodulex` converts a 4-channel **A-format** tetrahedral microphone signal
(Giuseppe's TETRAREC configuration — the instrumentalist immersed inside the
tetrahedron) into **First-Order AmbiX** (ACN/SN3D). It is the recording-side
inverse of `bamodulex` (which decodes AmbiX to four tetrahedral loudspeakers).

In the TETRAREC workflow: tetrahedral mic capsules → **abmodulex** → AmbiX →
`x2uhj` → UHJ stereo (L,R) for listening.

This is a **pedagogical** plugin (hand-written C++, Faust as spec), stateless,
pure matrix — no capsule-compensation filtering (that would require anechoic
measurement; out of scope, a future possibility).

## 2. The matrix (and the involution)

A-format capsule order: **LFU, RFD, RBU, LBD** (same corner order as
`bamodulex`). Output is AmbiX ACN: a0=W, a1=Y, a2=Z, a3=X.

```
a0 = (LFU + RFD + RBU + LBD) / 2
a1 = (LFU − RFD − RBU + LBD) / 2
a2 = (LFU − RFD + RBU − LBD) / 2
a3 = (LFU + RFD − RBU − LBD) / 2
```

These coefficients are **identical** to `bamodulex`. The matrix is a 4×4
Hadamard scaled by 1/2, which is **involutory** (M² = I): `abmodulex` and
`bamodulex` are the same linear map, differing only in the semantic labelling
of inputs/outputs. Hence `abmodulex(bamodulex(x)) = x`. This is the formal
basis of the round-trip identity already noted in the bamodulex header.

No capsule-compensation filters: the capsules are treated as ideal/coincident
at the tetrahedron vertices (pure matrix), consistent with how `bamodulex`
omits loudspeaker compensation.

## 3. DSP core (SDK-free, testable)

The matrix lives in a header-only, SDK-free core
`plugins/abmodulex/source/abmodulex_dsp.h` (mirroring the `x2uhj_dsp.h`
pattern), so it can be unit-tested with only a compiler. A single function
maps 4 inputs → 4 outputs (double precision, branchless).

The doctest verifies:
- the four matrix outputs for known inputs (e.g. unit capsules);
- the **involution**: feeding the abmodulex output back through the same
  matrix returns the original capsules (round-trip identity).

## 4. VST3 wrapper

A thin `SingleComponentEffect` mirroring `bamodulex_processor`:
- buses: `kAmbi1stOrderACN` 4-in (labelled "A-format In") / 4-out
  ("AmbiX Out"); `setBusArrangements` accepts only 4/4.
- `process`: silence fast-path; pass-through if < 4 channels; per-sample matrix
  via the SDK-free core; 32- and 64-bit sample support.
- no parameters (host handles bypass/level); `setState`/`getState` no-ops.
- `createView` returns a branded VSTGUI editor.

## 5. GUI

Branded editor `resource/abmodulex.uidesc` modeled on `bamodulex`/`sdmx`:
SEAM title block ("SEAM ABMODULEX", subtitle "A-format → AmbiX", an info line
e.g. "LFU RFD RBU LBD → W Y Z X") + logo, no controls.

## 6. Backbone deposit — Faust library

Add `abmodulex` to `seam.ambisonics.lib`, defined explicitly with A-format
inputs and ACN outputs, with a comment noting the involution and cross-
referencing `bamodulex`:

```faust
// A-format (tetrahedral mic: LFU,RFD,RBU,LBD) -> AmbiX (ACN). Inverse of
// bamodulex; the matrix is involutory (M^2 = I), so the coefficients match.
abmodulex(lfu,rfd,rbu,lbd) = a0, a1, a2, a3 with {
    a0 = (lfu + rfd + rbu + lbd) / 2;
    a1 = (lfu - rfd - rbu + lbd) / 2;
    a2 = (lfu - rfd + rbu - lbd) / 2;
    a3 = (lfu + rfd - rbu - lbd) / 2;
};
```

(The pre-existing `smg.abmodule` in `seam.gerzon.lib` outputs FuMa W,X,Y,Z and
remains as the historical Gerzon reference; the canonical ACN form for this
plugin is `sam.abmodulex`.)

Then `doc/abmodulex.dsp` is a library pointer (`import("seam.lib"); process =
sam.abmodulex;`), and `tools/gen-faust-doc.sh abmodulex` produces the block
diagram (`abmodulex-svg/`) and the mathematical-documentation PDF.

## 7. File layout

```
plugins/abmodulex/
├── CMakeLists.txt              # registered in root CMakeLists.txt; links sdk vstgui_support
├── source/
│   ├── abmodulex_ids.h         # fresh unique FUID
│   ├── abmodulex_dsp.h         # SDK-free matrix core
│   ├── abmodulex_processor.h   # FAUST REFERENCE block + class
│   ├── abmodulex_processor.cpp # IAudioProcessor + factory
│   └── version.h
├── resource/
│   └── abmodulex.uidesc
└── doc/
    └── abmodulex.dsp           # -> sam.abmodulex (svg/pdf generated)
```

Plus: `tests/abmodulex_dsp_test.cpp` (wired in `tests/CMakeLists.txt`), a README
row in the root `../README.md`, and registration in the root `CMakeLists.txt`.

## 8. Testing & validation

- doctest: matrix values + involution (fast loop, no SDK).
- Build with the SDK; run the Steinberg validator (expect all pass).

## 9. Out of scope

- Capsule-compensation / A-format EQ filtering (needs anechoic measurement).
- The full TETRAREC capture rig and the X2UHJ stage (already exists).
- Higher orders; parameters; presets.
