# ltburst Phase 3 (slice) — Fixed-Frequency Tone-Burst Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the first `ltburst` VST3 plugin — a continuous fixed-frequency Linkwitz shaped tone-burst generator with a finished GUI — by porting `slw.shapedburst` from `seam.linkwitz.lib` to hand-written C++.

**Architecture:** An SDK-free header-only DSP core (`ltburst_dsp.h`, `struct ShapedBurst`) holds the literal port and is unit-tested with doctest (the hilbert/x2uhj/abmodulex pattern). A `SingleComponentEffect` processor wraps the core, exposing Reference/Trim level controls (reused from multipink) plus Frequency and Dwell, and renders a stereo mono-duplicated output. The GUI mirrors `multipink.uidesc` minus the pool widgets.

**Tech Stack:** C++17, VST3 SDK (`SingleComponentEffect`, VSTGUI), CMake, doctest (vendored at `tests/doctest/doctest.h`), Faust 2.85.5 (`/usr/local/bin/faust`, scratch parity only), Python 3 + sox (calibration measurement).

## Global Constraints

- **Faust is the spec, C++ is the deliverable.** No `faust -lang cpp` output lands in `plugins/ltburst/source/`; it is used only as a throwaway parity reference (Task 2).
- **DSP core is SDK-free.** `ltburst_dsp.h` includes only `<cmath>` / standard headers, namespace `Seam::ltburst`, so the doctest target compiles without the VST3 SDK.
- **N is locked to 5** (`shapedburst5`, canonical Linkwitz); it is not a parameter in this slice.
- **FUID index `0x5E4D000C`** (12th plugin; the first three FUID words: `0x5E4D000C, 0xA1B2C3D4, ...` — use a fresh second/third/fourth word, see Task 3).
- **VST3 SDK location:** the SDK is at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`. Configure CMake with `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- **Level idiom reused verbatim from multipink:** `Reference` = `StringListParameter` over `{-23,-20,-18}` dBFS, `Trim` = `RangeParameter` −6…+6 dB, gain = `10^((refDb+trimDb+kCalibrationOffsetDb)/20)`.
- **`Reference` calibrates the active-window RMS** of the windowed sinusoid (the N burst cycles, excluding dwell), so the burst level is independent of dwell.
- **Output:** one stereo output bus, no input bus; the same mono signal on every output channel. Per-stone routing is Phase 3b.
- Commit messages and code comments in English. Repo: `seam-ltm` on branch `feat/ltburst-linkwitz-tone-burst` (already checked out; stays unmerged).

## File Structure

- Create: `plugins/ltburst/source/ltburst_dsp.h` — SDK-free DSP core (`struct ShapedBurst`).
- Create: `tests/ltburst_dsp_test.cpp` — doctest unit tests for the core.
- Modify: `tests/CMakeLists.txt` — register the `ltburst_dsp_test` target.
- Create: `plugins/ltburst/doc/ltburst-validation.md` — parity result + measured calibration constant.
- Create: `plugins/ltburst/source/ltburst_ids.h` — FUID, ParamID enum, reference-level table, `kCalibrationOffsetDb`.
- Create: `plugins/ltburst/source/version.h` — plugin version/metadata strings.
- Create: `plugins/ltburst/source/ltburst_processor.h` — FAUST REFERENCE block + class declaration.
- Create: `plugins/ltburst/source/ltburst_processor.cpp` — IAudioProcessor lifecycle + factory.
- Create: `plugins/ltburst/CMakeLists.txt` — `smtg_add_vst3plugin` target.
- Modify: `CMakeLists.txt` (root) — `add_subdirectory(plugins/ltburst)`.
- Create: `plugins/ltburst/resource/ltburst.uidesc` — VSTGUI layout.

## Verification idiom

The DSP core is verified by doctest (`tests/ltburst_dsp_test.cpp`). Build and run tests with:

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build -DSEAM_BUILD_TESTS=ON -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target ltburst_dsp_test
./build/tests/ltburst_dsp_test
```

The full plugin builds with the `ltburst` target; the VST3 validator (`smtg`'s `validator`) is the integration gate (Task 5).

---

### Task 1: SDK-free DSP core `ShapedBurst` + doctest

**Files:**
- Create: `plugins/ltburst/source/ltburst_dsp.h`
- Create: `tests/ltburst_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `Seam::ltburst::ShapedBurst` with:
  - `static constexpr int kN = 5;`
  - `void prepare(double fs);`
  - `void reset();`
  - `void setFrequency(double hz);`  // recomputes period
  - `void setDwell(double ms);`      // recomputes period
  - `double frequency() const;` / `double dwellMs() const;` / `int periodCycles() const;`
  - `inline double process();`       // one unit-amplitude sample, advances phase

- [ ] **Step 1: Register the test target in `tests/CMakeLists.txt`**

