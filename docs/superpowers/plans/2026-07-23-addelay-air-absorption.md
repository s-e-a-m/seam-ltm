# ADDELAY — Air-Absorption Delay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `addelay`, a 4-channel plugin that inherits ddelay's exact metres→samples+nextPrime delay and adds a distance-dependent minimum-phase air-absorption filter (ISO 9613-1) with two live-switchable topologies and optional 1/r spreading.

**Architecture:** Physics and filter design live SDK-free and unit-tested in a new `plugins/_common/seam_airabsorption.h` (mirroring how `seam_quadrature.h` backs `hilbert`). A plugin-local `addelay_dsp.h` composes the reused integer ring delay (per channel), one shared air-filter coefficient set (per-channel runtime state), and a scalar spreading gain. `addelay_processor.{h,cpp}` is the thin VST3 wrapper: five parameters, a control-rate re-design in the parameter-change handler guarded by a dirty-check, a 4ch↔4ch bus, and the suite's raw-blob state idiom.

**Tech Stack:** C++17, VST3 SDK + VSTGUI, CMake, doctest, `tools/check-uidesc.py` (ctest). No Faust codegen — Faust is the spec, C++ is the hand-port.

## Global Constraints

- Language: code/commits/docs in English; study diaries under `doc/study/` may be Italian.
- No `faust -lang cpp` output ever lands in `source/`. Hand-port only.
- DSP cores are header-only, SDK-free, unit-testable (pattern: `seam_quadrature.h`, `hilbert_dsp.h`).
- Speed of sound `c = 331.4 m/s` (matches `seam.math.lib::isos` and ddelay). Do not use 343.
- Bus: 4ch in → 4ch out, `SpeakerArr::kAmbi1stOrderACN` (same as ddelay).
- All four channels share one distance → one delay → one filter coefficient set → one spreading gain. Filter **state** is per channel.
- Filter re-design is control-rate (parameter-change handler), never in the sample loop; guard with a dirty-check on the quantized (d, T, RH) triple.
- Minimum-phase IIR only. Zero added latency. No FIR, no look-ahead.
- ISO 9613-1 α transcription in the spec is **NOT authoritative** — verify every constant against the standard (this is the doc-debt hard gate, memory `project_addelay_documentation_debt`).
- Parameter IDs (fixed): Distance=100, Temperature=101, Relative Humidity=102, Topology=103, Spreading=104.
- Ranges: Distance 0…30 m (default 0); Temperature −20…50 °C (default 20); RH 0…100 % (default 50); Topology {Shelf, Cascade} (default Shelf); Spreading {Off, On} (default Off).
- Pressure fixed at 1 atm = 101.325 kPa (a documented constant, not a control).
- Spreading: attenuation-only, 1 m reference, `gain = 1 / max(d, 1 m)`.
- Plugin UID: `0x5E4D000E, 0xA1B2C3D4, 0x41444C59, 0x0000000E` (14th plugin; ddelay used `…0001`, hilbert `…000B` / word3 = ASCII of the name; `41444C59` = "ADLY").
- Display name registered with the factory: `SEAM ADDELAY` (must match in `_ids.h` factory, CMakeLists project/bundle, and README — the linter cross-checks these).

---

## File Structure

- `plugins/_common/seam_airabsorption.h` — **new.** ISO 9613-1 α(f,T,RH,p); the min-phase first-order shelf building block; shelf + cascade fit to α·d; runtime `AirFilter`; `spreadingGain`. Pure math + tiny runtime, no SDK.
- `plugins/addelay/source/addelay_dsp.h` — **new.** `addelay::AirDelay`: 4-channel integer ring delay (reused ddelay core) + shared `AirFilterDesign` + per-channel `AirFilter` + spreading. `prepare/reset/setParams/process`. No SDK.
- `plugins/addelay/source/addelay_ids.h` — **new.** UID + `AddelayParams` enum.
- `plugins/addelay/source/addelay_processor.h` / `.cpp` — **new.** VST3 `SingleComponentEffect`: 5 params, param-change handler with dirty-check re-design, bus, state, view.
- `plugins/addelay/source/version.h` — **new.** Copy of ddelay's, renamed.
- `plugins/addelay/resource/addelay.uidesc` — **new.** Window per `doc/style/ui-style.md`.
- `plugins/addelay/CMakeLists.txt` — **new.** Copy of hilbert's (it already wires `../_common` includes).
- `plugins/addelay/doc/addelay.dsp` — **new.** Faust spec scaffold citing the new `seam.filters.lib` air-filter function.
- `tests/seam_airabsorption_test.cpp` — **new.** α anchors/monotonicity + fit quality + spreading + min-phase.
- `tests/addelay_dsp_test.cpp` — **new.** delay length, filter application, 4-channel identity.
- `tests/CMakeLists.txt` — **modify.** Register the two new test executables.
- `CMakeLists.txt` (root) — **modify.** `add_subdirectory(plugins/addelay)` after ddelay (line ~104).
- `plugins/README.md` — **modify.** Add ADDELAY to the gallery + `docs/img/addelay.png`.

---

## Task 1: ISO 9613-1 absorption coefficient α(f, T, RH, p)

**Files:**
- Create: `plugins/_common/seam_airabsorption.h`
- Test: `tests/seam_airabsorption_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `double Seam::air::alphaISO9613(double fHz, double tempC, double rhPercent, double paKPa = 101.325)` — attenuation coefficient in **dB/m**.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_airabsorption_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_airabsorption.h"
#include <cmath>

using namespace Seam::air;

// Reference conditions: 20 C, 50% RH, 1 atm. These BRACKETS are order-of-
// magnitude anchors (dB/m). Before merge, pin exact ISO 9613-1 Table values
// (or the NPL calculator) and tighten — see Step 5's provenance note.
TEST_CASE("alpha is small at low f, grows with frequency (20C/50%RH/1atm)") {
    const double a125  = alphaISO9613(125.0,   20.0, 50.0);
    const double a1k   = alphaISO9613(1000.0,  20.0, 50.0);
    const double a4k   = alphaISO9613(4000.0,  20.0, 50.0);
    const double a10k  = alphaISO9613(10000.0, 20.0, 50.0);

    CHECK(a125 < a1k);
    CHECK(a1k  < a4k);
    CHECK(a4k  < a10k);

    // 1 kHz ~ a few dB/km = a few 1e-3 dB/m; 10 kHz ~ 1e-1 dB/m.
    CHECK(a1k  == doctest::Approx(0.005).epsilon(0.6));   // 0.002..0.008 dB/m
    CHECK(a10k > 0.03);
    CHECK(a10k < 0.30);
    CHECK(a125 > 0.0);
}

TEST_CASE("alpha stays finite and positive at humidity and temperature edges") {
    CHECK(alphaISO9613(4000.0, 20.0,  0.0) > 0.0);   // bone-dry
    CHECK(alphaISO9613(4000.0, 20.0, 100.0) > 0.0);  // saturated
    CHECK(std::isfinite(alphaISO9613(8000.0, -20.0, 10.0)));
    CHECK(std::isfinite(alphaISO9613(8000.0,  50.0, 90.0)));
}

// Dry air absorbs high frequencies MORE than very humid air in the mid band
// is a common misconception; the ISO curve peaks at intermediate humidity.
// Just assert humidity actually changes the result (not a stubbed constant).
TEST_CASE("humidity changes alpha") {
    CHECK(alphaISO9613(4000.0, 20.0, 20.0) != alphaISO9613(4000.0, 20.0, 80.0));
}
```

