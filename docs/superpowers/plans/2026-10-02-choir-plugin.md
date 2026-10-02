# choir plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A four-channel VST3 plugin, hand-written in C++, equal to `sdt.choir(350, 1.5)` of `seam.tedesco.lib`, with a 4×16 grid of the analysis bands and a GUI-only RESET.

**Architecture:** Three new `_common` libraries mirror standard Faust (`fi.svf.bp`, `an.amp_follower`, `ba.tau2pole`); an SDK-free engine (`choir_dsp.h`) wires them with the SSCDO#2 noise block (`Seam::MultinoiseBlock(72, 8, 64)`); the processor follows delRM's shape (SingleComponentEffect, ParamBox of atomics, append-only state); two custom views (grid, RESET) read the engine directly.

**Tech Stack:** C++17, VST3 SDK + VSTGUI, CMake (Xcode generator), doctest, Faust 2.88 with the faustlibraries clone for references.

**Spec:** `docs/superpowers/specs/2026-10-02-choir-plugin-design.md`

## Global Constraints

- State and arithmetic in `double` throughout; conversion to the bus type only at the output store.
- SR rule: every process sounds as at 96 kHz (`choirdens = sqrt(fs/96000)` on the voices).
- Constants: f = {48, 48, 96, 96} Hz, a = {1, 1.01, 1.1, 0.9}, Q = 350, release 1.5 s, 16 bands; noise `MultinoiseBlock(72, 8, 64)`, channel c on streams 16c … 16c+15 of the block.
- A band whose centre is ≥ 20000 Hz is inactive; every band is designed at `min(fc, 19999)` Hz.
- No DC blocker. No allocation in the audio thread. Engine inside `Seam::ScopedNoDenormals`.
- Ramps: `Seam::LinearRamp`, 25 ms, for output and POWER.
- FUID `(0x5E4D0013, 0xA1B2C3D4, 0x43485200, 0x00000013)` (word3 = ASCII "CHR\0"); parameter IDs Power 100, Output 101; factory name `SEAM CHOIR`; subcategory `Fx`; bus `kAmbi1stOrderACN` in and out.
- Window: format L, 460 px wide; title `SEAM CHOIR`; zone order HEADER, OPS, FINE, FOOTER; palette names only (`BgDark`, `TextLight`, `SliderTrack`, `SliderActive`, `Structure`); never `TextDim`.
- Every test verified by mutation: break the code, see it RED, restore; record in `doc/study/sscdo2/choir-plugin/mutations.md`.
- Commit as you go; never push without Giuseppe's confirmation.
- Code, comments, commits in English; commit trailer:
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM`.

## Build and test commands

- Tests tree (no plugins): `cmake --build build-test --target <test>` then `(cd build-test && ctest -C Debug -R <test> --output-on-failure)`. After adding a test to `tests/CMakeLists.txt`, reconfigure first: `cmake -S . -B build-test`.
- Plugin tree: `cmake -S . -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` (already configured), `cmake --build build --config Debug --target choir`.
- References: `bash doc/study/sscdo2/choir-plugin/gen-ref.sh` (needs `faust` and the clone at `/Users/giuseppe/Documents/github/grame/faustlibraries`); tests read the committed headers in `tests/ref/`.

## Review Focus

1. A host block of 1 sample, or of 4096: the engine must give the same output as with 512-sample blocks (RESET served per block, display per block, nothing else block-dependent). Test in Task 4.
2. A host that calls `process` before `setActive(true)`, or after `setActive(false)`: silence, no crash. Test in Task 4 (`process` on an unprepared engine).
3. 32-bit and 64-bit buses: the same engine template instantiated for `float` and `double`. Test in Task 4 (float bus equals double bus to float precision).
4. RESET pressed while the host is stopped (no `process` calls): the request is kept and served at the next block, not lost. Test in Task 4.
5. A rate change (48 kHz session reopened at 96 kHz): `prepare` redesigns every band and `choirdens`, and the output equals a fresh engine's at the new rate. Test in Task 4.

---

### Task 1: `seam_basics.h` — `tau2pole` moved out of the compressor

**Files:**
- Create: `plugins/_common/seam_basics.h`
- Modify: `plugins/_common/seam_compressors.h` (remove `tau2pole`, include `seam_basics.h`)
- Create: `tests/seam_basics_test.cpp`
- Modify: `tests/CMakeLists.txt` (register `seam_basics_test`)

**Interfaces:**
- Produces: `inline double Seam::tau2pole(double tau, double fs)` in `seam_basics.h`.

- [ ] **Step 1: Write the failing test**

`tests/seam_basics_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_basics.h"
#include <cmath>

TEST_CASE("tau2pole is exp(-1/(tau*fs))") {
    CHECK(Seam::tau2pole(1.5, 96000.0) == std::exp(-1.0 / (1.5 * 96000.0)));
    CHECK(Seam::tau2pole(0.03, 48000.0) == std::exp(-1.0 / (0.03 * 48000.0)));
}

TEST_CASE("tau2pole is 0 for a time constant below epsilon, as ba.tau2pole") {
    CHECK(Seam::tau2pole(0.0, 96000.0) == 0.0);
    CHECK(Seam::tau2pole(1e-20, 96000.0) == 0.0);
    CHECK(Seam::tau2pole(-1e-20, 96000.0) == 0.0);
}
```

Register in `tests/CMakeLists.txt`, after the `seam_ramp_test` block:

```cmake
add_executable(seam_basics_test seam_basics_test.cpp)
target_include_directories(seam_basics_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_basics_test PRIVATE cxx_std_17)
add_test(NAME seam_basics_test COMMAND seam_basics_test)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake -S . -B build-test && cmake --build build-test --target seam_basics_test`
Expected: compile error, `seam_basics.h` not found.

- [ ] **Step 3: Write `seam_basics.h` and move the function**

`plugins/_common/seam_basics.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_basics.h — the C++ side of basics.lib (ba)
//
// FAUST REFERENCE (basics.lib, standard):
//   ba.tau2pole(tau) = 0 when |tau| < ma.EPSILON, else exp(-1/(tau*ma.SR));
//
// The pole of a one-pole smoother whose time constant is tau seconds. Moved
// here from seam_compressors.h (2026-10-02) when the choir's follower needed
// it too: a follower should not depend on a compressor.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cfloat>
#include <cmath>

namespace Seam {

inline double tau2pole(double tau, double fs) {
    return std::fabs(tau) < DBL_EPSILON ? 0.0 : std::exp(-1.0 / (tau * fs));
}

} // namespace Seam
```

In `plugins/_common/seam_compressors.h`, delete the `inline double tau2pole(...)` definition (the four lines starting `inline double tau2pole`) and add `#include "seam_basics.h"` after `#include <cmath>`. Keep the FAUST REFERENCE comment line about `ba.tau2pole` as it is (it documents the compressor's dependency).

- [ ] **Step 4: Run the new test and the compressor's**

Run: `cmake --build build-test --target seam_basics_test seam_compressors_test delrm_dsp_test && (cd build-test && ctest -C Debug -R "seam_basics|seam_compressors|delrm_dsp" --output-on-failure)`
Expected: 3 tests pass.

- [ ] **Step 5: Mutation**

Change `exp(-1.0 / (tau * fs))` to `exp(-1.0 / (tau * fs * 2.0))`, run `seam_basics_test`: RED on the first case. Restore. Change `DBL_EPSILON` to `0.0` (strict `< 0.0`): RED on the zero case. Restore.

- [ ] **Step 6: Commit**

```bash
git add plugins/_common/seam_basics.h plugins/_common/seam_compressors.h tests/seam_basics_test.cpp tests/CMakeLists.txt
git commit -m "refactor(common): tau2pole moves to seam_basics.h

The choir's follower needs it; a follower should not depend on a
compressor. CompressorMono unchanged, its tests green.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 2: `seam_svf.h` — `SvfBandpass` (fi.svf.bp)

**Files:**
- Create: `plugins/_common/seam_svf.h`
- Create: `doc/study/sscdo2/choir-plugin/gen-ref.sh`, `doc/study/sscdo2/choir-plugin/dsp/svf48.dsp`, `doc/study/sscdo2/choir-plugin/dsp/svf1k.dsp`
- Create: `tests/ref/seam_svf_ref.h` (generated)
- Create: `tests/seam_svf_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class Seam::SvfBandpass { void design(double fs, double f, double q); double tick(double x); void reset(); bool hasSubnormalState() const; }`.
- Produces: `gen-ref.sh` (extended by Tasks 3 and 4), which uses `doc/study/sscdo2/lmo-plugin/refdump.cpp` (usage `refdump SR N NAME impulse|zero [SKIP]`, prints `static const double NAME[outputs][N]` in hex-float).

- [ ] **Step 1: The reference DSPs and the generator**

`doc/study/sscdo2/choir-plugin/dsp/svf48.dsp`:

```faust
// fi.svf.bp at the choir's lowest centre: the reference for Seam::SvfBandpass.
import("stdfaust.lib");
process = fi.svf.bp(48, 350);
```

`doc/study/sscdo2/choir-plugin/dsp/svf1k.dsp`:

```faust
// fi.svf.bp, low Q, another rate: Seam::SvfBandpass is generic.
import("stdfaust.lib");
process = fi.svf.bp(1000, 0.7);
```

`doc/study/sscdo2/choir-plugin/gen-ref.sh`:

```bash
#!/usr/bin/env bash
# gen-ref.sh -- render the Faust references of the choir plugin and of the
# _common libraries it added (seam_svf.h, seam_analyzers.h) into tests/ref/.
# Run by hand when the spec changes; the tests read the committed headers
# and need no faust binary. Uses LMO's refdump (impulse or zero input).
#
# FAUSTLIBS: faustlibraries clone (0965ea2 or later)
# SEAMLIBS:  faust-libraries/src (seam.tedesco.lib)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
FAUSTLIBS="${FAUSTLIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}"
SEAMLIBS="${SEAMLIBS:-$(cd "$ROOT/../faust-libraries/src" && pwd)}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$ROOT/tests/ref"

