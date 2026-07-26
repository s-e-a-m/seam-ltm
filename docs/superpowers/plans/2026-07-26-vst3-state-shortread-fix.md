# VST3 State Short-Read Suite-Wide Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the unchecked bulk `state->read` in five plugins with a shared, per-field, short-read-safe decode so legacy (shorter) state blobs restore defaults instead of stack garbage.

**Architecture:** One header-only helper `plugins/_common/seam_state.h` (`Seam::readStateDoubles`) reads little-endian doubles field-by-field via `IBStreamer`, stopping at the first short read; callers pre-load the value array with parameter defaults and then apply every field. A new doctest ctest `seam_state_test` proves the decode rule against SDK `MemoryStream` blobs, including the real legacy layouts.

**Tech Stack:** C++17, VST3 SDK (`IBStream`, `IBStreamer`, `MemoryStream`), doctest, CMake/ctest (Xcode generator).

Spec: `docs/superpowers/specs/2026-07-26-vst3-state-shortread-design.md`.

## Global Constraints

- The bytes written by `getState` stay identical to today's format for every plugin (bare little-endian double array, append-only).
- Behavior contract (spec table): `state == nullptr` → `kResultFalse`; empty stream → full defaults + `kResultOk`; short/truncated blob → prefix applied, tail defaulted, `kResultOk`.
- Defaults come from the registered parameters (`getInfo().defaultNormalizedValue`) — no duplicated identity constants.
- Missing fields must reset the DSP member variables too, not only the VST3 parameters.
- Two build trees, fixed roles: unit tests run in `build-test` (`-C Debug`, plugins OFF); plugin targets compile in `build` (`--config Release`, owns the `~/Library/.../VST3` symlinks — do not build plugins in any other tree).
- Every test is verified by mutation before its commit (memory `feedback_verify_tests_by_mutation`).
- Code, comments, and commits in English. Commit style: `type(scope): summary`.

---

### Task 1: `seam_state.h` helper + `seam_state_test` ctest

**Files:**
- Create: `plugins/_common/seam_state.h`
- Create: `tests/seam_state_test.cpp`
- Modify: `tests/CMakeLists.txt` (append test target; adjust the header comment at line 1)
- Test: `tests/seam_state_test.cpp`

**Interfaces:**
- Produces: `int Seam::readStateDoubles(Steinberg::IBStream* state, double* values, int count)` — reads up to `count` little-endian doubles into `values` (caller pre-loads defaults), stops at first short read, returns fields actually read. Tasks 2–4 call exactly this.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_state_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_state.h"

#include "public.sdk/source/common/memorystream.h"

using Steinberg::IBStream;
using Steinberg::IBStreamer;
using Steinberg::MemoryStream;

// Sentinel pre-load: proves that fields the decoder does not reach keep the
// caller's defaults — never stack garbage. 0.777 is outside every value any
// test writes, so an overwrite is always detectable.
static constexpr double kSentinel = 0.777;

static void rewindStream(MemoryStream& s) {
    Steinberg::int64 pos = 0;
    s.seek(0, IBStream::kIBSeekSet, &pos);
}

TEST_CASE("full round-trip: write N, read N") {
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        for (int i = 0; i < 6; ++i) w.writeDouble(0.1 * i);
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 6);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == doctest::Approx(0.1 * i));
}

TEST_CASE("legacy short blob: 3 doubles into a 6-field reader") {
    // The real m2xhgr case: a pre-trim session saved yaw/pitch/roll only.
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(0.25);  // yaw
        w.writeDouble(0.50);  // pitch
        w.writeDouble(0.75);  // roll
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 3);
    CHECK(values[0] == doctest::Approx(0.25));
    CHECK(values[1] == doctest::Approx(0.50));
    CHECK(values[2] == doctest::Approx(0.75));
    CHECK(values[3] == kSentinel);   // trims keep the caller's defaults
    CHECK(values[4] == kSentinel);
    CHECK(values[5] == kSentinel);
}

TEST_CASE("empty stream: zero fields read, all defaults intact") {
    MemoryStream stream;

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 0);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == kSentinel);
}