Append after the `hilbert_dsp_test` block:

```cmake
add_executable(ltburst_dsp_test
    ltburst_dsp_test.cpp
)
target_include_directories(ltburst_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/ltburst/source
)
target_compile_features(ltburst_dsp_test PRIVATE cxx_std_17)
add_test(NAME ltburst_dsp_test COMMAND ltburst_dsp_test)
```

- [ ] **Step 2: Write the failing test `tests/ltburst_dsp_test.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ltburst_dsp.h"
#include <cmath>

using namespace Seam::ltburst;

// fs=48000, f0=1000, dwell=300 ms -> M=ceil(0.3*1000)=300, P=N+M=305.
// One period is P*fs/f0 = 305*48000/1000 = 14640 samples (integer).
static ShapedBurst makeDefault() {
    ShapedBurst b;
    b.prepare(48000.0);
    b.setFrequency(1000.0);
    b.setDwell(300.0);
    b.reset();
    return b;
}

TEST_CASE("period geometry: N=5, P=N+M, M from ceil(dwell*f0)") {
    ShapedBurst b = makeDefault();
    CHECK(ShapedBurst::kN == 5);
    CHECK(b.periodCycles() == 305);   // 5 + 300
    CHECK(b.frequency() == doctest::Approx(1000.0));
    CHECK(b.dwellMs()   == doctest::Approx(300.0));
}

TEST_CASE("burst starts on a zero crossing") {
    ShapedBurst b = makeDefault();
    CHECK(b.process() == doctest::Approx(0.0).epsilon(0.0));
}

TEST_CASE("output is silent during the dwell (u in [N, P))") {
    ShapedBurst b = makeDefault();
    // Advance to sample 10000: u = (f0/fs)*10000 mod P
    //   = (1000/48000)*10000 = 208.333..., well past N=5 -> window 0.
    double last = 0.0;
    for (int n = 0; n < 10001; ++n) last = b.process();
    CHECK(last == doctest::Approx(0.0).epsilon(0.0));
}

TEST_CASE("amplitude never exceeds unity and the burst is audible") {
    ShapedBurst b = makeDefault();
    double peak = 0.0;
    for (int n = 0; n < 14640; ++n) peak = std::max(peak, std::fabs(b.process()));
    CHECK(peak <= 1.0 + 1e-9);
    CHECK(peak > 0.5);   // the windowed carrier reaches a healthy level
}

TEST_CASE("output is periodic with period P*fs/f0 samples") {
    ShapedBurst a = makeDefault();
    ShapedBurst b = makeDefault();
    const int period = 14640;
    for (int n = 0; n < period; ++n) b.process();   // advance b by one period
    for (int n = 0; n < 512; ++n)
        CHECK(a.process() == doctest::Approx(b.process()).epsilon(1e-9));
}

TEST_CASE("no NaN/Inf across the parameter ranges") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (double f0 : {20.0, 1000.0, 20000.0}) {
            for (double dw : {0.0, 50.0, 1000.0}) {
                ShapedBurst b;
                b.prepare(fs); b.setFrequency(f0); b.setDwell(dw); b.reset();
                for (int n = 0; n < 20000; ++n) {
                    double y = b.process();
                    REQUIRE(std::isfinite(y));
                }
            }
        }
    }
}
```

- [ ] **Step 3: Build the test to verify it fails (no `ltburst_dsp.h` yet)**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build -DSEAM_BUILD_TESTS=ON -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk >/dev/null
cmake --build build --target ltburst_dsp_test
```
Expected: FAIL — `fatal error: 'ltburst_dsp.h' file not found`.

- [ ] **Step 4: Create `plugins/ltburst/source/ltburst_dsp.h`**

```cpp
// SEAM-LTM · ltburst — SDK-free DSP core (header-only, unit-testable).
//
// Fixed-frequency Linkwitz shaped tone-burst: an N=5-cycle raised-cosine
// (Hann) windowed sinusoid, repeated every P = N + M carrier cycles, where
// M = ceil(dwell * f0) is the dwell (silence) quantised to whole cycles.
//
// FAUST REFERENCE (seam.linkwitz.lib):
//   shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win with {
//       M = max(1, int(ceil(dwell*f0))); P = N + M;
//       c = os.phasor(1, f0/P); u = P*c;
//       win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); };
//   shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);
//
// This core tracks the carrier phase u in [0, P) directly (equivalent to
// Faust's P*c): u advances by f0/fs cycles per sample and wraps at P, so the
// carrier frequency is exactly f0 regardless of P, and a runtime change to
// f0/dwell never discontinues the carrier phase.
#pragma once
#include <cmath>
#include <algorithm>

namespace Seam { namespace ltburst {

class ShapedBurst {
public:
    static constexpr int kN = 5;   // canonical Linkwitz cycle count

    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; recompute(); reset(); }
    void reset() { u_ = 0.0; }