- [ ] **Step 2: Run test to verify it fails**

Add to `tests/CMakeLists.txt` (after the `hilbert_dsp_test` block):

```cmake
add_executable(seam_airabsorption_test
    seam_airabsorption_test.cpp
)
target_include_directories(seam_airabsorption_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_airabsorption_test PRIVATE cxx_std_17)
add_test(NAME seam_airabsorption_test COMMAND seam_airabsorption_test)
```

Run: `cmake --build build --target seam_airabsorption_test`
Expected: FAIL to compile — `seam_airabsorption.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/_common/seam_airabsorption.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · seam_airabsorption — atmospheric absorption (ISO 9613-1)
//
// Atmospheric absorption coefficient alpha(f, T, RH, p) in dB/m, and the
// minimum-phase filters that render alpha*d over distance. The magnitude
// target is physics; the phase is the minimum phase that physics implies.
//
// ISO REFERENCE: ISO 9613-1:1993 "Attenuation of sound during propagation
// outdoors — Part 1: Calculation of the absorption of sound by the
// atmosphere." Constants below are TRANSCRIBED and MUST be verified
// constant-by-constant against the standard (Bass, Sutherland, Zuckerwar
// behind it). See doc/math/ (documentation debt).
//
// FAUST REFERENCE (seam.filters.lib): the air-absorption function (roadmap).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam { namespace air {

// ISO 9613-1 atmospheric absorption coefficient, dB per metre.
//   fHz       : frequency (Hz)
//   tempC     : temperature (deg C)
//   rhPercent : relative humidity (%)
//   paKPa     : atmospheric pressure (kPa), default 1 atm
inline double alphaISO9613(double fHz, double tempC, double rhPercent,
                           double paKPa = 101.325) {
    const double pr = 101.325;                 // reference pressure, kPa
    const double T  = tempC + 273.15;          // temperature, K
    const double T0 = 293.15;                   // reference temperature (20 C), K
    const double T01 = 273.16;                  // triple-point isotherm, K
    const double f2 = fHz * fHz;
    const double pRatio = paKPa / pr;
    const double tRatio = T / T0;

    // Saturation vapour pressure ratio psat/pr (ISO 9613-1 Annex B).
    const double C = -6.8346 * std::pow(T01 / T, 1.261) + 4.6151;
    const double psatRatio = std::pow(10.0, C);

    // Molar concentration of water vapour, % (h).
    const double h = rhPercent * psatRatio / pRatio;

    // Relaxation frequencies (oxygen, nitrogen).
    const double frO = pRatio * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = pRatio * std::pow(tRatio, -0.5)
                     * (9.0 + 280.0 * h * std::exp(-4.170 * (std::pow(tRatio, -1.0/3.0) - 1.0)));

    // Absorption coefficient, nepers-based term times 8.686 -> dB/m.
    const double classical = 1.84e-11 * (1.0 / pRatio) * std::sqrt(tRatio);
    const double oxygen = 0.01275 * std::exp(-2239.1 / T) / (frO + f2 / frO);
    const double nitro  = 0.1068  * std::exp(-3352.0 / T) / (frN + f2 / frN);
    const double relax = std::pow(tRatio, -2.5) * (oxygen + nitro);

    return 8.686 * f2 * (classical + relax);   // dB/m
}

}} // namespace Seam::air
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target seam_airabsorption_test && ./build/tests/seam_airabsorption_test`
Expected: PASS (all three cases).

- [ ] **Step 5: Verify by mutation, then commit**

Mutation check (memory `feedback_verify_tests_by_mutation`): temporarily change `8.686` to `0.0`, rebuild — the monotonic/bracket case must go RED. Restore. Also confirm removing the `h` humidity term makes the "humidity changes alpha" case RED. Restore.

Provenance note for later (doc-debt gate, do NOT skip at merge): open ISO 9613-1, confirm every constant (`-6.8346`, `1.261`, `4.6151`, `4.04e4`, `0.02`, `0.391`, `280.0`, `4.170`, `2239.1`, `3352.0`, `0.01275`, `0.1068`, `1.84e-11`), then tighten the bracket tests to the standard's tabulated α (or NPL calculator) values.

```bash
git add plugins/_common/seam_airabsorption.h tests/seam_airabsorption_test.cpp tests/CMakeLists.txt
git commit -m "feat(addelay): ISO 9613-1 atmospheric absorption coefficient"
```

---

## Task 2: Minimum-phase air filter — building block, fit, runtime, spreading

> **REVISED 2026-07-23 (supersedes the first-order design below).** The
> first-order mix-form cascade in the original code blocks CANNOT track the
> ISO 9613-1 target at large distance (α ∝ f² ⇒ the dB rolloff accelerates
> faster than any fixed number of first-order sections; measured 8–13 dB error
> at d = 30 m). Per Giuseppe's decision (2026-07-23): **Cascade = 3× 2nd-order
> RBJ high-shelf biquads at fixed corners {3000, 8000, 16000} Hz, LSQ gains
> (verified < 0.55 dB error, 2–30 m, all RH, all fs); Shelf = 1× first-order
> high-shelf biquad (b2 = a2 = 0), fixed corner 5000 Hz.** Unified Direct-Form
> biquad-cascade runtime; the design bakes fs into the coefficients so the
> `fsHint` field is removed. **The authoritative, empirically-verified spec for
> this task is `.superpowers/sdd/task-2r-brief.md`** — implement that, not the
> first-order code blocks below. The public interface consumed by Task 3
> (`Topology`, `AirFilterDesign`, `designAirFilter`, `AirFilter`,
> `spreadingGain`) is unchanged; only struct internals and section math differ.

**Files:**
- Modify: `plugins/_common/seam_airabsorption.h`
- Test: `tests/seam_airabsorption_test.cpp` (add cases)

**Interfaces:**
- Consumes: `alphaISO9613` (Task 1).
- Produces:
  - `enum class Seam::air::Topology { Shelf = 0, Cascade = 1 }`
  - `struct Seam::air::AirFilterDesign { static constexpr int kMaxSections = 3; int numSections; double corner[3]; double hfGain[3]; double maxErrorDb; bool converged; }`
  - `Seam::air::AirFilterDesign Seam::air::designAirFilter(double dMeters, double tempC, double rhPercent, double fs, Topology topo, double paKPa = 101.325)`
  - `struct Seam::air::AirFilter { void configure(const AirFilterDesign&); void reset(); double process(double x); }`
  - `double Seam::air::spreadingGain(double dMeters)`

