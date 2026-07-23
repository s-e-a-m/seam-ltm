# m2xhgr / lr2xhgr Harmonic Gain Trims — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-harmonic gain trims (A1/A2/A3, A0 fixed) to `m2xhgr` and `lr2xhgr`, the **G** their names already promise, driven by a single shared DSP primitive and mirrored in the Faust spec.

**Architecture:** A composable `gainRotateYPR` primitive in `seam_rotation.h` carries the load-bearing order — gains applied to A1/A2/A3 **before** rotation, because rotation mixes those channels. `m2xhgr`'s post-Haar DSP *is* that primitive; `lr2xhgr` calls it once per Haar bank with the **same** three gains (the sharing rule), summed and globally rotated by a small `lr2xhgr_dsp.h` core. Faust's `seam.ambisonics.lib` gets the matching `hgain` stage so the specification still describes the plugins.

**Tech Stack:** C++17, VST3 SDK (`SingleComponentEffect`), VSTGUI `.uidesc`, CMake + ctest, doctest, Faust (`seam.ambisonics.lib`).

**Source spec:** `docs/superpowers/specs/2026-07-23-mxhgr-harmonic-gain-design.md` (approved).

## Global Constraints

- **Trims:** three per plugin, range **−12 … +12 dB**, default **0**, unit `"dB"`, `ParameterInfo::kCanAutomate`. IDs **103 (A1), 104 (A2), 105 (A3)** in both plugins. `A0` has no control.
- **Gain before rotation** is load-bearing: `rotateYPR` mixes A1/A2/A3, so a trim after it stops naming a fixed component.
- **No smoothing:** trims are read per-block (last queue point) and applied as a block constant, exactly like the existing Yaw/Pitch/Roll.
- **dB→linear** conversion happens **once per block, outside the sample loop**: `gain = pow(10, dB/20)`.
- **State is clean-break** (the `multipink` precedent): `m2xhgr` 6 doubles, `lr2xhgr` 7 doubles, all normalized. Old-format sessions fall to defaults (0 dB = identity).
- **Faust is the spec, C++ is the deliverable** (`CLAUDE.md`): the Faust change lands in `seam.ambisonics.lib`; no `faust -lang cpp` output is used.
- **Build:** Xcode generator is required — `cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`, then `cmake --build build --config Release`.
- **Both windows become L** (width ≥ 460, two columns), lint-clean under `tools/check-uidesc.py`.
- **Every test is confirmed red before green, and again by deliberate mutation** (suite practice).
- **Working language:** code, comments, commits in English.

---

### Task 1: Faust spec — `hgain` stage in `seam.ambisonics.lib`

The specification comes first. This file lives in the sibling repo
`librerie/faust-libraries`, committed there separately.

**Files:**
- Modify: `../faust-libraries/src/seam.ambisonics.lib` (the `m2xhgr` / `lr2xhgr` block, ~lines 165–178)

**Interfaces:**
- Produces (spec only, mirrored by C++ later): `hgain(g1,g2,g3)`, `m2xhgr(g1,g2,g3)`, `lr2xhgr(divergence,yaw,pitch,roll,g1,g2,g3)`.

- [ ] **Step 1: Edit the definitions**

Replace the current `m2xhgr = sdw.haarmn(1);` and the `lr2xhgr(...)` block with:

```faust
// Harmonic gain trim: A0 (W) passes, A1/A2/A3 are scaled. Linear gains;
// the dB conversion lives in the GUI/C++ layer (Faust convention).
hgain(g1, g2, g3) = _, *(g1), *(g2), *(g3);

// Mono to first-order AmbiX via Haar, with per-harmonic gain. Rotation is
// applied separately by the plugin with rotateYPR. SEAM-LTM M2XHGR plugin.
m2xhgr(g1, g2, g3) = sdw.haarmn(1) : hgain(g1, g2, g3);
//process = no.noise : m2xhgr(1, 1, 1);
//
// Stereo (L,R) to first-order AmbiX via Haar. The SAME three gains feed both
// banks — the sharing rule is the argument forwarding, nothing more.
// divergence/yaw/pitch/roll in radians. SEAM-LTM LR2XHGR plugin.
lr2xhgr(divergence, yaw, pitch, roll, g1, g2, g3) =
    par(i, 2, m2xhgr(g1, g2, g3)) :
    rotateYPR(divergence,   pitch, roll),
    rotateYPR(0 - divergence, pitch, roll)
    :> si.bus(4) :
    rotateYPR(yaw, 0, 0);
//process = (no.noise, no.noise) : lr2xhgr(ma.PI/2, 0, 0, 0, 1, 1, 1);
```

Preserve the surrounding academic/structure comments already in the file.

- [ ] **Step 2: Verify both inline tests compile**