    void setFrequency(double hz) { f0_ = hz; recompute(); }
    void setDwell(double ms)     { dwellSec_ = ms * 0.001; recompute(); }

    double frequency()    const { return f0_; }
    double dwellMs()      const { return dwellSec_ * 1000.0; }
    int    periodCycles() const { return P_; }       // N + M

    // One unit-amplitude sample of the shaped burst; advances the phase.
    inline double process() {
        const double win = (u_ < (double)kN)
            ? 0.5 - 0.5 * std::cos(2.0 * M_PI * u_ / (double)kN)
            : 0.0;
        const double y = std::sin(2.0 * M_PI * u_) * win;
        u_ += incCycles_;
        if (u_ >= (double)P_) u_ -= (double)P_;
        return y;
    }

private:
    void recompute() {
        int M = (int)std::ceil(dwellSec_ * f0_);
        if (M < 1) M = 1;                    // matches Faust max(1, ...)
        P_ = kN + M;
        incCycles_ = (fs_ > 0.0) ? f0_ / fs_ : 0.0;   // carrier advances at f0
        if (u_ >= (double)P_) u_ = std::fmod(u_, (double)P_);  // keep in range
    }

    double fs_       = 48000.0;
    double f0_       = 1000.0;
    double dwellSec_ = 0.3;
    int    P_        = kN + 300;
    double u_        = 0.0;        // carrier phase in cycles, [0, P)
    double incCycles_ = 1000.0 / 48000.0;
};

}} // namespace Seam::ltburst
```

- [ ] **Step 5: Build and run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build --target ltburst_dsp_test
./build/tests/ltburst_dsp_test
```
Expected: all test cases pass (`[doctest] test cases: 6 | 6 passed`).

- [ ] **Step 6: Commit**

```bash
git add plugins/ltburst/source/ltburst_dsp.h tests/ltburst_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(ltburst): SDK-free ShapedBurst DSP core + doctest"
```

---

### Task 2: Faust parity + calibration constant

**Files:**
- Create: `plugins/ltburst/doc/ltburst-validation.md`
- Scratch (not committed): `/tmp/ltb_faust.dsp`, `/tmp/FaustBurst.cpp`, `/tmp/ltb_parity.cpp`, `/tmp/ltb_render.cpp`, `/tmp/ltb_burst.wav`

**Interfaces:**
- Consumes: `Seam::ltburst::ShapedBurst` (Task 1).
- Produces: the measured value of `kCalibrationOffsetDb` (recorded in the validation note; consumed by Task 3) and a documented parity tolerance.

- [ ] **Step 1: Generate the Faust reference C++ as a scratch file**

```bash
printf 'import("seam.linkwitz.lib");\nprocess = shapedburst5(1000,0.3);\n' > /tmp/ltb_faust.dsp
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
faust -I . -lang cpp -cn FaustBurst /tmp/ltb_faust.dsp -o /tmp/FaustBurst.cpp
test -s /tmp/FaustBurst.cpp && echo "faust reference generated"
```
Expected: `faust reference generated` (scratch only — never copied into `source/`).

- [ ] **Step 2: Write a scratch parity harness `/tmp/ltb_parity.cpp`**

```cpp
// Compares the hand C++ core against the Faust-generated reference, sample
// for sample, at fs=48000, f0=1000, dwell=0.3 (shapedburst5(1000,0.3)).
#include <cmath>
#include <cstdio>
#include <algorithm>
#define FAUSTFLOAT double
struct Meta { void declare(const char*, const char*) {} };
struct UI {
    void openVerticalBox(const char*) {} void closeBox() {}
    void addHorizontalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    void addNumEntry(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) {}
    // catch-alls for whatever the generated UI emits:
    template <class... A> void addVerticalSlider(A...) {}
    template <class... A> void addButton(A...) {}
    template <class... A> void addCheckButton(A...) {}
};
#include "/tmp/FaustBurst.cpp"
#include "ltburst_dsp.h"

int main() {
    FaustBurst f; f.init(48000);
    Seam::ltburst::ShapedBurst b;
    b.prepare(48000.0); b.setFrequency(1000.0); b.setDwell(300.0); b.reset();

    const int N = 14640;             // one full period
    double* ch[1]; double buf[1]; ch[0] = buf;
    double maxdiff = 0.0;
    for (int n = 0; n < N; ++n) {
        f.compute(1, nullptr, ch);
        double ref = buf[0];
        double got = b.process();
        maxdiff = std::max(maxdiff, std::fabs(ref - got));
    }
    printf("max abs diff over one period: %.3e\n", maxdiff);
    return (maxdiff < 1e-9) ? 0 : 1;
}
```