render() { # dsp sr n name input [skip]
    faust -I "$FAUSTLIBS" -I "$SEAMLIBS" -double -lang cpp -cn Ref "$HERE/dsp/$1" -o "$WORK/ref.h"
    c++ -std=c++17 -O1 -I "$WORK" "$HERE/../lmo-plugin/refdump.cpp" -o "$WORK/refdump"
    "$WORK/refdump" "$2" "$3" "$4" "$5" "${6:-0}"
}
banner() {
    echo "// GENERATED by doc/study/sscdo2/choir-plugin/gen-ref.sh -- do not edit."
    echo "// $(faust --version | head -1); faustlibraries $(git -C "$FAUSTLIBS" rev-parse --short HEAD);"
    echo "// faust-libraries $(git -C "$SEAMLIBS" rev-parse --short HEAD)."
    echo "#pragma once"
}

OUT="$ROOT/tests/ref/seam_svf_ref.h"
{ banner; echo "namespace svfref {"
  render svf48.dsp 96000 2048 kSvf48 impulse
  render svf1k.dsp 48000 512  kSvf1k impulse
  echo "} // namespace svfref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
```

Run: `chmod +x doc/study/sscdo2/choir-plugin/gen-ref.sh && bash doc/study/sscdo2/choir-plugin/gen-ref.sh`
Expected: `wrote .../tests/ref/seam_svf_ref.h`.

- [ ] **Step 2: Write the failing test**

`tests/seam_svf_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_svf.h"
#include "ref/seam_svf_ref.h"
#include <algorithm>
#include <cmath>

using Seam::SvfBandpass;

template <int N>
static double impulseRelErr(double fs, double f, double q, const double (*ref)[N]) {
    SvfBandpass b; b.design(fs, f, q);
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < N; ++k) {
        const double y = b.tick(k == 0 ? 1.0 : 0.0);
        e = std::max(e, std::fabs(y - ref[0][k]));
        pk = std::max(pk, std::fabs(ref[0][k]));
    }
    return e / pk;
}

TEST_CASE("equals fi.svf.bp(48, 350) at 96 kHz, impulse response") {
    CHECK(impulseRelErr<2048>(96000.0, 48.0, 350.0, svfref::kSvf48) < 1e-13);
}

TEST_CASE("equals fi.svf.bp(1000, 0.7) at 48 kHz, impulse response") {
    CHECK(impulseRelErr<512>(48000.0, 1000.0, 0.7, svfref::kSvf1k) < 1e-13);
}

TEST_CASE("peak gain is q at the centre") {
    const double fs = 48000.0, f = 1000.0, q = 0.7;
    SvfBandpass b; b.design(fs, f, q);
    double pk = 0.0;
    for (int n = 0; n < (int)fs; ++n) {
        const double y = b.tick(std::sin(2.0 * M_PI * f * n / fs));
        if (n > fs / 2) pk = std::max(pk, std::fabs(y));
    }
    CHECK(pk == doctest::Approx(q).epsilon(1e-4));
}

TEST_CASE("reset empties the state") {
    SvfBandpass b; b.design(96000.0, 48.0, 350.0);
    for (int n = 0; n < 1000; ++n) b.tick(1.0);
    b.reset();
    CHECK(b.tick(0.0) == 0.0);
}
```

Register in `tests/CMakeLists.txt` (same block shape as `seam_basics_test`, name `seam_svf_test`).

Run: `cmake -S . -B build-test && cmake --build build-test --target seam_svf_test`
Expected: compile error, `seam_svf.h` not found.

- [ ] **Step 3: Write `seam_svf.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_svf.h — the C++ side of fi.svf (filters.lib), band-pass
//
// FAUST REFERENCE (filters.lib, standard, Andrew Simper's SVF):
//   svf(T,F,Q,G) = tick ~ (_,_) : !,!,si.dot(3, mix)
//   with { tick(ic1eq, ic2eq, v0) = 2*v1 - ic1eq, 2*v2 - ic2eq, v0, v1, v2
//          with { v1 = ic1eq + g*(v0-ic2eq) : /(1 + g*(g+k));
//                 v2 = ic2eq + g*v1; };
//          g = tan(F*ma.PI/ma.SR); k = 1/Q; };
//   bp(f,q) = svf(1, f, q, 0);          // mix = 0, 1, 0: the output is v1
//
// The bilinear transform, prewarped at f, of H(s) = s/(s^2 + s/q + 1): peak
// gain q at f. Above fs/2 tan turns negative and the filter is unstable; the
// caller keeps f below it (the choir designs at min(fc, 19999) Hz).
// Callers that let it ring into silence wrap process in seam_denormals.h.
// Written 2026-10-02 for SSCDO#2's choir.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class SvfBandpass {
public:
    void design(double fs, double f, double q) {
        g_ = std::tan(f * M_PI / fs);
        k_ = 1.0 / q;
        d_ = 1.0 + g_ * (g_ + k_);
    }

    double tick(double x) {
        const double v1 = (ic1_ + g_ * (x - ic2_)) / d_;
        const double v2 = ic2_ + g_ * v1;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return v1;
    }

    void reset() { ic1_ = ic2_ = 0.0; }

    bool hasSubnormalState() const {
        return std::fpclassify(ic1_) == FP_SUBNORMAL || std::fpclassify(ic2_) == FP_SUBNORMAL;
    }

private:
    double g_ = 0.0, k_ = 1.0, d_ = 1.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

} // namespace Seam
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-test --target seam_svf_test && (cd build-test && ctest -C Debug -R seam_svf --output-on-failure)`
Expected: PASS, 4 test cases. If the impulse cases fail only by rounding (between 1e-13 and 1e-11), read the generated Faust code (`faust -double -lang cpp doc/study/sscdo2/choir-plugin/dsp/svf48.dsp | grep -n fConst`) and match its order of operations (division vs multiplication by a reciprocal); do not loosen the bound.

- [ ] **Step 5: Mutations**

Each, then `ctest -R seam_svf`, then restore: (a) `v1 = ... / d_` → `* d_`: RED; (b) return `v2` instead of `v1` (the low-pass): RED; (c) `k_ = q`: RED on both impulses and on the peak gain.

- [ ] **Step 6: Commit**

```bash
git add plugins/_common/seam_svf.h doc/study/sscdo2/choir-plugin/gen-ref.sh doc/study/sscdo2/choir-plugin/dsp/svf48.dsp doc/study/sscdo2/choir-plugin/dsp/svf1k.dsp tests/ref/seam_svf_ref.h tests/seam_svf_test.cpp tests/CMakeLists.txt
git commit -m "feat(common): seam_svf.h, Seam::SvfBandpass = fi.svf.bp

Simper's SVF band-pass, equal to the Faust impulse response within 1e-13
at 48 Hz Q 350 (96 kHz) and 1 kHz Q 0.7 (48 kHz); peak gain q.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 3: `seam_analyzers.h` — `AmpFollower` (an.amp_follower)

**Files:**
- Create: `plugins/_common/seam_analyzers.h`
- Create: `doc/study/sscdo2/choir-plugin/dsp/follow.dsp`
- Modify: `doc/study/sscdo2/choir-plugin/gen-ref.sh` (append the analyzers block)
- Create: `tests/ref/seam_analyzers_ref.h` (generated)
- Create: `tests/seam_analyzers_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::tau2pole` (Task 1).
- Produces: `class Seam::AmpFollower { void prepare(double fs, double rel); double tick(double x); void reset(); double value() const; bool hasSubnormalState() const; }`.

- [ ] **Step 1: Reference**

`doc/study/sscdo2/choir-plugin/dsp/follow.dsp`:

```faust
// an.amp_follower(1.5) on a 48 Hz sine switched off at 1 s: the reference
// for Seam::AmpFollower. The input is generated here and in the C++ test by
// the same formula.
import("stdfaust.lib");
process = sin(2*ma.PI*48*ba.time/ma.SR) * (ba.time < ma.SR) : an.amp_follower(1.5);
```

Append to `gen-ref.sh`:

```bash
OUT="$ROOT/tests/ref/seam_analyzers_ref.h"
{ banner; echo "namespace analyzersref {"
  render follow.dsp 96000 2048 kFollowStop96 zero 95000
  render follow.dsp 96000 1024 kFollowLate96 zero 191000
  echo "} // namespace analyzersref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
```

Run: `bash doc/study/sscdo2/choir-plugin/gen-ref.sh`
Expected: both headers written.

- [ ] **Step 2: Write the failing test**

`tests/seam_analyzers_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_analyzers.h"
#include "ref/seam_analyzers_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

// The same input as follow.dsp: sin(2*pi*48*n/SR), off from n = SR.
static std::vector<double> followed(double fs, int n) {
    Seam::AmpFollower f; f.prepare(fs, 1.5);
    std::vector<double> y((size_t)n);
    for (int k = 0; k < n; ++k)
        y[(size_t)k] = f.tick(k < (int)fs ? std::sin(2 * M_PI * 48 * k / fs) : 0.0);
    return y;
}

template <int N>
static double relErr(const std::vector<double>& y, int skip, const double (*ref)[N]) {
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < N; ++k) {
        e = std::max(e, std::fabs(y[(size_t)(skip + k)] - ref[0][k]));
        pk = std::max(pk, std::fabs(ref[0][k]));
    }
    return e / pk;
}

TEST_CASE("equals an.amp_follower(1.5) around the stop and a second later") {
    auto y = followed(96000.0, 192000 + 1024);
    CHECK(relErr<2048>(y, 95000, analyzersref::kFollowStop96) < 1e-12);
    CHECK(relErr<1024>(y, 191000, analyzersref::kFollowLate96) < 1e-12);
}

TEST_CASE("the attack is immediate") {
    Seam::AmpFollower f; f.prepare(96000.0, 1.5);
    CHECK(f.tick(-0.7) == 0.7);
}

TEST_CASE("the release is exp(-1/1.5) after one second, at any rate") {
    for (double fs : {48000.0, 96000.0}) {
        Seam::AmpFollower f; f.prepare(fs, 1.5);
        f.tick(1.0);
        for (int k = 0; k < (int)fs; ++k) f.tick(0.0);
        CHECK(f.value() == doctest::Approx(std::exp(-1.0 / 1.5)).epsilon(1e-4));
    }
}
```

Register `seam_analyzers_test` in `tests/CMakeLists.txt` (same block shape).

Run: `cmake -S . -B build-test && cmake --build build-test --target seam_analyzers_test`
Expected: compile error, `seam_analyzers.h` not found.

- [ ] **Step 3: Write `seam_analyzers.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_analyzers.h — the C++ side of analyzers.lib (an)
//
// FAUST REFERENCE (analyzers.lib, standard):
//   amp_follower(rel) = abs : env with {
//       p = ba.tau2pole(rel);
//       env(x) = x * (1.0 - p) : (+ : max(x,_)) ~ *(p);
//   };
//
// e[n] = max(|x[n]|, (1-p)|x[n]| + p e[n-1]): an immediate attack, a
// release of rel seconds at any rate. Callers that let it decay into
// silence wrap process in seam_denormals.h. Written 2026-10-02 for SSCDO#2's
// choir.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_basics.h"
#include <algorithm>
#include <cmath>

namespace Seam {

class AmpFollower {
public:
    void prepare(double fs, double rel) { p_ = tau2pole(rel, fs); }

    double tick(double x) {
        const double a = std::fabs(x);
        y_ = std::max(a, a * (1.0 - p_) + p_ * y_);
        return y_;
    }

    void   reset()       { y_ = 0.0; }
    double value() const { return y_; }
    bool   hasSubnormalState() const { return std::fpclassify(y_) == FP_SUBNORMAL; }

private:
    double p_ = 0.0, y_ = 0.0;
};

} // namespace Seam
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-test --target seam_analyzers_test && (cd build-test && ctest -C Debug -R seam_analyzers --output-on-failure)`
Expected: PASS. The input is computed as `2*M_PI*48*k/fs`; if the reference differs above 1e-12 only, match the Faust expression order from the generated code (`faust -double -lang cpp .../follow.dsp`), do not loosen the bound.

- [ ] **Step 5: Mutations**

(a) `std::max(a, ...)` → just the smoother `a * (1.0 - p_) + p_ * y_`: RED (attack and reference); (b) `tau2pole(rel, fs)` → `tau2pole(rel / 2, fs)`: RED (release and reference). Restore each.

- [ ] **Step 6: Commit**

```bash
git add plugins/_common/seam_analyzers.h doc/study/sscdo2/choir-plugin/dsp/follow.dsp doc/study/sscdo2/choir-plugin/gen-ref.sh tests/ref/seam_analyzers_ref.h tests/seam_analyzers_test.cpp tests/CMakeLists.txt
git commit -m "feat(common): seam_analyzers.h, Seam::AmpFollower = an.amp_follower

Immediate attack, release in seconds; equal to Faust within 1e-12 around
the stop and a second later.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 4: The engine — `choir_dsp.h`, `choir_display.h`

**Files:**
- Create: `plugins/choir/source/choir_display.h`
- Create: `plugins/choir/source/choir_dsp.h`
- Create: `doc/study/sscdo2/choir-plugin/dsp/choir.dsp`
- Modify: `doc/study/sscdo2/choir-plugin/gen-ref.sh` (append the choir block)
- Create: `tests/ref/choir_ref.h` (generated)
- Create: `tests/choir_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::SvfBandpass` (Task 2), `Seam::AmpFollower` (Task 3), `Seam::MultinoiseBlock(int m, int o, int n)` with `tick(double*)`, `reset()` (`seam_noise.h`), `Seam::LinearRamp` (`setTarget(target, seconds, fs)`, `snap()`, `next()`, `value()`, `target()`), `Seam::ScopedNoDenormals`.
- Produces (namespace `choir`):
  - `constexpr int kChannels = 4, kBands = 16;`
  - `struct Config { std::array<double,4> f{{48,48,96,96}}; std::array<double,4> a{{1.0,1.01,1.1,0.9}}; double q = 350.0; double release = 1.5; };`
  - `class Display { void store(int c, int k, double v); float load(int c, int k) const; }` (relaxed atomics; −1 marks an inactive band).
  - `class Engine { explicit Engine(Config = Config()); void prepare(double fs); void reset(); void setOutput(double); void setPower(bool); void requestReset(); template<class T> void process(const T* const* in, T* const* out, int n); bool prepared() const; double sampleRate() const; double gain() const; bool active(int c, int k) const; bool hasSubnormalState() const; const Display& display() const; const Config& config() const; }`.

- [ ] **Step 1: Reference**

`doc/study/sscdo2/choir-plugin/dsp/choir.dsp`:

```faust
// sdt.choir(350, 1.5) on four generated inputs: channel c hears 16 sines at
// f_c*k, 0.05 each. The C++ test generates the same inputs by the same formula.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
src(c) = sum(k, 16, 0.05 * sin(2*ma.PI*sdt.choirf(c)*(k+1)*ba.time/ma.SR));
process = par(c, 4, src(c)) : sdt.choir(350, 1.5);
```

Append to `gen-ref.sh`:

```bash
OUT="$ROOT/tests/ref/choir_ref.h"
{ banner; echo "namespace choirref {"
  render choir.dsp 96000 2048 kChoir96 zero 288000
  render choir.dsp 48000 1024 kChoir48 zero 144000
  echo "} // namespace choirref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
```

Run: `bash doc/study/sscdo2/choir-plugin/gen-ref.sh`
Expected: three headers written; `choir_ref.h` about 300 KB.

- [ ] **Step 2: Write the failing tests**

`tests/choir_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_dsp.h"
#include "ref/choir_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using choir::Engine;
using choir::Config;
using Buf = std::vector<std::vector<double>>;

// The inputs of choir.dsp: channel c, 16 sines at f_c*(k+1), 0.05 each.
struct Source {
    Config cfg; double fs; long n = 0;
    double sample(int c, long t) const {
        double x = 0.0;
        for (int k = 0; k < 16; ++k) x += 0.05 * std::sin(2 * M_PI * cfg.f[(size_t)c] * (k + 1) * (double)t / fs);
        return x;
    }
    void fill(Buf& in, int m) {
        for (int c = 0; c < 4; ++c) for (int i = 0; i < m; ++i) in[(size_t)c][(size_t)i] = sample(c, n + i);
        n += m;
    }
};

// Engine holds atomics: neither copyable nor movable, so it is built in
// place and settled by reference.
static void settle(Engine& e, double fs) {
    e.prepare(fs);
    e.setOutput(1.0); e.setPower(true);
    e.reset();
}

// Runs n samples of src through e in blocks; returns the last `keep` samples.
template <class T = double>
static Buf run(Engine& e, Source& src, long n, int keep, int block = 512) {
    Buf in(4, std::vector<double>((size_t)block)), out(4, std::vector<double>((size_t)block));
    std::vector<std::vector<T>> ti(4, std::vector<T>((size_t)block)), to(4, std::vector<T>((size_t)block));
    Buf kept(4);
    for (long pos = 0; pos < n; pos += block) {
        const int m = (int)std::min<long>(block, n - pos);
        src.fill(in, m);
        const T* ip[4]; T* op[4];
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < m; ++i) ti[(size_t)c][(size_t)i] = (T)in[(size_t)c][(size_t)i];
            ip[c] = ti[(size_t)c].data(); op[c] = to[(size_t)c].data();
        }
        e.process(ip, op, m);
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < m; ++i)
                if (pos + i >= n - keep) kept[(size_t)c].push_back((double)to[(size_t)c][(size_t)i]);
    }
    return kept;
}