Run (from `librerie/faust-libraries`):
```bash
echo 'import("src/seam.lib"); process = sam.m2xhgr(1, 0.5, 0.25);' | faust -
echo 'import("src/seam.lib"); process = sam.lr2xhgr(ma.PI/2, 0, 0, 0, 1, 0.5, 0.25);' | faust -
```
Expected: both emit C++ to stdout with no error (the `sam` prefix is `seam.ambisonics.lib` per `seam.lib`).

- [ ] **Step 3: Commit (in the faust-libraries repo)**

```bash
cd ../faust-libraries
git add src/seam.ambisonics.lib
git commit -m "feat(ambisonics): per-harmonic gain (hgain) on m2xhgr/lr2xhgr

The G in the SEAM-LTM plugin names: a gain trim on A1/A2/A3 with A0 fixed.
m2xhgr becomes a 3-arg function; lr2xhgr forwards the same three gains to
both Haar banks (the shared-trim rule). Inline tests updated to identity."
cd ../seam-ltm
```

---

### Task 2: `db2lin` dB→linear helper in `seam_meter.h`

`seam_meter.h` already holds `lin2db`, `db2norm`, `lin2norm`, `norm2db` but
no dB→linear-amplitude helper. Add it there (its natural home) so both
plugins share one implementation.

**Files:**
- Modify: `plugins/_common/seam_meter.h`
- Test: `tests/seam_meter_test.cpp` (append cases)

**Interfaces:**
- Produces: `Seam::db2lin(double db) -> double` (linear amplitude, `pow(10, db/20)`; no floor — a gain of exactly 0 is expressible as a very negative dB, which trims never reach).

- [ ] **Step 1: Write the failing test**

Append to `tests/seam_meter_test.cpp`:

```cpp
TEST_CASE("db2lin: 0 dB is unity, ±20 dB is ×10 / ÷10") {
    CHECK(Seam::db2lin(0.0)   == doctest::Approx(1.0));
    CHECK(Seam::db2lin(20.0)  == doctest::Approx(10.0));
    CHECK(Seam::db2lin(-20.0) == doctest::Approx(0.1));
    CHECK(Seam::db2lin(-6.0)  == doctest::Approx(0.5011872336));
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Release --target seam_meter_test && ./build/Release/seam_meter_test -tc="db2lin*"`
Expected: FAIL — `db2lin` is not declared.

- [ ] **Step 3: Add the helper**

In `plugins/_common/seam_meter.h`, next to `lin2db`:

```cpp
// dB -> linear amplitude. Inverse of lin2db (unfloored). 0 dB -> 1.0.
inline double db2lin(double db) {
    return std::pow(10.0, db / 20.0);
}
```

- [ ] **Step 4: Run it to verify it passes**

Run: `cmake --build build --config Release --target seam_meter_test && ./build/Release/seam_meter_test -tc="db2lin*"`
Expected: PASS.

- [ ] **Step 5: Mutation check**

Temporarily change `db / 20.0` to `db / 10.0`; rerun — `db2lin(20)` becomes 100, test goes RED. Revert.

- [ ] **Step 6: Commit**

```bash
git add plugins/_common/seam_meter.h tests/seam_meter_test.cpp
git commit -m "feat(seam_meter): db2lin dB->linear helper"
```

---

### Task 3: `gainRotateYPR` — the shared ordering primitive

The load-bearing composition: apply gains to A1/A2/A3, then rotate. This one
function is `m2xhgr`'s post-Haar DSP and each of `lr2xhgr`'s per-bank stages,
so its test pins gain-before-rotation for both plugins.

**Files:**
- Modify: `plugins/_common/seam_rotation.h`
- Create: `tests/seam_rotation_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test)

**Interfaces:**
- Consumes: `Seam::rotateYPR` (existing, same file).
- Produces: `Seam::gainRotateYPR(T g1,T g2,T g3, T yaw,T pitch,T roll, T a0,T a1,T a2,T a3, T& o0,T& o1,T& o2,T& o3)`.

- [ ] **Step 1: Write the failing tests**

Create `tests/seam_rotation_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_rotation.h"
#include <cmath>

using Seam::rotateYPR;
using Seam::gainRotateYPR;

TEST_CASE("gainRotateYPR: gains hit A1/A2/A3, A0 fixed (zero rotation)") {
    double o0, o1, o2, o3;
    gainRotateYPR(0.5, 1.0, 0.25, 0.0, 0.0, 0.0,
                  1.0, 2.0, 3.0, 4.0, o0, o1, o2, o3);
    CHECK(o0 == doctest::Approx(1.0));   // A0 untouched
    CHECK(o1 == doctest::Approx(1.0));   // 2 * 0.5
    CHECK(o2 == doctest::Approx(3.0));   // 3 * 1.0
    CHECK(o3 == doctest::Approx(1.0));   // 4 * 0.25
}