TEST_CASE("blob truncated mid-field: partial field is never applied") {
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(0.25);
        w.writeDouble(0.50);
        w.writeDouble(0.75);
        w.writeInt32(0xDEAD);   // 4 stray bytes — half of a double
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 3);
    CHECK(values[3] == kSentinel);   // the partial field stays default
    CHECK(values[4] == kSentinel);
    CHECK(values[5] == kSentinel);
}

TEST_CASE("longer blob than expected: read the first N, ignore the tail") {
    // The reverse migration: an older plugin build reading a newer state.
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        for (int i = 0; i < 9; ++i) w.writeDouble(0.1 * i);
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 6);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == doctest::Approx(0.1 * i));
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_state_test deliberately links the VST3 SDK base/common layers: the
# unit under test is the suite's IBStream decode rule itself (seam_state.h),
# exercised against SDK MemoryStream blobs including the legacy layouts.
add_executable(seam_state_test
    seam_state_test.cpp
)
target_include_directories(seam_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(seam_state_test PRIVATE base sdk_common pluginterfaces)
target_compile_features(seam_state_test PRIVATE cxx_std_17)
add_test(NAME seam_state_test COMMAND seam_state_test)
```

Update the comment on line 1 of `tests/CMakeLists.txt` from

```cmake
# SEAM-LTM unit tests (doctest, SDK-free DSP cores)
```

to

```cmake
# SEAM-LTM unit tests (doctest; SDK-free DSP cores, except seam_state_test
# which links the SDK base/common layers to test the IBStream decode rule)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test --config Debug --target seam_state_test
```

Expected: FAIL — `'seam_state.h' file not found` (the header does not exist yet).

- [ ] **Step 3: Write the helper**

Create `plugins/_common/seam_state.h`:

```cpp
//─────────────────────────────────────────────────────────────────────────────
// seam_state.h — the SEAM-LTM suite's shared VST3 state decode rule
//
// Suite plugin state is a bare array of little-endian doubles with an
// append-only contract: new fields are only ever appended, never reordered,
// so a blob's "version" is implicit in its length. Restoring therefore reads
// field by field and stops cleanly at the first short read — the caller
// pre-loads `values` with each field's default, so the missing tail of a
// legacy blob restores as defaults instead of uninitialised stack memory.
//
// Spec: docs/superpowers/specs/2026-07-26-vst3-state-shortread-design.md
//─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

namespace Seam {

// Reads up to `count` little-endian doubles from `state` into `values`.
// `values` must arrive pre-loaded with each field's default: fields past the
// first short read keep those defaults. Returns how many fields were read.
inline int readStateDoubles (Steinberg::IBStream* state,
                             double* values, int count)
{
    Steinberg::IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < count; ++i) {
        double v = 0.0;
        if (!s.readDouble (v))   // false ⇔ fewer than 8 bytes available
            return i;
        values[i] = v;
    }
    return count;
}

} // namespace Seam
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test --config Debug --target seam_state_test
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug -R seam_state_test --output-on-failure
```

Expected: `100% tests passed` (5 doctest cases).

- [ ] **Step 5: Verify the tests by mutation**

Temporarily replace the helper's loop with the original bug (bulk read, byte count unchecked):

```cpp
// MUTATION — must turn tests red; revert immediately after.
inline int readStateDoubles (Steinberg::IBStream* state,
                             double* values, int count)
{
    if (state->read (values, count * (int)sizeof (double)) !=
        Steinberg::kResultOk)
        return 0;
    return count;
}
```

Re-run:

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test --config Debug --target seam_state_test
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug -R seam_state_test --output-on-failure
```

Expected: FAIL — at minimum the "legacy short blob", "truncated mid-field", and "empty stream" cases go red (sentinels overwritten or wrong return count). If everything stays green, the tests are broken: stop and fix them before proceeding.