template <int L>
static double relErr(const Buf& y, const double (*ref)[L]) {
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < L; ++k) {
            e  = std::max(e, std::fabs(y[(size_t)c][(size_t)k] - ref[c][k]));
            pk = std::max(pk, std::fabs(ref[c][k]));
        }
    return e / pk;
}

TEST_CASE("equals sdt.choir(350, 1.5) at 96 kHz") {
    Engine e; settle(e, 96000.0);
    Source s{Config(), 96000.0};
    auto y = run(e, s, 288000 + 2048, 2048);
    CHECK(relErr<2048>(y, choirref::kChoir96) < 1e-12);
}

TEST_CASE("equals sdt.choir(350, 1.5) at 48 kHz, choirdens included") {
    Engine e; settle(e, 48000.0);
    Source s{Config(), 48000.0};
    auto y = run(e, s, 144000 + 1024, 1024);
    CHECK(relErr<1024>(y, choirref::kChoir48) < 1e-12);
}

TEST_CASE("the block size does not change the output") {
    Engine a, b, c; settle(a, 96000.0); settle(b, 96000.0); settle(c, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0}, sc{Config(), 96000.0};
    auto ya = run(a, sa, 20000, 4096, 512);
    auto yb = run(b, sb, 20000, 4096, 1);
    auto yc = run(c, sc, 20000, 4096, 4096);
    for (int ch = 0; ch < 4; ++ch) {
        CHECK(ya[(size_t)ch] == yb[(size_t)ch]);
        CHECK(ya[(size_t)ch] == yc[(size_t)ch]);
    }
}

TEST_CASE("a float bus equals a double bus to float precision") {
    Engine a, b; settle(a, 96000.0); settle(b, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0};
    auto yd = run<double>(a, sa, 96000, 2048);
    auto yf = run<float>(b, sb, 96000, 2048);
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (size_t k = 0; k < yd[(size_t)c].size(); ++k) {
            e = std::max(e, std::fabs(yd[(size_t)c][k] - yf[(size_t)c][k]));
            pk = std::max(pk, std::fabs(yd[(size_t)c][k]));
        }
    CHECK(e / pk < 1e-5);
}

TEST_CASE("an unprepared engine writes silence") {
    Engine e;
    std::vector<double> z(64, 1.0), o(64, 1.0);
    const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
    double* out[4] = { o.data(), o.data(), o.data(), o.data() };
    e.process(in, out, 64);
    for (double v : o) CHECK(v == 0.0);
}

TEST_CASE("RESET silences the ringing bands; the request survives a stopped host") {
    Engine e; settle(e, 96000.0);
    Source s{Config(), 96000.0};
    run(e, s, 192000, 1);
    // ringing: with zero input the choir still sings for seconds
    Buf in(4, std::vector<double>(512, 0.0)), out(4, std::vector<double>(512, 0.0));
    const double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = in[(size_t)c].data(); op[c] = out[(size_t)c].data(); }
    e.process(ip, op, 512);
    double ring = 0.0;
    for (auto& ch : out) for (double v : ch) ring = std::max(ring, std::fabs(v));
    CHECK(ring > 0.0);
    e.requestReset();                       // the host is stopped: no process here
    for (int b = 0; b < 20; ++b) {
        e.process(ip, op, 512);
        for (auto& ch : out) for (double v : ch) REQUIRE(v == 0.0);
    }
}

TEST_CASE("bands at or above 20 kHz are inactive, and the engine stays finite") {
    Config cfg; cfg.f = {{5000, 5000, 5000, 5000}}; cfg.a = {{1, 1, 1, 1}};
    Engine e(cfg); settle(e, 48000.0);
    for (int k = 0; k < 16; ++k) CHECK(e.active(0, k) == (5000.0 * (k + 1) < 20000.0));
    Source s{cfg, 48000.0};
    auto y = run(e, s, 20 * 48000, 4096);
    for (auto& ch : y) for (double v : ch) REQUIRE(std::isfinite(v));
    CHECK(e.display().load(0, 15) == -1.0f);
}

TEST_CASE("sixty seconds of silence leave no subnormal state") {
    Config fast; fast.q = 5.0; fast.release = 0.01;     // decays to the subnormal range within the run
    Engine e(fast); settle(e, 8000.0);
    Source s{fast, 8000.0};
    run(e, s, 8000, 1);
    Buf in(4, std::vector<double>(512, 0.0)), out(4, std::vector<double>(512, 0.0));
    const double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = in[(size_t)c].data(); op[c] = out[(size_t)c].data(); }
    for (int b = 0; b < 60 * 8000 / 512; ++b) e.process(ip, op, 512);
    CHECK_FALSE(e.hasSubnormalState());
}

TEST_CASE("output and POWER reach their targets in 25 ms at every rate") {
    for (double fs : {48000.0, 96000.0}) {
        Engine e; e.prepare(fs); e.reset();               // gain 0
        e.setOutput(1.0); e.setPower(true);
        const int n = (int)std::lround(0.025 * fs);
        std::vector<double> z((size_t)n, 0.0), o((size_t)n, 0.0);
        const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
        double* out[4] = { o.data(), o.data(), o.data(), o.data() };
        e.process(in, out, n - 1);
        CHECK(e.gain() < 1.0);
        e.process(in, out, 1);
        CHECK(e.gain() == 1.0);
    }
}

TEST_CASE("the display reads a partial's amplitude in its band") {
    Engine e; settle(e, 96000.0);
    // channel 0 hears one sine of amplitude 0.1 at 3*48 Hz = band k = 2
    struct One { long n = 0; void fill(Buf& in, int m) {
        for (int c = 0; c < 4; ++c) for (int i = 0; i < m; ++i)
            in[(size_t)c][(size_t)i] = c == 0 ? 0.1 * std::sin(2 * M_PI * 144.0 * (double)(n + i) / 96000.0) : 0.0;
        n += m; } } src;
    Buf in(4, std::vector<double>(512)), out(4, std::vector<double>(512));
    const double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = in[(size_t)c].data(); op[c] = out[(size_t)c].data(); }
    for (int b = 0; b < 12 * 96000 / 512; ++b) { src.fill(in, 512); e.process(ip, op, 512); }
    CHECK(20 * std::log10(e.display().load(0, 2)) == doctest::Approx(-20.0).epsilon(0.005));
    for (int k = 0; k < 16; ++k) if (k != 2) CHECK(20 * std::log10(std::max(1e-12f, e.display().load(0, k))) < -40.0);
}

TEST_CASE("a new rate redesigns everything: equal to a fresh engine") {
    Engine a; settle(a, 48000.0);
    Source s48{Config(), 48000.0};
    run(a, s48, 48000, 1);
    a.prepare(96000.0); a.setOutput(1.0); a.setPower(true); a.reset();
    Engine b; settle(b, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0};
    auto ya = run(a, sa, 96000, 1024), yb = run(b, sb, 96000, 1024);
    for (int c = 0; c < 4; ++c) CHECK(ya[(size_t)c] == yb[(size_t)c]);
}
```

Register `choir_dsp_test` in `tests/CMakeLists.txt` with include dirs `${CMAKE_CURRENT_SOURCE_DIR}`, `../plugins/choir/source`, `../plugins/_common` (as `lmo_dsp_test`).

Run: `cmake -S . -B build-test && cmake --build build-test --target choir_dsp_test`
Expected: compile error, `choir_dsp.h` not found.

- [ ] **Step 3: Write `choir_display.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the grid's data between the threads (SDK-free)
//
// 64 independent relaxed atomics, one per band: the audio thread writes
// the block peak of each envelope divided by Q (the amplitude of the
// input's partial in that band), the GUI reads them at 30 Hz. A bar grid
// has no invariant across bands, so each value is valid on its own and no
// snapshot is needed (spec 2026-10-02). -1 marks an inactive band.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <atomic>