TEST_CASE("gainRotateYPR: gain precedes rotation (yaw = pi/2 diverges the two orders)") {
    const double yaw = M_PI / 2.0;
    const double g1 = 0.5, g2 = 1.0, g3 = 0.25;
    const double a0 = 1.0, a1 = 2.0, a2 = 3.0, a3 = 4.0;

    // Reference built in the CORRECT order: gain first, then rotate.
    double r0, r1, r2, r3;
    rotateYPR(yaw, 0.0, 0.0, a0, a1 * g1, a2 * g2, a3 * g3, r0, r1, r2, r3);

    double o0, o1, o2, o3;
    gainRotateYPR(g1, g2, g3, yaw, 0.0, 0.0, a0, a1, a2, a3, o0, o1, o2, o3);

    CHECK(o0 == doctest::Approx(r0));
    CHECK(o1 == doctest::Approx(r1));
    CHECK(o2 == doctest::Approx(r2));
    CHECK(o3 == doctest::Approx(r3));

    // Pin the concrete numbers too: rotateYPR(pi/2) maps (a0,a1,a2,a3) ->
    // (a0, -a3, a2, a1), so gain-first gives (1, -a3*g3, a2*g2, a1*g1).
    CHECK(o1 == doctest::Approx(-4.0 * 0.25)); // -1.0
    CHECK(o3 == doctest::Approx( 2.0 * 0.5));  //  1.0
}
```

- [ ] **Step 2: Register the test in `tests/CMakeLists.txt`**

Mirror the existing `abmodulex_dsp_test` registration. Find that block and add an analogous one:

```cmake
add_executable(seam_rotation_test seam_rotation_test.cpp)
target_include_directories(seam_rotation_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(seam_rotation_test PRIVATE doctest)
add_test(NAME seam_rotation_test COMMAND seam_rotation_test)
```

(Match the include-path and doctest-linking style of the sibling `*_dsp_test`
targets already in the file; copy their exact form if it differs.)

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk && cmake --build build --config Release --target seam_rotation_test`
Expected: FAIL — `gainRotateYPR` is not declared.

- [ ] **Step 4: Add the primitive**

In `plugins/_common/seam_rotation.h`, after `rotateYPR` (before the closing
`}` of `namespace Seam`):

```cpp
// Apply per-harmonic gains to A1/A2/A3 (A0 fixed), THEN rotate. The order is
// deliberate and load-bearing: rotateYPR mixes A1/A2/A3 among themselves, so
// the gains must precede it to keep naming a fixed component at every angle.
// This composition is m2xhgr's post-Haar DSP and each of lr2xhgr's per-bank
// stages.
template <typename T>
inline void gainRotateYPR (T g1, T g2, T g3,
                           T yaw, T pitch, T roll,
                           T a0, T a1, T a2, T a3,
                           T& out0, T& out1, T& out2, T& out3)
{
    rotateYPR (yaw, pitch, roll,
               a0, a1 * g1, a2 * g2, a3 * g3,
               out0, out1, out2, out3);
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --config Release --target seam_rotation_test && ./build/Release/seam_rotation_test`
Expected: PASS (both cases).

- [ ] **Step 6: Mutation check (the crux)**

Temporarily rewrite the body to rotate first, then gain:
```cpp
rotateYPR (yaw, pitch, roll, a0, a1, a2, a3, out0, out1, out2, out3);
out1 *= g1; out2 *= g2; out3 *= g3;
```
Rerun: the second test goes RED (at yaw=π/2, `o1` expected −1.0 becomes −2.0). Revert to the correct body; confirm GREEN.

- [ ] **Step 7: Commit**

```bash
git add plugins/_common/seam_rotation.h tests/seam_rotation_test.cpp tests/CMakeLists.txt
git commit -m "feat(seam_rotation): gainRotateYPR — gains before rotation

The load-bearing order for the harmonic trims: rotation mixes A1/A2/A3, so
gains precede it. Shared by m2xhgr and lr2xhgr. Test pins the order against
a reference composition and by mutation."
```

---

### Task 4: `m2xhgr` — parameters, DSP, state

Wire the three trims into the mono plugin. The DSP core (`gainRotateYPR`) is
already proven in Task 3, so this task's gate is a clean build plus the
plugin behaving in the validator; no new doctest is added (its per-sample DSP
*is* the tested primitive).

**Files:**
- Modify: `plugins/m2xhgr/source/m2xhgr_ids.h`
- Modify: `plugins/m2xhgr/source/m2xhgr_processor.h`
- Modify: `plugins/m2xhgr/source/m2xhgr_processor.cpp`

**Interfaces:**
- Consumes: `Seam::gainRotateYPR` (Task 3), `Seam::db2lin` (Task 2).

- [ ] **Step 1: Add parameter IDs**

In `m2xhgr_ids.h`, after `kParamRoll = 102`:

```cpp
    kParamTrimA1 = 103,
    kParamTrimA2 = 104,
    kParamTrimA3 = 105,
```

- [ ] **Step 2: Add state fields**

In `m2xhgr_processor.h`, after `double fRoll = 0.0;`:

```cpp
    double fTrimA1Db = 0.0;   // dB, -12..+12
    double fTrimA2Db = 0.0;
    double fTrimA3Db = 0.0;
```

Also add the include at the top, next to `#include "seam_haar.h"`:

```cpp
#include "seam_rotation.h"
#include "seam_meter.h"
```

- [ ] **Step 3: Register the parameters**

In `m2xhgr_processor.cpp`, `initialize()`, after the Roll `addParameter` block:

```cpp
    parameters.addParameter (
        new RangeParameter (STR16 ("A1"), kParamTrimA1, STR16 ("dB"),
                            -12.0, 12.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter (
        new RangeParameter (STR16 ("A2"), kParamTrimA2, STR16 ("dB"),
                            -12.0, 12.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter (
        new RangeParameter (STR16 ("A3"), kParamTrimA3, STR16 ("dB"),
                            -12.0, 12.0, 0.0, 0, ParameterInfo::kCanAutomate));
```

- [ ] **Step 4: Read the parameter changes**

In `process()`, extend the `switch (id)` that handles Yaw/Pitch/Roll with
(normalized [0,1] → dB [−12,12], symmetric so 0.5 → 0 dB):

```cpp
                    case kParamTrimA1:
                        fTrimA1Db = -12.0 + value * 24.0;
                        break;
                    case kParamTrimA2:
                        fTrimA2Db = -12.0 + value * 24.0;
                        break;
                    case kParamTrimA3:
                        fTrimA3Db = -12.0 + value * 24.0;
                        break;
```

- [ ] **Step 5: Apply gain before rotation in the DSP**

In `processHaarMono`, convert once outside the loop and replace the
`rotateYPR(...)` call with `gainRotateYPR(...)`:

```cpp
    const SampleType g1 = static_cast<SampleType>(Seam::db2lin (fTrimA1Db));
    const SampleType g2 = static_cast<SampleType>(Seam::db2lin (fTrimA2Db));
    const SampleType g3 = static_cast<SampleType>(Seam::db2lin (fTrimA3Db));
    // ... existing yaw/pitch/roll locals ...
    // inside the loop, after haarDecompose produces a0..a3:
        gainRotateYPR (g1, g2, g3, yaw, pitch, roll,
                       a0, a1, a2, a3,
                       out0[i], out1[i], out2[i], out3[i]);
```

Remove the now-unused direct `rotateYPR` call it replaces.

- [ ] **Step 6: Widen the state to 6 doubles (clean-break)**

In `getState`, change `double saved[3]` to `double saved[6]` and append:

```cpp
    saved[3] = parameters.getParameter (kParamTrimA1) ? parameters.getParameter (kParamTrimA1)->getNormalized () : 0.5;
    saved[4] = parameters.getParameter (kParamTrimA2) ? parameters.getParameter (kParamTrimA2)->getNormalized () : 0.5;
    saved[5] = parameters.getParameter (kParamTrimA3) ? parameters.getParameter (kParamTrimA3)->getNormalized () : 0.5;
```

In `setState`, change `double saved[3]` to `double saved[6]` and append,
after the Roll restore:

```cpp
    fTrimA1Db = -12.0 + saved[3] * 24.0;
    fTrimA2Db = -12.0 + saved[4] * 24.0;
    fTrimA3Db = -12.0 + saved[5] * 24.0;
    if (auto* p = parameters.getParameter (kParamTrimA1)) p->setNormalized (saved[3]);
    if (auto* p = parameters.getParameter (kParamTrimA2)) p->setNormalized (saved[4]);
    if (auto* p = parameters.getParameter (kParamTrimA3)) p->setNormalized (saved[5]);
```

- [ ] **Step 7: Update the FAUST REFERENCE comment**

In `m2xhgr_processor.h`, update the reference block to the new signature:

```cpp
//   m2xhgr(g1,g2,g3) = sdw.haarmn(1) : hgain(g1,g2,g3);
//   process = m2xhgr(g1,g2,g3) : rotateYPR(yaw, pitch, roll);
```

- [ ] **Step 8: Build the plugin target**

Run: `cmake --build build --config Release --target m2xhgr`
Expected: builds with no errors.

- [ ] **Step 9: Commit**

```bash
git add plugins/m2xhgr/source/
git commit -m "feat(m2xhgr): harmonic gain trims A1/A2/A3

Three ±12 dB trims (IDs 103-105), A0 fixed, applied via gainRotateYPR before
rotation. Clean-break 6-double state. DSP core proven in seam_rotation_test."
```

---

### Task 5: `lr2xhgr_dsp.h` — shared-gain two-bank core + test

Extract `lr2xhgr`'s post-Haar mix (two banks, divergence, sum, global yaw)
into a pure, testable function that receives one set of gains and applies it
to **both** banks. This mirrors `abmodulex_dsp.h` and is where the sharing
rule is pinned.

**Files:**
- Create: `plugins/lr2xhgr/source/lr2xhgr_dsp.h`
- Create: `tests/lr2xhgr_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt` (register the new test)

**Interfaces:**
- Consumes: `Seam::gainRotateYPR`, `Seam::rotateYPR`.
- Produces: `Seam::lr2xhgr::mix(T divergence, T yaw, T pitch, T roll, T g1, T g2, T g3, const T la[4], const T ra[4], T out[4])`.

- [ ] **Step 1: Write the failing test**

Create `tests/lr2xhgr_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lr2xhgr_dsp.h"

using Seam::lr2xhgr::mix;

TEST_CASE("lr2xhgr mix: one gain scales BOTH banks (the sharing rule)") {
    // Zero divergence and zero rotation: mix reduces to the gained sum.
    const double la[4] = {0.0, 1.0, 0.0, 0.0};   // A1 only, left bank
    const double ra[4] = {0.0, 3.0, 0.0, 0.0};   // A1 only, right bank
    double out[4];
    mix(0.0, 0.0, 0.0, 0.0, /*g1*/0.5, /*g2*/1.0, /*g3*/1.0, la, ra, out);

    // Shared trim: (1 + 3) * 0.5 = 2.0. A per-bank bug (gain on L only) would
    // give 1*0.5 + 3 = 3.5.
    CHECK(out[1] == doctest::Approx(2.0));
    CHECK(out[0] == doctest::Approx(0.0));
}

TEST_CASE("lr2xhgr mix: A0 (W) is never gained, sums straight through") {
    const double la[4] = {2.0, 0.0, 0.0, 0.0};
    const double ra[4] = {5.0, 0.0, 0.0, 0.0};
    double out[4];
    mix(0.3, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, la, ra, out);
    CHECK(out[0] == doctest::Approx(7.0));   // 2 + 5, untouched by trims or rotation
}
```

- [ ] **Step 2: Register the test in `tests/CMakeLists.txt`**

```cmake
add_executable(lr2xhgr_dsp_test lr2xhgr_dsp_test.cpp)
target_include_directories(lr2xhgr_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lr2xhgr/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lr2xhgr_dsp_test PRIVATE doctest)
add_test(NAME lr2xhgr_dsp_test COMMAND lr2xhgr_dsp_test)
```

(Match the sibling `*_dsp_test` form already in the file.)

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk && cmake --build build --config Release --target lr2xhgr_dsp_test`
Expected: FAIL — `lr2xhgr_dsp.h` does not exist.

- [ ] **Step 4: Write the core**

Create `plugins/lr2xhgr/source/lr2xhgr_dsp.h`:

```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LR2XHGR — Stereo → AmbiX (Haar) post-decomposition mix
//
// FAUST REFERENCE (seam.ambisonics.lib):
//   lr2xhgr(divergence, yaw, pitch, roll, g1, g2, g3) =
//       par(i, 2, m2xhgr(g1, g2, g3)) :
//       rotateYPR(divergence, pitch, roll),
//       rotateYPR(0-divergence, pitch, roll) :> si.bus(4) :
//       rotateYPR(yaw, 0, 0);
//
// The two Haar banks receive the SAME three gains — the shared-trim rule.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "seam_rotation.h"

namespace Seam { namespace lr2xhgr {

// la/ra are the two banks' Haar outputs (A0..A3). Angles in radians.
template <typename T>
inline void mix (T divergence, T yaw, T pitch, T roll,
                 T g1, T g2, T g3,
                 const T la[4], const T ra[4], T out[4])
{
    T l0, l1, l2, l3, r0, r1, r2, r3;
    // Same g1,g2,g3 to both banks — gains before the divergence rotation.
    gainRotateYPR (g1, g2, g3,  divergence, pitch, roll,
                   la[0], la[1], la[2], la[3], l0, l1, l2, l3);
    gainRotateYPR (g1, g2, g3, T(0) - divergence, pitch, roll,
                   ra[0], ra[1], ra[2], ra[3], r0, r1, r2, r3);

    const T s0 = l0 + r0, s1 = l1 + r1, s2 = l2 + r2, s3 = l3 + r3;
    rotateYPR (yaw, T(0), T(0), s0, s1, s2, s3,
               out[0], out[1], out[2], out[3]);
}

}} // namespace Seam::lr2xhgr
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --config Release --target lr2xhgr_dsp_test && ./build/Release/lr2xhgr_dsp_test`
Expected: PASS.

- [ ] **Step 6: Mutation check**

In `mix`, temporarily pass `T(1)` instead of `g1` to the *second*
`gainRotateYPR` (gain on the left bank only). Rerun: the sharing-rule test
goes RED (out[1] becomes 3.5). Revert; confirm GREEN.

- [ ] **Step 7: Commit**

```bash
git add plugins/lr2xhgr/source/lr2xhgr_dsp.h tests/lr2xhgr_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(lr2xhgr): lr2xhgr_dsp.h shared-gain two-bank core

Pure mix() applying one gain set to both Haar banks before divergence, summed
and globally rotated. Test pins the sharing rule by mutation."
```

---

### Task 6: `lr2xhgr` — parameters, DSP via the core, state

Wire the trims into the stereo plugin and route its per-sample DSP through
`lr2xhgr::mix`.

**Files:**
- Modify: `plugins/lr2xhgr/source/lr2xhgr_ids.h`
- Modify: `plugins/lr2xhgr/source/lr2xhgr_processor.h`
- Modify: `plugins/lr2xhgr/source/lr2xhgr_processor.cpp`

**Interfaces:**
- Consumes: `Seam::lr2xhgr::mix` (Task 5), `Seam::db2lin` (Task 2).

- [ ] **Step 1: Add parameter IDs**

In `lr2xhgr_ids.h`, after `kParamRoll = 102`:

```cpp
    kParamTrimA1 = 103,
    kParamTrimA2 = 104,
    kParamTrimA3 = 105,
```

- [ ] **Step 2: Add state fields + includes**

In `lr2xhgr_processor.h`, after `double fRoll = 0.0;`:

```cpp
    double fTrimA1Db = 0.0;
    double fTrimA2Db = 0.0;
    double fTrimA3Db = 0.0;
```

Add includes near the existing ones:

```cpp
#include "lr2xhgr_dsp.h"
#include "seam_meter.h"
```

- [ ] **Step 3: Register the parameters**

In `initialize()`, after the Roll `addParameter` block, add the same three
`RangeParameter`s as Task 4 Step 3 (labels `"A1"`, `"A2"`, `"A3"`, unit
`"dB"`, −12..12, default 0, `kCanAutomate`, IDs `kParamTrimA1/A2/A3`).

- [ ] **Step 4: Read the parameter changes**

In `process()`, extend the `switch (id)` with the same three cases as Task 4
Step 4 (`fTrimA1Db = -12.0 + value * 24.0;` etc.).

- [ ] **Step 5: Route the DSP through `mix`**

In `processHaarStereo`, convert gains once outside the loop:

```cpp
    const SampleType g1 = static_cast<SampleType>(Seam::db2lin (fTrimA1Db));
    const SampleType g2 = static_cast<SampleType>(Seam::db2lin (fTrimA2Db));
    const SampleType g3 = static_cast<SampleType>(Seam::db2lin (fTrimA3Db));
```

Replace the inline "Step 2 / Step 3 / Step 4" rotation-sum-rotation block
(the two `rotateYPR` calls, the four `s0..s3` sums, and the final `rotateYPR`)
with a single call, keeping the Haar decomposition above it:

```cpp
        const SampleType la[4] = { la0, la1, la2, la3 };
        const SampleType ra[4] = { ra0, ra1, ra2, ra3 };
        SampleType o[4];
        Seam::lr2xhgr::mix (divYaw, yaw, pitch, roll, g1, g2, g3, la, ra, o);
        out0[i] = o[0]; out1[i] = o[1]; out2[i] = o[2]; out3[i] = o[3];
```

(`divYaw`, `yaw`, `pitch`, `roll` are the existing block-constant locals.)

- [ ] **Step 6: Widen the state to 7 doubles (clean-break)**

In `getState`, change `double saved[4]` to `double saved[7]` and append the
three trims at `saved[4..6]` (normalized, same pattern as Task 4 Step 6).

In `setState`, change `double saved[4]` to `double saved[7]` and append,
after the Roll restore:

```cpp
    fTrimA1Db = -12.0 + saved[4] * 24.0;
    fTrimA2Db = -12.0 + saved[5] * 24.0;
    fTrimA3Db = -12.0 + saved[6] * 24.0;
    if (auto* p = parameters.getParameter (kParamTrimA1)) p->setNormalized (saved[4]);
    if (auto* p = parameters.getParameter (kParamTrimA2)) p->setNormalized (saved[5]);
    if (auto* p = parameters.getParameter (kParamTrimA3)) p->setNormalized (saved[6]);
```

- [ ] **Step 7: Build the plugin target**

Run: `cmake --build build --config Release --target lr2xhgr`
Expected: builds with no errors.

- [ ] **Step 8: Commit**

```bash
git add plugins/lr2xhgr/source/
git commit -m "feat(lr2xhgr): shared harmonic gain trims via lr2xhgr::mix

Three ±12 dB trims (IDs 103-105) driving both Haar banks. DSP routed through
the tested mix() core. Clean-break 7-double state."
```

---

### Task 7: `m2xhgr` window → L format (`— ROTATION —` / `— GAIN —`)

Grow the S window to two columns. Geometry follows `ltglide`'s L window:
460 wide, two 180 px columns at x=30 and x=250 (40 px gutter), slider block =
label 14 / slider 18 / value 16 on a 58 px stride, column sub-labels in
`InfoFont`.

**Files:**
- Modify: `plugins/m2xhgr/resource/m2xhgr.uidesc`

- [ ] **Step 1: Resize the window and set two columns**

Change the `<template ... size>` to `460, 380` (with matching `minSize`/`maxSize`).
Keep the HEADER (title `SEAM M2XHGR`, subtitle, info line) centred at width 460.
Add the two column sub-labels below the header:

```xml
<view class="CTextLabel" origin="30, 96" size="180, 16" font="InfoFont"
      font-color="TextLight" text-alignment="center" title="&#x2014; ROTATION &#x2014;" transparent="true"/>
<view class="CTextLabel" origin="250, 96" size="180, 16" font="InfoFont"
      font-color="TextLight" text-alignment="center" title="&#x2014; GAIN &#x2014;" transparent="true"/>
```

- [ ] **Step 2: Left column — the existing rotation controls**

Move the existing Yaw/Pitch/Roll label+slider+value groups into the left
column at `x=30`, `size=180`, starting at `y=118`, stride 58:
Yaw at 118, Pitch at 176, Roll at 234. Keep their `control-tag`s unchanged.

- [ ] **Step 3: Right column — the three trims**

Add A1/A2/A3 groups at `x=250`, `size=180`, same `y` rows (118/176/234). For
each trim (shown for A1; repeat for A2 at y=176, A3 at y=234):

```xml
<view class="CTextLabel" origin="250, 118" size="180, 14" font="KnobLabelFont"
      font-color="TextLight" text-alignment="center" title="A1" transparent="true"/>
<view class="CSlider" origin="250, 132" size="180, 18" control-tag="TrimA1" default-value="0.5"
      orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
      draw-value="true" draw-value-color="SliderActive"
      draw-frame="true" draw-frame-color="SliderTrack"
      frame-width="1" mode="free click" transparent="false"/>
<view class="CTextEdit" origin="250, 150" size="180, 16" font="ValueFont" control-tag="TrimA1"
      font-color="TextLight" text-alignment="center" transparent="true"
      value-precision="1" style-no-frame="true"/>
```

- [ ] **Step 4: Map the new control-tags**

In the `<control-tags>` section, add three tags mapping to the new IDs
(mirroring the existing Yaw/Pitch/Roll entries):

```xml
<control-tag name="TrimA1" tag="103"/>
<control-tag name="TrimA2" tag="104"/>
<control-tag name="TrimA3" tag="105"/>
```

- [ ] **Step 5: Lint the window**

Run: `python3 tools/check-uidesc.py plugins/m2xhgr/resource/m2xhgr.uidesc`
Expected: `0 error(s)` for this file (title `SEAM M2XHGR`, palette, all text
`font-color="TextLight"`, zone order intact).

- [ ] **Step 6: Commit**

```bash
git add plugins/m2xhgr/resource/m2xhgr.uidesc
git commit -m "style(m2xhgr): L window, — ROTATION — / — GAIN — columns"
```

---

### Task 8: `lr2xhgr` window → L format

Same treatment, with a four-row left column (Divergence, Yaw, Pitch, Roll)
and the three-row GAIN column.

**Files:**
- Modify: `plugins/lr2xhgr/resource/lr2xhgr.uidesc`

- [ ] **Step 1: Resize and add sub-labels**

Set `<template ... size>` to `460, 440` (matching min/max). Add the
`— ROTATION —` (x=30) and `— GAIN —` (x=250) sub-labels at `y=96` exactly as
Task 7 Step 1.

- [ ] **Step 2: Left column — Divergence + rotation**

Place the existing Divergence, Yaw, Pitch, Roll groups at `x=30`, `size=180`,
starting `y=118`, stride 58: Divergence 118, Yaw 176, Pitch 234, Roll 292.
Keep their `control-tag`s.

- [ ] **Step 3: Right column — the three trims**

Add A1/A2/A3 groups at `x=250`, `y` = 118 / 176 / 234, identical to Task 7
Step 3 (label `A1`/`A2`/`A3`, slider `control-tag="TrimA1/2/3"` default 0.5,
value CTextEdit precision 1).

- [ ] **Step 4: Map the new control-tags**

Add the same three `<control-tag>` entries (103/104/105) as Task 7 Step 4.

- [ ] **Step 5: Lint the window**

Run: `python3 tools/check-uidesc.py plugins/lr2xhgr/resource/lr2xhgr.uidesc`
Expected: `0 error(s)` for this file.

- [ ] **Step 6: Commit**

```bash
git add plugins/lr2xhgr/resource/lr2xhgr.uidesc
git commit -m "style(lr2xhgr): L window, — ROTATION — / — GAIN — columns"
```

---

### Task 9: Integration — build, validator, ctest, retake screenshots

The final gate: the whole suite builds, the VST3 validator accepts both
plugins, every ctest passes, and the two windows are re-photographed (which
clears the `check_screenshots` WARN the window changes introduced).

**Files:**
- Modify: `docs/img/m2xhgr.png`, `docs/img/lr2xhgr.png` (retaken)

- [ ] **Step 1: Full build**

Run: `cmake --build build --config Release`
Expected: all targets build, including `m2xhgr` and `lr2xhgr`.

- [ ] **Step 2: Run the whole test suite**

Run: `ctest --test-dir build -C Release --output-on-failure`
Expected: 100% pass, including `seam_meter_test`, `seam_rotation_test`,
`lr2xhgr_dsp_test`, `uidesc_lint_selftest`, `uidesc_lint`.

- [ ] **Step 3: Confirm the screenshot WARN is present (the guard working)**

Run: `python3 tools/check-uidesc.py`
Expected: `0 error(s)` and **2 warning(s)** — the retake reminders for
`m2xhgr` and `lr2xhgr` (their windows changed; PNGs are stale). This proves
the guard fires; Step 6 clears it.

- [ ] **Step 4: VST3 validator**

Run the SDK validator on both bundles (the suite's usual validation step):
```bash
./build/bin/Release/validator build/VST3/Release/m2xhgr.vst3
./build/bin/Release/validator build/VST3/Release/lr2xhgr.vst3
```
Expected: no failures.

- [ ] **Step 5: Host check (manual, Giuseppe)**

Load both in Reaper. Confirm: A1/A2/A3 trims move the encoded image size,
A0/level unaffected; at a non-zero rotation the trim still tracks the named
component; state persists across save/reload of a new session.

- [ ] **Step 6: Retake the two screenshots**

Capture the new L windows to `docs/img/m2xhgr.png` and `docs/img/lr2xhgr.png`.
Then confirm the WARN is gone:

Run: `python3 tools/check-uidesc.py`
Expected: `checked 16 file(s): 0 error(s), 0 warning(s)`.

- [ ] **Step 7: Commit**

```bash
git add docs/img/m2xhgr.png docs/img/lr2xhgr.png
git commit -m "docs(img): retake m2xhgr and lr2xhgr with their L windows"
```

---

## Self-Review

**Spec coverage:**
- §1 Faust (`hgain`, `m2xhgr(g1,g2,g3)`, `lr2xhgr(...,g1,g2,g3)`, inline tests) → Task 1. ✓
- §2 m2xhgr C++ (params 103-105, DSP gain-before-rotation, 6-double state) → Tasks 2, 3, 4. ✓
- §3 lr2xhgr C++ (shared trim, 7-double state, mirrors Faust) → Tasks 5, 6. ✓
- §4 L windows (`— ROTATION —` / `— GAIN —`, A1/A2/A3, lint-clean) → Tasks 7, 8. ✓
- §5 Testing (Faust compile, TDD crux by mutation, ctest, lint WARN, host, retake) → Tasks 1, 3, 5, 9. ✓
- Out of scope (LR→MS→X, ADDELAY) → not planned. ✓

**Placeholder scan:** No TBD/TODO. Every code step shows the code; every command shows expected output. The uidesc tasks give concrete XML for the new GAIN column and geometry, with the lint as the objective gate (Tasks 7/8 Step 5). ✓

**Type consistency:** `db2lin(double)→double` (Task 2) used in Tasks 4/6. `gainRotateYPR(g1,g2,g3, yaw,pitch,roll, a0..a3, out0..3&)` (Task 3) used in Task 4 and inside `lr2xhgr::mix` (Task 5). `Seam::lr2xhgr::mix(div,yaw,pitch,roll,g1,g2,g3, la[4], ra[4], out[4])` (Task 5) used in Task 6. Param IDs 103/104/105 and `fTrim{A1,A2,A3}Db` fields consistent across Tasks 4/6/7/8. State widths 6 (m2xhgr) / 7 (lr2xhgr) consistent. ✓