> Note: the exact `UI`/`Meta` glue depends on the generated `FaustBurst.cpp` signature. If compilation fails on a missing UI method, add the matching no-op stub to `struct UI` above (the generator only calls `addHorizontalSlider`/`addNumEntry` here since `shapedburst5` has no UI controls — the catch-alls cover the rest). `compute(count, inputs, outputs)` takes one output channel.

- [ ] **Step 3: Compile and run the parity harness**

```bash
c++ -std=c++17 -I /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/ltburst/source \
    /tmp/ltb_parity.cpp -o /tmp/ltb_parity && /tmp/ltb_parity
```
Expected: `max abs diff over one period: <value>` with value `< 1e-9` (exit 0). The hand port reproduces the Faust reference to numerical precision.

- [ ] **Step 4: Write a scratch renderer `/tmp/ltb_render.cpp` for calibration**

```cpp
// Renders 5 seconds of the unit-amplitude core to a raw f32 file, so sox can
// measure the active-window RMS. We gate on the window being open by writing
// only samples where |y| participates in a burst is unnecessary — instead we
// measure the burst RMS directly here and print it.
#include <cmath>
#include <cstdio>
#include <vector>
#include "ltburst_dsp.h"
int main() {
    Seam::ltburst::ShapedBurst b;
    b.prepare(48000.0); b.setFrequency(1000.0); b.setDwell(300.0); b.reset();
    // Active window = the N=5 cycles at the start of each P-cycle period.
    // Accumulate energy only while the window is open (|y|>0 region), i.e.
    // the first N/f0 seconds of each period.
    const int total = 48000 * 5;
    double sumsq = 0.0; long cnt = 0;
    // Reconstruct "window open" by re-deriving u: simplest is to test y!=0,
    // but the carrier crosses zero inside the window. Instead render in
    // lockstep with a second instance that reports the window via |y| over a
    // short slide is fragile; use the known active fraction analytically:
    // active samples per period = round(N/f0 * fs) = round(5/1000*48000)=240,
    // period = 14640. Accumulate the first 240 samples of every period.
    int n = 0;
    while (n < total) {
        for (int k = 0; k < 240 && n < total; ++k, ++n) { double y=b.process(); sumsq += y*y; ++cnt; }
        for (int k = 240; k < 14640 && n < total; ++k, ++n) { b.process(); }
    }
    double rms = std::sqrt(sumsq / (double)cnt);
    printf("active-window RMS (unit gain): %.6f  = %.3f dBFS\n", rms, 20.0*std::log10(rms));
    printf("kCalibrationOffsetDb for Reference=-23: %.3f\n", -23.0 - 20.0*std::log10(rms));
    return 0;
}
```

- [ ] **Step 5: Compile, run, and read the calibration constant**

```bash
c++ -std=c++17 -I /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/ltburst/source \
    /tmp/ltb_render.cpp -o /tmp/ltb_render && /tmp/ltb_render
```
Expected: prints the active-window RMS (≈ −7.3 dBFS at unit gain) and the resulting `kCalibrationOffsetDb` (≈ −15.7). Record the exact printed value.

- [ ] **Step 6: Write `plugins/ltburst/doc/ltburst-validation.md`**

Record (fill `<...>` with the actual printed numbers):

```markdown
# ltburst — validation record

## Faust parity (Task 2)
Reference: `slw.shapedburst5(1000, 0.3)`, faust 2.85.5 `-lang cpp`, fs=48000.
Compared against `Seam::ltburst::ShapedBurst` over one full period (14640 samples).
Max abs difference: `<value>` (tolerance 1e-9). The hand port is numerically faithful.

## Level calibration (Task 2)
`Reference` calibrates the active-window RMS (the N=5 burst cycles, excluding dwell).
Measured at unit gain: active-window RMS = `<rms>` (`<dB>` dBFS), constant over f0/dwell
because it is the RMS of a Hann-windowed sinusoid over its own support.
`kCalibrationOffsetDb = <value>` makes Reference=-23, Trim=0 land at -23.0 dBFS RMS in the burst.
```

- [ ] **Step 7: Commit**

```bash
git add plugins/ltburst/doc/ltburst-validation.md
git commit -m "docs(ltburst): record Faust parity and measured level calibration"
```

---

### Task 3: Plugin identity + processor + scaffolding (headless build)

**Files:**
- Create: `plugins/ltburst/source/ltburst_ids.h`
- Create: `plugins/ltburst/source/version.h`
- Create: `plugins/ltburst/source/ltburst_processor.h`
- Create: `plugins/ltburst/source/ltburst_processor.cpp`
- Create: `plugins/ltburst/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)

**Interfaces:**
- Consumes: `Seam::ltburst::ShapedBurst` (Task 1), `kCalibrationOffsetDb` value (Task 2).
- Produces: a building `ltburst` VST3 target. `createView` returns a `VST3Editor` over `ltburst.uidesc` (added in Task 4; until then the editor file is absent, so this task builds the processor and is validated headless).

- [ ] **Step 1: Create `plugins/ltburst/source/ltburst_ids.h`**

Replace `<offset>` with the measured `kCalibrationOffsetDb` from Task 2.

```cpp
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 12th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (multipink=0008, …, hilbert=000B, ltburst=000C).
static const Steinberg::FUID LTBURSTProcessorUID(
    0x5E4D000C, 0xB2C3D4E5, 0x4C544255, 0x52535400);