**Design rationale (read before coding):** The building block is one **first-order minimum-phase shelf** written in mix form so its magnitude is exactly analyzable and its coefficients cannot be fumbled:

```
lp += c*(x - lp);   c = 1 - exp(-2*pi*fc/fs)   // one-pole low-pass
y   = v*x + (1 - v)*lp                          // v = HF linear gain (0<v<=1)
```

DC (`lp→x`) ⇒ `y=x` (0 dB, per spec); HF (`lp→0`) ⇒ `y=v·x` (gain v). One pole ⇒ minimum phase, zero latency. **Shelf** topology = one such section; **Cascade** = up to three, at fixed log-spaced corners, whose HF gains are fit to `α·d`. Fit is least-squares in dB over a log-frequency grid, per spec §"Fit method".

- [ ] **Step 1: Write the failing tests** (append to `tests/seam_airabsorption_test.cpp`)

```cpp
// Magnitude (dB) of a designed filter at frequency f.
static double designMagDb(const AirFilterDesign& d, double f, double fs) {
    // Product of first-order mix sections: y = v*x + (1-v)*LP(x).
    // Complex one-pole LP response H(w) = c / (1 - (1-c) e^{-jw}).
    const double w = 2.0 * M_PI * f / fs;
    std::complex<double> H(1.0, 0.0);
    for (int s = 0; s < d.numSections; ++s) {
        const double c = 1.0 - std::exp(-2.0 * M_PI * d.corner[s] / fs);
        const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
        const std::complex<double> lp = c / (1.0 - (1.0 - c) * z1);
        const double v = d.hfGain[s];
        H *= (v + (1.0 - v) * lp);
    }
    return 20.0 * std::log10(std::abs(H));
}

TEST_CASE("cascade tracks alpha*d across the band; shelf is looser") {
    const double fs = 48000.0, d = 20.0, T = 20.0, RH = 50.0;
    auto cas = designAirFilter(d, T, RH, fs, Topology::Cascade);
    auto shl = designAirFilter(d, T, RH, fs, Topology::Shelf);
    CHECK(cas.converged);
    CHECK(shl.converged);

    double casErr = 0.0, shlErr = 0.0;
    for (double f = 20.0; f <= 20000.0; f *= 1.3) {
        const double target = -alphaISO9613(f, T, RH) * d;   // dB (negative)
        casErr = std::max(casErr, std::abs(designMagDb(cas, f, fs) - target));
        shlErr = std::max(shlErr, std::abs(designMagDb(shl, f, fs) - target));
    }
    CHECK(casErr < 1.5);          // cascade tracks tightly
    CHECK(shlErr < casErr * 3.0); // shelf allowed to be looser, but same sign of tilt
    CHECK(cas.maxErrorDb == doctest::Approx(casErr).epsilon(0.25));
}

TEST_CASE("filter is unity at DC (no broadband loss without spreading)") {
    auto d = designAirFilter(20.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    CHECK(designMagDb(d, 1.0, 48000.0) == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("zero distance is a flat 0 dB pass") {
    auto d = designAirFilter(0.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    for (double f = 50.0; f <= 18000.0; f *= 2.0)
        CHECK(designMagDb(d, f, 48000.0) == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("AirFilter runtime matches its designed DC and settles") {
    auto des = designAirFilter(25.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    AirFilter f; f.configure(des); f.reset();
    double y = 0.0;
    for (int n = 0; n < 4096; ++n) y = f.process(1.0);   // DC step
    CHECK(y == doctest::Approx(1.0).epsilon(0.01));       // DC gain ~ unity
}

TEST_CASE("spreading gain: 1/max(d,1), attenuation only") {
    CHECK(spreadingGain(0.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(0.5)  == doctest::Approx(1.0));   // below 1 m: unity
    CHECK(spreadingGain(1.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(2.0)  == doctest::Approx(0.5));   // -6 dB
    CHECK(spreadingGain(4.0)  == doctest::Approx(0.25));  // -12 dB
    CHECK(spreadingGain(30.0) == doctest::Approx(1.0/30.0));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target seam_airabsorption_test`
Expected: FAIL to compile — `designAirFilter`, `AirFilter`, `spreadingGain`, `Topology` undefined.

- [ ] **Step 3: Write minimal implementation** (append inside `namespace Seam { namespace air {` in `seam_airabsorption.h`, before the closing braces)