namespace choir {

class Display {
public:
    Display() { for (auto& v : v_) v.store(0.0f, std::memory_order_relaxed); }
    void  store(int c, int k, double v) { v_[c * 16 + k].store((float)v, std::memory_order_relaxed); }
    float load(int c, int k) const      { return v_[c * 16 + k].load(std::memory_order_relaxed); }

private:
    std::atomic<float> v_[64];
};

} // namespace choir
```

- [ ] **Step 4: Write `choir_dsp.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the engine (SDK-free)
//
// sdt.choir(350, 1.5) of seam.tedesco.lib on four channels. Channel c, band
// k = 0..15: the input through a band at f_c*(k+1) and a follower (what the
// choir hears), times noise stream 16c+k of the SSCDO#2 noise's block 3
// through a band at f_c*(k+1)^a_c, held at its 96 kHz level (the voice);
// the 16 products summed and divided by Q*2*pi. A band centred at 20 kHz or
// above is inactive, every band is designed at min(fc, 19999) Hz. No DC
// blocker. This file only wires the _common blocks.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_display.h"
#include "seam_analyzers.h"
#include "seam_denormals.h"
#include "seam_noise.h"
#include "seam_ramp.h"
#include "seam_svf.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace choir {

constexpr int    kChannels    = 4;
constexpr int    kBands       = 16;
constexpr int    kNoiseStreams = 72;          // sdt.sscdo2streams(4): LMO's 8 + the choir's 64
constexpr int    kNoiseOffset  = 8;           // block 3 starts after LMO's two blocks
constexpr double kRefRate     = 96000.0;      // SSCDO#2 is played at 96 kHz
constexpr double kSilentFrom  = 20000.0;      // Hz, sdt.choirband
constexpr double kDesignCeil  = 19999.0;      // Hz
constexpr double kShortRamp   = 0.025;        // s

struct Config {
    std::array<double, kChannels> f{{48.0, 48.0, 96.0, 96.0}};      // sdt.choirf
    std::array<double, kChannels> a{{1.0, 1.01, 1.1, 0.9}};         // sdt.choira
    double q = 350.0;
    double release = 1.5;                                           // s
};

class Engine {
public:
    explicit Engine(Config cfg = Config())
        : cfg_(cfg), noise_(kNoiseStreams, kNoiseOffset, kChannels * kBands) {}

    // Outside the audio thread (setActive): designs every band, zeroes every
    // state, rewinds the noise to the generator's start.
    void prepare(double fs) {
        fs_ = fs;
        dens_ = std::sqrt(fs / kRefRate);
        qnorm_ = cfg_.q * 2.0 * M_PI;
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k) {
                const double fl = cfg_.f[(size_t)c] * (k + 1);
                const double fv = cfg_.f[(size_t)c] * std::pow(k + 1.0, cfg_.a[(size_t)c]);
                listen_[c][k].design(fs, std::min(fl, kDesignCeil), cfg_.q);
                sing_[c][k].design(fs, std::min(fv, kDesignCeil), cfg_.q);
                follow_[c][k].prepare(fs, cfg_.release);
                active_[c][k] = fl < kSilentFrom && fv < kSilentFrom;
                display_.store(c, k, active_[c][k] ? 0.0 : -1.0);
            }
        clearStates();
        noise_.reset();
        served_ = resetGen_.load();
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        sampleRate_.store(fs);
        prepared_ = true;
    }

    void release() { prepared_ = false; sampleRate_.store(0.0); }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { out_.snap(); pow_.snap(); }

    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // GUI thread: served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!prepared_) {
            for (int c = 0; c < kChannels; ++c) std::fill(out[c], out[c] + n, (T)0);
            return;
        }
        // 128 bands and 64 followers decay in silence: no subnormals.
        Seam::ScopedNoDenormals noDenormals;
        const uint32_t g = resetGen_.load();
        if (g != served_) { clearStates(); served_ = g; }

        double peak[kChannels][kBands] = {};
        double nz[kChannels * kBands];
        for (int i = 0; i < n; ++i) {
            const double gain = out_.next() * pow_.next();
            noise_.tick(nz);
            for (int c = 0; c < kChannels; ++c) {
                const double x = (double)in[c][i];
                double acc = 0.0;
                for (int k = 0; k < kBands; ++k) {
                    if (!active_[c][k]) continue;
                    const double e = follow_[c][k].tick(listen_[c][k].tick(x));
                    const double v = sing_[c][k].tick(nz[c * kBands + k]) * dens_;
                    acc += e * v;
                    peak[c][k] = std::max(peak[c][k], e);
                }
                out[c][i] = (T)(acc / qnorm_ * gain);
            }
        }
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k)
                if (active_[c][k]) display_.store(c, k, peak[c][k] / cfg_.q);
    }

    bool    prepared() const        { return prepared_; }
    double  sampleRate() const      { return sampleRate_.load(); }
    double  gain() const            { return out_.value() * pow_.value(); }
    bool    active(int c, int k) const { return active_[c][k]; }
    const Display& display() const  { return display_; }
    const Config&  config() const   { return cfg_; }

    bool hasSubnormalState() const {
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k)
                if (listen_[c][k].hasSubnormalState() || sing_[c][k].hasSubnormalState() ||
                    follow_[c][k].hasSubnormalState()) return true;
        return false;
    }

private:
    void clearStates() {
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k) {
                listen_[c][k].reset(); sing_[c][k].reset(); follow_[c][k].reset();
            }
    }

    Config cfg_;
    double fs_ = kRefRate, dens_ = 1.0, qnorm_ = 350.0 * 2.0 * M_PI;
    bool   prepared_ = false;
    Seam::SvfBandpass listen_[kChannels][kBands], sing_[kChannels][kBands];
    Seam::AmpFollower follow_[kChannels][kBands];
    bool   active_[kChannels][kBands] = {};
    Seam::MultinoiseBlock noise_;
    Seam::LinearRamp out_, pow_;
    Display display_;
    std::atomic<uint32_t> resetGen_{0};
    uint32_t served_ = 0;
    std::atomic<double> sampleRate_{0.0};
};

} // namespace choir
```

Note on the Faust equivalence: in `sdt.choirchan` the product is `listen * sing` with `sing = band : *(choirdens)`, summed by `:>` from band 0 to 15, then `/(q*2*ma.PI)`; the loop above keeps that order (`acc` starts at 0.0, `0.0 + p0 == p0` exactly). Inactive bands contribute exactly 0 in Faust (`band * 0` folded), so skipping them is exact.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build-test --target choir_dsp_test && (cd build-test && ctest -C Debug -R choir_dsp --output-on-failure)`
Expected: PASS, 11 test cases. If the two reference cases fail by more than rounding, compare one channel band by band against a Faust probe (as `doc/study/sscdo2/choir-chain/spec.dsp` does) before touching the bound; if they fail only by input rounding (`sin` argument order), match the expression order from `faust -double -lang cpp .../choir.dsp`.