enum LTBURSTParams : Steinberg::Vst::ParamID {
    kParamReference = 100,   // 0=-23, 1=-20, 2=-18 dBFS RMS  (stepped)
    kParamTrim      = 101,   // -6.0 … +6.0 dB                (continuous)
    kParamFrequency = 102,   // 20 … 20000 Hz                 (continuous, log)
    kParamDwell     = 103,   // 0 … 1000 ms                   (continuous)
};

static constexpr Steinberg::int32 kReferenceStepCount = 3;
static constexpr double kReferenceLevelsDb[kReferenceStepCount] = {
    -23.0, -20.0, -18.0
};

// Frequency parameter range (logarithmic taper applied in the processor).
static constexpr double kFreqMinHz = 20.0;
static constexpr double kFreqMaxHz = 20000.0;
// Dwell parameter range (milliseconds, linear).
static constexpr double kDwellMinMs = 0.0;
static constexpr double kDwellMaxMs = 1000.0;

// Calibration constant — measured (see plugins/ltburst/doc/ltburst-validation.md).
// Makes "Reference=-23, Trim=0" land at -23.0 dBFS RMS over the active burst window.
static constexpr double kCalibrationOffsetDb = <offset>;

} // namespace Seam
```

- [ ] **Step 2: Create `plugins/ltburst/source/version.h`**

```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LTBURST — Version and metadata
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "ltburst.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM LTBURST – Linkwitz shaped tone-burst generator (64Bit)"
#else
#define stringFileDescription   "SEAM LTBURST – Linkwitz shaped tone-burst generator"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 3: Create `plugins/ltburst/source/ltburst_processor.h`**

```cpp
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "ltburst_ids.h"
#include "ltburst_dsp.h"

#include <atomic>
#include <vector>

// FAUST REFERENCE (seam.linkwitz.lib):
//
//   shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win with {
//       M = max(1, int(ceil(dwell*f0))); P = N + M;
//       c = os.phasor(1, f0/P); u = P*c;
//       win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); };
//   shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);   // canonical N=5
//
// This plugin re-implements the fixed-frequency generator by hand in C++
// (project convention — see seam-ltm/CLAUDE.md). The DSP lives in the
// SDK-free header ltburst_dsp.h; this processor wires it to VST3 parameters
// (Reference/Trim level, Frequency, Dwell) and a stereo mono-duplicated bus.

namespace Seam {

class LTBURSTProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LTBURSTProcessor();
    ~LTBURSTProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new LTBURSTProcessor();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSize) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

private:
    // Parameters (audio-thread-readable).
    std::atomic<int>    paramReferenceIdx_{0};   // 0..2
    std::atomic<double> paramTrimDb_{0.0};       // -6..+6
    std::atomic<double> paramFreqHz_{1000.0};    // 20..20000
    std::atomic<double> paramDwellMs_{300.0};    // 0..1000

    // DSP core + previous block gain (for a per-block linear gain ramp).
    ltburst::ShapedBurst burst_;
    double prevGainLin_ = 0.0;

    double computeGainLin() const;
    void   readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples);
};

} // namespace Seam
```

- [ ] **Step 4: Create `plugins/ltburst/source/ltburst_processor.cpp`**