Revert the mutation (restore Step 3's loop exactly), rebuild, re-run, and confirm green again.

- [ ] **Step 6: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/_common/seam_state.h tests/seam_state_test.cpp tests/CMakeLists.txt
git commit -m "feat(state): shared short-read-safe state decode + seam_state_test

Seam::readStateDoubles reads little-endian doubles field by field via
IBStreamer, stopping at the first short read; callers pre-load defaults
so legacy (shorter, append-only) blobs restore defaults, not garbage.
Tested against SDK MemoryStream blobs incl. the real 3-into-6 legacy case."
```

---

### Task 2: fix the live bug — `m2xhgr` and `lr2xhgr` call sites

**Files:**
- Modify: `plugins/m2xhgr/source/m2xhgr_processor.cpp` (includes; `setState`/`getState`, currently lines 228–274)
- Modify: `plugins/lr2xhgr/source/lr2xhgr_processor.cpp` (includes; `setState`/`getState`, currently lines 271–321)

**Interfaces:**
- Consumes: `Seam::readStateDoubles(IBStream*, double*, int)` from Task 1.
- Produces: nothing new — plugin behavior only.

- [ ] **Step 1: Rewrite the m2xhgr state block**

In `plugins/m2xhgr/source/m2xhgr_processor.cpp`, add after the existing `#include` lines at the top of the file:

```cpp
#include "seam_state.h"
```

Replace the whole `setState` body:

```cpp
tresult PLUGIN_API M2XHGRProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Short-read-safe restore (seam_state.h): pre-load registered defaults,
    // read what the blob actually holds, then apply every field — so a
    // legacy pre-trim blob (3 doubles) restores its rotation and identity
    // trims instead of stack garbage.
    const ParamID ids[6] = { kParamYaw, kParamPitch, kParamRoll,
                             kParamTrimA1, kParamTrimA2, kParamTrimA3 };
    double saved[6];
    for (int i = 0; i < 6; ++i) {
        auto* p = parameters.getParameter (ids[i]);
        saved[i] = p ? p->getInfo ().defaultNormalizedValue : 0.5;
    }
    Seam::readStateDoubles (state, saved, 6);

    fYaw   = (-180.0 + saved[0] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[2]);

    fTrimA1Db = -12.0 + saved[3] * 24.0;
    fTrimA2Db = -12.0 + saved[4] * 24.0;
    fTrimA3Db = -12.0 + saved[5] * 24.0;
    if (auto* p = parameters.getParameter (kParamTrimA1)) p->setNormalized (saved[3]);
    if (auto* p = parameters.getParameter (kParamTrimA2)) p->setNormalized (saved[4]);
    if (auto* p = parameters.getParameter (kParamTrimA3)) p->setNormalized (saved[5]);

    return kResultOk;
}
```

In `getState`, replace only the final write call

```cpp
    state->write (saved, sizeof (saved));
    return kResultOk;
```

with the symmetric per-field write (bytes produced are identical):

```cpp
    IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < 6; ++i)
        s.writeDouble (saved[i]);
    return kResultOk;
```

- [ ] **Step 2: Rewrite the lr2xhgr state block**

In `plugins/lr2xhgr/source/lr2xhgr_processor.cpp`, add after the existing `#include` lines:

```cpp
#include "seam_state.h"
```

Replace the whole `setState` body:

```cpp
tresult PLUGIN_API LR2XHGRProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Short-read-safe restore (seam_state.h): pre-load registered defaults,
    // read what the blob actually holds, then apply every field — so a
    // legacy pre-trim blob (4 doubles) restores divergence + rotation and
    // identity trims instead of stack garbage.
    const ParamID ids[7] = { kParamDivergence, kParamYaw, kParamPitch,
                             kParamRoll, kParamTrimA1, kParamTrimA2,
                             kParamTrimA3 };
    double saved[7];
    for (int i = 0; i < 7; ++i) {
        auto* p = parameters.getParameter (ids[i]);
        saved[i] = p ? p->getInfo ().defaultNormalizedValue : 0.5;
    }
    Seam::readStateDoubles (state, saved, 7);

    // Divergence: normalized 0–1 maps to plain 0–360°
    fDivergence = (saved[0] * 360.0 / 2.0) * M_PI / 180.0;
    fYaw   = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[3] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamDivergence))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[2]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[3]);

    fTrimA1Db = -12.0 + saved[4] * 24.0;
    fTrimA2Db = -12.0 + saved[5] * 24.0;
    fTrimA3Db = -12.0 + saved[6] * 24.0;
    if (auto* p = parameters.getParameter (kParamTrimA1)) p->setNormalized (saved[4]);
    if (auto* p = parameters.getParameter (kParamTrimA2)) p->setNormalized (saved[5]);
    if (auto* p = parameters.getParameter (kParamTrimA3)) p->setNormalized (saved[6]);

    return kResultOk;
}
```

In `getState`, replace only

```cpp
    state->write (saved, sizeof (saved));
    return kResultOk;
```

with:

```cpp
    IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < 7; ++i)
        s.writeDouble (saved[i]);
    return kResultOk;
```

- [ ] **Step 3: Build both plugins (compiles + runs the VST3 validator)**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build --config Release --target m2xhgr lr2xhgr
```

Expected: `** BUILD SUCCEEDED **` — the SDK's post-build validator step runs on each plugin and must report no errors.

- [ ] **Step 4: Run the full test suite**

```bash
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug --output-on-failure
```

Expected: `100% tests passed` (22 tests: the 21 existing + seam_state_test).

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/m2xhgr/source/m2xhgr_processor.cpp plugins/lr2xhgr/source/lr2xhgr_processor.cpp
git commit -m "fix(m2xhgr,lr2xhgr): short-read-safe setState — legacy pre-trim states restore identity trims

Bulk state->read ignored numBytesRead, so pre-trim session blobs
(3 resp. 4 doubles) filled the trim fields with uninitialised stack
memory. Decode via Seam::readStateDoubles with parameter defaults
pre-loaded; getState writes the identical bytes via IBStreamer."
```

---

### Task 3: `xyprrot` and `b2xrot` call sites

**Files:**
- Modify: `plugins/xyprrot/source/xyprrot_processor.cpp` (includes; `setState`/`getState`, currently lines 197–235)
- Modify: `plugins/b2xrot/source/b2xrot_processor.cpp` (includes; `setState`/`getState`, currently lines 202–238)

**Interfaces:**
- Consumes: `Seam::readStateDoubles(IBStream*, double*, int)` from Task 1.
- Produces: nothing new — plugin behavior only.

- [ ] **Step 1: Rewrite the xyprrot state block**

In `plugins/xyprrot/source/xyprrot_processor.cpp`, add after the existing `#include` lines:

```cpp
#include "seam_state.h"
```

Replace the whole `setState` body:

```cpp
tresult PLUGIN_API XYPRrotProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Short-read-safe restore (seam_state.h): pre-load registered defaults,
    // read what the blob actually holds, then apply every field. The layout
    // never grew, so this guards truncated/corrupt blobs and every future
    // append-only extension.
    const ParamID ids[3] = { kParamYaw, kParamPitch, kParamRoll };
    double saved[3];
    for (int i = 0; i < 3; ++i) {
        auto* p = parameters.getParameter (ids[i]);
        saved[i] = p ? p->getInfo ().defaultNormalizedValue : 0.5;
    }
    Seam::readStateDoubles (state, saved, 3);

    fYaw   = (-180.0 + saved[0] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[2]);

    return kResultOk;
}
```

In `getState`, replace only

```cpp
    state->write (saved, sizeof (saved));
    return kResultOk;
```

with:

```cpp
    IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < 3; ++i)
        s.writeDouble (saved[i]);
    return kResultOk;
```

- [ ] **Step 2: Rewrite the b2xrot state block**

In `plugins/b2xrot/source/b2xrot_processor.cpp`, add after the existing `#include` lines:

```cpp
#include "seam_state.h"
```

Replace the whole `setState` body:

```cpp
tresult PLUGIN_API B2XrotProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Short-read-safe restore (seam_state.h): pre-load registered defaults,
    // read what the blob actually holds, then apply every field. The layout
    // never grew, so this guards truncated/corrupt blobs and every future
    // append-only extension.
    const ParamID ids[3] = { kParamYaw, kParamPitch, kParamRoll };
    double saved[3];
    for (int i = 0; i < 3; ++i) {
        auto* p = parameters.getParameter (ids[i]);
        saved[i] = p ? p->getInfo ().defaultNormalizedValue : 0.5;
    }
    Seam::readStateDoubles (state, saved, 3);

    fYaw   = (-180.0 + saved[0] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[2]);

    return kResultOk;
}
```

In `getState`, replace only

```cpp
    state->write (saved, sizeof (saved));
    return kResultOk;
```

with:

```cpp
    IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < 3; ++i)
        s.writeDouble (saved[i]);
    return kResultOk;
```

- [ ] **Step 3: Build both plugins (compiles + runs the VST3 validator)**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build --config Release --target xyprrot b2xrot
```

Expected: `** BUILD SUCCEEDED **`, validator clean.

- [ ] **Step 4: Run the full test suite**

```bash
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/xyprrot/source/xyprrot_processor.cpp plugins/b2xrot/source/b2xrot_processor.cpp
git commit -m "fix(xyprrot,b2xrot): short-read-safe setState via Seam::readStateDoubles

Same latent bulk-read defect as m2xhgr/lr2xhgr; the 3-double layout
never grew, so this hardens truncated/corrupt blobs and pre-arms every
future append-only field."
```

---

### Task 4: `ddelay` call site

**Files:**
- Modify: `plugins/ddelay/CMakeLists.txt` (add the `_common` include dir)
- Modify: `plugins/ddelay/source/ddelay_processor.cpp` (includes; `setState`/`getState`, currently lines 265–291)

**Interfaces:**
- Consumes: `Seam::readStateDoubles(IBStream*, double*, int)` from Task 1.
- Produces: nothing new — plugin behavior only.

- [ ] **Step 1: Add the `_common` include dir to ddelay**

`plugins/ddelay/CMakeLists.txt` is the only one of the five without it. Add, right after the `target_link_libraries(${target} ...)` line (matching the other plugins' placement):

```cmake
target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../_common)
```

- [ ] **Step 2: Rewrite the ddelay state block**

In `plugins/ddelay/source/ddelay_processor.cpp`, add after the existing `#include` lines:

```cpp
#include "seam_state.h"
```

Replace the whole `setState` body:

```cpp
tresult PLUGIN_API DDELAYProcessor::setState (IBStream* state)
{
    if (!state) return kResultFalse;

    // Short-read-safe restore (seam_state.h): pre-load the registered
    // default, read what the blob actually holds, then apply.
    double saved = 0.0;
    if (auto* p = parameters.getParameter (kParamDistance))
        saved = p->getInfo ().defaultNormalizedValue;
    Seam::readStateDoubles (state, &saved, 1);

    distanceMeters_ = saved * kMaxDistance;
    updateDelaySamples ();

    if (auto* p = parameters.getParameter (kParamDistance))
        p->setNormalized (saved);

    return kResultOk;
}
```

In `getState`, replace only

```cpp
    state->write (&saved, sizeof (saved));
    return kResultOk;
```

with:

```cpp
    IBStreamer s (state, kLittleEndian);
    s.writeDouble (saved);
    return kResultOk;
```

- [ ] **Step 3: Build the plugin (compiles + runs the VST3 validator)**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build --config Release --target ddelay
```

Expected: `** BUILD SUCCEEDED **`, validator clean.

- [ ] **Step 4: Run the full test suite**

```bash
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/ddelay/CMakeLists.txt plugins/ddelay/source/ddelay_processor.cpp
git commit -m "fix(ddelay): short-read-safe setState via Seam::readStateDoubles

Closes the last of the five bulk-read call sites; also wires ddelay to
plugins/_common (it was the only one of the five without the include)."
```

---

### Task 5: suite-wide verification

**Files:**
- No source changes — verification only.

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Rebuild all five plugins from a clean slate of this change**

```bash
cmake --build /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build --config Release --target m2xhgr lr2xhgr xyprrot b2xrot ddelay
```

Expected: `** BUILD SUCCEEDED **`; each plugin's post-build VST3 validator pass (which round-trips `getState`/`setState`) reports no errors.

- [ ] **Step 2: Run the complete ctest suite one last time**

```bash
ctest --test-dir /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test -C Debug --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 22`.

- [ ] **Step 3: Confirm the working tree is clean and the history tells the story**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git status
git log --oneline -5
```

Expected: clean tree; four commits (Tasks 1–4) on top of the plan/spec commits.