- [ ] **Step 6: Mutations** (record each in Task 7's `mutations.md`)

Each: edit, rebuild `choir_dsp_test`, run, expect RED, restore.
1. Noise stream `nz[c * kBands + k]` → `nz[((c + 1) % kChannels) * kBands + k]`: RED (96 and 48 kHz).
2. `* dens_` removed: RED at 48 kHz only (96 kHz stays green: `dens_` is 1 there).
3. `* dens_ * dens_`: RED at 48 kHz.
4. Listen centre `fl = f*(k+1)` → `f*pow(k+1, a)`: RED.
5. `follow_[c][k].prepare(fs, cfg_.release)` → `prepare(fs, cfg_.release / 2)`: RED.
6. `if (!active_[c][k]) continue;` removed and `std::min(.., kDesignCeil)` removed: RED in "bands at or above 20 kHz" (non-finite).
7. `clearStates()` in the reset branch replaced by `listen_`/`sing_` resets only (followers kept): RED in the RESET test.
8. Display `peak[c][k] / cfg_.q` → `peak[c][k]`: RED in the display test.
9. `ScopedNoDenormals` removed: RED in the subnormal test (on the machine that builds the suite; if it stays green, record it as not observable there and why).
10. `served_ = resetGen_.load()` in `prepare` removed and the reset served before `prepare`: not a mutation to run; instead check by reading that `requestReset()` before `process` is served in the first block (the RESET test covers it).

- [ ] **Step 7: Commit**

```bash
git add plugins/choir/source/choir_display.h plugins/choir/source/choir_dsp.h doc/study/sscdo2/choir-plugin/dsp/choir.dsp doc/study/sscdo2/choir-plugin/gen-ref.sh tests/ref/choir_ref.h tests/choir_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(choir): the engine, equal to sdt.choir at 96 and 48 kHz

128 SVF bands, 64 followers, block 3 of the SSCDO#2 noise; inactive
bands from 20 kHz, GUI RESET by generation counter, display per block.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 5: Parameters and state — `choir_params.h`, `choir_state.h`

**Files:**
- Create: `plugins/choir/source/choir_params.h`, `plugins/choir/source/choir_state.h`
- Create: `tests/choir_params_test.cpp`, `tests/choir_state_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `choir::Engine::setOutput`, `setPower`, `gain` (Task 4); `Seam::readStateDoubles(IBStream*, double*, int)` (`seam_state.h`).
- Produces: `enum class choir::Param : int { Power = 0, Output }; constexpr int kNumParams = 2; double defaultNormalized(Param); struct Plain { bool power; double output; }; class ParamBox { store, normalized, plain }; void applyTo(const Plain&, Engine&);` and `void writeState(IBStream*, const ParamBox&); int readState(IBStream*, ParamBox&);`.

- [ ] **Step 1: Write the failing tests**

`tests/choir_params_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_params.h"

using namespace choir;

TEST_CASE("defaults: POWER off, output 0") {
    ParamBox b;
    CHECK_FALSE(b.plain().power);
    CHECK(b.plain().output == 0.0);
}

TEST_CASE("plain values from normalized ones") {
    ParamBox b;
    b.store(Param::Power, 0.6); b.store(Param::Output, 0.25);
    CHECK(b.plain().power);
    CHECK(b.plain().output == 0.25);
    b.store(Param::Output, 1.7);
    CHECK(b.plain().output == 1.0);
}

TEST_CASE("applyTo sets the engine's targets") {
    Engine e; e.prepare(96000.0); e.reset();
    ParamBox b; b.store(Param::Power, 1.0); b.store(Param::Output, 0.5);
    applyTo(b.plain(), e);
    e.reset();
    CHECK(e.gain() == 0.5);
}
```

`tests/choir_state_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace choir;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the state round-trips POWER and output") {
    ParamBox a; a.store(Param::Power, 1.0); a.store(Param::Output, 0.8);
    MemoryStream stream; writeState(&stream, a); rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kNumParams);
    CHECK(b.normalized(Param::Power) == 1.0);
    CHECK(b.normalized(Param::Output) == 0.8);
}

TEST_CASE("a short blob keeps the default output") {
    MemoryStream stream;
    { Steinberg::IBStreamer w(&stream, kLittleEndian); w.writeDouble(1.0); }
    rewindStream(stream);
    ParamBox b; b.store(Param::Output, 0.9);
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(b.normalized(Param::Output) == defaultNormalized(Param::Output));
}

TEST_CASE("the on-disk order is Power, Output, and nothing else") {
    ParamBox a; a.store(Param::Power, 1.0); a.store(Param::Output, 0.75);
    MemoryStream stream; writeState(&stream, a);
    Steinberg::int64 end = 0; stream.tell(&end);
    CHECK(end == 2 * (Steinberg::int64)sizeof(double));      // RESET is not state
    rewindStream(stream);
    Steinberg::IBStreamer r(&stream, kLittleEndian);
    double p = 0, o = 0; r.readDouble(p); r.readDouble(o);
    CHECK(p == 1.0); CHECK(o == 0.75);
}
```

Register `choir_params_test` like `delrm_params_test` (include `../plugins/choir/source`), and `choir_state_test` exactly like `delrm_state_test` (memorystream.cpp source, SDK include, `base sdk_common pluginterfaces`, CoreFoundation on Apple).

Run: `cmake -S . -B build-test && cmake --build build-test --target choir_params_test choir_state_test`
Expected: compile errors, headers not found.

- [ ] **Step 2: Write `choir_params.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the parameters between the threads (SDK-free)
//
// POWER and output as normalized values in atomics, as in LMO, stunedrev
// and delRM: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. RESET is not a
// parameter (choir_views.h).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_dsp.h"
#include <algorithm>
#include <atomic>

namespace choir {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Output };
constexpr int kNumParams = 2;

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double defaultNormalized(Param) { return 0.0; }

struct Plain { bool power; double output; };

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }
    Plain plain() const { return { normalized(Param::Power) >= 0.5, clamp01(normalized(Param::Output)) }; }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace choir
```

- [ ] **Step 3: Write `choir_state.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the processor's state (SDK)
//
// Two normalized doubles in Param order, little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. RESET and the grid are not state.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace choir {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace choir
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build-test --target choir_params_test choir_state_test && (cd build-test && ctest -C Debug -R "choir_params|choir_state" --output-on-failure)`
Expected: PASS.

- [ ] **Step 5: Mutations**

(a) in `plain()`, read Output from `Param::Power` and Power from `Param::Output`: RED in "plain values"; (b) `readState` without restoring defaults for missing fields (`v[i]` uninitialised → set to 0.9 in the mutation): RED in "short blob". Restore each.

- [ ] **Step 6: Commit**

```bash
git add plugins/choir/source/choir_params.h plugins/choir/source/choir_state.h tests/choir_params_test.cpp tests/choir_state_test.cpp tests/CMakeLists.txt
git commit -m "feat(choir): parameters and state, POWER and output

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 6: The plugin — processor, views, window, build

**Files:**
- Create: `plugins/choir/CMakeLists.txt`, `plugins/choir/source/choir_ids.h`, `plugins/choir/source/version.h`, `plugins/choir/source/choir_processor.h`, `plugins/choir/source/choir_processor.cpp`, `plugins/choir/source/choir_views.h`, `plugins/choir/resource/choir.uidesc`
- Modify: `CMakeLists.txt` (root: `add_subdirectory(plugins/choir)` after `plugins/delrm`)

**Interfaces:**
- Consumes: everything in Tasks 4–5; `choir::Engine::display()`, `active()`, `config()`, `sampleRate()`, `requestReset()`.
- Produces: the `choir.vst3` bundle.

- [ ] **Step 1: `choir_ids.h`, `version.h`, `CMakeLists.txt`**

`plugins/choir/source/choir_ids.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 20th plugin in the suite. word3 = ASCII "CHR\0".
static const Steinberg::FUID ChoirProcessorUID (0x5E4D0013, 0xA1B2C3D4, 0x43485200, 0x00000013);

enum ChoirParams : Steinberg::Vst::ParamID {
    kParamPower  = 100,   // off / on   (100 + choir::Param index)
    kParamOutput = 101    // linear, CC86 in the patch
};

} // namespace Seam
```

`plugins/choir/source/version.h`: copy `plugins/delrm/source/version.h` and replace the header line with `// SEAM-LTM · choir — Version and metadata`, `stringOriginalFilename` with `"choir.vst3"`, and both `stringFileDescription` values with `"SEAM CHOIR – SSCDO#2 choir (64Bit)"` and `"SEAM CHOIR – SSCDO#2 choir"`.

`plugins/choir/CMakeLists.txt`: copy `plugins/delrm/CMakeLists.txt` and replace `delrm` with `choir` everywhere, `DESCRIPTION "SEAM CHOIR – SSCDO#2 choir"`, the source list with:

```cmake
set(choir_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/choir_ids.h
    source/choir_dsp.h
    source/choir_display.h
    source/choir_params.h
    source/choir_state.h
    source/choir_views.h
    source/choir_processor.cpp
    source/choir_processor.h
    source/version.h
    resource/choir.uidesc
)
```

and `BUNDLE_IDENTIFIER "io.github.s-e-a-m.choir"`.

Root `CMakeLists.txt`: after `add_subdirectory(plugins/delrm)` add `add_subdirectory(plugins/choir)`.

