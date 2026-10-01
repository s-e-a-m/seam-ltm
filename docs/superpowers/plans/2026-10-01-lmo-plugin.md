# LMO plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A hand-written C++ VST3 of SSCDO#2's LMO (`sdt.lmo(4, f, d)`), double precision, sounding as at 96 kHz at any rate, with an in-plugin glissando, plus three reusable `_common/` headers.

**Architecture:** Three SDK-free headers in `plugins/_common/` (`seam_noise.h` = bit-exact `no.multinoise`, `seam_butterworth.h` = order-N Butterworth in trapezoidal SVF sections, `seam_ramp.h` = linear time-anchored ramp) compose an SDK-free engine `plugins/lmo/source/lmo_dsp.h`, which a thin `SingleComponentEffect` processor drives. Faust references are rendered once by a committed script into a committed header, so the doctest suite proves C++ == spec without needing `faust` to build.

**Tech Stack:** C++17, VST3 SDK + VSTGUI, CMake (Xcode generator), doctest, Faust 2.88 + faustlibraries clone (reference generation only), Python 3 (lint, docs).

**Spec:** `docs/superpowers/specs/2026-10-01-lmo-plugin-design.md`

## Global Constraints

- All DSP state and arithmetic in `double`; the host may hand `float` buffers, converted only at the output write.
- SR rule: every process sounds as at 96 kHz. Time constants are seconds converted at `prepare(fs)`; band level is anchored by `densityGain(fs) = sqrt(fs/96000)`.
- Faust is the spec, C++ the deliverable: no `faust -lang cpp` output in `plugins/*/source/` or in `tests/` (only numeric reference arrays).
- Code, comments, commits in English; the session log may be English like the existing `logs/2026-09-29-sscdo2-ricognizione.md`.
- UI per `doc/style/ui-style.md`: format S (300 px), zones HEADER → OPS → FINE → FOOTER, `tools/check-uidesc.py` clean.
- FUID `0x5E4D0010`; parameter IDs 100–104 (POWER, f, glide, Δ, volume), 200 (f now, read-only).
- Build: `cmake --build build --config Release --target lmo` (Xcode generator, SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`). Tests: `cmake --build build-test --config Release && ctest --test-dir build-test -C Release` (`build-test` has `SEAM_BUILD_PLUGINS=OFF`; never build plugins there — it would steal the VST3 symlinks).
- Faust references: `FAUSTLIBS=/Users/giuseppe/Documents/github/grame/faustlibraries` (needs ≥ 0965ea2), `SEAMLIBS=/Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src`.
- Commit as you go in seam-ltm and faust-libraries; never push without asking Giuseppe; never run `make -C doc publish` without asking.
- Every test is verified by mutation: break the code, see RED, restore, record it.

## Review Focus

1. **Automation re-sending the same f while glide > 0** (Reaper sends a point every block): the glissando must not restart or stall. Pinned by `"same target does not restart the glide"` in Task 4.
2. **Δ larger than 2f** (f = 20, Δ = 50 puts the low band at −5 Hz): the filter must stay stable and finite. The band centre is clamped to ≥ 1 Hz in both the C++ and the Faust spec (Task 0, Task 4 test `"extreme f and delta stay finite"`).
3. **Host block sizes that are not multiples of the update period** (odd sizes, 1-sample blocks): the output must not depend on how the host slices time. Pinned by `"output independent of block partition"` in Task 4.
4. **A sample-rate change between sessions** (prepare at 48 k then at 96 k): ramps, update period and density follow the new rate. Pinned by `"re-prepare at a new rate"` in Task 4.
5. **POWER toggled again mid-fade**: the gain must reverse from where it is, no jump. Pinned by `"power reverses from the current gain"` in Task 4.

---

## File map

| File | Responsibility |
|---|---|
| `faust-libraries/src/seam.tedesco.lib` (modify) | density factor and band clamp in `sdt.lmoosc`/`sdt.lmo` |
| `doc/study/sscdo2/lmo-beats/dsp/probes.dsp` (modify) | prototype `lmo2` follows the spec's density |
| `doc/study/sscdo2/lmo-plugin/` (create) | `gen-ref.sh`, `refdump.cpp`, `dsp/*.dsp`, `README.md`, `mutations.md` |
| `tests/ref/lmo_ref.h` (generated, committed) | Faust reference arrays |
| `plugins/_common/seam_noise.h` | `Seam::FaustMultinoise` |
| `plugins/_common/seam_butterworth.h` | `Seam::ButterworthSVF<N>` |
| `plugins/_common/seam_ramp.h` | `Seam::LinearRamp` |
| `plugins/lmo/source/lmo_dsp.h` | `lmo::Engine` |
| `plugins/lmo/source/{lmo_ids.h, version.h, lmo_processor.h, lmo_processor.cpp}` | VST3 shell |
| `plugins/lmo/resource/lmo.uidesc`, `plugins/lmo/CMakeLists.txt`, `plugins/lmo/doc/README.md` | GUI, build, doc |
| `tests/{seam_noise_test, seam_butterworth_test, seam_ramp_test, lmo_dsp_test}.cpp`, `tests/CMakeLists.txt` | tests |
| `CMakeLists.txt` | `add_subdirectory(plugins/lmo)` |
| `logs/2026-10-01-sscdo2-plugins.md` | session log of the C++ phase |

---

### Task 0: The Faust spec gains the density anchor and the band clamp

**Files:**
- Modify: `/Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src/seam.tedesco.lib` (the `lmoosc` and `lmo` entries)
- Modify: `doc/study/sscdo2/lmo-beats/dsp/probes.dsp` (seam-ltm)

**Interfaces:**
- Produces: `sdt.lmodens` (= `sqrt(ma.SR/96000)`), `sdt.lmo(N, f, d)` with density and `max(1, ·)` on the low band; used by Task 1's reference DSP.

- [ ] **Step 1: Edit `lmoosc`.** Replace the line `lmoosc(N, f) = no.multinoise(N) : par(i, N, lmoband(f + i));` with:

```
lmoosc(N, f) = no.multinoise(N) : par(i, N, lmoband(f + i) : *(lmodens));
```

and append to its comment block, before `#### Usage`:

```
// The level is anchored at 96 kHz (`lmodens`): white noise spreads a constant
// power over SR/2, so without it a band would sound 3.01 dB louder at 48 kHz
// than at 96 kHz. With it, every band has its 96 kHz level at any rate, and at
// 96 kHz nothing changes (2026-10-01, with the C++ port).
```

- [ ] **Step 2: Add `lmodens` above `lmoosc`'s comment block:**

```
//------------------------------`(sdt.)lmodens`---------------------------------
// The density anchor of the LMO: sqrt(ma.SR/96000). A white source of constant
// RMS puts power SR/2 into every hertz-width less as the rate rises, so a band
// of fixed width loses 3.01 dB per doubling of the rate. Multiplying by this
// factor holds the band at its level at 96 kHz, the rate SSCDO#2 is played at
// (the rule of `delrmint`, applied to a level). Unity at 96 kHz.
//------------------------------------------------------------------------------
lmodens = sqrt(ma.SR/96000);
```

- [ ] **Step 3: Edit `lmo`.** Replace its definition with:

```
lmo(N, f, d) = no.multinoise(2*N)
             : par(i, N, lmoband(max(1, f - d/2 + i))), par(i, N, lmoband(f + d/2 + i))
             :> par(i, N, /(sqrt(2)) : *(lmodens));
```

and append to its comment, before `#### Usage`:

```
// The level is anchored at 96 kHz by `lmodens`, as in `lmoosc`. The low band's
// centre is held at 1 Hz or above: with f near 20 Hz and d near 50 Hz,
// f - d/2 would be negative and the filters would not be a band-pass any more
// (2026-10-01, with the C++ port, which does the same).
```

- [ ] **Step 4: Compile-check the library.**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
echo 'sdt = library("seam.tedesco.lib"); process = sdt.lmo(4, 97.44, 20);' > /tmp/lmo_chk.dsp
faust -I /Users/giuseppe/Documents/github/grame/faustlibraries -I src -double /tmp/lmo_chk.dsp > /dev/null && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Keep the lmo-beats prototype in step with the spec.** In `doc/study/sscdo2/lmo-beats/dsp/probes.dsp` change the `lmo2` definition's last line from `:> par(i, N, /(sqrt(2)));` to `:> par(i, N, /(sqrt(2)) : *(sdt.lmodens));` and wrap its low band: in `bandsA` replace `sdt.lmoband(f - d/2 + i)` with `sdt.lmoband(max(1, f - d/2 + i))`. The study runs at 48 kHz, where `lmodens` is not 1: the prototype must follow the spec or "sdt.lmo equals the prototype" fails for the wrong reason, and the level check against `sdt.lmoosc` (now density-scaled) would fail too.

- [ ] **Step 6: Re-run the lmo-beats self-test.**

Run: `cd doc/study/sscdo2/lmo-beats && ./build.sh && ../../../../.venv/bin/python analyze.py`
Expected: every check `ok`, every MUTATION check reported as expected FAIL. Then `git status`: if `renders/` or `results.md` changed, inspect with `git diff --stat`; the relative numbers (dB re one oscillator, correlations) must be unchanged. Restore any re-written render with `git checkout -- renders/` (the committed renders document what was heard on 2026-09-29). If a number in `results.md` changed, STOP and report it.

- [ ] **Step 7: Commit in both repos.**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.tedesco.lib
git commit -m "feat(tedesco): LMO level anchored at 96 kHz (lmodens), low band held >= 1 Hz"
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add doc/study/sscdo2/lmo-beats/dsp/probes.dsp
git commit -m "fix(sscdo2/lmo-beats): prototype follows the spec's density anchor and band clamp"
```
(Both messages end with the Co-Authored-By / Claude-Session trailer.)

---

### Task 1: Faust reference generator and `FaustMultinoise`

**Files:**
- Create: `doc/study/sscdo2/lmo-plugin/refdump.cpp`, `doc/study/sscdo2/lmo-plugin/gen-ref.sh`, `doc/study/sscdo2/lmo-plugin/dsp/noise8.dsp`, `dsp/bw.dsp`, `dsp/lmo.dsp`
- Create: `tests/ref/lmo_ref.h` (generated)
- Create: `plugins/_common/seam_noise.h`, `tests/seam_noise_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `Seam::FaustMultinoise(int n, int32_t seed = 12345)`, `void reset()`, `int size() const`, `void tick(double* out)`; reference arrays `lmoref::kNoise8[8][512]`, `lmoref::kBw[4][2048]`, `lmoref::kLmo96[4][2048]`, `lmoref::kLmo48[4][1024]` in `tests/ref/lmo_ref.h`.

- [ ] **Step 1: Write the reference DSPs.**

`doc/study/sscdo2/lmo-plugin/dsp/noise8.dsp`:
```
// The standard multinoise, eight streams: the reference for Seam::FaustMultinoise.
import("stdfaust.lib");
process = no.multinoise(8);
```

`dsp/bw.dsp`:
```
// Order-24 and order-2 Butterworth on the current SVF faustlibraries:
// the reference for Seam::ButterworthSVF. Driven by an impulse.
import("stdfaust.lib");
process = _ <: fi.highpass(24, 97.44), fi.lowpass(24, 97.44),
               fi.highpass(2, 1000), fi.lowpass(2, 1000);
```

`dsp/lmo.dsp`:
```
// The spec itself: sdt.lmo(4, 97.44, 20), cue 1 with the bands 20 Hz apart.
sdt = library("seam.tedesco.lib");
process = sdt.lmo(4, 97.44, 20);
```

- [ ] **Step 2: Write `refdump.cpp`** (compiled with the Faust class included as `ref.h`, class name `Ref`):

```cpp
// refdump.cpp -- render a Faust DSP offline and print its outputs as a C++
// array of hex-float doubles, exact to the bit. Used by gen-ref.sh only.
//
// usage: refdump SR NSAMPLES NAME impulse|zero
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define FAUSTFLOAT double
struct Meta { virtual ~Meta() {} virtual void declare(const char*, const char*) {} };
struct UI {
    void openVerticalBox(const char*) {} void openHorizontalBox(const char*) {}
    void closeBox() {} void declare(double*, const char*, const char*) {}
    void addHorizontalSlider(const char*, double*, double, double, double, double) {}
    void addVerticalSlider(const char*, double*, double, double, double, double) {}
    void addNumEntry(const char*, double*, double, double, double, double) {}
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

int main(int argc, char** argv) {
    if (argc != 5) { fprintf(stderr, "usage: refdump SR N NAME impulse|zero\n"); return 2; }
    const int sr = atoi(argv[1]); const int n = atoi(argv[2]);
    const bool imp = strcmp(argv[4], "impulse") == 0;
    Ref d; d.init(sr);
    const int ni = d.getNumInputs(), no = d.getNumOutputs();
    std::vector<std::vector<double>> in(ni ? ni : 1, std::vector<double>(n, 0.0));
    std::vector<std::vector<double>> out(no, std::vector<double>(n, 0.0));
    if (imp && ni) in[0][0] = 1.0;
    std::vector<double*> ip, op;
    for (auto& v : in) ip.push_back(v.data());
    for (auto& v : out) op.push_back(v.data());
    d.compute(n, ip.data(), op.data());
    printf("// %s: %d outputs x %d samples at %d Hz\n", argv[3], no, n, sr);
    printf("static const double %s[%d][%d] = {\n", argv[3], no, n);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int k = 0; k < n; ++k) printf("%a%s", out[c][k], k + 1 < n ? "," : "");
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\n");
    return 0;
}
```

Note: `d.init(sr)` calls `classInit` + `instanceInit`, which zero the state; the noise starts from `iRec0 = 0` as in a fresh Faust instance.

- [ ] **Step 3: Write `gen-ref.sh`.**

```bash
#!/usr/bin/env bash
# gen-ref.sh -- render the Faust references of the LMO plugin into
# tests/ref/lmo_ref.h. Run by hand when the spec changes; the tests read the
# committed header and need no faust binary.
#
# FAUSTLIBS: faustlibraries clone >= 0965ea2 (SVF fi.highpass/lowpass)
# SEAMLIBS:  faust-libraries/src (seam.tedesco.lib)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
FAUSTLIBS="${FAUSTLIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}"
SEAMLIBS="${SEAMLIBS:-$ROOT/../faust-libraries/src}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
OUT="$ROOT/tests/ref/lmo_ref.h"
mkdir -p "$(dirname "$OUT")"

render() { # dsp sr n name input
    faust -I "$FAUSTLIBS" -I "$SEAMLIBS" -double -lang cpp -cn Ref "$HERE/dsp/$1" -o "$WORK/ref.h"
    c++ -std=c++17 -O1 -I "$WORK" "$HERE/refdump.cpp" -o "$WORK/refdump"
    "$WORK/refdump" "$2" "$3" "$4" "$5"
}

{
    echo "// GENERATED by doc/study/sscdo2/lmo-plugin/gen-ref.sh -- do not edit."
    echo "// $(faust --version | head -1); faustlibraries $(git -C "$FAUSTLIBS" rev-parse --short HEAD);"
    echo "// faust-libraries $(git -C "$SEAMLIBS" rev-parse --short HEAD)."
    echo "#pragma once"
    echo "namespace lmoref {"
    render noise8.dsp 96000 512  kNoise8 zero
    render bw.dsp     96000 2048 kBw     impulse
    render lmo.dsp    96000 2048 kLmo96  zero
    render lmo.dsp    48000 1024 kLmo48  zero
    echo "} // namespace lmoref"
} > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
```

- [ ] **Step 4: Generate.**

Run: `chmod +x doc/study/sscdo2/lmo-plugin/gen-ref.sh && doc/study/sscdo2/lmo-plugin/gen-ref.sh`
Expected: `wrote .../tests/ref/lmo_ref.h (... bytes)`, about 0.4–0.5 MB. Check `head -5 tests/ref/lmo_ref.h` names a faust-libraries hash that contains Task 0.

- [ ] **Step 5: Write the failing test** `tests/seam_noise_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_noise.h"
#include "ref/lmo_ref.h"

using Seam::FaustMultinoise;

TEST_CASE("FaustMultinoise(8) equals no.multinoise(8) bit for bit") {
    FaustMultinoise nz(8);
    double v[8];
    int mismatches = 0;
    for (int k = 0; k < 512; ++k) {
        nz.tick(v);
        for (int c = 0; c < 8; ++c)
            if (v[c] != lmoref::kNoise8[c][k]) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("reset returns to the start of the sequence") {
    FaustMultinoise nz(8);
    double a[8], b[8];
    nz.tick(a);
    for (int k = 0; k < 100; ++k) nz.tick(b);
    nz.reset();
    nz.tick(b);
    for (int c = 0; c < 8; ++c) CHECK(a[c] == b[c]);
}

TEST_CASE("a different seed gives a different sequence") {
    FaustMultinoise a(4), b(4, 54321);
    double va[4], vb[4];
    a.tick(va); b.tick(vb);
    CHECK(va[0] != vb[0]);
}

TEST_CASE("values lie in [-1, 1]") {
    FaustMultinoise nz(8);
    double v[8];
    for (int k = 0; k < 100000; ++k) {
        nz.tick(v);
        for (double x : v) { REQUIRE(x >= -1.0); REQUIRE(x <= 1.0); }
    }
}
```

Add to `tests/CMakeLists.txt` (after the `multipink_pink_engine_test` block):

```cmake
add_executable(seam_noise_test seam_noise_test.cpp)
target_include_directories(seam_noise_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_noise_test PRIVATE cxx_std_17)
add_test(NAME seam_noise_test COMMAND seam_noise_test)
```

- [ ] **Step 6: Run to see it fail.**

Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target seam_noise_test`
Expected: compile error `'seam_noise.h' file not found`.

- [ ] **Step 7: Implement** `plugins/_common/seam_noise.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_noise.h — the standard Faust multinoise, bit for bit
//
// FAUST REFERENCE (noises.lib, _noise_env(seed)):
//
//   multirandom(N) = randomize(N) ~ _
//   with {
//       randomize(1) = +(seed) : *(1103515245);
//       randomize(N) = randomize(1) <: randomize(N-1), _;
//   };
//   multinoise(N) = multirandom(N) : par(i, N, /(RANDMAX)) : par(i, N, float);
//
// ONE 32-bit linear congruential generator, stepped N times per sample
// inside one feedback loop: stream k is not separately seeded, it is step
// N-1-k of a single sequence (the recursion emits the deepest step first,
// and that same step is the one fed back). Within one call the streams are
// decorrelated (measured, doc/study/sscdo2/lmo-streams/); two calls with the
// same seed give the same noise. The seed is the additive constant of the
// LCG, 12345 in no.multinoise; another seed is another sequence.
//
// Arithmetic: Faust's int is 32-bit with wraparound, which unsigned
// arithmetic reproduces exactly; the result is reinterpreted as signed and
// scaled by 1/2147483647 in double (the constant Faust emits,
// 4.656612875245797e-10, is that double to the bit).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstdint>
#include <vector>

namespace Seam {

class FaustMultinoise {
public:
    explicit FaustMultinoise(int n, int32_t seed = 12345)
        : n_(n), seed_(seed), steps_((size_t)n, 0) {}

    void reset() { state_ = 0; }
    int  size() const { return n_; }

    // Writes n_ values in [-1, 1] to out[0 .. n_-1].
    void tick(double* out) {
        uint32_t x = (uint32_t)state_;
        for (int s = 0; s < n_; ++s) {
            x = (x + (uint32_t)seed_) * 1103515245u;
            steps_[(size_t)s] = (int32_t)x;
        }
        state_ = steps_[(size_t)(n_ - 1)];
        for (int k = 0; k < n_; ++k)
            out[k] = kScale * (double)steps_[(size_t)(n_ - 1 - k)];
    }

private:
    static constexpr double kScale = 1.0 / 2147483647.0;   // 1/RANDMAX
    int n_;
    int32_t seed_;
    int32_t state_ = 0;
    std::vector<int32_t> steps_;
};

} // namespace Seam
```

- [ ] **Step 8: Run to see it pass.**

Run: `cmake --build build-test --config Release --target seam_noise_test && ctest --test-dir build-test -C Release -R seam_noise_test --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 9: Mutation check.** Change the output index `steps_[(size_t)(n_ - 1 - k)]` to `steps_[(size_t)k]` (the natural-looking order); rebuild and run: the bit-for-bit test must FAIL. Restore. Then change `state_ = steps_[n_-1]` to `state_ = steps_[0]`: must FAIL. Restore. Write both results into `doc/study/sscdo2/lmo-plugin/mutations.md` (create it with a header `# LMO plugin — mutation record` and a table `| test | mutation | result |`).

- [ ] **Step 10: Commit.**

```bash
git add plugins/_common/seam_noise.h tests/seam_noise_test.cpp tests/CMakeLists.txt tests/ref/lmo_ref.h doc/study/sscdo2/lmo-plugin
git commit -m "feat(_common): seam_noise.h, the standard multinoise bit for bit; LMO Faust references"
```

---

### Task 2: `ButterworthSVF<N>`

**Files:**
- Create: `plugins/_common/seam_butterworth.h`, `tests/seam_butterworth_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `lmoref::kBw` (Task 1).
- Produces: `enum class Seam::ButterworthType { Lowpass, Highpass }`; `template<int N> class Seam::ButterworthSVF` with `void setType(ButterworthType)`, `void setFrequency(double fc, double fs)`, `void reset()`, `double tick(double x)`; even N only.

Deviation from the spec, recorded in the log: odd orders (the first-order `tf1s` section) are not implemented — YAGNI, LMO and the choir use even orders; a `static_assert` says so.

- [ ] **Step 1: Write the failing test** `tests/seam_butterworth_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_butterworth.h"
#include "ref/lmo_ref.h"
#include <cmath>
#include <algorithm>

using namespace Seam;

template <int N>
static double maxErr(ButterworthType t, double fc, const double* ref, int n) {
    ButterworthSVF<N> f;
    f.setType(t);
    f.setFrequency(fc, 96000.0);
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < n; ++k) {
        const double y = f.tick(k == 0 ? 1.0 : 0.0);
        e  = std::max(e, std::fabs(y - ref[k]));
        pk = std::max(pk, std::fabs(ref[k]));
    }
    return e / pk;
}

TEST_CASE("order 24 equals fi.highpass/fi.lowpass(24, 97.44) at 96 kHz") {
    CHECK(maxErr<24>(ButterworthType::Highpass, 97.44, lmoref::kBw[0], 2048) < 1e-12);
    CHECK(maxErr<24>(ButterworthType::Lowpass,  97.44, lmoref::kBw[1], 2048) < 1e-12);
}

TEST_CASE("order 2 equals fi.highpass/fi.lowpass(2, 1000) at 96 kHz") {
    CHECK(maxErr<2>(ButterworthType::Highpass, 1000.0, lmoref::kBw[2], 2048) < 1e-12);
    CHECK(maxErr<2>(ButterworthType::Lowpass,  1000.0, lmoref::kBw[3], 2048) < 1e-12);
}

TEST_CASE("-3.01 dB at fc, designed per rate") {
    for (double fs : {48000.0, 96000.0, 192000.0}) {
        ButterworthSVF<24> f;
        f.setType(ButterworthType::Lowpass);
        f.setFrequency(1000.0, fs);
        // steady-state amplitude of a 1 kHz sine after 1 s
        const int n = (int)fs, tail = (int)(fs / 10);
        double pk = 0.0;
        for (int k = 0; k < n; ++k) {
            const double y = f.tick(std::sin(2.0 * M_PI * 1000.0 * k / fs));
            if (k >= n - tail) pk = std::max(pk, std::fabs(y));
        }
        CHECK(std::fabs(20.0 * std::log10(pk) + 3.0103) < 0.003);
    }
}

TEST_CASE("reset clears the state") {
    ButterworthSVF<24> f;
    f.setType(ButterworthType::Highpass);
    f.setFrequency(97.44, 96000.0);
    const double y0 = f.tick(1.0);
    for (int k = 0; k < 1000; ++k) f.tick(0.3);
    f.reset();
    CHECK(f.tick(1.0) == y0);
}
```

(Explicit absolute tolerance, ±0.003 dB: doctest `Approx` is avoided on purpose, it is one of the suite's known traps.)

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(seam_butterworth_test seam_butterworth_test.cpp)
target_include_directories(seam_butterworth_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_butterworth_test PRIVATE cxx_std_17)
add_test(NAME seam_butterworth_test COMMAND seam_butterworth_test)
```

- [ ] **Step 2: Run to see it fail.**

Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target seam_butterworth_test`
Expected: compile error `'seam_butterworth.h' file not found`.

- [ ] **Step 3: Implement** `plugins/_common/seam_butterworth.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_butterworth.h — order-N Butterworth, SVF sections
//
// FAUST REFERENCE (filters.lib, faustlibraries 0965ea2, J.O. Smith, #262):
//
//   lowpass0_highpass1(s,N,fc) = lphpr(s,N,N,fc) with {
//     lphpr(s,O,N,fc) = lphpr(s,(O-2),N,fc) : section(s) with {
//       S   = O/2;                                   // section 1 .. N/2
//       a1s = -2*cos(-PI + PI/(2N) + (S-1)*PI/N);    // (even N)
//       section(0) = svf.lp(fc, 1/a1s);
//       section(1) = svf.hp(fc, 1/a1s);
//     };
//   };
//
// and svf (filters.lib), with k = 1/Q = a1s and g = tan(PI*fc/SR):
//
//   v1 = (ic1 + g*(v0 - ic2)) / (1 + g*(g + k));   v2 = ic2 + g*v1;
//   ic1' = 2*v1 - ic1;   ic2' = 2*v2 - ic2;
//   LP = v2;   HP = v0 - k*v1 - v2.
//
// Each section is the bilinear transform of a Butterworth biquad, prewarped
// at fc, in trapezoidal state-variable form: the integrator states keep
// low-fc/SR sections accurate where a direct-form biquad loses them. The
// damping k of each section depends only on N, so it is computed once; a
// frequency change recomputes g and one reciprocal per section — which is
// what makes a per-sample glissando cheap. Sections run in Faust's order,
// S = 1 first. Even N only: the first-order section of odd N is not needed
// by the suite yet.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <array>
#include <cmath>

namespace Seam {

enum class ButterworthType { Lowpass, Highpass };

template <int N>
class ButterworthSVF {
    static_assert(N >= 2 && N % 2 == 0, "ButterworthSVF: even orders only");
public:
    static constexpr int kSections = N / 2;

    ButterworthSVF() {
        constexpr double pi = 3.14159265358979323846;
        for (int s = 1; s <= kSections; ++s)
            k_[s - 1] = -2.0 * std::cos(-pi + pi / (2.0 * N) + (s - 1) * pi / N);
        setFrequency(1000.0, 48000.0);
    }

    void setType(ButterworthType t) { hp_ = (t == ButterworthType::Highpass); }

    void setFrequency(double fc, double fs) {
        constexpr double pi = 3.14159265358979323846;
        g_ = std::tan(pi * fc / fs);
        for (int s = 0; s < kSections; ++s)
            den_[s] = 1.0 / (1.0 + g_ * (g_ + k_[s]));
    }

    void reset() { ic1_.fill(0.0); ic2_.fill(0.0); }

    double tick(double x) {
        for (int s = 0; s < kSections; ++s) {
            const double v0 = x;
            const double v1 = (ic1_[s] + g_ * (v0 - ic2_[s])) * den_[s];
            const double v2 = ic2_[s] + g_ * v1;
            ic1_[s] = 2.0 * v1 - ic1_[s];
            ic2_[s] = 2.0 * v2 - ic2_[s];
            x = hp_ ? (v0 - k_[s] * v1 - v2) : v2;
        }
        return x;
    }

private:
    std::array<double, kSections> k_{}, den_{}, ic1_{}, ic2_{};
    double g_ = 0.0;
    bool hp_ = false;
};

} // namespace Seam
```

- [ ] **Step 4: Run to see it pass.**

Run: `cmake --build build-test --config Release --target seam_butterworth_test && ctest --test-dir build-test -C Release -R seam_butterworth_test --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 5: Mutation check.** (a) Reverse the section order (`k_[kSections - s]` in the constructor); (b) use `+ (s - 1) * pi / N` with `s` starting at 0; (c) HP output `v0 - v1 - v2`. Each must turn the order-24 test RED (order 2 has one section, so (a) is invisible there — which is why order 24 is tested). Restore and record in `mutations.md`.

- [ ] **Step 6: Commit.**

```bash
git add plugins/_common/seam_butterworth.h tests/seam_butterworth_test.cpp tests/CMakeLists.txt doc/study/sscdo2/lmo-plugin/mutations.md
git commit -m "feat(_common): seam_butterworth.h, order-N Butterworth in Smith's SVF sections"
```

---

### Task 3: `LinearRamp`

**Files:**
- Create: `plugins/_common/seam_ramp.h`, `tests/seam_ramp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class Seam::LinearRamp` with `void setTarget(double target, double seconds, double fs)`, `void snap()`, `double next()`, `double value() const`, `double target() const`, `bool active() const`.

Pd `line` semantics: a new target starts from the current value; the ramp reaches the target in exactly `max(1, round(seconds*fs))` samples and lands on it exactly.

- [ ] **Step 1: Write the failing test** `tests/seam_ramp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_ramp.h"
#include <cmath>

using Seam::LinearRamp;

TEST_CASE("reaches the target in round(seconds*fs) samples, exactly") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        LinearRamp r;
        r.setTarget(0.0, 0.0, fs); r.snap();
        r.setTarget(1.0, 0.025, fs);
        const long n = std::lround(0.025 * fs);
        for (long k = 0; k < n - 1; ++k) { r.next(); REQUIRE(r.active()); }
        CHECK(r.next() == 1.0);
        CHECK_FALSE(r.active());
        CHECK(r.next() == 1.0);
    }
}

TEST_CASE("linear: the midpoint is half way") {
    LinearRamp r;
    r.setTarget(100.0, 0.0, 1000.0); r.snap();
    r.setTarget(200.0, 1.0, 1000.0);
    for (int k = 0; k < 500; ++k) r.next();
    CHECK(std::fabs(r.value() - 150.0) < 1e-9);
}

TEST_CASE("a new target mid-ramp starts from the current value") {
    LinearRamp r;
    r.setTarget(0.0, 0.0, 1000.0); r.snap();
    r.setTarget(1.0, 1.0, 1000.0);
    for (int k = 0; k < 400; ++k) r.next();
    const double v = r.value();
    r.setTarget(0.0, 0.1, 1000.0);
    const double first = r.next();
    CHECK(first < v);
    CHECK(std::fabs((v - first) - v / 100.0) < 1e-12);
}

TEST_CASE("zero seconds is one sample") {
    LinearRamp r;
    r.setTarget(5.0, 0.0, 48000.0);
    CHECK(r.next() == 5.0);
}

TEST_CASE("snap jumps to the target") {
    LinearRamp r;
    r.setTarget(3.0, 10.0, 48000.0);
    r.snap();
    CHECK(r.value() == 3.0);
    CHECK_FALSE(r.active());
}
```

CMake block (same pattern as Task 2, target `seam_ramp_test`, include `plugins/_common`).

- [ ] **Step 2: Run to see it fail.** Expected: `'seam_ramp.h' file not found`.

- [ ] **Step 3: Implement** `plugins/_common/seam_ramp.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_ramp.h — a linear ramp anchored in time
//
// The semantics of Pure Data's `line`: a new target starts from wherever
// the ramp is, and is reached in a duration given in SECONDS, converted to
// samples at the session's rate — so a 120 s glissando lasts 120 s at any
// rate. Unlike `line`, which Pd updates once per 64-sample block, this ramp
// steps every sample and lands on the target exactly (no accumulated
// rounding left at the end).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class LinearRamp {
public:
    void setTarget(double target, double seconds, double fs) {
        target_ = target;
        long n = std::lround(seconds * fs);
        if (n < 1) n = 1;
        remaining_ = n;
        inc_ = (target_ - value_) / (double)n;
    }

    void snap() { value_ = target_; remaining_ = 0; inc_ = 0.0; }

    double next() {
        if (remaining_ > 0) {
            if (--remaining_ == 0) value_ = target_;
            else                   value_ += inc_;
        }
        return value_;
    }

    double value()  const { return value_; }
    double target() const { return target_; }
    bool   active() const { return remaining_ > 0; }

private:
    double value_ = 0.0, target_ = 0.0, inc_ = 0.0;
    long remaining_ = 0;
};

} // namespace Seam
```

- [ ] **Step 4: Run to see it pass.** `ctest --test-dir build-test -C Release -R seam_ramp_test --output-on-failure` → passed.

- [ ] **Step 5: Mutation check.** (a) `long n = (long)(seconds * fs)` (truncation instead of rounding) at 44.1 kHz: 0.025·44100 = 1102.5 → the exact-length test must go RED; (b) remove the `value_ = target_` landing: the `== 1.0` exactness check must go RED at some rate. Record in `mutations.md`.

- [ ] **Step 6: Commit.** `git commit -m "feat(_common): seam_ramp.h, a linear ramp anchored in seconds"`

---

### Task 4: The LMO engine

**Files:**
- Create: `plugins/lmo/source/lmo_dsp.h`, `tests/lmo_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::FaustMultinoise`, `Seam::ButterworthSVF<24>`, `Seam::LinearRamp`, `lmoref::kLmo96`, `lmoref::kLmo48`.
- Produces (namespace `lmo`): constants `kChannels = 4`, `kRefRate = 96000.0`, `kShortRamp = 0.025`, `kUpdateRate = 6000.0`, `kLpOffset = 0.0001`, `kMinBand = 1.0`; `double densityGain(double fs)`; `int updatePeriod(double fs)`; `class Engine` with `void prepare(double fs)`, `void reset()`, `void setFrequency(double hz)`, `void setGlide(double seconds)`, `void setDelta(double hz)`, `void setVolume(double v)`, `void setPower(bool on)`, `double currentFrequency() const`, `template<class T> void process(T* const* out, int n)`.

- [ ] **Step 1: Write the failing test** `tests/lmo_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lmo_dsp.h"
#include "ref/lmo_ref.h"
#include <vector>
#include <cmath>
#include <algorithm>

using lmo::Engine;

// A settled engine: targets set, then reset() snaps every ramp.
static Engine settled(double fs, double f, double d) {
    Engine e;
    e.prepare(fs);
    e.setFrequency(f); e.setDelta(d); e.setVolume(1.0); e.setPower(true);
    e.reset();
    return e;
}

static std::vector<std::vector<double>> render(Engine& e, int n, int block = 512) {
    std::vector<std::vector<double>> y(4, std::vector<double>((size_t)n));
    for (int pos = 0; pos < n; pos += block) {
        const int m = std::min(block, n - pos);
        double* out[4] = { y[0].data() + pos, y[1].data() + pos, y[2].data() + pos, y[3].data() + pos };
        e.process(out, m);
    }
    return y;
}

static double relErr(const std::vector<std::vector<double>>& y, const double (*ref)[2048], int n) {
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < n; ++k) {
            e  = std::max(e, std::fabs(y[(size_t)c][(size_t)k] - ref[c][k]));
            pk = std::max(pk, std::fabs(ref[c][k]));
        }
    return e / pk;
}

TEST_CASE("equals sdt.lmo(4, 97.44, 20) at 96 kHz") {
    Engine e = settled(96000.0, 97.44, 20.0);
    auto y = render(e, 2048);
    CHECK(relErr(y, lmoref::kLmo96, 2048) < 1e-12);
}

TEST_CASE("equals sdt.lmo(4, 97.44, 20) at 48 kHz, density included") {
    Engine e = settled(48000.0, 97.44, 20.0);
    auto y = render(e, 1024);
    double err = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 1024; ++k) {
            err = std::max(err, std::fabs(y[(size_t)c][(size_t)k] - lmoref::kLmo48[c][k]));
            pk  = std::max(pk, std::fabs(lmoref::kLmo48[c][k]));
        }
    CHECK(err / pk < 1e-12);
}

TEST_CASE("density holds the band's noise energy at its 96 kHz value") {
    // Deterministic: output variance of white noise through H is
    // sigma^2 * sum(h^2). sum(h^2) of a fixed analog band scales as 1/fs;
    // density^2 = fs/96000 cancels it.
    auto bandEnergy = [](double fs) {
        Seam::ButterworthSVF<24> hp, lp;
        hp.setType(Seam::ButterworthType::Highpass); lp.setType(Seam::ButterworthType::Lowpass);
        hp.setFrequency(1000.0, fs); lp.setFrequency(1000.0 - lmo::kLpOffset, fs);
        double e = 0.0;
        for (int k = 0; k < (int)fs; ++k) {
            const double h = lp.tick(hp.tick(k == 0 ? 1.0 : 0.0));
            e += h * h;
        }
        const double g = lmo::densityGain(fs);
        return 10.0 * std::log10(e * g * g);
    };
    const double ref = bandEnergy(96000.0);
    CHECK(std::fabs(bandEnergy(48000.0)  - ref) < 0.01);
    CHECK(std::fabs(bandEnergy(44100.0)  - ref) < 0.01);
    CHECK(std::fabs(bandEnergy(192000.0) - ref) < 0.01);
}

TEST_CASE("output level at 48 kHz matches 96 kHz (statistical)") {
    auto levelDb = [](double fs) {
        Engine e = settled(fs, 1000.0, 0.0);
        const int n = (int)(fs * 20.0);
        double s = 0.0;
        double buf[4][512];
        double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
        for (int pos = 0; pos < n; pos += 512) {
            e.process(out, 512);
            for (int c = 0; c < 4; ++c) for (int k = 0; k < 512; ++k) s += buf[c][k] * buf[c][k];
        }
        return 10.0 * std::log10(s / (4.0 * n));
    };
    // 4 channels x 20 s x ~73 Hz bandwidth: ~1.3 % power deviation per run (1 sigma)
    CHECK(std::fabs(levelDb(48000.0) - levelDb(96000.0)) < 0.25);
}

TEST_CASE("glide: 97.44 -> 112.67 Hz in 120 s, monotonic, no jumps") {
    const double fs = 48000.0;
    Engine e = settled(fs, 97.44, 0.0);
    e.setGlide(120.0);
    e.setFrequency(112.67);
    const int block = 480;
    double buf[4][480]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    double prev = e.currentFrequency(), maxStep = 0.0;
    bool monotonic = true;
    const long total = (long)(121.0 * fs);
    long reachedAt = -1;
    for (long pos = 0; pos < total; pos += block) {
        e.process(out, block);
        const double f = e.currentFrequency();
        if (f < prev) monotonic = false;
        maxStep = std::max(maxStep, f - prev);
        prev = f;
        if (reachedAt < 0 && f == 112.67) reachedAt = pos + block;
    }
    CHECK(monotonic);
    CHECK(reachedAt >= (long)(120.0 * fs));
    CHECK(reachedAt <= (long)(120.0 * fs) + block);
    // per block of 480 samples the ramp moves (112.67-97.44)/(120*fs)*480
    CHECK(maxStep < 1.01 * (112.67 - 97.44) / (120.0 * fs) * block);
}

TEST_CASE("same target does not restart the glide") {
    const double fs = 48000.0;
    Engine e = settled(fs, 97.44, 0.0);
    e.setGlide(120.0);
    e.setFrequency(112.67);
    double buf[4][480]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    for (int b = 0; b < 1000; ++b) { e.setFrequency(112.67); e.process(out, 480); }
    // 480000 samples = 10 s of the 120 s ramp
    const double expected = 97.44 + (112.67 - 97.44) * 10.0 / 120.0;
    CHECK(std::fabs(e.currentFrequency() - expected) < 1e-6);
}

TEST_CASE("glide 0 uses the 25 ms ramp") {
    const double fs = 96000.0;
    Engine e = settled(fs, 100.0, 0.0);
    e.setFrequency(200.0);
    double buf[4][2400]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 2399);
    CHECK(e.currentFrequency() < 200.0);
    e.process(out, 1);
    CHECK(e.currentFrequency() == 200.0);
}

TEST_CASE("volume ramp: 25 ms, measured against the full-volume output") {
    for (double fs : {48000.0, 96000.0}) {
        Engine full = settled(fs, 97.44, 0.0);
        Engine ramp; ramp.prepare(fs);
        ramp.setFrequency(97.44); ramp.setPower(true); ramp.setVolume(0.0);
        ramp.reset();
        ramp.setVolume(1.0);
        const int n = (int)(0.05 * fs);
        auto a = render(full, n), b = render(ramp, n);
        const long rampLen = std::lround(0.025 * fs);
        // gain at sample k (1-based count of next() calls) is k/rampLen
        for (long k : {rampLen / 2, rampLen - 2, rampLen - 1, rampLen + 10}) {
            const double g = (k + 1 >= rampLen) ? 1.0 : (double)(k + 1) / rampLen;
            CHECK(std::fabs(b[0][(size_t)k] - g * a[0][(size_t)k]) < 1e-12);
        }
    }
}

TEST_CASE("power reverses from the current gain") {
    const double fs = 48000.0;
    Engine e; e.prepare(fs);
    e.setFrequency(97.44); e.setVolume(1.0); e.setPower(false);
    e.reset();
    e.setPower(true);
    double buf[4][600]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 600);       // half of the 1200-sample ramp
    e.setPower(false);
    Engine ref = settled(fs, 97.44, 0.0);
    double rbuf[4][600]; double* rout[4] = { rbuf[0], rbuf[1], rbuf[2], rbuf[3] };
    ref.process(rout, 600);
    e.process(out, 1);
    ref.process(rout, 1);
    // gain was 0.5 at sample 599; first sample after the reversal: 0.5 - 0.5/1200
    const double g = 0.5 - 0.5 / 1200.0;
    CHECK(std::fabs(buf[0][0] - g * rbuf[0][0]) < 1e-12);
}

TEST_CASE("output independent of block partition") {
    Engine a = settled(96000.0, 97.44, 20.0), b = settled(96000.0, 97.44, 20.0);
    a.setGlide(0.5); a.setFrequency(150.0);
    b.setGlide(0.5); b.setFrequency(150.0);
    auto ya = render(a, 20000, 512);
    std::vector<std::vector<double>> yb(4, std::vector<double>(20000));
    const int sizes[] = { 1, 7, 13, 16, 17, 100, 255 };
    int pos = 0, i = 0;
    while (pos < 20000) {
        const int m = std::min(sizes[i++ % 7], 20000 - pos);
        double* out[4] = { yb[0].data() + pos, yb[1].data() + pos, yb[2].data() + pos, yb[3].data() + pos };
        b.process(out, m);
        pos += m;
    }
    for (int c = 0; c < 4; ++c) CHECK(ya[(size_t)c] == yb[(size_t)c]);
}

TEST_CASE("extreme f and delta stay finite") {
    Engine e = settled(44100.0, 20.0, 50.0);
    auto y = render(e, 44100);
    bool finite = true; double pk = 0.0;
    for (auto& ch : y) for (double v : ch) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
    CHECK(finite);
    CHECK(pk < 1.0);
}

TEST_CASE("re-prepare at a new rate") {
    Engine e = settled(48000.0, 97.44, 0.0);
    e.prepare(96000.0);
    e.setFrequency(200.0);   // glide 0: 25 ms at 96 kHz = 2400 samples
    double buf[4][2400]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 2399);
    CHECK(e.currentFrequency() < 200.0);
    e.process(out, 1);
    CHECK(e.currentFrequency() == 200.0);
    CHECK(lmo::updatePeriod(96000.0) == 16);
    CHECK(lmo::updatePeriod(48000.0) == 8);
}

TEST_CASE("float buffers carry the same signal") {
    Engine a = settled(96000.0, 97.44, 20.0), b = settled(96000.0, 97.44, 20.0);
    auto yd = render(a, 1024);
    std::vector<std::vector<float>> yf(4, std::vector<float>(1024));
    float* out[4] = { yf[0].data(), yf[1].data(), yf[2].data(), yf[3].data() };
    b.process(out, 1024);
    for (int k = 0; k < 1024; ++k) CHECK(yf[0][(size_t)k] == (float)yd[0][(size_t)k]);
}
```

CMake block:

```cmake
add_executable(lmo_dsp_test lmo_dsp_test.cpp)
target_include_directories(lmo_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lmo/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(lmo_dsp_test PRIVATE cxx_std_17)
add_test(NAME lmo_dsp_test COMMAND lmo_dsp_test)
```

- [ ] **Step 2: Run to see it fail.** Expected: `'lmo_dsp.h' file not found`.

- [ ] **Step 3: Implement** `plugins/lmo/source/lmo_dsp.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — the engine (SDK-free)
//
// sdt.lmo(4, f, d) of seam.tedesco.lib: per channel i (0..3), two bands of
// noise, HP24 : LP24 at the same centre, one at f - d/2 + i (held >= 1 Hz),
// one at f + d/2 + i; streams 0..3 of ONE multinoise(8) feed the low bands,
// streams 4..7 the high ones; channel i = (low + high)/sqrt(2), times the
// density anchor sqrt(fs/96000), times volume and POWER.
//
// Everything that is time is seconds: the ramps (glide for f, 25 ms for
// d, volume and POWER, the ramp of the original's interpolator_4ch), and
// the cadence of the filter redesign, 6 kHz (16 samples at 96 kHz).
// Ramps step every sample; the filters follow every update period, and
// only when f or d has actually moved.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_noise.h"
#include "seam_butterworth.h"
#include "seam_ramp.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace lmo {

constexpr int    kChannels   = 4;
constexpr int    kOrder      = 24;        // the original's Butterworth order
constexpr double kRefRate    = 96000.0;   // SSCDO#2 is played at 96 kHz
constexpr double kShortRamp  = 0.025;     // s
constexpr double kUpdateRate = 6000.0;    // Hz, filter redesign cadence
constexpr double kLpOffset   = 0.0001;    // Hz, the original's LP offset
constexpr double kMinBand    = 1.0;       // Hz, lowest band centre

// White noise of constant RMS spreads its power over fs/2: a band of fixed
// width gets 3.01 dB less per doubling of fs. This factor holds it at its
// 96 kHz level (sdt.lmodens).
inline double densityGain(double fs) { return std::sqrt(fs / kRefRate); }

inline int updatePeriod(double fs) {
    return std::max(1, (int)std::lround(fs / kUpdateRate));
}

class Band {
public:
    Band() {
        hp_.setType(Seam::ButterworthType::Highpass);
        lp_.setType(Seam::ButterworthType::Lowpass);
    }
    void set(double fb, double fs) {
        hp_.setFrequency(fb, fs);
        lp_.setFrequency(fb - kLpOffset, fs);
    }
    void reset() { hp_.reset(); lp_.reset(); }
    double tick(double x) { return lp_.tick(hp_.tick(x)); }
private:
    Seam::ButterworthSVF<kOrder> hp_, lp_;
};

class Engine {
public:
    Engine() : noise_(2 * kChannels) {}

    void prepare(double fs) {
        fs_ = fs;
        period_ = updatePeriod(fs);
        density_ = densityGain(fs);
        reset();
    }

    // Noise to its seed, filters to zero, every ramp onto its target.
    void reset() {
        noise_.reset();
        for (auto& b : bands_) b.reset();
        f_.snap(); d_.snap(); vol_.snap(); pow_.snap();
        design(f_.value(), d_.value());
        countdown_ = 0;
    }

    // glide applies to the NEXT frequency target, as Pd's line takes its
    // time before its target: a cue sets glide, then f.
    void setGlide(double seconds) { glide_ = std::max(0.0, seconds); }

    void setFrequency(double hz) {
        if (hz == f_.target()) return;   // automation re-sends; never restart
        f_.setTarget(hz, glide_ > 0.0 ? glide_ : kShortRamp, fs_);
    }
    void setDelta(double hz) {
        if (hz == d_.target()) return;
        d_.setTarget(hz, kShortRamp, fs_);
    }
    void setVolume(double v) {
        if (v == vol_.target()) return;
        vol_.setTarget(v, kShortRamp, fs_);
    }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t == pow_.target()) return;
        pow_.setTarget(t, kShortRamp, fs_);
    }

    double currentFrequency() const { return f_.value(); }

    template <class T>
    void process(T* const* out, int n) {
        double nz[2 * kChannels];
        for (int i = 0; i < n; ++i) {
            if (countdown_ == 0) {
                if (f_.value() != fDesigned_ || d_.value() != dDesigned_)
                    design(f_.value(), d_.value());
                countdown_ = period_;
            }
            --countdown_;
            f_.next();
            d_.next();

            noise_.tick(nz);
            const double g = density_ * vol_.next() * pow_.next();
            for (int c = 0; c < kChannels; ++c) {
                const double lo = bands_[c].tick(nz[c]);
                const double hi = bands_[kChannels + c].tick(nz[kChannels + c]);
                out[c][i] = (T)((lo + hi) / std::sqrt(2.0) * g);
            }
        }
    }

private:
    void design(double f, double d) {
        for (int c = 0; c < kChannels; ++c) {
            bands_[c].set(std::max(kMinBand, f - d / 2.0 + c), fs_);
            bands_[kChannels + c].set(f + d / 2.0 + c, fs_);
        }
        fDesigned_ = f;
        dDesigned_ = d;
    }

    double fs_ = kRefRate, density_ = 1.0, glide_ = 0.0;
    int period_ = 16, countdown_ = 0;
    double fDesigned_ = -1.0, dDesigned_ = -1.0;

    Seam::FaustMultinoise noise_;
    std::array<Band, 2 * kChannels> bands_;
    Seam::LinearRamp f_, d_, vol_, pow_;
};

} // namespace lmo
```

Note on initial targets: `LinearRamp` starts at 0, so a fresh engine's f target is 0 Hz until the processor sets 48 Hz; `design()` with f = 0 clamps the low bands to 1 Hz and puts the high bands at 0..3 Hz, which is stable (g ≥ 0). The processor always sets every parameter before `reset()`.

- [ ] **Step 4: Run to see it pass.**

Run: `cmake --build build-test --config Release --target lmo_dsp_test && ctest --test-dir build-test -C Release -R lmo_dsp_test --output-on-failure`
Expected: all test cases pass. If "equals sdt.lmo" fails at ~1e-16 · k level only, the tolerance is fine; if it fails grossly, compare channel 0 of streams first (Task 1's noise order) before touching anything else.

- [ ] **Step 5: Mutation checks** — each must turn the named test RED; restore after each; record in `mutations.md`:
  1. density removed (`density_ = 1.0` in `prepare`) → "48 kHz, density included" and "density holds".
  2. low and high streams swapped (`nz[kChannels + c]` for the low band) → "equals sdt.lmo ... 96 kHz".
  3. `setFrequency` without the same-target guard → "same target does not restart the glide".
  4. the volume ramp converted at a fixed rate (`vol_.setTarget(v, kShortRamp, 48000.0)`) → "volume ramp" at 96 kHz.
  5. `countdown_` reset per `process()` call (move `countdown_ = 0;` to the top of `process`) → "output independent of block partition".
  6. clamp removed (`f - d / 2.0 + c`) → "extreme f and delta stay finite" (if it stays green, the negative-g filter happens to be stable: record that and keep the clamp anyway, since the spec has it).

- [ ] **Step 6: Commit.** `git commit -m "feat(lmo): SDK-free engine, sdt.lmo(4, f, d) with glide and 96 kHz density"`

---

### Task 5: The VST3 plugin

**Files:**
- Create: `plugins/lmo/CMakeLists.txt`, `plugins/lmo/source/lmo_ids.h`, `plugins/lmo/source/version.h`, `plugins/lmo/source/lmo_processor.h`, `plugins/lmo/source/lmo_processor.cpp`, `plugins/lmo/resource/lmo.uidesc`
- Modify: `CMakeLists.txt` (root: `add_subdirectory(plugins/lmo)` after `plugins/strx`)

**Interfaces:**
- Consumes: `lmo::Engine` (Task 4), `Seam::readStateDoubles` (`_common/seam_state.h`).

- [ ] **Step 1: The window first** (the suite's rule: lint before C++). `plugins/lmo/resource/lmo.uidesc`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="KnobLabelFont" size="13"/>
        <font font-name="Source Code Pro Light" name="ValueFont" size="12"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="12"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
        <color name="Structure" rgba="#888888ff"/>
    </colors>

    <!-- S format: 300 px, one column at x=20 of width 260. Five fine
         controls or fewer: POWER in OPS, then f, glide, delta, volume. No
         SETUP zone: LMO has no station identity. Zone order HEADER, OPS,
         FINE, FOOTER. -->
    <template name="view" class="CViewContainer" origin="0, 0" size="300, 490"
              minSize="300, 490" maxSize="300, 490"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 18" size="300, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM LMO" transparent="true"/>
        <view class="CTextLabel" origin="0, 46" size="300, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Studio sul Corpo d'Ombra #2" transparent="true"/>
        <view class="CTextLabel" origin="0, 66" size="300, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center"
              title="two beating noise bands &#xB7; four channels" transparent="true"/>

        <!-- ── OPS ───────────────────────────────────────────────────────
             POWER on means sounding. Box and caption centred on 150 as in
             multipink. -->
        <view class="CCheckBox" origin="121, 100" size="14, 14" control-tag="Power"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>
        <view class="CTextLabel" origin="139, 98" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="POWER" transparent="true"/>

        <!-- ── FINE ───────────────────────────────────────────────────────
             Blocks of label 14 / slider 18 / value 16 on a 58 px stride. -->
        <view class="CTextLabel" origin="20, 126" size="260, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="f (Hz)" transparent="true"/>
        <view class="CSlider" origin="20, 142" size="260, 18" control-tag="Frequency"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 162" size="260, 16" font="ValueFont" control-tag="Frequency"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <view class="CTextLabel" origin="20, 184" size="260, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="glide (s)" transparent="true"/>
        <view class="CSlider" origin="20, 200" size="260, 18" control-tag="Glide"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 220" size="260, 16" font="ValueFont" control-tag="Glide"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <view class="CTextLabel" origin="20, 242" size="260, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="&#x394; (Hz)" transparent="true"/>
        <view class="CSlider" origin="20, 258" size="260, 18" control-tag="Delta"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 278" size="260, 16" font="ValueFont" control-tag="Delta"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <view class="CTextLabel" origin="20, 300" size="260, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="volume" transparent="true"/>
        <view class="CSlider" origin="20, 316" size="260, 18" control-tag="Volume"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 336" size="260, 16" font="ValueFont" control-tag="Volume"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="3" style-no-frame="true"/>

        <!-- ── FOOTER — what the plugin reports, then the logo ──────────
             f now: the band centre during a glissando. -->
        <view class="CTextLabel" origin="20, 372" size="80, 16" font="InfoFont"
              font-color="TextLight" text-alignment="left" title="f now:" transparent="true"/>
        <view class="CParamDisplay" origin="100, 372" size="180, 16" control-tag="FNow"
              font="InfoFont" font-color="TextLight" text-alignment="left" transparent="true" value-precision="2"/>

        <!-- SEAM logo (native 240x77 — CView does not scale bitmaps) -->
        <view class="CView" origin="30, 400" size="240, 77" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags>
        <control-tag name="Power"     tag="100"/>
        <control-tag name="Frequency" tag="101"/>
        <control-tag name="Glide"     tag="102"/>
        <control-tag name="Delta"     tag="103"/>
        <control-tag name="Volume"    tag="104"/>
        <control-tag name="FNow"      tag="200"/>
    </control-tags>
</vstgui-ui-description>
```

- [ ] **Step 2: Lint.** Run: `python3 tools/check-uidesc.py plugins/lmo/resource/lmo.uidesc`
Expected: 0 errors, 0 warnings. If it complains, fix the `.uidesc` per its message (it encodes `ui-style.md`) and re-run; do not edit the linter.

- [ ] **Step 3: `lmo_ids.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 17th plugin in the suite. word3 = ASCII "LMO\0".
static const Steinberg::FUID LmoProcessorUID (0x5E4D0010, 0xA1B2C3D4, 0x4C4D4F00, 0x00000010);

enum LmoParams : Steinberg::Vst::ParamID {
    kParamPower     = 100,   // off / on
    kParamFrequency = 101,   // Hz
    kParamGlide     = 102,   // s, time of the next frequency move
    kParamDelta     = 103,   // Hz, distance between the two bands
    kParamVolume    = 104,   // linear, CC81 in the original
    kParamFNow      = 200    // read-only: the band centre now
};

static constexpr double kLmoFMin     = 20.0;
static constexpr double kLmoFMax     = 1500.0;   // the committed .dsp range
static constexpr double kLmoFDefault = 48.0;     // cue 0
static constexpr double kLmoGlideMax = 300.0;
static constexpr double kLmoDeltaMax = 50.0;

} // namespace Seam
```

- [ ] **Step 4: `version.h`:**

```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — Version and metadata
//─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "lmo.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM LMO – SSCDO#2 noise-band generator (64Bit)"
#else
#define stringFileDescription   "SEAM LMO – SSCDO#2 noise-band generator"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 5: `lmo_processor.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The generator of SSCDO#2: on each of the four drivers of STONED, two
// narrow bands of noise that can beat at a distance delta.
//
// FAUST REFERENCE (seam.tedesco.lib):
//
//   lmoband(f)  = fi.highpass(24, f) : fi.lowpass(24, f - 0.0001);
//   lmodens     = sqrt(ma.SR/96000);
//   lmo(N,f,d)  = no.multinoise(2*N)
//               : par(i, N, lmoband(max(1, f - d/2 + i))),
//                 par(i, N, lmoband(f + d/2 + i))
//               :> par(i, N, /(sqrt(2)) : *(lmodens));
//
// Re-implemented by hand (seam-ltm convention) in lmo_dsp.h on three
// reusable headers: seam_noise.h (no.multinoise bit for bit),
// seam_butterworth.h (Smith's SVF Butterworth sections), seam_ramp.h.
//
// SR rule of the SSCDO#2 port: it sounds as at 96 kHz at any rate.
// Frequencies are designed at the session's rate, ramps are in seconds,
// and lmodens holds each band at its 96 kHz level.
//
// What the plugin adds to the spec: the glissando of the cues (f moves
// linearly in Hz over `glide` seconds, Pd line semantics; glide 0 = 25 ms,
// which smooths host automation), 25 ms ramps on delta, volume and POWER,
// and a read-only "f now".
//
// Studies and decisions: doc/study/sscdo2/ (lmo-*), logs/2026-10-01-sscdo2-plugins.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "lmo_dsp.h"

namespace Seam {

class LmoProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LmoProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new LmoProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    // Denormalize the parameters and hand them to the engine, on the audio
    // thread. glide goes first: it is the time of the next f move.
    void applyParams();
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }

    lmo::Engine engine_;
};

} // namespace Seam
```

- [ ] **Step 6: `lmo_processor.cpp`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "lmo_processor.h"
#include "lmo_ids.h"
#include "version.h"
#include "seam_state.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static const ParamID kStateIds[5] = {
    kParamPower, kParamFrequency, kParamGlide, kParamDelta, kParamVolume
};

tresult PLUGIN_API LmoProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // A generator still declares an input: with zero input buses a host
    // routes the track AROUND the insert (reference: multipink). The input
    // is never read; process() writes every output channel.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    auto* f = new RangeParameter(STR16("f"), kParamFrequency, STR16("Hz"),
        kLmoFMin, kLmoFMax, kLmoFDefault, 0, ParameterInfo::kCanAutomate);
    f->setPrecision(2);
    parameters.addParameter(f);

    auto* glide = new RangeParameter(STR16("Glide"), kParamGlide, STR16("s"),
        0.0, kLmoGlideMax, 0.0, 0, ParameterInfo::kCanAutomate);
    glide->setPrecision(2);
    parameters.addParameter(glide);

    auto* delta = new RangeParameter(STR16("Delta"), kParamDelta, STR16("Hz"),
        0.0, kLmoDeltaMax, 0.0, 0, ParameterInfo::kCanAutomate);
    delta->setPrecision(2);
    parameters.addParameter(delta);

    auto* vol = new RangeParameter(STR16("Volume"), kParamVolume, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    vol->setPrecision(3);
    parameters.addParameter(vol);

    auto* fnow = new RangeParameter(STR16("f now"), kParamFNow, STR16("Hz"),
        kLmoFMin, kLmoFMax, kLmoFDefault, 0, ParameterInfo::kIsReadOnly);
    fnow->setPrecision(2);
    parameters.addParameter(fnow);

    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LmoProcessor::setupProcessing(ProcessSetup& setup) {
    tresult res = SingleComponentEffect::setupProcessing(setup);
    engine_.prepare(sampleRate());
    return res;
}

tresult PLUGIN_API LmoProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        applyParams();      // recalled state becomes the targets...
        engine_.reset();    // ...and the engine starts ON them, no glide from defaults
    }
    return SingleComponentEffect::setActive(state);
}

void LmoProcessor::applyParams() {
    auto plain = [&](ParamID id) -> double {
        auto* p = parameters.getParameter(id);
        return p ? p->toPlain(p->getNormalized()) : 0.0;
    };
    engine_.setGlide(plain(kParamGlide));
    engine_.setFrequency(plain(kParamFrequency));
    engine_.setDelta(plain(kParamDelta));
    engine_.setVolume(plain(kParamVolume));
    engine_.setPower(plain(kParamPower) >= 0.5);
}

tresult PLUGIN_API LmoProcessor::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        const int32 nq = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < nq; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const int32 np = q->getPointCount();
            if (np <= 0) continue;
            int32 off; ParamValue v;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                setParamNormalized(q->getParameterId(), v);
        }
    }
    applyParams();   // the engine ignores unchanged targets

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A generator is never silent by inheritance (reference: multipink).
        data.outputs[0].silenceFlags = 0;
        const int32 outCh = data.outputs[0].numChannels;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        if (outCh < lmo::kChannels) {
            const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
            for (int32 c = 0; c < outCh; ++c) if (out[c]) memset(out[c], 0, bytes);
        } else if (data.symbolicSampleSize == kSample32) {
            engine_.process(reinterpret_cast<float* const*>(out), data.numSamples);
        } else {
            engine_.process(reinterpret_cast<double* const*>(out), data.numSamples);
        }
    }

    if (auto* oc = data.outputParameterChanges) {
        int32 idx;
        if (auto* q = oc->addParameterData(kParamFNow, idx)) {
            const double f = std::min(kLmoFMax, std::max(kLmoFMin, engine_.currentFrequency()));
            int32 off = 0;
            q->addPoint(0, (f - kLmoFMin) / (kLmoFMax - kLmoFMin), off);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// State: five normalized doubles, append-only (seam_state.h): a short blob
// keeps the registered defaults for the fields it lacks.
tresult PLUGIN_API LmoProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    double saved[5];
    for (int i = 0; i < 5; ++i) {
        auto* p = parameters.getParameter(kStateIds[i]);
        saved[i] = p ? p->getInfo().defaultNormalizedValue : 0.0;
    }
    Seam::readStateDoubles(state, saved, 5);
    for (int i = 0; i < 5; ++i) setParamNormalized(kStateIds[i], saved[i]);
    return kResultOk;   // the engine picks the values up in the next process()
}

tresult PLUGIN_API LmoProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    for (ParamID id : kStateIds) {
        auto* p = parameters.getParameter(id);
        s.writeDouble(p ? p->getNormalized() : 0.0);
    }
    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API LmoProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "lmo.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::LmoProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM LMO",
        0,
        "Fx|Generator",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::LmoProcessor::createInstance)
END_FACTORY
```

Add `#include <algorithm>` with the other standard includes (for `std::min/max`).

- [ ] **Step 7: `plugins/lmo/CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-lmo
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM LMO – SSCDO#2 noise-band generator"
)

set(lmo_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/lmo_ids.h
    source/lmo_dsp.h
    source/lmo_processor.cpp
    source/lmo_processor.h
    source/version.h
    resource/lmo.uidesc
)

set(target lmo)

smtg_add_vst3plugin(${target} ${lmo_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/lmo.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.lmo"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

Root `CMakeLists.txt`: add `    add_subdirectory(plugins/lmo)` after `    add_subdirectory(plugins/strx)`.

- [ ] **Step 8: Build.**

Run: `cmake -S . -B build && cmake --build build --config Release --target lmo 2>&1 | tail -5`
Expected: `** BUILD SUCCEEDED **`. (`--config Release` strips the live editor and makes `build` own the VST3 symlink.)

- [ ] **Step 9: Validate.**

Run: `build/bin/Release/validator build/VST3/Release/lmo.vst3 2>&1 | tail -5`
Expected: all tests passed (the suite's plugins report 47/47) and `[1 In(s) => 1 Out(s)]`. If the path differs, `find build -name lmo.vst3 -maxdepth 4`.

- [ ] **Step 10: Whole test suite and lint.**

Run: `cmake --build build-test --config Release && ctest --test-dir build-test -C Release`
Expected: all tests pass, `uidesc_lint` included (it lints every plugin's `.uidesc`, LMO's now too).

- [ ] **Step 11: Commit.** `git commit -m "feat(lmo): the VST3 plugin, window in format S"`

---

### Task 6: Plugin and study documentation, session log

**Files:**
- Create: `plugins/lmo/doc/README.md`, `doc/study/sscdo2/lmo-plugin/README.md`, `logs/2026-10-01-sscdo2-plugins.md`
- Modify: `doc/study/sscdo2/README.md` (table row), `logs/2026-09-29-sscdo2-ricognizione.md` (one line pointing to the new log)

- [ ] **Step 1: `plugins/lmo/doc/README.md`** — sections: *What it is* (LMO in SSCDO#2, four channels LFU, RFD, RBU, LBD); *Parameters* (the table of the spec, plus "glide is the time of the NEXT f move: a cue sets glide, then f"); *Sample-rate rule* (density anchor with the 3.01 dB/doubling reason, ramps in seconds, 6 kHz redesign cadence); *Specification* (the `sdt.lmo` lines and where the tests prove equality); *Out of scope* (cues and MIDI live in Reaper). One sentence per line.

- [ ] **Step 2: `doc/study/sscdo2/lmo-plugin/README.md`** — per the scope-README rule: what each file is (`gen-ref.sh`, `refdump.cpp`, `dsp/*.dsp`, `mutations.md`), how to run (`FAUSTLIBS=... ./gen-ref.sh`, then the ctest commands), how it fits (references → `tests/ref/lmo_ref.h` → `seam_noise_test`, `seam_butterworth_test`, `lmo_dsp_test`), and the measured results (the actual numbers printed by the tests: relative error of each Faust comparison, the 48/96 level difference). Add a row to `doc/study/sscdo2/README.md`'s table: `| LMO | the C++ plugin | lmo-plugin/ | plugins/lmo, C++ == sdt.lmo to <1e-12 | none |`.

- [ ] **Step 3: `logs/2026-10-01-sscdo2-plugins.md`** — the session log of the C++ phase, in the style of the survey log (headings, one sentence per line). Record every decision with its reason:
  - the SR rule (Giuseppe, 2026-10-01): every process sounds as at 96 kHz;
  - stunedrev: the 2026-09-29 reading confirmed (times invariant, primes per rate);
  - order: LMO first, then stunedrev;
  - glissando option C (f + glide, glide 0 = 25 ms smoothing), Pd line semantics, glide before f;
  - Δ on the 25 ms ramp, not on glide (changed from the in-chat design, with the reason);
  - the density anchor `lmodens`, added to the spec too; the lmo-beats prototype updated, its renders kept as heard;
  - the low band held ≥ 1 Hz (Review Focus), in spec and C++;
  - name `lmo`, FUID `0x5E4D0010`, format S, no SETUP;
  - reusable headers: `seam_noise.h` (with the note that no.multinoise's stream k is step N−1−k, seed configurable for the choir), `seam_butterworth.h` (even orders only, a deviation from the spec), `seam_ramp.h` (not in the spec, added as reusable);
  - the verification: test results, mutation record, validator result;
  - open: Reaper listening at 96 kHz, screenshot, registry, report cards.

  Add to the survey log's end: `C++ phase: see logs/2026-10-01-sscdo2-plugins.md.`

- [ ] **Step 4: Commit.** `git commit -m "docs(lmo): plugin doc, study folder, session log of the C++ phase"`

---

### Task 7: Listening, screenshot, registry, report (with Giuseppe)

**Files:**
- Create: `docs/img/lmo.png` (from Giuseppe)
- Modify: `doc/plugins.toml`, `doc/scripts/test-doc.sh`, `doc/scripts/render-readme.py`, `README.md` (generated), `doc/study/sscdo2/report/parte2-lmo.tex`, `logs/2026-10-01-sscdo2-plugins.md`

- [ ] **Step 1: Ask Giuseppe** to load LMO in Reaper at 96 kHz on a 4-channel track and check: POWER/volume fades without clicks; cue 1 (f = 97.44); cue 2 (glide = 120, then f = 112.67), "f now" moving; Δ sweep 0–50 Hz; switch the project to 48 kHz and compare the level. Ask for the window screenshot saved as `docs/img/lmo.png`. Record his findings in the log.

- [ ] **Step 2: Registry.** Append to `doc/plugins.toml`:

```toml
[[family]]
title = "Works — SSCDO#2"

  [[family.plugin]]
  name = "LMO"
  io = "→ 4ch"
  screenshot = "lmo.png"
  faust = "seam.tedesco.lib"
  description = """
The generator of Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco): on each of four channels, two narrow bands of noise through 24th-order Butterworth high- and low-pass filters, which beat at a distance Δ. The band centre glides linearly over a set time, as the piece's cues ask; the level is anchored at 96 kHz, so the bands sound the same at any sample rate"""
```

Change the three `16`s in `doc/scripts/test-doc.sh` (lines with "16 plugin", "16 schede", "16 screenshot") to `17`, and "All sixteen windows" in `doc/scripts/render-readme.py` to "All seventeen windows". Update the CLAUDE.md sentence "registry of the sixteen plugins" and "all sixteen cards" to seventeen.

- [ ] **Step 3: Regenerate and test.** Run: `make -C doc doc && make -C doc test`. Expected: 12 checks ok.

- [ ] **Step 4: Report cards.** In `doc/study/sscdo2/report/parte2-lmo.tex` add to the frequency card the plugin's controls (f, glide; "glide prima di f") and to the volume card the 96 kHz density rule; then `make -C doc/study/sscdo2/report check`. Expected: build ok, every `\misura` verified. Commit sources and the rebuilt PDF together.

- [ ] **Step 5: Commit**, then **ask Giuseppe** before `make -C doc publish` and before any `git push` (seam-ltm and faust-libraries).

---

## Self-review notes

- Spec coverage: architecture (Tasks 1–5), density (0, 4), ramps and cadence (3, 4), parameters/bus/state (5), window (5), tests 1–6 of the spec (1, 2, 4), mutation (every task), docs/log/registry/report (6, 7). Deviations recorded for the log: even orders only; `seam_ramp.h` added; band clamp added to both spec and C++.
- The spec's test 2 asked for N = 1, 2, 3; odd orders are dropped (YAGNI), N = 2 and 24 tested.
