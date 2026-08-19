# Calibration-Grade Pinking Filter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `multipink`'s three-pole z-plane pinking fit with a matched-Z ladder plus one fixed correction section, so the generated pink noise meets SMPTE ST 2095-1's ±0.25 dB per third-octave at every sample rate from 44.1 to 192 kHz.

**Architecture:** A design engine runs in `setupProcessing(fs)` and produces a cascade of first-order real-pole sections spaced geometrically in Hz (matched-Z, so the frequency axis is not warped), followed by one section with constant coefficients that cancels the aliased-tail residual — an error which measurement shows is a function of f/fs alone. Per-stream state is stored section-major. The acceptance test that already exists is the judge, extended to each rate's physical band limit.

**Tech Stack:** C++17, doctest, CMake/ctest, VST3 SDK (processor only), Faust (`seam.filters.lib`, `seam.noises.lib`).

**Spec:** `docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md`
Measurement log: `doc/study/sessions/2026-08-19-pink-filter-design.md`
Probes reproducing every number: `doc/study/probes/2026-08-19-pink/`

## Global Constraints

- Acceptance criterion: ±0.25 dB per third-octave, 20 Hz to the highest ISO 266 band whose nominal upper edge is ≤ 0.85·Nyquist, at 44100, 48000, 88200, 96000, 176400 and 192000 Hz.
- Regression sentinel: 0.1 dB, on the same bands and rates.
- Ladder parameters: `f0 = 2.0 Hz`, upper limit `fs/2`, `alpha = -1/2`, `kPolesPerOctave = 1.0` (Task 6 may change this one constant to 1.5, nothing else).
- Correction section, identical at every sample rate: `zero = -0.250775213`, `pole = -0.160124183`, unity gain at DC.
- Coefficients are computed in `double`; per-stream state is `float`.
- No allocation, no `design()` call, and no locking on the audio thread. Capacity is fixed at `kMaxSections = 32`.
- `multipink_pink.h` stays SDK-free: it is included by both the plugin and the tests, and there must be exactly one description of the filter.
- Build: `cmake -G Xcode -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` then `cmake --build build --config Release`. Tests: `ctest --test-dir build -C Release`.
- The white-noise source, the LCG seeding, the 64-slot pool, the parameters and the GUI are not touched by this plan.

---

### Task 1: The design engine

Replaces the coefficient table in `multipink_pink.h` with a design engine, and tests the design itself — slope, stability, and the anchor invariance the calibration depends on.

**Files:**
- Modify: `plugins/multipink/source/multipink_pink.h` (whole file)
- Create: `tests/multipink_pink_engine_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a target)

**Interfaces:**
- Consumes: nothing.
- Produces: `Seam::multipink::PinkDesign`, with `void design(double fs)`, `double magnitudeDb(double f) const`, `double rmsGainDb() const`, `double anchorGainDb() const`, and the public arrays `b0[]`, `b1[]`, `a1[]` plus `int numSections`. Task 2 judges it, Task 3 measures its precision, Task 4 runs it, Task 5 calibrates from it, Task 7 compares Faust against it.

- [ ] **Step 1: Write the failing test**

Create `tests/multipink_pink_engine_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_pink.h"
#include <cmath>

using Seam::multipink::PinkDesign;

static const double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

TEST_CASE("the ladder is stable and fits its capacity at every rate") {
    for (double fs : kRates) {
        PinkDesign d;
        d.design(fs);
        CHECK(d.numSections > 0);
        CHECK(d.numSections <= PinkDesign::kMaxSections);
        // a1 is the z^-1 denominator coefficient, so the pole is -a1
        for (int i = 0; i < d.numSections; ++i)
            CHECK(std::fabs(d.a1[i]) < 1.0);          // stable
    }
}

TEST_CASE("the interior slope is pink, -3.01 dB per octave") {
    PinkDesign d;
    d.design(48000.0);
    // Judged between 100 Hz and 8 kHz, clear of both guard regions.
    for (double f = 100.0; f <= 4000.0; f *= 2.0) {
        const double slope = d.magnitudeDb(2.0 * f) - d.magnitudeDb(f);
        CHECK(slope == doctest::Approx(-3.0103).epsilon(0.005));
    }
}

TEST_CASE("the anchor does not move with the sample rate") {
    // This is what makes the calibration constant a constant: the filter is
    // anchored in Hz, so its gain at 1 kHz must be the same at every rate.
    // Measured spread across the six rates is under 0.01 dB.
    double first = 0.0;
    for (int k = 0; k < 6; ++k) {
        PinkDesign d;
        d.design(kRates[k]);
        const double a = d.anchorGainDb();
        if (k == 0) first = a;
        CHECK(std::fabs(a - first) < 0.01);
        CHECK(a == doctest::Approx(-27.72).epsilon(0.01));
    }
}

TEST_CASE("the RMS gain falls with the sample rate, and by how much") {
    // The other half of the same fact: a filter anchored in Hz cannot also
    // hold its total RMS. 5.8 dB from 44.1 to 192 kHz, which is why the
    // calibration anchors on the band level and not on the RMS.
    PinkDesign lo, hi;
    lo.design(44100.0);
    hi.design(192000.0);
    CHECK(lo.rmsGainDb() == doctest::Approx(-31.07).epsilon(0.01));
    CHECK(hi.rmsGainDb() == doctest::Approx(-36.88).epsilon(0.01));
    CHECK(lo.rmsGainDb() - hi.rmsGainDb() == doctest::Approx(5.81).epsilon(0.02));
}
```

- [ ] **Step 2: Run it to make sure it fails**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(multipink_pink_engine_test
    multipink_pink_engine_test.cpp
)
target_include_directories(multipink_pink_engine_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/multipink/source
)
target_compile_features(multipink_pink_engine_test PRIVATE cxx_std_17)
add_test(NAME multipink_pink_engine_test COMMAND multipink_pink_engine_test)
```