- [ ] **Step 2: `choir_views.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the two views the uidesc cannot describe
//
// RESET is not a parameter (a momentary parameter is lost when the host
// coalesces 0->1->0; ltglide, Reaper): the view reaches the engine directly
// and raises its generation counter, served at the next block. The square
// is filled while pressed.
//
// ChoirGrid: four rows (channels) of 16 bars (bands). Each bar is the block
// peak of the band's envelope divided by Q, the amplitude of the input's
// partial there, in dBFS from -80 to 0, read from 64 relaxed atomics by a
// 30 Hz timer. Row labels carry f and a; the last line carries Q, the
// release, the noise block and the rate. An inactive band is an empty
// grey frame.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_dsp.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Seam {

class ChoirResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    ChoirResetButton(const VSTGUI::CRect& size, choir::Engine* engine,
                     const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                     const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {}

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const CRect vs = getViewSize();
        CRect r(0, 0, kBoxPx, kBoxPx);
        r.offset(vs.left + 1.0, vs.top + std::ceil((vs.getHeight() - kBoxPx) / 2.0));
        c->setDrawMode(kAntiAliasing);
        c->setFrameColor(frame_);
        c->setFillColor(pressed_ ? active_ : idle_);
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawFilledAndStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState& b) override {
        if (!b.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
        pressed_ = true; invalid();
        return VSTGUI::kMouseEventHandled;
    }
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState&) override {
        if (!pressed_) return VSTGUI::kMouseEventNotHandled;
        pressed_ = false;
        if (engine_ && getViewSize().pointInside(where)) engine_->requestReset();
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

private:
    choir::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    bool pressed_ = false;
};

class ChoirGrid : public VSTGUI::CView {
public:
    static constexpr double kLabelW = 110.0, kRowH = 32.0, kAxisH = 14.0, kLineH = 16.0;
    static constexpr double kFloorDb = -80.0;

    ChoirGrid(const VSTGUI::CRect& size, const choir::Engine* engine, VSTGUI::CFontRef font,
              const VSTGUI::CColor& text, const VSTGUI::CColor& track,
              const VSTGUI::CColor& fill, const VSTGUI::CColor& structure)
        : CView(size), engine_(engine), font_(font),
          text_(text), track_(track), fill_(fill), structure_(structure) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 33, true);
    }
    ~ChoirGrid() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const CRect vs = getViewSize();
        const choir::Config& cfg = engine_->config();
        const double pitch = (vs.getWidth() - kLabelW) / choir::kBands;
        const double barW = std::floor(pitch * 0.66);
        c->setFont(font_);
        c->setFontColor(text_);
        c->setDrawMode(kAntiAliasing);
        c->setLineWidth(1.0);
        char s[96];
        for (int ch = 0; ch < choir::kChannels; ++ch) {
            const double top = vs.top + ch * kRowH;
            std::snprintf(s, sizeof s, "%d  %g Hz  a %g", ch + 1, cfg.f[(size_t)ch], cfg.a[(size_t)ch]);
            c->drawString(s, CRect(vs.left, top, vs.left + kLabelW, top + kRowH - 4), kLeftText);
            for (int k = 0; k < choir::kBands; ++k) {
                const double x = vs.left + kLabelW + k * pitch;
                const CRect slot(x, top + 3, x + barW, top + kRowH - 3);
                const float v = engine_->display().load(ch, k);
                if (v < 0.0f) {                                   // inactive band
                    c->setFrameColor(structure_);
                    c->drawRect(slot, kDrawStroked);
                    continue;
                }
                c->setFillColor(track_);
                c->drawRect(slot, kDrawFilled);
                const double db = 20.0 * std::log10(std::max(1e-12, (double)v));
                const double frac = std::min(1.0, std::max(0.0, (db - kFloorDb) / -kFloorDb));
                if (frac > 0.0) {
                    CRect bar = slot;
                    bar.top = slot.bottom - frac * slot.getHeight();
                    c->setFillColor(fill_);
                    c->drawRect(bar, kDrawFilled);
                }
            }
        }
        const double axisTop = vs.top + choir::kChannels * kRowH;
        for (int k = 0; k < choir::kBands; k += 1) {
            if (k != 0 && k != 3 && k != 7 && k != 11 && k != 15) continue;
            const double x = vs.left + kLabelW + k * pitch;
            std::snprintf(s, sizeof s, "%d", k + 1);
            c->drawString(s, CRect(x - 4, axisTop, x + barW + 4, axisTop + kAxisH), kCenterText);
        }
        const double fs = engine_->sampleRate();
        if (fs > 0.0)
            std::snprintf(s, sizeof s, "Q %g \xC2\xB7 release %g s \xC2\xB7 noise block 3 \xC2\xB7 @ %g kHz",
                          cfg.q, cfg.release, fs / 1000.0);
        else
            std::snprintf(s, sizeof s, "Q %g \xC2\xB7 release %g s \xC2\xB7 noise block 3 \xC2\xB7 inactive",
                          cfg.q, cfg.release);
        c->drawString(s, CRect(vs.left, axisTop + kAxisH + 2, vs.right, axisTop + kAxisH + 2 + kLineH), kLeftText);
        setDirty(false);
    }

private:
    const choir::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor text_, track_, fill_, structure_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
```

- [ ] **Step 3: `choir_processor.h` and `choir_processor.cpp`**

`choir_processor.h`: copy `plugins/delrm/source/delrm_processor.h`, rename the class to `ChoirProcessor`, include `choir_params.h`, drop `publishMeters`, keep `createCustomView`, members `choir::Engine engine_; choir::ParamBox box_;`. Replace the header comment with:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The choir (pitchDetectorChoirMcAdams, four instances): on each channel 16
// bands listen to the TETRAREC A around f*k and make 16 voices of noise sing
// around f*k^a, each as loud as its band of the input.
//
// FAUST REFERENCE (seam.tedesco.lib, seam.noises.lib, filters.lib,
// analyzers.lib):
//
//   choirnoise(N)  = sno.multinoiseblock(N*(2 + 16), 2*N, N*16);
//   choirdens      = sqrt(ma.SR/96000);
//   choirband(fc,q) = fi.svf.bp(min(fc, 19999), q) : *(fc < 20000);
//   choirchan(f,a,q,rel) = (listen, sing) : ro.interleave(16, 2)
//                        : par(k, 16, *) :> /(q*2*ma.PI)
//   with { listen = _ <: par(k, 16, choirband(f*(k+1), q) : an.amp_follower(rel));
//          sing   = par(k, 16, choirband(f*pow(k+1, a), q) : *(choirdens)); };
//   choir(q,rel)   = si.bus(4), choirnoise(4) : (route) :
//                    par(c, 4, choirchan(choirf(c), choira(c), q, rel));
//   choirf = 48, 48, 96, 96;  choira = 1, 1.01, 1.1, 0.9;  q = 350;  rel = 1.5
//
// Re-implemented by hand (seam-ltm convention) in choir_dsp.h on the
// reusable libraries of _common: seam_svf.h (fi.svf.bp), seam_analyzers.h
// (an.amp_follower), seam_basics.h (ba.tau2pole), seam_noise.h
// (sno.multinoiseblock), seam_ramp.h, seam_denormals.h.
//
// SR rule of the SSCDO#2 port: centres in Hz, follower in seconds, voices
// at their 96 kHz level (choirdens): the choir sounds at 48 kHz as at 96.
//
// Against the original (Giuseppe, 2026-10-02): no DC blocker, voices at
// their 96 kHz level, bands from 20 kHz silent, the noise is block 3 of the
// SSCDO#2 noise (one stream per band, decorrelated from LMO and between
// channels); f, a, Q and the release are the performance's constants,
// shown in the window; POWER, output (CC86), RESET, the 4x16 grid.
//
// Studies and decisions: doc/study/sscdo2/ (choir-*), logs/2026-10-02-sscdo2-choir.md.
//──────────────────────────────────────────────────────────────────────────
```

`choir_processor.cpp`: copy `plugins/delrm/source/delrm_processor.cpp` and change:
- includes: `choir_processor.h`, `choir_ids.h`, `choir_state.h`, `choir_views.h`, `version.h` (drop `seam_meter.h`);
- `idOf(choir::Param p)`; delete `ReductionParameter`;
- `initialize`: audio buses as delRM (comment: "Four channels: channel c listens to input c, TETRAREC A, patch inputs 5–8"); parameters only:

```cpp
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);
```

- `setActive`: `engine_.prepare(sampleRate()); choir::applyTo(box_.plain(), engine_); engine_.reset();` on true; `engine_.release();` on false;
- `process`: the queue loop with the id range `kParamPower..kParamOutput` and `box_.store((choir::Param)(id - kParamPower), v)`, then `choir::applyTo`, then the same bus handling with `choir::kChannels`; no `publishMeters`;
- `setState`/`getState` with `choir::readState`/`writeState` and `choir::kNumParams`;
- `createView`: `"choir.uidesc"`;
- `createCustomView`:

```cpp
VSTGUI::CView* PLUGIN_API ChoirProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name) return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor, structure = VSTGUI::kGreyCColor,
                   bg = VSTGUI::kBlackCColor, track = VSTGUI::kGreyCColor,
                   azure(0x4a, 0x9e, 0xc8, 0xff), meter(0xc8, 0xa2, 0x4a, 0xff);
    if (description) {
        description->getColor("TextLight", text);
        description->getColor("Structure", structure);
        description->getColor("BgDark", bg);
        description->getColor("SliderTrack", track);
        description->getColor("SliderActive", azure);     // the RESET square, as POWER's checkmark
        description->getColor("MeterFill", meter);        // the grid's bars are meters
    }
    if (std::string(name) == "ChoirReset")
        return new ChoirResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, structure, bg, azure);
    if (std::string(name) == "ChoirGrid") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new ChoirGrid(VSTGUI::CRect(0, 0, 400, 176), &engine_, font, text, track, meter, structure);
    }
    return nullptr;
}
```

- factory: `INLINE_UID_FROM_FUID(Seam::ChoirProcessorUID)`, name `"SEAM CHOIR"`, subcategory `"Fx"`, `Seam::ChoirProcessor::createInstance`.

- [ ] **Step 4: `choir.uidesc`**

Copy `plugins/_template/resource/_template.uidesc` to `plugins/choir/resource/choir.uidesc` and edit it to:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="KnobLabelFont" size="12"/>
        <font font-name="Source Code Pro Light" name="ValueFont" size="11"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="11"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
        <color name="Structure" rgba="#888888ff"/>
        <color name="MeterFill" rgba="#c8a24aff"/>
    </colors>

    <!-- L format for the grid's width (strx's reason), two controls only.
         No SETUP zone. Zone order HEADER, OPS, FINE, FOOTER. -->
    <template name="view" class="CViewContainer" origin="0, 0" size="460, 470"
              minSize="460, 470" maxSize="460, 470"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM CHOIR" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Studio sul Corpo d'Ombra #2" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center"
              title="sixteen voices on four channels &#xB7; noise block 3" transparent="true"/>

        <!-- ── OPS ───────────────────────────────────────────────────────
             POWER over the left column, RESET over the right one. RESET
             is UI-only: it silences the ringing bands, not a parameter. -->
        <view class="CCheckBox" origin="93, 90" size="14, 14" control-tag="Power"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>
        <view class="CTextLabel" origin="111, 88" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="POWER" transparent="true"/>
        <view class="CView" origin="313, 90" size="14, 14" custom-view-name="ChoirReset"
              tooltip="silence the ringing bands"/>
        <view class="CTextLabel" origin="331, 88" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="RESET" transparent="true"/>

        <!-- ── FINE — the output, full width (dslar's Output) ───────────── -->
        <view class="CTextLabel" origin="0, 120" size="460, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="output" transparent="true"/>
        <view class="CSlider" origin="80, 136" size="300, 18" control-tag="Output"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="80, 156" size="300, 16" font="ValueFont" control-tag="Output"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="3" style-no-frame="true"/>

        <!-- ── FOOTER — what the choir hears, band by band, then the logo ─ -->
        <view class="CView" origin="30, 188" size="400, 176" custom-view-name="ChoirGrid"/>
        <view class="CView" origin="110, 380" size="240, 77" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags>
        <control-tag name="Power"  tag="100"/>
        <control-tag name="Output" tag="101"/>
    </control-tags>
</vstgui-ui-description>
```