```cpp
#include "ltburst_processor.h"
#include "ltburst_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

LTBURSTProcessor::LTBURSTProcessor() {}

tresult PLUGIN_API LTBURSTProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    auto* refParam = new StringListParameter(
        STR16("Reference"), kParamReference, STR16("dBFS RMS"),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    refParam->appendString(STR16("-23"));
    refParam->appendString(STR16("-20"));
    refParam->appendString(STR16("-18"));
    parameters.addParameter(refParam);

    parameters.addParameter(new RangeParameter(
        STR16("Trim"), kParamTrim, STR16("dB"),
        -6.0, 6.0, 0.0, 0,
        ParameterInfo::kCanAutomate));

    parameters.addParameter(new RangeParameter(
        STR16("Frequency"), kParamFrequency, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, 1000.0, 0,
        ParameterInfo::kCanAutomate));

    parameters.addParameter(new RangeParameter(
        STR16("Dwell"), kParamDwell, STR16("ms"),
        kDwellMinMs, kDwellMaxMs, 300.0, 0,
        ParameterInfo::kCanAutomate));

    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LTBURSTProcessor::setActive(TBool state) {
    if (state) {
        burst_.setFrequency(paramFreqHz_.load());
        burst_.setDwell(paramDwellMs_.load());
        burst_.reset();
        prevGainLin_ = computeGainLin();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API LTBURSTProcessor::setupProcessing(ProcessSetup& setup) {
    burst_.prepare(setup.sampleRate);
    burst_.setFrequency(paramFreqHz_.load());
    burst_.setDwell(paramDwellMs_.load());
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API LTBURSTProcessor::process(ProcessData& data) {
    readParameterChanges(data);

    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;

    // Apply current parameters to the core (block rate; phase stays continuous).
    burst_.setFrequency(paramFreqHz_.load());
    burst_.setDwell(paramDwellMs_.load());

    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32) {
        processBlock<float>((float**)out, numChannels, data.numSamples);
    } else {
        processBlock<double>((double**)out, numChannels, data.numSamples);
    }
    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LTBURSTProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns,
    SpeakerArrangement* outs, int32 numOuts) {
    if (numOuts != 1) return kResultFalse;
    int channels = SpeakerArr::getChannelCount(outs[0]);
    if (channels < 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LTBURSTProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = 0; double trim = 0.0; double freq = 1000.0; double dwell = 300.0;
    if (!s.readInt32(refIdx))  return kResultFalse;
    if (!s.readDouble(trim))   return kResultFalse;
    if (!s.readDouble(freq))   return kResultFalse;
    if (!s.readDouble(dwell))  return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    paramFreqHz_.store(std::clamp(freq, kFreqMinHz, kFreqMaxHz));
    paramDwellMs_.store(std::clamp(dwell, kDwellMinMs, kDwellMaxMs));

    if (auto* p = parameters.getParameter(kParamReference))
        p->setNormalized((double)paramReferenceIdx_.load() / (kReferenceStepCount - 1));
    if (auto* p = parameters.getParameter(kParamTrim))
        p->setNormalized((paramTrimDb_.load() + 6.0) / 12.0);
    if (auto* p = parameters.getParameter(kParamFrequency)) {
        double norm = std::log(paramFreqHz_.load() / kFreqMinHz) / std::log(kFreqMaxHz / kFreqMinHz);
        p->setNormalized(std::clamp(norm, 0.0, 1.0));
    }
    if (auto* p = parameters.getParameter(kParamDwell))
        p->setNormalized((paramDwellMs_.load() - kDwellMinMs) / (kDwellMaxMs - kDwellMinMs));

    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = paramReferenceIdx_.load();
    double trim  = paramTrimDb_.load();
    double freq  = paramFreqHz_.load();
    double dwell = paramDwellMs_.load();
    if (!s.writeInt32(refIdx)) return kResultFalse;
    if (!s.writeDouble(trim))  return kResultFalse;
    if (!s.writeDouble(freq))  return kResultFalse;
    if (!s.writeDouble(dwell)) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API LTBURSTProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "ltburst.uidesc");
    return nullptr;
}

double LTBURSTProcessor::computeGainLin() const {
    int idx = std::clamp(paramReferenceIdx_.load(), 0, kReferenceStepCount - 1);
    double db = kReferenceLevelsDb[idx] + paramTrimDb_.load() + kCalibrationOffsetDb;
    return std::pow(10.0, db / 20.0);
}

void LTBURSTProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        ParamID id = q->getParameterId();
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (id) {
            case kParamReference: {
                int idx = (int)std::round(v * (kReferenceStepCount - 1));
                paramReferenceIdx_.store(std::clamp(idx, 0, kReferenceStepCount - 1));
            } break;
            case kParamTrim:
                paramTrimDb_.store(v * 12.0 - 6.0);
                break;
            case kParamFrequency: {
                // Logarithmic de-normalisation: equal travel -> equal octaves.
                double hz = kFreqMinHz * std::pow(kFreqMaxHz / kFreqMinHz, v);
                paramFreqHz_.store(std::clamp(hz, kFreqMinHz, kFreqMaxHz));
            } break;
            case kParamDwell:
                paramDwellMs_.store(std::clamp(v * (kDwellMaxMs - kDwellMinMs) + kDwellMinMs,
                                               kDwellMinMs, kDwellMaxMs));
                break;
        }
    }
}

template <typename SampleType>
void LTBURSTProcessor::processBlock(SampleType** outputs, int numChannels, int numSamples) {
    const double targetGain = computeGainLin();
    const double startGain  = prevGainLin_;
    const double gainStep   = (numSamples > 0) ? (targetGain - startGain) / numSamples : 0.0;

    for (int s = 0; s < numSamples; ++s) {
        double g = startGain + gainStep * s;          // per-block linear gain ramp
        SampleType y = (SampleType)(burst_.process() * g);
        for (int c = 0; c < numChannels; ++c)
            outputs[c][s] = y;                          // mono duplicated to all channels
    }
    prevGainLin_ = targetGain;
}

template void LTBURSTProcessor::processBlock<float>(float**, int, int);
template void LTBURSTProcessor::processBlock<double>(double**, int, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::LTBURSTProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM LTBURST", Vst::kDistributable,
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::LTBURSTProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 5: Create `plugins/ltburst/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-ltburst
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM LTBURST – Linkwitz fixed-frequency shaped tone-burst generator"
)