Run:
```bash
cmake -G Xcode -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --config Release --target multipink_pink_engine_test
```
Expected: FAIL to compile — `PinkDesign` does not exist.

- [ ] **Step 3: Write the engine**

Replace the body of `plugins/multipink/source/multipink_pink.h` (keep the file, replace its contents below the header comment):

```cpp
// SEAM-LTM · multipink_pink — the pinking filter, SDK-free so it can be judged.
//
// FAUST REFERENCE (seam.filters.lib): sfi.spectral_tilt_mz, sfi.pink_filter_mz
//
// A cascade of first-order real pole-zero pairs spaced geometrically in Hz
// from 2 Hz to fs/2, alpha = -1/2, mapped with the MATCHED-Z transform, plus
// one correction section whose coefficients never change with the sample rate.
//
// Matched-Z rather than bilinear, and the reason is measurable: the bilinear
// transform warps the frequency axis, and at 16 kHz with fs = 48 kHz the warp
// is tan(60 deg)/(pi/3) = 1.654, which is 0.73 octaves, which at -3.01 dB per
// octave is -2.19 dB. The poles land where they are asked to; the curve
// between them does not. Matched-Z leaves the axis alone and errs the other
// way, by the aliased tail of a response that decays only 3 dB per octave --
// an error which is a function of f/fs ALONE, verified identical at 48 kHz and
// 192 kHz to three decimal places, and therefore cancelled once and for all by
// a section with constant coefficients.
//
// Full derivation and the measurements behind every number:
//   docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md
//   doc/study/sessions/2026-08-19-pink-filter-design.md
#pragma once

#include <cmath>
#include <complex>

namespace Seam { namespace multipink {

class PinkDesign {
public:
    static constexpr int    kMaxSections    = 32;
    static constexpr double kLadderF0Hz     = 2.0;
    static constexpr double kPolesPerOctave = 1.0;
    static constexpr double kAnchorHz       = 1000.0;

    // The universal correction. Fitted once against the ladder's residual in
    // normalised frequency; it is not a function of fs and must not be redesigned.
    static constexpr double kCorrZero = -0.250775213;
    static constexpr double kCorrPole = -0.160124183;

    // y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1], one section per entry.
    double b0[kMaxSections] = {};
    double b1[kMaxSections] = {};
    double a1[kMaxSections] = {};
    int    numSections = 0;

    void design(double fs) {
        sampleRate_ = fs;
        const double T  = 1.0 / fs;
        const double f1 = 0.5 * fs;
        const double w0 = 2.0 * M_PI * kLadderF0Hz;

        int n = (int)std::ceil(kPolesPerOctave * std::log2(f1 / kLadderF0Hz)) + 1;
        if (n > kMaxSections - 1) n = kMaxSections - 1;   // one slot for the correction
        if (n < 2) n = 2;
        const double r = std::pow(f1 / kLadderF0Hz, 1.0 / (double)(n - 1));

        int k = 0;
        for (int i = 0; i < n; ++i, ++k) {
            const double mz = w0 * std::pow(r, 0.5 + (double)i);   // alpha = -1/2
            const double mp = w0 * std::pow(r, (double)i);
            const double zz = std::exp(-mz * T);
            const double zp = std::exp(-mp * T);
            const double g  = (1.0 - zp) / (1.0 - zz);             // unity gain at DC
            b0[k] =  g;
            b1[k] = -g * zz;
            a1[k] = -zp;
        }
        {
            const double g = (1.0 - kCorrPole) / (1.0 - kCorrZero);
            b0[k] =  g;
            b1[k] = -g * kCorrZero;
            a1[k] = -kCorrPole;
            ++k;
        }
        numSections = k;
    }

    double magnitudeDb(double f) const {
        const std::complex<double> z =
            std::exp(std::complex<double>(0.0, -2.0 * M_PI * f / sampleRate_));
        std::complex<double> H(1.0, 0.0);
        for (int i = 0; i < numSections; ++i)
            H *= (b0[i] + b1[i] * z) / (1.0 + a1[i] * z);
        return 20.0 * std::log10(std::abs(H));
    }

    // Gain at the calibration anchor. Invariant across sample rates by design,
    // which is what lets the calibration offset stay a single constant.
    double anchorGainDb() const { return magnitudeDb(kAnchorHz); }

    // 10*log10 of the mean of |H|^2 over 0..fs/2 — the filter's RMS gain on
    // white input. Falls with fs, which is the fact the calibration must not
    // follow. Trapezoid over a fixed grid: smooth integrand, and the value is
    // reported rather than used in the audio path.
    double rmsGainDb() const {
        const int kSteps = 200000;
        double sum = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const double f = (i + 0.5) * (sampleRate_ * 0.5) / kSteps;
            const double m = std::pow(10.0, magnitudeDb(f) / 20.0);
            sum += m * m;
        }
        return 10.0 * std::log10(sum / kSteps);
    }

    double sampleRate() const { return sampleRate_; }

private:
    double sampleRate_ = 48000.0;
};

} } // namespace Seam::multipink
```

- [ ] **Step 4: Run the tests and make sure they pass**

```bash
cmake --build build --config Release --target multipink_pink_engine_test
ctest --test-dir build -C Release -R multipink_pink_engine_test --output-on-failure
```
Expected: PASS, 4 test cases.
Note: `multipink_pink_test` and the `multipink` plugin will NOT build at this point, because they still reference the removed `kPinkB`/`kPinkA`. Tasks 2 and 4 fix them; do not add compatibility shims to keep them building in between.