```cpp
#include <algorithm>

enum class Topology { Shelf = 0, Cascade = 1 };

struct AirFilterDesign {
    static constexpr int kMaxSections = 3;
    int    numSections = 0;
    double corner[kMaxSections]  = {0,0,0};   // Hz
    double hfGain[kMaxSections]  = {1,1,1};   // linear HF gain of each section
    double maxErrorDb = 0.0;
    bool   converged  = false;
};

// dB magnitude of a single mix-form first-order section at (fc, v) over fs.
inline double sectionMagDb_(double fc, double v, double f, double fs) {
    const double w = 2.0 * M_PI * f / fs;
    const double c = 1.0 - std::exp(-2.0 * M_PI * fc / fs);
    // one-pole LP complex response
    const double cr = std::cos(w), ci = std::sin(w);
    // lp = c / (1 - (1-c) e^{-jw})
    const double dr = 1.0 - (1.0 - c) * cr;
    const double di =        (1.0 - c) * ci;   // sign of -e^{-jw} imag: (1-c)*sin(w)
    // lp = c / (dr - j di)
    const double den = dr*dr + di*di;
    const double lpR = c * dr / den;
    const double lpI = c * di / den;
    // H = v + (1-v)*lp
    const double hR = v + (1.0 - v) * lpR;
    const double hI =     (1.0 - v) * lpI;
    return 10.0 * std::log10(hR*hR + hI*hI);
}

// Fit the minimum-phase air filter to A(f) = -alpha*d (dB) over 20 Hz..min(20k,0.45fs).
inline AirFilterDesign designAirFilter(double dMeters, double tempC, double rhPercent,
                                       double fs, Topology topo, double paKPa = 101.325) {
    AirFilterDesign out;
    const int M = 48;
    const double fLo = 20.0, fHi = std::min(20000.0, 0.45 * fs);
    double freq[64], target[64];
    for (int i = 0; i < M; ++i) {
        const double t = (double)i / (double)(M - 1);
        freq[i]   = fLo * std::pow(fHi / fLo, t);
        target[i] = -alphaISO9613(freq[i], tempC, rhPercent, paKPa) * dMeters; // <= 0 dB
    }

    // Fixed corners: one for shelf, three log-spaced for cascade.
    if (topo == Topology::Shelf) { out.numSections = 1; out.corner[0] = 2500.0; }
    else { out.numSections = 3; out.corner[0] = 2000.0; out.corner[1] = 6000.0; out.corner[2] = 14000.0; }

    // Linearised least-squares in dB: basis column s_k(f) = section-k dB shape at a
    // reference cut, scaled linearly by weight g_k; solve (B^T B) g = B^T target.
    // Reference cut per section: -1 dB HF (v_ref = 10^(-1/20)).
    const double vRef = std::pow(10.0, -1.0 / 20.0);
    const int N = out.numSections;
    double B[64][3];
    for (int i = 0; i < M; ++i)
        for (int k = 0; k < N; ++k)
            B[i][k] = sectionMagDb_(out.corner[k], vRef, freq[i], fs); // dB per unit weight

    // Normal equations (N up to 3): solve small SPD system by Gaussian elimination.
    double A[3][3] = {{0}}, rhs[3] = {0};
    for (int k = 0; k < N; ++k) {
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < M; ++i) A[k][j] += B[i][k] * B[i][j];
        for (int i = 0; i < M; ++i) rhs[k] += B[i][k] * target[i];
    }
    double g[3] = {0,0,0};
    // forward elimination
    for (int p = 0; p < N; ++p) {
        double piv = A[p][p];
        if (std::abs(piv) < 1e-12) piv = 1e-12;
        for (int r = p + 1; r < N; ++r) {
            const double m = A[r][p] / piv;
            for (int cc = p; cc < N; ++cc) A[r][cc] -= m * A[p][cc];
            rhs[r] -= m * rhs[p];
        }
    }
    for (int p = N - 1; p >= 0; --p) {
        double s = rhs[p];
        for (int cc = p + 1; cc < N; ++cc) s -= A[p][cc] * g[cc];
        g[p] = s / (std::abs(A[p][p]) < 1e-12 ? 1e-12 : A[p][p]);
    }

    // Convert each section weight -> actual HF cut in dB -> linear gain v (clamped).
    for (int k = 0; k < N; ++k) {
        double cutDb = g[k] * (-1.0);              // weight g on a -1 dB reference shape
        if (cutDb > 0.0)   cutDb = 0.0;            // attenuation only
        if (cutDb < -80.0) cutDb = -80.0;
        out.hfGain[k] = std::pow(10.0, cutDb / 20.0);
    }

    // Report the achieved max dB error over the grid (true nonlinear response).
    double err = 0.0;
    for (int i = 0; i < M; ++i) {
        double db = 0.0;
        for (int k = 0; k < N; ++k) db += sectionMagDb_(out.corner[k], out.hfGain[k], freq[i], fs);
        err = std::max(err, std::abs(db - target[i]));
    }
    out.maxErrorDb = err;
    out.converged  = std::isfinite(err) && err < 6.0;   // loose gate; shelf may sit high
    return out;
}

// Runtime: shared coefficients, per-instance state; processes one channel.
struct AirFilter {
    void configure(const AirFilterDesign& d) {
        n_ = d.numSections;
        for (int k = 0; k < n_; ++k) {
            v_[k]  = d.hfGain[k];
            // c is fs-baked into the design's corner; recompute needs fs, so store c via corner+fs.
            corner_[k] = d.corner[k];
        }
        fs_ = d.fsHint;   // see note below
    }
    void setSampleRate(double fs) { fs_ = fs; recompute_(); }
    void reset() { for (int k = 0; k < AirFilterDesign::kMaxSections; ++k) lp_[k] = 0.0; }

    inline double process(double x) {
        double y = x;
        for (int k = 0; k < n_; ++k) {
            lp_[k] += c_[k] * (y - lp_[k]);
            y = v_[k] * y + (1.0 - v_[k]) * lp_[k];
        }
        return y;
    }

private:
    void recompute_() {
        for (int k = 0; k < n_; ++k)
            c_[k] = 1.0 - std::exp(-2.0 * M_PI * corner_[k] / fs_);
    }
    int    n_ = 0;
    double fs_ = 48000.0;
    double v_[AirFilterDesign::kMaxSections]      = {1,1,1};
    double corner_[AirFilterDesign::kMaxSections] = {0,0,0};
    double c_[AirFilterDesign::kMaxSections]      = {0,0,0};
    double lp_[AirFilterDesign::kMaxSections]     = {0,0,0};
};

// Geometric 1/r spreading, attenuation-only, 1 m reference.
inline double spreadingGain(double dMeters) {
    const double dd = dMeters < 1.0 ? 1.0 : dMeters;
    return 1.0 / dd;
}
```

**NOTE for the implementer:** the `AirFilter` above needs `fs` to turn a corner into `c`. Two clean options — pick one and delete the other in code review:
1. Add `double fsHint = 48000.0;` to `AirFilterDesign`, set it inside `designAirFilter` (`out.fsHint = fs;`), and call `recompute_()` at the end of `configure()`. (Simplest; the design already knows fs.)
2. Drop `fsHint`, require `setSampleRate(fs)` after every `configure()`.
Option 1 is recommended — the design is always made at a known fs. Make `configure()` call `recompute_()`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target seam_airabsorption_test && ./build/tests/seam_airabsorption_test`
Expected: PASS. If `casErr < 1.5` fails, widen the three corners' spread or bump `M`; if the shelf's `converged` gate trips, raise the `< 6.0` gate — the shelf is the deliberately-loose topology.

- [ ] **Step 5: Verify by mutation, then commit**

Mutation: set every `out.hfGain[k] = 1.0` (no cut) — the cascade tracking case must go RED (target is negative, response would be 0 dB). Restore. Set `spreadingGain` to `return 1.0;` — the spreading case must go RED. Restore.

```bash
git add plugins/_common/seam_airabsorption.h tests/seam_airabsorption_test.cpp
git commit -m "feat(addelay): min-phase shelf/cascade fit to alpha*d + spreading gain"
```

---

## Task 3: Plugin DSP core — delay + air filter + spreading

**Files:**
- Create: `plugins/addelay/source/addelay_dsp.h`
- Test: `tests/addelay_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything in `seam_airabsorption.h` (Tasks 1–2).
- Produces:
  - `class Seam::addelay::AirDelay` with:
    - `static constexpr int kNumChannels = 4;`
    - `void prepare(double fs);`
    - `void reset();`
    - `void setParams(double dMeters, double tempC, double rhPercent, air::Topology topo, bool spreading);`
    - `int delaySamples() const;`
    - `void process(const float* const* in, float* const* out, int numSamples);`
    - `void process(const double* const* in, double* const* out, int numSamples);`
  - `static int Seam::addelay::nextPrime(int n)` (exposed for the delay-length test).

- [ ] **Step 1: Write the failing test**