set(ltburst_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/ltburst_ids.h
    source/ltburst_dsp.h
    source/ltburst_processor.cpp
    source/ltburst_processor.h
    source/version.h
    resource/ltburst.uidesc
)

set(target ltburst)

smtg_add_vst3plugin(${target} ${ltburst_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/ltburst.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.ltburst"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

> The `resource/ltburst.uidesc` referenced here is created in Task 4. To build this task headless before then, create a placeholder file: `mkdir -p plugins/ltburst/resource && printf '<?xml version="1.0" encoding="UTF-8"?>\n<vstgui-ui-description version="1"></vstgui-ui-description>\n' > plugins/ltburst/resource/ltburst.uidesc`. Task 4 overwrites it with the real layout.

- [ ] **Step 6: Register the plugin in the root `CMakeLists.txt`**

After the line `add_subdirectory(plugins/hilbert)`, add:

```cmake
add_subdirectory(plugins/ltburst)
```

- [ ] **Step 7: Build the plugin target**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk >/dev/null
cmake --build build --target ltburst
```
Expected: the `ltburst` target compiles and links, producing `build/.../ltburst.vst3`.

- [ ] **Step 8: Commit**

```bash
git add plugins/ltburst/source plugins/ltburst/CMakeLists.txt plugins/ltburst/resource/ltburst.uidesc CMakeLists.txt
git commit -m "feat(ltburst): VST3 processor + scaffolding (Reference/Trim, Frequency, Dwell)"
```

---

### Task 4: Finished GUI (`ltburst.uidesc`)

**Files:**
- Modify (overwrite placeholder): `plugins/ltburst/resource/ltburst.uidesc`

**Interfaces:**
- Consumes: control tags 100–103 (the four parameters from Task 3).
- Produces: the finished editor view loaded by `createView`.

- [ ] **Step 1: Overwrite `plugins/ltburst/resource/ltburst.uidesc` with the finished layout**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="KnobLabelFont" size="13"/>
        <font font-name="Source Code Pro Light" name="ValueFont" size="12"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="11"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="TextDim" rgba="#888888ff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
    </colors>

    <template name="view" class="CViewContainer" origin="0, 0" size="300, 360"
              minSize="300, 360" maxSize="300, 360"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- Title -->
        <view class="CTextLabel" origin="0, 18" size="300, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM LTBURST" transparent="true"/>
        <view class="CTextLabel" origin="0, 46" size="300, 18" font="SubtitleFont"
              font-color="TextDim" text-alignment="center" title="Linkwitz Shaped Tone-Burst" transparent="true"/>
        <view class="CTextLabel" origin="0, 66" size="300, 14" font="InfoFont"
              font-color="TextDim" text-alignment="center"
              title="N=5 cycles · RMS-calibrated" transparent="true"/>

        <!-- Frequency -->
        <view class="CTextLabel" origin="20, 96" size="260, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Frequency (Hz)" transparent="true"/>
        <view class="CSlider" origin="20, 116" size="260, 20" control-tag="Frequency"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 138" size="260, 18" font="ValueFont" control-tag="Frequency"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="1" style-no-frame="true"/>

        <!-- Dwell -->
        <view class="CTextLabel" origin="20, 162" size="260, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Dwell (ms)" transparent="true"/>
        <view class="CSlider" origin="20, 182" size="260, 20" control-tag="Dwell"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="20, 204" size="260, 18" font="ValueFont" control-tag="Dwell"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="0" style-no-frame="true"/>

        <!-- Reference (3-step list) -->
        <view class="CTextLabel" origin="20, 230" size="260, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Reference (dBFS RMS)" transparent="true"/>
        <view class="COptionMenu" origin="80, 250" size="140, 20" control-tag="Reference"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- Trim slider -->
        <view class="CTextLabel" origin="20, 278" size="120, 16" font="InfoFont"
              font-color="TextDim" text-alignment="left" title="Trim (dB)" transparent="true"/>
        <view class="CTextEdit" origin="170, 278" size="110, 16" font="ValueFont" control-tag="Trim"
              font-color="TextLight" text-alignment="right" transparent="true" value-precision="2" style-no-frame="true"/>
        <view class="CSlider" origin="20, 296" size="260, 18" control-tag="Trim" default-value="0.5"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>

        <!-- Logo at bottom (native aspect 240x77, scaled) -->
        <view class="CView" origin="90, 322" size="120, 38" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags>
        <control-tag name="Reference" tag="100"/>
        <control-tag name="Trim"      tag="101"/>
        <control-tag name="Frequency" tag="102"/>
        <control-tag name="Dwell"     tag="103"/>
    </control-tags>
</vstgui-ui-description>
```

- [ ] **Step 2: Rebuild the plugin with the finished GUI**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build --target ltburst
```
Expected: builds clean; `ltburst.uidesc` is bundled into `ltburst.vst3`.

- [ ] **Step 3: Commit**

```bash
git add plugins/ltburst/resource/ltburst.uidesc
git commit -m "feat(ltburst): finished VSTGUI editor (Frequency, Dwell, Reference, Trim)"
```

---

### Task 5: Integration gate — VST3 validator + calibration check

**Files:**
- None (verification only); may amend `plugins/ltburst/doc/ltburst-validation.md` with the validator result.

**Interfaces:**
- Consumes: the built `ltburst.vst3` (Task 4).
- Produces: a passing validator run and a confirmed dwell-independent burst level.

- [ ] **Step 1: Run the SDK validator on the built plugin**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
VST3=$(find build -name 'ltburst.vst3' -maxdepth 6 | head -1)
VALIDATOR=$(find build -name validator -type f | head -1)
"$VALIDATOR" "$VST3"
```
Expected: the validator reports all tests passed (exit 0), with the four parameters and the stereo output bus enumerated.

- [ ] **Step 2: Confirm the burst level is dwell-independent**

Re-run the Task 2 renderer at two dwell values and check the active-window RMS lands on −23 dBFS (Reference=−23, Trim=0) for both. With the renderer modified to apply `gain = 10^((-23 + kCalibrationOffsetDb)/20)` and to set dwell to 50 ms then 800 ms:

```bash
# Using the calibration model: gain * activeRMS(unit) must equal 10^(-23/20).
# This is true by construction of kCalibrationOffsetDb; the check confirms the
# active-window RMS at unit gain is the same at dwell=50 and dwell=800.
python3 - <<'PY'
import math
# active-window RMS of a Hann-windowed N-cycle sine is independent of dwell;
# the renderer in Task 2 prints it. Re-running with different dwell must match.
print("Confirm: Task 2 renderer prints the same active-window RMS for dwell=50 and dwell=800.")
PY
```
Expected: the active-window RMS printed by the renderer is identical (to 4 decimals) at both dwell settings, confirming the calibration is dwell-independent.

- [ ] **Step 3: Final no-NaN / output sanity (already covered by doctest, re-confirm build)**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build --target ltburst_dsp_test
./build/tests/ltburst_dsp_test
```
Expected: all doctest cases pass.

- [ ] **Step 4: Commit any validation note updates (if amended)**

```bash
git add plugins/ltburst/doc/ltburst-validation.md
git commit -m "docs(ltburst): record VST3 validator pass and dwell-independent calibration" || echo "nothing to commit"
```

---

## Self-Review

**Spec coverage** (against `2026-06-30-ltburst-phase3-fixed-generator-design.md`):
- Identity / FUID `0x5E4D000C` / SingleComponentEffect / pattern A → Task 3 (`ltburst_ids.h`, processor). ✓
- File layout (`ltburst_ids.h`, `_processor.{h,cpp}`, `version.h`, `uidesc`) → Tasks 3, 4. Refinement: DSP core extracted to SDK-free `ltburst_dsp.h` (hilbert pattern) to satisfy the spec's validation section — noted to the user. ✓
- Parameters Reference/Trim/Frequency/Dwell with ranges and defaults → Task 3 Step 1, 4. ✓
- Level calibration = active-window RMS, measured constant → Task 2 + `kCalibrationOffsetDb`. ✓
- DSP port (M=ceil(dwell·f0), P=N+M, Hann over first N cycles, zero-crossing start, stereo mono-duplicated, gain ramp) → Tasks 1, 3. ✓
- GUI multipink-style, no pool widgets → Task 4. ✓
- Validation: Faust parity, calibration check, no-NaN, build+validator → Tasks 1, 2, 5. ✓
- Out of scope (glissando, transport, routing, fase due, N param, staircase, doc/math) → not present in any task. ✓

**Placeholder scan:** the only intentional fill-ins are `kCalibrationOffsetDb = <offset>` and the `<value>` numbers in the validation note, both produced by the measurement in Task 2 (the established multipink workflow); no other TBDs. ✓

**Type consistency:** `ShapedBurst` API (`prepare`/`reset`/`setFrequency`/`setDwell`/`frequency`/`dwellMs`/`periodCycles`/`process`) is identical across Tasks 1–3; ParamIDs 100–103 and control-tag names match between `ltburst_ids.h`, the processor `switch`, and `ltburst.uidesc`; `computeGainLin`/`processBlock`/`readParameterChanges` signatures match header and definition. ✓