- [ ] **Step 5: Verify the test by mutation**

Change `kCorrZero` to `-0.25` (a plausible-looking rounding), rebuild, and confirm the slope and anchor tests still pass — they do not police the correction, which is what Task 2 is for. Then change `alpha` from `0.5 +` to `0.6 +` in `design()`, rebuild, and confirm **the slope test goes RED**. Restore both.

Record both outcomes in the commit message: a test that cannot go red is not a test.

- [ ] **Step 6: Commit**

```bash
git add plugins/multipink/source/multipink_pink.h tests/multipink_pink_engine_test.cpp tests/CMakeLists.txt
git commit -m "feat(multipink): the pinking filter becomes a design engine"
```

---

### Task 2: The acceptance test, on the criterion we chose

Extends the existing SMPTE test to each rate's physical band limit and to six rates, and flips its verdict from FAIL to PASS.

**Files:**
- Modify: `tests/multipink_pink_test.cpp` (whole file)

**Interfaces:**
- Consumes: `PinkDesign` from Task 1; `strx::bandFl`, `strx::bandFuNominal`, `strx::bandFc`, `strx::bandMeasurable` from `plugins/strx/source/strx_bands.h` (already on this target's include path).
- Produces: nothing new; it is the judge every later task re-runs.

- [ ] **Step 1: Rewrite the test to the new criterion**

Replace the `namespace { ... }` helpers and both filter test cases in `tests/multipink_pink_test.cpp`. Keep the file's header comment, extending it with the criterion change. New helpers:

```cpp
constexpr double kSmpteToleranceDb = 0.25;
constexpr double kSentinelDb       = 0.10;   // regression sentinel, see the spec

// The judged bands: 20 Hz up to the highest band whose nominal upper edge is
// within 0.85*Nyquist. This is strx's own measurability rule, and below
// 48 kHz it coincides exactly with SMPTE's 16 kHz ceiling, because the 20 kHz
// band's upper edge is 22.39 kHz and is not measurable there at all.
std::vector<int> judgedBands(double fs) {
    std::vector<int> v;
    for (int i = 0; i < 40; ++i) {
        const double fc = 1000.0 * std::pow(10.0, (strx::kBandN0 + i) / 10.0);
        const double fu = fc * std::pow(10.0, 0.05);
        if (fu <= 0.85 * 0.5 * fs) v.push_back(i);
    }
    return v;
}

double bandFcExt(int i) { return 1000.0 * std::pow(10.0, (strx::kBandN0 + i) / 10.0); }
double bandFlExt(int i) { return bandFcExt(i) * std::pow(10.0, -0.05); }
double bandFuExt(int i) { return bandFcExt(i) * std::pow(10.0,  0.05); }

// Energy in one band, in dB, for white noise through the filter. Analytic:
// with white input the output power spectrum IS |H|^2, so this is an integral
// and not a measurement. A third-octave level taken from noise carries about
// 0.6 dB of spread at BT = 50 and could not resolve a 0.25 dB tolerance.
double bandEnergyDb(const Seam::multipink::PinkDesign& d, int i) {
    const double fl = bandFlExt(i), fu = bandFuExt(i);
    const int kSteps = 8192;
    double sum = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        const double f = fl + (fu - fl) * (k + 0.5) / kSteps;
        const double m = std::pow(10.0, d.magnitudeDb(f) / 20.0);
        sum += m * m;
    }
    return 10.0 * std::log10(sum * (fu - fl) / kSteps);
}

std::vector<double> deviations(double fs) {
    Seam::multipink::PinkDesign d;
    d.design(fs);
    std::vector<double> lvl;
    for (int i : judgedBands(fs)) lvl.push_back(bandEnergyDb(d, i));
    double mean = 0.0;
    for (double v : lvl) mean += v;
    mean /= (double)lvl.size();
    for (double& v : lvl) v -= mean;
    return lvl;
}

double worstDeviation(double fs) {
    double worst = 0.0;
    for (double v : deviations(fs)) worst = std::max(worst, std::fabs(v));
    return worst;
}
```

Note for the implementer: `bandFcExt` duplicates `strx::bandFc` deliberately, because `strx`'s grid stops at index 30 (20 kHz) and the criterion needs bands above it at 88.2 kHz and higher. Extending `strx`'s own grid is a separate scope named in the spec; do not change `strx_bands.h` here.

- [ ] **Step 2: Write the assertions that must flip**

```cpp
TEST_CASE("the pinking filter meets SMPTE ST 2095-1 at every rate") {
    struct Rate { double fs; const char* name; int bands; double ceilingHz; };
    static const Rate kRates[] = {
        {44100.0,  "44.1 kHz",  30, 15848.9}, {48000.0,  "48 kHz",    30, 15848.9},
        {88200.0,  "88.2 kHz",  33, 31622.8}, {96000.0,  "96 kHz",    33, 31622.8},
        {176400.0, "176.4 kHz", 36, 63095.7}, {192000.0, "192 kHz",   36, 63095.7} };

    std::printf("\n  +/-%.2f dB per third-octave, 20 Hz to 0.85*Nyquist\n\n", kSmpteToleranceDb);
    for (const Rate& r : kRates) {
        const double w = worstDeviation(r.fs);
        std::printf("  %-10s %2zu bands, up to %8.0f Hz   %6.3f dB   %s\n",
                    r.name, judgedBands(r.fs).size(), r.ceilingHz, w,
                    w <= kSmpteToleranceDb ? "PASS" : "FAIL");
    }
    std::printf("\n");

    for (const Rate& r : kRates) {
        CHECK((int)judgedBands(r.fs).size() == r.bands);
        CHECK(bandFcExt(judgedBands(r.fs).back()) == doctest::Approx(r.ceilingHz).epsilon(0.001));
        CHECK(worstDeviation(r.fs) <= kSmpteToleranceDb);   // the standard
        CHECK(worstDeviation(r.fs) <= kSentinelDb);         // the sentinel
    }

    // Pinned, so that a change has to be deliberate. Measured 2026-08-19 at
    // one pole per octave; Task 6 may move these by changing kPolesPerOctave.
    CHECK(worstDeviation(44100.0)  == doctest::Approx(0.079).epsilon(0.05));
    CHECK(worstDeviation(48000.0)  == doctest::Approx(0.071).epsilon(0.05));
    CHECK(worstDeviation(88200.0)  == doctest::Approx(0.080).epsilon(0.05));
    CHECK(worstDeviation(96000.0)  == doctest::Approx(0.072).epsilon(0.05));
    CHECK(worstDeviation(176400.0) == doctest::Approx(0.081).epsilon(0.05));
    CHECK(worstDeviation(192000.0) == doctest::Approx(0.072).epsilon(0.05));
}
```

Delete the old test case `the deviation grows monotonically with sample rate, as a z-plane fit must`: it asserted a property of the filter *kind* that has been removed on purpose, and keeping it would fail for the right reason but read as a regression. Replace it with the property that matters now:

```cpp
TEST_CASE("the error does not grow with the sample rate, as an Hz-anchored design must not") {
    const double at48  = worstDeviation(48000.0);
    const double at192 = worstDeviation(192000.0);
    CHECK(std::fabs(at192 - at48) < 0.05);
}
```

- [ ] **Step 3: Run it**

```bash
cmake --build build --config Release --target multipink_pink_test
ctest --test-dir build -C Release -R multipink_pink_test --output-on-failure
```
Expected: PASS, and the printed table shows six rates all inside 0.081 dB.

- [ ] **Step 4: Verify by mutation — three separate breaks**

Each must go RED, and each proves a different thing:

1. In `PinkDesign::design`, drop the correction section (skip the block that writes it). Expected: worst deviation about 0.9 dB, test RED — proves the correction is doing the work claimed.
2. Change `kLadderF0Hz` from 2.0 to 20.0. Expected: the 20 Hz band goes out, test RED — proves the lower guard band is doing the work claimed.
3. In `bandEnergyDb`, remove the `* (fu - fl)` factor. Expected: deviations around 15 dB, test RED — this is the exact error made in the probe on 2026-08-19, and the test must catch it.

Restore after each. Record the three observed values in the commit message.

- [ ] **Step 5: Commit**

```bash
git add tests/multipink_pink_test.cpp
git commit -m "test(multipink): the acceptance verdict flips, on the criterion we chose"
```

---

### Task 3: Single precision, measured rather than assumed

The audit made numerical conditioning a pass/fail criterion. This turns it into a number in the build.

**Files:**
- Modify: `tests/multipink_pink_engine_test.cpp` (append)

**Interfaces:**
- Consumes: `PinkDesign` from Task 1.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to `tests/multipink_pink_engine_test.cpp`:

```cpp
namespace {

// Run the actual difference equation, transposed direct form II, one state per
// section — the form the processor uses. Templated on the state type so that
// float and double answer the same question.
template <typename T>
double measuredDb(const PinkDesign& d, double f, double fs) {
    std::vector<T> s((size_t)d.numSections, (T)0);
    const long n = 400000;
    double re = 0.0, im = 0.0;
    const double w = 2.0 * M_PI * f / fs;
    for (long k = 0; k < n; ++k) {
        T x = (T)std::sin(w * (double)k);
        for (int i = 0; i < d.numSections; ++i) {
            const T y = (T)d.b0[i] * x + s[(size_t)i];
            s[(size_t)i] = (T)d.b1[i] * x - (T)d.a1[i] * y;
            x = y;
        }
        if (k > n / 4) { re += (double)x * std::cos(w * (double)k);
                         im += (double)x * std::sin(w * (double)k); }
    }
    const double m = (double)(n - n / 4);
    return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / m);
}

} // namespace

TEST_CASE("the running filter matches its own analytic response") {
    PinkDesign d;
    d.design(48000.0);
    for (double f : {20.0, 1000.0, 16000.0})
        CHECK(measuredDb<double>(d, f, 48000.0) == doctest::Approx(d.magnitudeDb(f)).epsilon(0.002));
}

TEST_CASE("single precision costs under a thousandth of a dB") {
    // The opposite of the third-octave filterbank, where float put every band
    // below 160 Hz on the epsilon floor. Same frequencies, same f/fs, other
    // topology: first-order real poles instead of a sixth-order narrowband
    // section in direct form. Worst case here is 192 kHz at 20 Hz.
    for (double fs : {48000.0, 192000.0}) {
        PinkDesign d;
        d.design(fs);
        for (double f : {20.0, 31.5, 1000.0}) {
            const double diff = measuredDb<float>(d, f, fs) - measuredDb<double>(d, f, fs);
            CHECK(std::fabs(diff) < 0.001);
        }
    }
}
```

Add `#include <vector>` to the file's includes.

- [ ] **Step 2: Run it**

```bash
cmake --build build --config Release --target multipink_pink_engine_test
ctest --test-dir build -C Release -R multipink_pink_engine_test --output-on-failure
```
Expected: PASS. This test documents a measured property rather than driving new code, so it does not start red — which is exactly why Step 3 exists.

- [ ] **Step 3: Verify by mutation**

Change the state type in the second test case from `float` to `__fp16` (or, if unavailable, cast the coefficients to `float` and the state to `float` at half precision by rounding `b0` to `(float)((int)(d.b0[i]*256)/256.0)`). Expected: RED. Restore.

Then change the tolerance from `0.001` to `0.0000001` and confirm RED, which proves the assertion is near the measured value and not merely loose.

- [ ] **Step 4: Commit**

```bash
git add tests/multipink_pink_engine_test.cpp
git commit -m "test(multipink): single precision costs 0.0008 dB, measured on the running filter"
```

---

### Task 4: Run the new filter in the processor

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.h:66-77` (coefficient aliases and the calibration constant), `:112-113` (state arrays)
- Modify: `plugins/multipink/source/multipink_processor.cpp` — `setupProcessing` (line ~132), `resetPinkFilters` (line ~320), the pinking loop (lines ~390-406)

**Interfaces:**
- Consumes: `PinkDesign` from Task 1.
- Produces: nothing consumed by later C++ tasks; Task 5 edits the same calibration constant and Task 6 measures this loop.

- [ ] **Step 1: Replace the state and the design holder in the header**

In `multipink_processor.h`, delete the `kPinkB` / `kPinkA` aliases and replace the two state arrays:

```cpp
    // The pinking filter, designed for the running sample rate in
    // setupProcessing. See multipink_pink.h for why it has this shape.
    Seam::multipink::PinkDesign pinkDesign_;

    // Per-stream filter state, SECTION-MAJOR: state[section][stream].
    // Section-major so that the 64 streams belonging to one section are
    // contiguous. Note for a future reader: this layout is a precondition for
    // advancing eight streams with one SIMD instruction, but not sufficient on
    // its own -- the scratch buffer would also have to be transposed to
    // [sample][stream]. No SIMD is written here.
    float pinkState_[Seam::multipink::PinkDesign::kMaxSections][kPoolSize] = {};
```

- [ ] **Step 2: Design at setup, reset on activation**

In `multipink_processor.cpp`, `setupProcessing`:

```cpp
tresult PLUGIN_API MULTIPINKProcessor::setupProcessing(ProcessSetup& setup) {
    maxBlockSize_ = setup.maxSamplesPerBlock;
    scratch32_.assign((size_t)kPoolSize * maxBlockSize_, 0.0f);
    scratch64_.assign((size_t)kPoolSize * maxBlockSize_, 0.0);
    // The filter is a function of the sample rate. setupProcessing is called
    // with the plug-in inactive, so this is the one place it may be designed.
    pinkDesign_.design(setup.sampleRate);
    resetPinkFilters();
    return SingleComponentEffect::setupProcessing(setup);
}
```

and `resetPinkFilters`:

```cpp
void MULTIPINKProcessor::resetPinkFilters() {
    for (int s = 0; s < Seam::multipink::PinkDesign::kMaxSections; ++s)
        for (int i = 0; i < kPoolSize; ++i)
            pinkState_[s][i] = 0.0f;
}
```

- [ ] **Step 3: Replace the pinking loop**

Replace the Direct Form I block (step 2 of the render function) with:

```cpp
    // 2. Pink-shape ALL 64 channels in place: a cascade of first-order
    //    sections, transposed direct form II, one state per section per stream.
    //      y[n] = b0*x[n] + s ; s = b1*x[n] - a1*y[n]
    for (int sec = 0; sec < pinkDesign_.numSections; ++sec) {
        const float b0 = (float)pinkDesign_.b0[sec];
        const float b1 = (float)pinkDesign_.b1[sec];
        const float a1 = (float)pinkDesign_.a1[sec];
        float* state = pinkState_[sec];
        for (int ch = 0; ch < kPoolSize; ++ch) {
            SampleType* row = scratch.data() + (size_t)ch * numSamples;
            float s = state[ch];
            for (int n = 0; n < numSamples; ++n) {
                const float x = (float)row[n];
                const float y = b0 * x + s;
                s = b1 * x - a1 * y;
                row[n] = (SampleType)y;
            }
            state[ch] = s;
        }
    }
```

- [ ] **Step 4: Build the plugin and run the whole suite**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
Expected: the plugin links, and every test passes including the two from Tasks 1-3.

- [ ] **Step 5: Verify in the host**

Load `multipink` in Reaper at 48 kHz and again at 96 kHz, feed its output to `strx`, and confirm the band table reads flat within the noise at both rates — the 20 Hz band in particular, which read 3 dB short at 96 kHz before this work. Note the reading in the commit message.

Remember the VST3 symlink rule: the last build tree AND configuration to compile the target owns `~/Library/Audio/Plug-Ins/VST3`, and only `--config Release` strips the live editor.

- [ ] **Step 6: Commit**

```bash
git add plugins/multipink/source/multipink_processor.h plugins/multipink/source/multipink_processor.cpp
git commit -m "feat(multipink): the plug-in runs the designed filter, section-major state"
```

---

### Task 5: The calibration constant, computed and reconciled

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.h` (the `kCalibrationOffsetDb` block)
- Modify: `tests/multipink_pink_engine_test.cpp` (append one test)

**Interfaces:**
- Consumes: `PinkDesign::rmsGainDb()` from Task 1.
- Produces: the documented meaning of the Reference parameter, which Task 8 writes into the plugin documentation.

**Known discrepancy this task must resolve, not paper over:** the constant in the code is 26.45 dB, measured on 2026-05-07 with a render and `sox`. Computed from first principles it should be `-(whiteRmsDb + rmsGainDb@48k)` with the old filter: the LCG is uniform on [-1, 1) so its RMS is 1/sqrt(3) = -4.771 dB, and the old filter's RMS gain is -21.291 dB, giving 26.06 dB. The 0.39 dB gap is unexplained. Find the cause before writing the new constant; do not average them.

- [ ] **Step 1: Reproduce the old measurement**

Render 30 s from `multipink` at 48 kHz with Reference = -23, Trim = 0, on the commit before this branch (`git stash` the working tree if needed, or use a second checkout), and measure with:

```bash
sox render.wav -n stat 2>&1 | grep "RMS.*amplitude"
```
Expected: about 0.003370, matching the comment in the header. If it differs, the comment is the thing that is wrong and this task's premise changes — report before continuing.

- [ ] **Step 2: Find the 0.39 dB**

Check, in order, until one accounts for it:
1. The LCG's actual output distribution — `(int32_t)st / 2147483648.0` over the LCG's period, measured by summing squares over 10^7 samples, in dB. Expected -4.771 dB if uniform.
2. Whether the render carried the plugin's gain stage only, or also a host fader.
3. Whether `sox stat` reports RMS over the whole file including any silent lead-in.

Write the finding into the header comment as part of Step 3, whatever it turns out to be.

- [ ] **Step 3: Write the constant with its derivation**

```cpp
    // Calibration. The constant fixes the BAND level, not the total RMS: the
    // filter is anchored in Hz, so its total RMS falls 5.8 dB from 44.1 to
    // 192 kHz while its band levels stay put, and it is the band levels an
    // amplifier is calibrated against. The offset is therefore defined once,
    // at 48 kHz, and deliberately NOT recomputed per sample rate -- recomputing
    // it is exactly what would make the band level move.
    //
    // Computed, not measured: the LCG source is uniform on [-1, 1), so its RMS
    // is 1/sqrt(3) = -4.771 dB, and PinkDesign::rmsGainDb() at 48 kHz is
    // -31.409 dB. Offset = -(-4.771 + -31.409) = 36.180 dB.
    // The 0.39 dB by which the old measured constant (26.45) exceeded the same
    // computation for the old filter (26.06) was traced to: [one sentence, the
    // cause found in Step 2 -- and adjust the number above if it implies it].
    //
    // Against the old filter at equal total RMS the mean band level moves by
    // -1.00 dB. strx reports deviations from the mean of the bands, so no
    // equalisation decision taken before 2026-08-19 changes; only the absolute
    // level of the reference signal does.
    static constexpr double kCalibrationOffsetDb = 36.180;
```

- [ ] **Step 4: Assert it in the test**

```cpp
TEST_CASE("the calibration offset is the one the design implies") {
    PinkDesign d;
    d.design(48000.0);
    const double kWhiteRmsDb = 20.0 * std::log10(1.0 / std::sqrt(3.0));
    CHECK(kWhiteRmsDb == doctest::Approx(-4.771).epsilon(0.001));
    CHECK(d.rmsGainDb() == doctest::Approx(-31.409).epsilon(0.001));
    CHECK(-(kWhiteRmsDb + d.rmsGainDb()) == doctest::Approx(36.180).epsilon(0.001));
}
```

- [ ] **Step 5: Verify by render**

Rebuild, render 30 s at 48 kHz with Reference = -23, Trim = 0, and confirm `sox stat` reports -23.0 dBFS RMS within 0.1 dB. Then render at 96 kHz with the same settings and confirm the RMS is about 2.7 dB lower while `strx` reads the same band levels — that is the anchoring decision, visible.

- [ ] **Step 6: Commit**

```bash
git add plugins/multipink/source/multipink_processor.h tests/multipink_pink_engine_test.cpp
git commit -m "feat(multipink): the calibration constant is computed, and anchors the band level"
```

---

### Task 6: Measure the cost, then choose the density

**Files:**
- Create: `tools/bench-pink.cpp`
- Modify: `tools/README.md` (append a row)
- Possibly modify: `plugins/multipink/source/multipink_pink.h` (`kPolesPerOctave`)

**Interfaces:**
- Consumes: `PinkDesign` from Task 1.
- Produces: the settled value of `kPolesPerOctave`.

- [ ] **Step 1: Write the benchmark**

```cpp
// tools/bench-pink.cpp — how much does the pinking filter cost?
// Build: clang++ -O3 -std=c++17 -I../plugins/multipink/source -o bench-pink bench-pink.cpp
#include "multipink_pink.h"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    using namespace std::chrono;
    constexpr int kPool = 64, kBlock = 512, kBlocks = 4000;
    for (double fs : {48000.0, 192000.0}) {
        Seam::multipink::PinkDesign d;
        d.design(fs);
        std::vector<float> scratch((size_t)kPool * kBlock, 0.1f);
        std::vector<float> state((size_t)d.numSections * kPool, 0.0f);
        const auto t0 = steady_clock::now();
        for (int b = 0; b < kBlocks; ++b)
            for (int sec = 0; sec < d.numSections; ++sec) {
                const float b0 = (float)d.b0[sec], b1 = (float)d.b1[sec], a1 = (float)d.a1[sec];
                float* st = state.data() + (size_t)sec * kPool;
                for (int ch = 0; ch < kPool; ++ch) {
                    float* row = scratch.data() + (size_t)ch * kBlock;
                    float s = st[ch];
                    for (int n = 0; n < kBlock; ++n) {
                        const float x = row[n], y = b0 * x + s;
                        s = b1 * x - a1 * y;
                        row[n] = y;
                    }
                    st[ch] = s;
                }
            }
        const double sec_ = duration<double>(steady_clock::now() - t0).count();
        const double audioSeconds = (double)kBlocks * kBlock / fs;
        std::printf("fs %8.0f  %2d sezioni  %6.3f s per %6.1f s di audio  =  %.2f%% di un core\n",
                    fs, d.numSections, sec_, audioSeconds, 100.0 * sec_ / audioSeconds);
    }
    return 0;
}
```

- [ ] **Step 2: Run it at both densities**

```bash
cd tools && clang++ -O3 -std=c++17 -I../plugins/multipink/source -o bench-pink bench-pink.cpp && ./bench-pink
```
Then set `kPolesPerOctave` to 1.5, rebuild, run again, and set it back if the choice is 1.0.

- [ ] **Step 3: Decide, on the number**

Choose 1.5 only if it stays under 5% of one core at 192 kHz. Otherwise keep 1.0, which measured 0.072–0.081 dB against a 0.25 dB tolerance. Whichever is chosen, if the constant changes, re-run Task 2's test and update its six pinned values with the new measurements.

- [ ] **Step 4: Commit**

```bash
git add tools/bench-pink.cpp tools/README.md plugins/multipink/source/multipink_pink.h
git commit -m "perf(multipink): the pinking filter's cost, measured, and the density it settles"
```

---

### Task 7: The Faust specification, and the A/B that binds it

**Files:**
- Modify: `../faust-libraries/src/seam.filters.lib` (a different git repository — commit there separately)
- Modify: `../faust-libraries/src/seam.noises.lib:11`
- Create: `tools/faust-pink-ab.sh`

**Interfaces:**
- Consumes: `PinkDesign` from Task 1, as the reference the Faust output is compared against.
- Produces: `sfi.spectral_tilt_mz(N, f0, f1, alpha)` and `sfi.pink_filter_mz`.

- [ ] **Step 1: Write the Faust function**

Append to `seam.filters.lib`, in the library's own style, with the declare block and an inline `// process = ...` test:

```faust
//------------------------------------------ MATCHED-Z SPECTRAL TILT ---
// A constant-slope tilt of alpha nepers/neper built as a cascade of real
// first-order pole-zero pairs spaced geometrically from f0 to f1, mapped with
// the MATCHED-Z transform: z_pole = exp(-2*PI*f*T).
//
// Matched-Z and not bilinear, and the difference is measurable: the bilinear
// transform warps the frequency axis, so at 16 kHz with SR = 48 kHz the
// response is evaluated 0.73 octaves too high and reads -2.19 dB low. This is
// why fi.spectral_tilt cannot be used for a calibration reference; measured,
// it misses SMPTE ST 2095-1's +/-0.25 dB by 1.5 to 2.1 dB at every rate.
// See seam-ltm: plugins/multipink/source/multipink_pink.h,
// tests/multipink_pink_test.cpp, and
// docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md
spectral_tilt_mz(N,f0,f1,alpha) = seq(i,N,sec(i))
with {
    r      = (f1/f0)^(1.0/float(N-1));
    mp(i)  = 2*ma.PI*f0*r^i;                       // analog pole, rad/s
    mz(i)  = 2*ma.PI*f0*r^(-alpha+i);              // analog zero, rad/s
    zp(i)  = exp(-mp(i)/ma.SR);
    zz(i)  = exp(-mz(i)/ma.SR);
    g(i)   = (1.0-zp(i))/(1.0-zz(i));              // unity gain at DC
    sec(i) = fi.tf1(g(i), -g(i)*zz(i), -zp(i));
};
// process = spectral_tilt_mz(17, 2.0, ma.SR/2, -0.5);

//------------------------------------------------ PINKING FILTER ---
// The tilt above at alpha = -1/2, plus one correction section that cancels the
// aliased tail matched-Z leaves behind. That residual is a function of f/SR
// ALONE -- verified identical at 48 and 192 kHz to three decimal places -- so
// its correction has CONSTANT coefficients and must not be redesigned per rate.
// Fitted 2026-08-19; it takes the residual from 1.48 dB to 0.029 dB.
pink_filter_mz = spectral_tilt_mz(N, 2.0, ma.SR/2, -0.5) : corr
with {
    N       = int(ceil(log2((ma.SR/2)/2.0))) + 1;  // one pole per octave
    czero   = -0.250775213;
    cpole   = -0.160124183;
    cg      = (1.0-cpole)/(1.0-czero);
    corr    = fi.tf1(cg, -cg*czero, -cpole);
};
// process = no.noise : pink_filter_mz;
```

- [ ] **Step 2: Compile it, to prove it is a Faust program**

```bash
cd ../faust-libraries
echo 'import("seam.lib"); process = sfi.pink_filter_mz;' | faust -I src -
```
Expected: compiles without error.

- [ ] **Step 3: Point the generator at it**

`seam.noises.lib:11` becomes:

```faust
multipink(N,g) = no.multinoise(N) : par(i,N,sfi.pink_filter_mz : *(g));
```

- [ ] **Step 4: Write the A/B, which is what makes the cross-reference true**

Three files. The comparison is of impulse responses, which tests the whole
filter — coefficients, order, and the correction section — rather than a summary
of it.

This is the one sanctioned use of `faust -lang cpp` under the suite's
convention: a scratch comparison tool in `tools/`. Nothing it generates ever
lands in `plugins/*/source/`.

Create `tools/faust-ab-arch.cpp` — a Faust architecture file that runs an
impulse and prints it:

```cpp
// Architecture file for the Faust/C++ A/B. Not a plug-in architecture:
// it feeds one impulse and prints the response, so two implementations can be
// compared sample by sample.
#include <cstdio>
#include <cstdlib>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

<<includeIntrinsic>>
<<includeclass>>

int main(int argc, char* argv[]) {
    const int sr = (argc > 1) ? atoi(argv[1]) : 48000;
    const int n  = (argc > 2) ? atoi(argv[2]) : 4096;
    mydsp DSP;
    DSP.init(sr);
    FAUSTFLOAT* in  = new FAUSTFLOAT[n]();
    FAUSTFLOAT* out = new FAUSTFLOAT[n]();
    in[0] = (FAUSTFLOAT)1.0;
    FAUSTFLOAT* ins[]  = { in };
    FAUSTFLOAT* outs[] = { out };
    DSP.compute(n, ins, outs);
    for (int i = 0; i < n; ++i) printf("%.17g\n", (double)out[i]);
    return 0;
}
```

Create `tools/pink-ir-dump.cpp` — the same impulse through the C++ engine:

```cpp
// Prints the impulse response of PinkDesign, for the Faust/C++ A/B.
#include "multipink_pink.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char* argv[]) {
    const double fs = (argc > 1) ? atof(argv[1]) : 48000.0;
    const int    n  = (argc > 2) ? atoi(argv[2]) : 4096;
    Seam::multipink::PinkDesign d;
    d.design(fs);
    std::vector<double> s((size_t)d.numSections, 0.0);
    for (int k = 0; k < n; ++k) {
        double x = (k == 0) ? 1.0 : 0.0;
        for (int i = 0; i < d.numSections; ++i) {
            const double y = d.b0[i] * x + s[(size_t)i];
            s[(size_t)i] = d.b1[i] * x - d.a1[i] * y;
            x = y;
        }
        printf("%.17g\n", x);
    }
    return 0;
}
```

Create `tools/faust-pink-ab.sh`:

```bash
#!/usr/bin/env bash
# Do the Faust specification and the hand-written C++ port describe the same
# filter? Compares impulse responses sample by sample, at two sample rates.
set -euo pipefail
cd "$(dirname "$0")"
LIBS=../../faust-libraries/src
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/pink.dsp" <<'DSP'
import("seam.lib");
process = sfi.pink_filter_mz;
DSP

clang++ -O2 -std=c++17 -I../plugins/multipink/source -o "$TMP/cppir" pink-ir-dump.cpp

status=0
for FS in 48000 96000; do
    faust -I "$LIBS" -double -a faust-ab-arch.cpp -o "$TMP/pink_$FS.cpp" "$TMP/pink.dsp"
    clang++ -O2 -std=c++17 -I/usr/local/include -o "$TMP/faustir_$FS" "$TMP/pink_$FS.cpp"
    "$TMP/faustir_$FS" "$FS" 4096 > "$TMP/faust_$FS.txt"
    "$TMP/cppir"       "$FS" 4096 > "$TMP/cpp_$FS.txt"
    python3 - "$TMP/faust_$FS.txt" "$TMP/cpp_$FS.txt" "$FS" <<'PY' || status=1
import sys
a=[float(x) for x in open(sys.argv[1])]
b=[float(x) for x in open(sys.argv[2])]
assert len(a)==len(b), "different lengths: %d vs %d" % (len(a),len(b))
worst=max(abs(x-y) for x,y in zip(a,b))
print("fs=%s  worst |faust-cpp| over 4096 samples: %.3e  %s"
      % (sys.argv[3], worst, "PASS" if worst < 1e-9 else "FAIL"))
sys.exit(0 if worst < 1e-9 else 1)
PY
done
exit $status
```

```bash
chmod +x tools/faust-pink-ab.sh
```

If the two disagree, the difference is real and one of them is wrong — do not
loosen the threshold to make it pass. The most likely causes, in order: `fi.tf1`
is `y = b0*x + b1*x[n-1] - a1*y[n-1]`, so a sign convention mismatch on `a1`
shows as an immediate divergence; and Faust's `N` must evaluate to the same
section count as `PinkDesign::design`, which the first differing sample will
show.

- [ ] **Step 5: Run it**

```bash
tools/faust-pink-ab.sh
```
Expected: PASS, largest difference printed and under 0.01 dB.

- [ ] **Step 6: Commit, in both repositories**

```bash
cd ../faust-libraries
git add src/seam.filters.lib src/seam.noises.lib
git commit -m "feat(filters): matched-Z spectral tilt, and a pinking filter that meets SMPTE"
cd ../seam-ltm
git add tools/faust-pink-ab.sh tools/README.md
git commit -m "test(multipink): the Faust spec and the C++ port, compared rather than asserted"
```

---

### Task 8: Close the loop in the documentation

**Files:**
- Modify: `plugins/multipink/doc/calibration.md`
- Modify: `doc/study/sessions/2026-08-19-pink-filter-design.md` (the "Aperto" list)
- Modify: `doc/study/sessions/2026-08-18-strx-band-table.md` (the open item "pink: scegliere A / B / entrambi")

**Interfaces:**
- Consumes: the settled numbers from Tasks 2, 5 and 6.
- Produces: nothing.

- [ ] **Step 1: Update the calibration document**

State what Reference means now (band level anchored, defined at 48 kHz), the metering convention (dBFS RMS, and that SMPTE's dBFS(AES17) differs by 3.01 dB), and the 1.00 dB step against measurements taken before 2026-08-19 — with the note that band-table deviations are unaffected.

- [ ] **Step 2: Close the open items**

In the 2026-08-18 diary, replace the open item with the outcome and a pointer to the spec. In the 2026-08-19 diary, tick the items Tasks 1-7 discharged and leave the `strx` grid extension open, naming it as its own scope.

- [ ] **Step 3: Check the FAUST REFERENCE block is true**

`multipink_pink.h` names `sfi.spectral_tilt_mz` and `sfi.pink_filter_mz`; confirm both exist with those names in `seam.filters.lib` after Task 7, and that the library's comment names the plugin, the test and the spec back.

- [ ] **Step 4: Run everything one last time**

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
Expected: all tests pass, including the uidesc lint.

- [ ] **Step 5: Commit**

```bash
git add plugins/multipink/doc/calibration.md doc/study/sessions/
git commit -m "docs(multipink): what the reference level means now, and the open items closed"
```