Create `tests/addelay_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "addelay_dsp.h"
#include <vector>
#include <cmath>

using namespace Seam::addelay;
using Seam::air::Topology;

TEST_CASE("delay length = nextPrime(round(d*SR/331.4))") {
    AirDelay dsp; dsp.prepare(48000.0);
    dsp.setParams(10.0, 20.0, 50.0, Topology::Shelf, false);
    const int raw = (int)std::lround(10.0 * 48000.0 / 331.4);   // ~1449
    CHECK(dsp.delaySamples() == nextPrime(raw));
    dsp.setParams(0.0, 20.0, 50.0, Topology::Shelf, false);
    CHECK(dsp.delaySamples() == 0);   // zero distance = no delay
}

TEST_CASE("all four channels receive the identical delay and filter") {
    AirDelay dsp; dsp.prepare(48000.0);
    dsp.setParams(5.0, 20.0, 50.0, Topology::Cascade, false);
    const int N = 2048, D = dsp.delaySamples();
    std::vector<float> in[4], out[4];
    for (int c = 0; c < 4; ++c) { in[c].assign(N, 0.0f); out[c].assign(N, 0.0f); }
    for (int c = 0; c < 4; ++c) in[c][0] = 1.0f;               // identical impulse
    const float* ip[4] = {in[0].data(),in[1].data(),in[2].data(),in[3].data()};
    float* op[4] = {out[0].data(),out[1].data(),out[2].data(),out[3].data()};
    dsp.process(ip, op, N);
    for (int c = 1; c < 4; ++c)
        for (int i = 0; i < N; ++i)
            CHECK(out[c][i] == doctest::Approx(out[0][i]));      // bit-for-bit equal path
    // impulse arrives no earlier than the delay (filter adds no pre-echo).
    for (int i = 0; i < D; ++i) CHECK(out[0][i] == doctest::Approx(0.0f));
}

TEST_CASE("spreading toggles a broadband level drop") {
    AirDelay dsp; dsp.prepare(48000.0);
    const int N = 4096;
    auto energy = [&](bool spread) {
        dsp.setParams(8.0, 20.0, 50.0, Topology::Cascade, spread);
        dsp.reset();
        std::vector<float> in(N, 0.0f), out(N, 0.0f);
        for (int i = 0; i < N; ++i) in[i] = std::sin(2.0 * M_PI * 100.0 * i / 48000.0);
        const float* ip[4] = {in.data(),in.data(),in.data(),in.data()};
        float* op[4] = {out.data(),out.data(),out.data(),out.data()}; // same buf ok: read=write? no
        // use distinct output buffers per channel
        std::vector<float> o0(N,0),o1(N,0),o2(N,0),o3(N,0);
        float* op2[4] = {o0.data(),o1.data(),o2.data(),o3.data()};
        dsp.process(ip, op2, N);
        double e = 0; for (int i = 0; i < N; ++i) e += o0[i]*o0[i];
        return e;
    };
    const double dry = energy(false), wet = energy(true);
    CHECK(wet < dry);                                  // 8 m => 1/8 gain at 100 Hz (below absorption)
    CHECK(wet == doctest::Approx(dry / 64.0).epsilon(0.15)); // (1/8)^2 in energy
}
```

- [ ] **Step 2: Run test to verify it fails**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(addelay_dsp_test
    addelay_dsp_test.cpp
)
target_include_directories(addelay_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/addelay/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(addelay_dsp_test PRIVATE cxx_std_17)
add_test(NAME addelay_dsp_test COMMAND addelay_dsp_test)
```

Run: `cmake --build build --target addelay_dsp_test`
Expected: FAIL to compile — `addelay_dsp.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/addelay/source/addelay_dsp.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · addelay — SDK-free DSP core (header-only, unit-testable).
//
// The two halves of "distance", split by phase type:
//   1. bulk propagation delay  — integer samples, nextPrime (ddelay core;
//      LINEAR phase, exact, no interpolation)
//   2. atmospheric absorption  — minimum-phase shelf/cascade fitted to
//      ISO 9613-1 alpha*d (seam_airabsorption.h; MIN phase, zero latency)
//   3. optional geometric 1/r spreading — a scalar broadband gain.
//
// One distance -> one delay -> one filter coefficient set -> one spreading
// gain, shared by all four channels. Filter STATE is per channel.
//
// FAUST REFERENCE (seam.math.lib): isos=331.4; imt2samp(mt)=int(mt*SR/isos);
//   next prime from sff.np. FAUST REFERENCE (seam.filters.lib): air filter.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_airabsorption.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Seam { namespace addelay {

inline bool isPrime(int n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if ((n & 1) == 0) return false;
    const int lim = (int)std::sqrt((double)n);
    for (int i = 3; i <= lim; i += 2) if (n % i == 0) return false;
    return true;
}
inline int nextPrime(int n) {
    if (n < 2) return 2;
    int c = (n & 1) ? n + 2 : n + 1;
    while (!isPrime(c)) c += 2;
    return c;
}

class AirDelay {
public:
    static constexpr int kNumChannels = 4;
    static constexpr double kSpeedOfSound = 331.4;   // m/s (matches ddelay / seam.math.lib::isos)
    static constexpr double kMaxDistance  = 30.0;    // m
    static constexpr int    kBufferSize   = 32768;   // > 30m @ 192k, power of two
    static constexpr int    kBufferMask   = kBufferSize - 1;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        for (int c = 0; c < kNumChannels; ++c) buf_[c].assign(kBufferSize, 0.0);
        writeIndex_ = 0;
        redesign_();
        for (int c = 0; c < kNumChannels; ++c) { filt_[c].configure(design_); filt_[c].reset(); }
    }

    void reset() {
        for (int c = 0; c < kNumChannels; ++c) {
            std::fill(buf_[c].begin(), buf_[c].end(), 0.0);
            filt_[c].reset();
        }
        writeIndex_ = 0;
    }

    void setParams(double dMeters, double tempC, double rhPercent,
                   air::Topology topo, bool spreading) {
        dMeters   = std::clamp(dMeters, 0.0, kMaxDistance);
        d_ = dMeters; t_ = tempC; rh_ = rhPercent; topo_ = topo; spreading_ = spreading;
        updateDelay_();
        redesign_();
        for (int c = 0; c < kNumChannels; ++c) filt_[c].configure(design_); // shared coeffs, keep state
        spreadGain_ = spreading_ ? air::spreadingGain(d_) : 1.0;
    }

    int delaySamples() const { return delay_; }

    void process(const float* const* in, float* const* out, int n)  { run_(in, out, n); }
    void process(const double* const* in, double* const* out, int n){ run_(in, out, n); }

private:
    template <typename S>
    void run_(const S* const* in, S* const* out, int n) {
        int w = writeIndex_;
        const int delay = delay_;
        for (int i = 0; i < n; ++i) {
            const int r = (w - delay) & kBufferMask;
            for (int c = 0; c < kNumChannels; ++c) {
                buf_[c][w] = (double)in[c][i];
                const double delayed = buf_[c][r];
                const double filtered = filt_[c].process(delayed);   // per-channel state
                out[c][i] = (S)(filtered * spreadGain_);              // shared scalar
            }
            w = (w + 1) & kBufferMask;
        }
        writeIndex_ = w;
    }

    void updateDelay_() {
        const double mm = std::round(d_ * 1000.0) / 1000.0;          // mm quantization (ddelay idiom)
        const int nRaw = (int)std::lround(mm * fs_ / kSpeedOfSound);
        int d = (nRaw < 2) ? nRaw : nextPrime(nRaw);
        if (d > kBufferSize - 1) d = kBufferSize - 1;
        if (d < 0) d = 0;
        delay_ = d;
    }

    void redesign_() {
        design_ = air::designAirFilter(d_, t_, rh_, fs_, topo_);
    }

    double fs_ = 48000.0;
    double d_ = 0.0, t_ = 20.0, rh_ = 50.0;
    air::Topology topo_ = air::Topology::Shelf;
    bool   spreading_ = false;
    double spreadGain_ = 1.0;
    int    delay_ = 0, writeIndex_ = 0;

    std::vector<double> buf_[kNumChannels];
    air::AirFilterDesign design_;
    air::AirFilter       filt_[kNumChannels];
};

}} // namespace Seam::addelay
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target addelay_dsp_test && ./build/tests/addelay_dsp_test`
Expected: PASS.

- [ ] **Step 5: Verify by mutation, then commit**

Mutation: in `run_`, drop the `* spreadGain_` — the spreading case goes RED. Restore. Change one channel's index (`filt_[0]` for all c) — the 4-channel identity still passes (shared coeffs) but is a poor test; instead mutate `updateDelay_` to `d = nRaw` (skip nextPrime) — the delay-length case goes RED. Restore.

```bash
git add plugins/addelay/source/addelay_dsp.h tests/addelay_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(addelay): plugin DSP core — delay + air filter + spreading"
```

---

## Task 4: VST3 processor — IDs, parameters, param-change re-design, state, bus

**Files:**
- Create: `plugins/addelay/source/addelay_ids.h`
- Create: `plugins/addelay/source/version.h`
- Create: `plugins/addelay/source/addelay_processor.h`
- Create: `plugins/addelay/source/addelay_processor.cpp`

**Interfaces:**
- Consumes: `Seam::addelay::AirDelay` (Task 3).
- Produces: `Seam::AddelayProcessor` (VST3 `SingleComponentEffect`), `Seam::AddelayProcessorUID`, `enum Seam::AddelayParams`.

This task has no doctest harness (it is the SDK layer); its gate is a clean build plus the VST3 `validator`. Do all four files, then build once.

- [ ] **Step 1: Write `addelay_ids.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Distance Air-Absorption Delay
// Unique identifier + parameter IDs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 14th plugin in the suite. word3 = ASCII "ADLY".
static const Steinberg::FUID AddelayProcessorUID (0x5E4D000E, 0xA1B2C3D4, 0x41444C59, 0x0000000E);