The grid's height: 4 rows of 32 + axis 14 + 2 + status line 16 = 160, inside a 176 view.

- [ ] **Step 5: Build, lint, validator**

Run:
```bash
cmake -S . -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --config Debug --target choir 2>&1 | grep -E "error:|warning:|BUILD"
python3 tools/check-uidesc.py
build/bin/Debug/validator build/VST3/Debug/choir.vst3 2>&1 | tail -3
```
Expected: `BUILD SUCCEEDED` with no warning from `plugins/choir`; the lint reports no error (a WARN for the missing screenshot is expected until the end); the validator reports 47 tests passed, 0 failed (if the validator lives under `build/bin/Release`, build it with `cmake --build build --config Debug --target validator` first and use that path).

- [ ] **Step 6: Run the whole suite**

Run: `cmake -S . -B build-test && cmake --build build-test && (cd build-test && ctest -C Debug 2>&1 | tail -4)`
Expected: every test passes (the screenshot WARN does not fail the lint).

- [ ] **Step 7: Commit**

```bash
git add plugins/choir CMakeLists.txt
git commit -m "feat(choir): the plugin, window L with the 4x16 grid and RESET

20th plugin, FUID 0x5E4D0013; POWER, output (CC86); validator 47/47;
lint clean but for the screenshot, taken at the end.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 7: CPU, mutations record, documentation, report and log

**Files:**
- Create: `doc/study/sscdo2/choir-plugin/README.md`, `doc/study/sscdo2/choir-plugin/mutations.md`, `doc/study/sscdo2/choir-plugin/cpu.cpp`
- Create: `plugins/choir/doc/README.md`
- Create: `logs/2026-10-02-sscdo2-choir.md`
- Modify: `doc/study/sscdo2/README.md` (index row), `doc/study/sscdo2/report/parte2-coro-master.tex` (card `coro-uscita`)

**Interfaces:**
- Consumes: the engine (Task 4), the mutation results recorded during Tasks 1–5.

- [ ] **Step 1: CPU**

`doc/study/sscdo2/choir-plugin/cpu.cpp`:

```cpp
// cpu.cpp -- the cost of the choir engine: percent of a core at 96 and
// 48 kHz, 256-sample blocks, 20 s of the test signal then 60 s of silence
// (the silent tail is where subnormals would grow).
// c++ -std=c++17 -O3 -I ../../../../plugins/choir/source -I ../../../../plugins/_common cpu.cpp -o cpu
#include "choir_dsp.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 48000.0}) {
        choir::Engine e; e.prepare(fs);
        e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        const double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        long t = 0;
        auto time = [&](double seconds, bool silent) {
            const long blocks = (long)(seconds * fs / B);
            const auto t0 = std::chrono::steady_clock::now();
            for (long b = 0; b < blocks; ++b) {
                for (int c = 0; c < 4; ++c)
                    for (int i = 0; i < B; ++i) {
                        double x = 0.0;
                        if (!silent) for (int k = 1; k <= 16; ++k)
                            x += 0.05 * std::sin(2 * M_PI * e.config().f[(size_t)c] * k * (double)(t + i) / fs);
                        ib[(size_t)(c * B + i)] = x;
                    }
                t += B;
                e.process(in, out, B);
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / seconds;
        };
        const double sound = time(20.0, false), silence = time(60.0, true);
        printf("%6.0f Hz: %.2f %% of a core on sound (input synthesis included), %.2f %% on silence\n",
               fs, 100.0 * sound, 100.0 * silence);
    }
}
```

Run: `cd doc/study/sscdo2/choir-plugin && c++ -std=c++17 -O3 -I ../../../../plugins/choir/source -I ../../../../plugins/_common cpu.cpp -o /tmp/choir-cpu && /tmp/choir-cpu`
Expected: two lines; record them. The silence figure is the engine alone; the sound figure includes the sine synthesis of the bench.

- [ ] **Step 2: `mutations.md`**

A table `| test | mutation | result |` with one row per mutation run in Tasks 1–5, the result as observed (RED, or the reason it stayed green), in the format of `doc/study/sscdo2/delrm-plugin/mutations.md`.

- [ ] **Step 3: Study README and index**

`doc/study/sscdo2/choir-plugin/README.md`: what the plugin is (one paragraph), the references (`gen-ref.sh`, which DSPs, which headers), the results (equivalence figures from the test run, CPU), the files table (`gen-ref.sh`, `dsp/*.dsp`, `cpu.cpp`, `mutations.md`), how to run. Index row in `doc/study/sscdo2/README.md` after delRM's plugin row:

```
| choir | the C++ plugin | `choir-plugin/` | `plugins/choir`, equal to `sdt.choir` within 1e-12 of the peak; `seam_svf.h`, `seam_analyzers.h`, `seam_basics.h` | none |
```

- [ ] **Step 4: Plugin README**

`plugins/choir/doc/README.md`, in the shape of `plugins/delrm/doc/README.md`: what the choir does, the constants, the controls (POWER, output = CC86, RESET), the grid and how to read it (a bar is the amplitude of the input's partial in that band, dBFS), the SR rule, the CPU figures, the references to the spec, the studies and the log.

- [ ] **Step 5: Report card**

In `doc/study/sscdo2/report/parte2-coro-master.tex`, replace the placeholder card `coro-uscita` with:

```latex
\scheda{coro-uscita}{Uscita del coro (CC86)}
{fader lineare da 0 a 1, rampa di \qty{25}{\milli\second}; nel patch uno stadio di guadagno di Pure Data (\texttt{interpolator\_4ch})}
{0}
{il plugin ha un solo volume, \texttt{output}, mosso dal CC86; POWER spento lascia il coro in ascolto e porta l'uscita a zero; RESET zittisce le bande che suonano ancora (fino a \misurau{16.0}{\second}{choir-chain} a \qty{48}{\hertz}); la griglia mostra, banda per banda, che cosa il coro sta ascoltando}
{come nel patch, con la rampa della suite}
{\daprovare}
{\studio{choir-plugin}}
```

Run: `make -C doc/study/sscdo2/report check`
Expected: `CHECK OK` (add `choir-plugin` to nothing else: the index row makes the checker require the `\studio{choir-plugin}` citation, which the card provides).

- [ ] **Step 6: Log**

`logs/2026-10-02-sscdo2-choir.md`: the design decisions (spec), the tasks done with their commits, the equivalence figures, the mutations summary, the CPU figures, what is deferred (Task 8). Header `**Who:** Claude (agent), on Giuseppe's instructions.` as the other logs.

- [ ] **Step 7: Commit**

```bash
git add doc/study/sscdo2/choir-plugin doc/study/sscdo2/README.md doc/study/sscdo2/report plugins/choir/doc logs/2026-10-02-sscdo2-choir.md
git commit -m "docs(choir): study, mutations, CPU, report card coro-uscita, log

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM"
```

---

### Task 8 (with Giuseppe, at the end): Release build of LMO and the choir, host check, registry

Not for an agent alone: Giuseppe asked to build LMO and the choir together at the end, and the screenshot and the site need him.

- [ ] **Step 1:** `cmake --build build --config Release --target lmo choir` (the Release tree owns the symlinks; the live editor is stripped).
- [ ] **Step 2:** Giuseppe in Reaper at 96 kHz: LMO with the new noise; the choir on the TETRAREC A (inputs 5–8), the grid while the clarinet plays, RESET, POWER, output on CC86; the same session at 48 kHz. Screenshot to `docs/img/choir.png`.
- [ ] **Step 3:** Registry: a `[[family.plugin]]` entry `CHOIR` in `doc/plugins.toml` after `DELRM` (`io = "4ch → 4ch"`, `screenshot = "choir.png"`, `faust = "seam.tedesco.lib"`, a description in the registry's style); counts 19 → 20 in `doc/scripts/test-doc.sh` (three checks), `doc/scripts/render-readme.py` ("All twenty windows"), `CLAUDE.md` (two mentions); `make -C doc doc && make -C doc test`.
- [ ] **Step 4:** Commit; push and `make -C doc publish` only after Giuseppe confirms.