enum AddelayParams : Steinberg::Vst::ParamID {
    kParamDistance    = 100,
    kParamTemperature = 101,
    kParamHumidity    = 102,
    kParamTopology    = 103,   // {Shelf, Cascade}
    kParamSpreading   = 104    // {Off, On}
};

// Parameter ranges (physical units).
static constexpr double kAddDistMax   = 30.0;    // m
static constexpr double kAddTempMin   = -20.0;   // C
static constexpr double kAddTempMax   = 50.0;    // C
static constexpr double kAddRhMin     = 0.0;     // %
static constexpr double kAddRhMax     = 100.0;   // %

} // namespace Seam
```

- [ ] **Step 2: Write `version.h`** (copy ddelay's, swap the strings)

```cpp
#pragma once
#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR "1"
#define MAJOR_VERSION_INT 1
#define SUB_VERSION_STR "0"
#define SUB_VERSION_INT 0
#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR "1"
#define BUILD_NUMBER_INT 1

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR

#define stringOriginalFilename "addelay.vst3"
#define stringFileDescription "SEAM ADDELAY"
#define stringCompanyName "SEAM"
#define stringCompanyWeb "https://s-e-a-m.github.io"
#define stringCompanyEmail "mailto:info@s-e-a-m.net"
#define stringLegalCopyright "© 2026 SEAM"
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
```

(Confirm the exact macro set against `plugins/ddelay/source/version.h` and match it verbatim; only the description/filename strings differ.)

- [ ] **Step 3: Write `addelay_processor.h`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Distance Air-Absorption Delay
//
// ddelay's exact metres->samples+nextPrime delay (linear-phase bulk
// propagation) plus a minimum-phase air-absorption filter fitted to
// ISO 9613-1 alpha*d (the min-phase residual that completes the model),
// with optional 1/r geometric spreading. 4ch in -> 4ch out; one distance
// drives delay, filter and spreading; the same filter on all four channels
// preserves inter-channel phase by construction.
//
// FAUST REFERENCE (seam.math.lib): isos=331.4; imt2samp; sff.np nextPrime.
// FAUST REFERENCE (seam.filters.lib): the air-absorption filter (roadmap).
// ISO REFERENCE: ISO 9613-1:1993 (alpha), Bass/Sutherland/Zuckerwar behind it.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "addelay_dsp.h"

namespace Seam {

class AddelayProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    AddelayProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new AddelayProcessor);
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
    // Pull current normalized params, denormalize, and push into the DSP.
    // Guarded by a dirty-check so identical automation points don't re-fit.
    void applyParams();

    addelay::AirDelay dsp_;

    // Cached denormalized inputs for the dirty-check.
    double lastD_ = -1.0, lastT_ = -999.0, lastRh_ = -1.0;
    int    lastTopo_ = -1, lastSpread_ = -1;
};

} // namespace Seam
```

- [ ] **Step 4: Write `addelay_processor.cpp`**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "addelay_processor.h"
#include "addelay_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cmath>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

AddelayProcessor::AddelayProcessor() {}

tresult PLUGIN_API AddelayProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new RangeParameter(
        STR16("Distance"), kParamDistance, STR16("m"),
        0.0, kAddDistMax, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(new RangeParameter(
        STR16("Temperature"), kParamTemperature, STR16("C"),
        kAddTempMin, kAddTempMax, 20.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter(new RangeParameter(
        STR16("Humidity"), kParamHumidity, STR16("%"),
        kAddRhMin, kAddRhMax, 50.0, 0, ParameterInfo::kCanAutomate));

    auto* topo = new StringListParameter(STR16("Topology"), kParamTopology);
    topo->appendString(STR16("Shelf"));
    topo->appendString(STR16("Cascade"));
    parameters.addParameter(topo);

    auto* spread = new StringListParameter(STR16("Spreading"), kParamSpreading);
    spread->appendString(STR16("Off"));
    spread->appendString(STR16("On"));
    parameters.addParameter(spread);

    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API AddelayProcessor::setupProcessing(ProcessSetup& setup) {
    tresult res = SingleComponentEffect::setupProcessing(setup);
    dsp_.prepare(processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0);
    return res;
}

tresult PLUGIN_API AddelayProcessor::setActive(TBool state) {
    if (state) {
        dsp_.prepare(processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0);
        lastD_ = lastT_ = lastRh_ = -1e9;      // force a re-design on first block
        lastTopo_ = lastSpread_ = -1;
        applyParams();                          // honour recalled state
        dsp_.reset();
    }
    return SingleComponentEffect::setActive(state);
}

void AddelayProcessor::applyParams() {
    auto norm = [&](ParamID id) -> double {
        auto* p = parameters.getParameter(id);
        return p ? p->getNormalized() : 0.0;
    };
    const double d  = norm(kParamDistance)    * kAddDistMax;
    const double t  = kAddTempMin + norm(kParamTemperature) * (kAddTempMax - kAddTempMin);
    const double rh = kAddRhMin   + norm(kParamHumidity)    * (kAddRhMax   - kAddRhMin);
    const int topo   = norm(kParamTopology)  >= 0.5 ? 1 : 0;
    const int spread = norm(kParamSpreading) >= 0.5 ? 1 : 0;

    // Dirty-check on the mm/quantized triple + discrete choices.
    const double dq = std::round(d * 1000.0) / 1000.0;
    if (dq == lastD_ && t == lastT_ && rh == lastRh_ &&
        topo == lastTopo_ && spread == lastSpread_)
        return;
    lastD_ = dq; lastT_ = t; lastRh_ = rh; lastTopo_ = topo; lastSpread_ = spread;

    dsp_.setParams(d, t, rh,
                   topo ? air::Topology::Cascade : air::Topology::Shelf,
                   spread != 0);
}

tresult PLUGIN_API AddelayProcessor::process(ProcessData& data) {
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
        applyParams();   // one re-design per block at most (dirty-checked)
    }

    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    const int32 inCh  = data.inputs[0].numChannels;
    const int32 outCh = data.outputs[0].numChannels;
    const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    if (inCh < 4 || outCh < 4) {
        for (int32 c = 0; c < outCh; ++c) if (out[c]) memset(out[c], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;   // IIR tail: never claim silence out

    if (data.symbolicSampleSize == kSample32)
        dsp_.process(reinterpret_cast<const float* const*>(in),
                     reinterpret_cast<float* const*>(out), data.numSamples);
    else
        dsp_.process(reinterpret_cast<const double* const*>(in),
                     reinterpret_cast<double* const*>(out), data.numSamples);
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// State: five normalized floats, fixed order. Read defensively (short-read
// caveat, memory project_vst3_state_shortread_rotation_family): a missing
// field keeps the parameter's current default rather than corrupting it.
tresult PLUGIN_API AddelayProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    const ParamID ids[5] = { kParamDistance, kParamTemperature, kParamHumidity,
                             kParamTopology, kParamSpreading };
    for (int i = 0; i < 5; ++i) {
        float v = 0.0f;
        if (!s.readFloat(v)) break;         // stop on short read, keep remaining defaults
        setParamNormalized(ids[i], v);
    }
    lastD_ = lastT_ = lastRh_ = -1e9; lastTopo_ = lastSpread_ = -1;
    applyParams();
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    const ParamID ids[5] = { kParamDistance, kParamTemperature, kParamHumidity,
                             kParamTopology, kParamSpreading };
    for (int i = 0; i < 5; ++i) {
        auto* p = parameters.getParameter(ids[i]);
        s.writeFloat(p ? (float)p->getNormalized() : 0.0f);
    }
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API AddelayProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "addelay.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::AddelayProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM ADDELAY",
        0,
        "Fx|Delay",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::AddelayProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 5: Build (deferred to Task 5's CMake) — commit the sources**

The processor cannot build until Task 5 wires CMake. Commit the four source files now; the build gate is at the end of Task 5.

```bash
git add plugins/addelay/source/addelay_ids.h plugins/addelay/source/version.h \
        plugins/addelay/source/addelay_processor.h plugins/addelay/source/addelay_processor.cpp
git commit -m "feat(addelay): VST3 processor — 5 params, dirty-checked re-design, state, bus"
```

---

## Task 5: CMake wiring + build + VST3 validator

**Files:**
- Create: `plugins/addelay/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)

**Interfaces:** none (build system).

- [ ] **Step 1: Write `plugins/addelay/CMakeLists.txt`** (modeled on hilbert's — it already adds the `../_common` include path the DSP needs)

```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-addelay
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM ADDELAY – Distance Air-Absorption Delay"
)

set(addelay_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/addelay_ids.h
    source/addelay_dsp.h
    source/addelay_processor.cpp
    source/addelay_processor.h
    source/version.h
    resource/addelay.uidesc
)

set(target addelay)

smtg_add_vst3plugin(${target} ${addelay_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/addelay.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.addelay"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

**NOTE:** `resource/addelay.uidesc` is referenced here but created in Task 6. Create a minimal placeholder file now (`echo '<?xml version="1.0" encoding="UTF-8"?><vstgui-ui-description version="1"></vstgui-ui-description>' > plugins/addelay/resource/addelay.uidesc`) so this task builds; Task 6 replaces it with the real window. `createView` will return a blank editor until then — acceptable for the build gate.

- [ ] **Step 2: Register in the root `CMakeLists.txt`**

Modify `CMakeLists.txt` — add after the ddelay line (~104):

```cmake
    add_subdirectory(plugins/addelay)
```

- [ ] **Step 3: Configure + build the plugin and the tests**

Run (VST3 SDK path per memory `reference_vst3sdk_location`; Xcode generator per memory `project_dslar_plugin_status`):

```bash
cmake -G Xcode -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target addelay --config Debug
```

Expected: `addelay.vst3` builds with no errors.

- [ ] **Step 4: Run the VST3 validator + the full ctest suite**

```bash
./build/bin/Debug/validator build/VST3/Debug/addelay.vst3   # path per your generator
ctest --test-dir build --output-on-failure
```

Expected: validator reports all tests passed; ctest green including `seam_airabsorption_test` and `addelay_dsp_test`. (The `check-uidesc` ctest may WARN about the placeholder uidesc / missing screenshot — those close in Task 6/final. It must not ERROR.)

- [ ] **Step 5: Commit**

```bash
git add plugins/addelay/CMakeLists.txt CMakeLists.txt plugins/addelay/resource/addelay.uidesc
git commit -m "build(addelay): register plugin, placeholder window, green validator + ctest"
```

---

## Task 6: The window (`addelay.uidesc`) — UI standard

**Files:**
- Modify: `plugins/addelay/resource/addelay.uidesc` (replace placeholder)
- Reference: `plugins/_template/resource/_template.uidesc`, `doc/style/ui-style.md`, `plugins/ddelay/resource/ddelay.uidesc` (sibling), `plugins/hilbert/resource/hilbert.uidesc` (Topology selector precedent)

**Interfaces:** control tags must match the parameter IDs (100–104).

- [ ] **Step 1: Copy the template as the starting window**

```bash
cp plugins/_template/resource/_template.uidesc plugins/addelay/resource/addelay.uidesc
```

- [ ] **Step 2: Fill the five zones** (per spec §Window; ddelay is the S single-column sibling — start S, taller, and only move to a light L if five controls crowd)

Edit `addelay.uidesc` so it declares, in zone order:
- **HEADER:** title `SEAM ADDELAY`, subtitle `Distance Air-Absorption Delay`, an info line.
- **SETUP:** the **Topology** selector (`COptionMenu`, tag = `kParamTopology`/103), placed where hilbert places its Topology.
- **OPS:** the **Spreading** selector (2-item `COptionMenu` or checkbox, tag = `kParamSpreading`/104) — the on/off operational control.
- **FINE:** three label/slider/value blocks — Distance (100), Temperature (101), Relative Humidity (102).
- **FOOTER:** the SEAM logo.

Bind every control-tag to its parameter id; use the palette, fonts (`InfoFont`/SourceCodePro-Light), and zone metrics from `ui-style.md`. No `TextDim` colour anywhere (the linter rejects it).

- [ ] **Step 3: Lint the window**

Run: `python3 tools/check-uidesc.py`
Expected: `plugins/addelay/resource/addelay.uidesc` reports no `ERROR` (a `WARN` about the missing screenshot is expected until the final task).

- [ ] **Step 4: Build + eyeball in the host**

```bash
cmake --build build --target addelay --config Debug
```

Deploy to Reaper (memory `reference_vst3_momentary_button_and_vstgui_attrs` / `reference_vst3_symlink_ownership`) and confirm all five controls render in the correct zones and move their parameters.

- [ ] **Step 5: Commit**

```bash
git add plugins/addelay/resource/addelay.uidesc
git commit -m "feat(addelay): window — five controls across the five UI zones"
```

---

## Task 7: Faust spec + docs + screenshot + README (documentation debt)

**Files:**
- Create: `plugins/addelay/doc/addelay.dsp`
- Create: `plugins/addelay/doc/math/` (English, formal — the ISO 9613-1 model)
- Create (optional): `plugins/addelay/doc/study/` (Italian diary)
- Add: `librerie/faust-libraries/src/seam.filters.lib` air-filter function (per spec §Faust)
- Create: `docs/img/addelay.png`
- Modify: `plugins/README.md`

**Interfaces:** none (docs). This is the hard gate on "done" (memory `project_addelay_documentation_debt`): the ISO model must be sourced, not invented.

- [ ] **Step 1: Add the Faust air-filter function to `seam.filters.lib`**

Write the α(f, T, h_rel) formula and the two minimum-phase designs (shelf, cascade) as a new `sfi.*` function, matching the C++ hand-port. The delay half is already specified (`seam.math.lib::isos`/`imt2samp`, `sff.np`). Add the `declare` metadata + inline test line per the library conventions in the workspace `CLAUDE.md`.

- [ ] **Step 2: Write `plugins/addelay/doc/addelay.dsp`**

A Faust program that `import("seam.lib")` and wires the reused delay + the new air-filter function, so `tools/gen-faust-doc.sh` can render its `-svg` + mathdoc PDF (memory `reference_faust_doc_generation`).

- [ ] **Step 3: Write `doc/math/` (English, formal)**

Document, sourced from ISO 9613-1: α(f, T, h_rel) in dB/m; the assumed atmospheric conditions (1 atm) and the exposed T/RH ranges; the minimum-phase fit method for each topology. **Verify every constant against the standard now** (close the Task 1 provenance note). One sentence per line (memory `feedback_latex_writing_style`).

- [ ] **Step 4: Generate Faust mathdoc + retake the screenshot**

```bash
tools/gen-faust-doc.sh addelay     # or the repo's exact invocation
```

Screenshot the host window to `docs/img/addelay.png` (after the Task 6 host check).

- [ ] **Step 5: Add ADDELAY to `plugins/README.md` gallery, lint, final ctest, commit**

Add the README gallery entry referencing `docs/img/addelay.png` (the `check_screenshots` linter rule requires it). Then:

```bash
python3 tools/check-uidesc.py                       # now 0 WARN for addelay
ctest --test-dir build --output-on-failure          # all green
git add plugins/addelay/doc plugins/README.md docs/img/addelay.png \
        ../faust-libraries/src/seam.filters.lib      # path as applicable
git commit -m "docs(addelay): ISO 9613-1 math, Faust spec + mathdoc, screenshot, README"
```

---

## Self-Review

**Spec coverage:**
- Purpose / two-halves-of-distance → Tasks 1–3 (delay reused + min-phase residual). ✔
- Five parameters (100–104), ranges, defaults → Task 4 `initialize`. ✔
- Filter redesigned on Distance/T/RH change, control-rate, dirty-checked → Task 4 `applyParams`. ✔
- ISO 9613-1 magnitude, sourced-not-invented → Task 1 + Task 7 Step 3 (hard gate). ✔
- Minimum phase, zero latency, FIR rejected → Task 2 mix-form one-pole sections. ✔
- Inter-channel phase preserved (same filter, all 4 ch) → Task 3 shared `design_`, per-channel state; tested. ✔
- Both topologies, live-switchable, same ISO target → Task 2 `Topology` + Task 4 param. ✔
- Fit by least-squares in dB over log-freq → Task 2 `designAirFilter`. ✔
- DSP flow delay→filter→spreading → Task 3 `run_`. ✔
- Geometric spreading, toggle, 1 m ref, attenuation-only, default Off → Task 2 `spreadingGain` + Task 3/4. ✔
- Bus 4→4 kAmbi1stOrderACN; raw-blob state; short-read caveat → Task 4. ✔
- Faust spec + mathdoc, doc/math, README, screenshot → Task 7. ✔
- Window five zones, lint clean → Task 6. ✔

**Gaps flagged:** (a) exact ISO constant verification and tightened α anchors are deferred to Task 7 Step 3 but MUST be closed before "done" — this is the documentation-debt hard gate, not optional. (b) The `AirFilterDesign::fsHint` vs `setSampleRate` choice (Task 2 Step 3 NOTE) must be resolved in code review — recommended: `fsHint`, and make `configure()` call `recompute_()`.

**Placeholder scan:** no TBD/TODO; every code step carries complete code. The only intentional placeholder is the empty `addelay.uidesc` in Task 5 Step 1, explicitly replaced in Task 6.

**Type consistency:** `air::Topology`, `air::AirFilterDesign`, `air::AirFilter`, `air::designAirFilter`, `air::spreadingGain`, `addelay::AirDelay`, `addelay::nextPrime` are used identically across Tasks 2–4. Parameter IDs 100–104 match between `_ids.h`, `initialize`, `applyParams`, and the uidesc tags.

---

**Execution note:** Tasks 1→2→3 are pure TDD DSP (fast, high-confidence). Task 4 depends on 3; Task 5 gates the first real build; Tasks 6–7 are UI + the documentation-debt hard gate. Recommended order is linear 1→7.
