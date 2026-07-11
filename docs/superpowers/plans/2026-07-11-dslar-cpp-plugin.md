# dslar C++ Plugin (Phase 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the loadable `dslar` VST3 plugin — a hand-written C++ port of the frozen Faust spec `sds.lar` — plus Phase A of the suite metering facility (`_common/seam_meter.h`), with two live meters (r, g).

**Architecture:** A header-only SDK-free DSP core (`dslar_dsp.h`) re-implements `sds.lar` by hand as small tested units (Pd dB converters, `OnePoleHip`, `ControlLine`, `HannRms`, `DelayLine`, and the `Larsen` assembly), feedforward and mono. The VST3 processor wires it to eight parameters and publishes r/g through the `multipink` read-only-output-parameter idiom, normalized by `seam_meter.h`. The GUI is a two-column VSTGUI layout plus two read-only meter bars.

**Tech Stack:** VST3 SDK + VSTGUI, CMake, doctest (header-only, SDK-free core tests), C++17.

## Global Constraints

- Two repos are in play but only `seam-ltm` changes here; the Faust libraries are the frozen spec and are NOT modified. Repo: `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm`, branch `dslar`.
- VST3 SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`; configure with `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- The plugin is mono 1-in / 1-out (`LAR.pd` is mono feedforward; the Larsen loop is acoustic/external).
- FUID: next free suite index is `0x5E4D000E` (0x5E4D000C ltburst, 0x5E4D000D ltglide are the last used).
- Meter dB floor is −60 dBFS everywhere (`seam_meter.h` default).
- Scope is Phase A metering only (Level + Gain via dB helpers + `LevelFollower`); no gain-reduction/generic helpers, no EBU R128, no retrofit of past plugins.
- Faust literal pi `3.14159` (NOT `M_PI`) is reproduced wherever the Pd clone uses it (`hip`, `env`), so coefficients stay bit-identical to Pd.
- Faust `spd.env` overlap-add uses the constraint `npoints % period == 0`; the C++ sizes `npoints = round(fs*2048/44100)`, `period = npoints/2`, so the constraint always holds.
- Doc/prose lines: one sentence per line (clean diffs).
- Every commit ends with the trailer `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- DSP-core tests compile header-only with a direct `c++` invocation (fast TDD loop, no SDK needed) and are ALSO registered in `tests/CMakeLists.txt` for `ctest`.
- Sample-rate-dependent tests sweep the common studio rates `{44100, 48000, 96000, 192000}` Hz (SR-independence must hold across the whole range, up to 192 kHz).

## File Structure

- Create `plugins/_common/seam_meter.h` — metering facility Phase A: dB helpers + `LevelFollower` (header-only, SDK-free).
- Create `plugins/dslar/source/dslar_dsp.h` — the hand port of `sds.lar` (all DSP units + `Larsen`).
- Create `plugins/dslar/source/dslar_ids.h` — UID, 8 parameter IDs, 2 read-only meter tags, ranges.
- Create `plugins/dslar/source/dslar_processor.h` / `.cpp` — VST3 wiring, mono I/O, publishes r/g.
- Create `plugins/dslar/source/version.h` — plugin metadata.
- Create `plugins/dslar/CMakeLists.txt` — the plugin target; register it in the root `CMakeLists.txt`.
- Create `plugins/dslar/resource/dslar.uidesc` — GUI (two columns + two meter bars).
- Create `tests/seam_meter_test.cpp`, `tests/dslar_dsp_test.cpp`; register both in `tests/CMakeLists.txt`.
- Modify `plugins/dslar/doc/study/dslar-study.tex` — add the Phase 4 (C++ port) diary section.

---

## Task 1: `seam_meter.h` — metering facility Phase A

**Files:**
- Create: `plugins/_common/seam_meter.h`
- Test: `tests/seam_meter_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: namespace `seam::meter` with `double lin2db(double x, double floorDb=-60.0)`, `double db2norm(double db, double floorDb=-60.0)`, `double lin2norm(double x, double floorDb=-60.0)`, `double norm2db(double n, double floorDb=-60.0)`, and `class LevelFollower { enum class Mode{Peak,Rms}; void prepare(double fs, Mode=Mode::Rms, double windowMs=300.0); void reset(); double feed(double x); double value() const; }`.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_meter_test.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_meter.h"
#include <cmath>

using namespace seam::meter;

TEST_CASE("lin2db: unity is 0 dB, silence floors, half is ~-6 dB") {
    CHECK(lin2db(1.0)   == doctest::Approx(0.0));
    CHECK(lin2db(0.0)   == doctest::Approx(-60.0));      // floor, never -inf
    CHECK(lin2db(0.5)   == doctest::Approx(-6.0205999));
    CHECK(lin2db(1e-9)  == doctest::Approx(-60.0));      // below floor clamps
}

TEST_CASE("db2norm maps [floor,0] to [0,1] and clamps") {
    CHECK(db2norm(0.0)    == doctest::Approx(1.0));
    CHECK(db2norm(-60.0)  == doctest::Approx(0.0));
    CHECK(db2norm(-30.0)  == doctest::Approx(0.5));
    CHECK(db2norm(6.0)    == doctest::Approx(1.0));      // above 0 clamps to 1
    CHECK(db2norm(-90.0)  == doctest::Approx(0.0));      // below floor clamps to 0
}

TEST_CASE("lin2norm composes lin2db and db2norm; norm2db inverts") {
    CHECK(lin2norm(1.0) == doctest::Approx(1.0));
    CHECK(lin2norm(0.0) == doctest::Approx(0.0));
    CHECK(norm2db(0.5)  == doctest::Approx(-30.0));
}

TEST_CASE("LevelFollower(RMS) of a DC 0.5 settles to 0.5") {
    LevelFollower f;
    f.prepare(48000.0, LevelFollower::Mode::Rms, 10.0);
    double v = 0.0;
    for (int i = 0; i < 48000; ++i) v = f.feed(0.5);
    CHECK(v == doctest::Approx(0.5).epsilon(1e-3));
}

TEST_CASE("LevelFollower(Peak) tracks the absolute value") {
    LevelFollower f;
    f.prepare(48000.0, LevelFollower::Mode::Peak, 1.0);
    double v = 0.0;
    for (int i = 0; i < 48000; ++i) v = f.feed(-0.8);
    CHECK(v == doctest::Approx(0.8).epsilon(1e-3));
}
```

- [ ] **Step 2: Register the test and run it to verify it FAILS**

Append to `tests/CMakeLists.txt`:
```cmake
add_executable(seam_meter_test
    seam_meter_test.cpp
)
target_include_directories(seam_meter_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_meter_test PRIVATE cxx_std_17)
add_test(NAME seam_meter_test COMMAND seam_meter_test)
```
Run (fast header-only compile, no SDK):
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/_common tests/seam_meter_test.cpp -o /tmp/seam_meter_test
```
Expected: FAIL — `fatal error: 'seam_meter.h' file not found`.

- [ ] **Step 3: Write the minimal implementation**

Create `plugins/_common/seam_meter.h`:
```cpp
// SEAM-LTM · seam_meter — shared metering facility (Phase A).
//
// Header-only, SDK-free. Three-layer metering system (measure / transport /
// render); this header is the MEASURE + normalization layer, usable from a
// plugin's SDK-free <plugin>_dsp.h or from its processor. Transport is the
// multipink read-only-output-parameter idiom; render is the uidesc convention.
// See docs/superpowers/specs/2026-07-11-seam-ltm-metering-system-design.md.
#pragma once
#include <cmath>

namespace seam { namespace meter {

// Linear amplitude -> dBFS, floored (silence maps to floorDb, never -inf).
inline double lin2db(double x, double floorDb = -60.0) {
    if (x <= 0.0) return floorDb;
    const double db = 20.0 * std::log10(x);
    return db < floorDb ? floorDb : db;
}

// dB -> normalized [0,1] over [floorDb, 0], clamped.
inline double db2norm(double db, double floorDb = -60.0) {
    const double n = (db - floorDb) / (0.0 - floorDb);
    return n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
}

// Linear amplitude -> normalized [0,1] bar value.
inline double lin2norm(double x, double floorDb = -60.0) {
    return db2norm(lin2db(x, floorDb), floorDb);
}

// Normalized [0,1] -> dB (for labels).
inline double norm2db(double n, double floorDb = -60.0) {
    return floorDb + n * (0.0 - floorDb);
}

// One-pole level follower (RMS or peak) with a time-constant window.
class LevelFollower {
public:
    enum class Mode { Peak, Rms };
    void prepare(double fs, Mode mode = Mode::Rms, double windowMs = 300.0) {
        fs_   = (fs > 0.0) ? fs : 48000.0;
        mode_ = mode;
        const double tau = windowMs * 0.001;
        coef_ = (tau > 0.0) ? std::exp(-1.0 / (tau * fs_)) : 0.0;
        reset();
    }
    void reset() { state_ = 0.0; }
    double feed(double x) {
        const double v = (mode_ == Mode::Rms) ? x * x : std::fabs(x);
        state_ = v + coef_ * (state_ - v);      // one-pole toward v
        return value();
    }
    double value() const {
        return (mode_ == Mode::Rms) ? std::sqrt(state_) : state_;
    }
private:
    double fs_ = 48000.0, coef_ = 0.0, state_ = 0.0;
    Mode   mode_ = Mode::Rms;
};

}} // namespace seam::meter
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/_common tests/seam_meter_test.cpp -o /tmp/seam_meter_test && /tmp/seam_meter_test
```
Expected: all test cases pass (`[doctest] ... 0 failed`).

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/_common/seam_meter.h tests/seam_meter_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(_common): seam_meter.h — suite metering facility Phase A (dB + LevelFollower)

Header-only, SDK-free measure/normalization layer of the seam-ltm metering
system: lin2db/db2norm/lin2norm/norm2db at a -60 dBFS floor, plus a one-pole
RMS/peak LevelFollower. doctest-verified. First consumer: dslar.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `dslar_dsp.h` scaffolding + Pd dB converters

**Files:**
- Create: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: namespace `Seam::dslar`, inner namespace `Seam::dslar::pd` with `inline double powtodb(double p)` and `inline double dbtorms(double d)` (Pd `x_acoustics.c`, 100 dB offset scale, clamped, silence→0).

- [ ] **Step 1: Write the failing test**

Create `tests/dslar_dsp_test.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "dslar_dsp.h"
#include <cmath>

using namespace Seam::dslar;

TEST_CASE("pd::powtodb — Pd 100 dB-offset power->dB, clamped") {
    CHECK(pd::powtodb(1.0)  == doctest::Approx(100.0));   // 100 + 10*log10(1)
    CHECK(pd::powtodb(0.01) == doctest::Approx(80.0));    // 100 + 10*log10(0.01)
    CHECK(pd::powtodb(0.0)  == doctest::Approx(0.0));      // silence -> 0
    CHECK(pd::powtodb(0.25) == doctest::Approx(93.9794000867));
}

TEST_CASE("pd::dbtorms — inverse converter, clamped") {
    CHECK(pd::dbtorms(100.0) == doctest::Approx(1.0));
    CHECK(pd::dbtorms(80.0)  == doctest::Approx(0.1));
    CHECK(pd::dbtorms(0.0)   == doctest::Approx(0.0));
    // dbtorms(powtodb(0.25)) == sqrt(0.25) == 0.5 (the dB round-trip cancels)
    CHECK(pd::dbtorms(pd::powtodb(0.25)) == doctest::Approx(0.5));
}
```

- [ ] **Step 2: Register the test and run it to verify it FAILS**

Append to `tests/CMakeLists.txt`:
```cmake
add_executable(dslar_dsp_test
    dslar_dsp_test.cpp
)
target_include_directories(dslar_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/dslar/source
)
target_compile_features(dslar_dsp_test PRIVATE cxx_std_17)
add_test(NAME dslar_dsp_test COMMAND dslar_dsp_test)
```
Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `fatal error: 'dslar_dsp.h' file not found`.

- [ ] **Step 3: Write the minimal implementation**

Create `plugins/dslar/source/dslar_dsp.h`:
```cpp
// SEAM-LTM · dslar — SDK-free DSP core (header-only, unit-testable).
//
// Hand port of Di Scipio's LAR.pd: a mono FEEDFORWARD homeostatic processor.
// The Larsen loop is acoustic and external (dac~ -> room -> mic -> adc~), so
// there is NO internal feedback; tab1 is a feedforward delay, not a recursion.
//
// FAUST REFERENCE (seam.discipio.lib / seam.pdclone.lib):
//   lar(gate,drive,ref,k,tsmooth,tab1,tab2,output,x) =
//       audio(fx) * analysisGain(fx) * output with {
//     fx = x * (gate : spd.line(2000.0));
//     audio(s)        = s : spd.hip(100.0) : *(drive) : delms(tab1);
//     analysisGain(s) = s : delms(tab2) : larsengain(2048,1024,ref,k) : spd.line(tsmooth); };
//   larsengain(np,pd,ref,k) = spd.env(np,pd) : spd.dbtorms : (\r.|r-ref|^k);
//   spd.hip(f)  = fi.pole(coef):fi.zero(1):*(normal), coef=clip(1-f*2*3.14159/SR,0,1);
//   spd.env     = Hann-weighted RMS-power, control-rate hold (period = np/2);
//   spd.line    = control-rate ramp, 20 ms grain staircase, restart-from-current.
//
// Re-implemented by hand in readable C++ (project convention, seam-ltm/CLAUDE.md);
// faust -lang cpp is never used for plugin DSP. SR-independence lives here: the
// env window is sized round(fs*2048/44100) at prepare(fs). Pd's literal pi
// 3.14159 is reproduced so coefficients are bit-identical to Pd.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace Seam { namespace dslar {

// --- Pd x_acoustics.c converters (100 dB-offset scale, clamped, silence->0) ---
namespace pd {
    inline double powtodb(double p) {
        if (p <= 0.0) return 0.0;
        const double db = 100.0 + 10.0 * std::log10(p);
        return db < 0.0 ? 0.0 : db;
    }
    inline double dbtorms(double d) {
        if (d <= 0.0) return 0.0;
        const double dd = d > 485.0 ? 485.0 : d;   // Pd clamp
        return std::pow(10.0, (dd - 100.0) / 20.0);
    }
}

}} // namespace Seam::dslar
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(dslar): dslar_dsp.h scaffold + Pd powtodb/dbtorms converters

Header-only SDK-free DSP core with the FAUST REFERENCE block. First units: the
Pd x_acoustics.c dB converters (100 dB offset, clamped). doctest-verified,
including the dbtorms(powtodb(p)) = sqrt(p) round-trip.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `OnePoleHip` — Pd `hip~` one-pole highpass

**Files:**
- Modify: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `class OnePoleHip { void prepare(double fs); void reset(); void setCutoff(double hz); double process(double x); }` in `Seam::dslar`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dslar_dsp_test.cpp`:
```cpp
TEST_CASE("OnePoleHip: coef follows Pd 1-f*2*3.14159/SR; blocks DC") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    // Feed DC 1.0; a highpass settles its output toward 0.
    double y = 0.0;
    for (int n = 0; n < 44100; ++n) y = hp.process(1.0);
    CHECK(std::fabs(y) < 1e-3);
}

TEST_CASE("OnePoleHip: first sample of a unit step equals normal") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    const double coef   = std::min(1.0, std::max(0.0, 1.0 - 100.0*(2.0*3.14159)/44100.0));
    const double normal = (1.0 + coef) / 2.0;
    // w0 = 1 + coef*0 = 1 ; y0 = normal*(w0 - 0) = normal.
    CHECK(hp.process(1.0) == doctest::Approx(normal));
}
```

- [ ] **Step 2: Run to verify it FAILS**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `'OnePoleHip' was not declared`.

- [ ] **Step 3: Write the minimal implementation**

Insert into `dslar_dsp.h` before the closing `}} // namespace Seam::dslar`:
```cpp
// --- Pd d_filter.c sighip: one-pole/one-zero highpass -------------------------
// coef = clip(1 - f*2pi/SR, 0, 1); w[n] = x[n] + coef*w[n-1];
// y[n] = normal*(w[n] - w[n-1]); normal = (1+coef)/2. Pd literal pi 3.14159.
// DIFFERS from a Butterworth biquad; this is Pd's exact topology.
class OnePoleHip {
public:
    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; setCutoff(cutoff_); reset(); }
    void reset() { w1_ = 0.0; }
    void setCutoff(double hz) {
        cutoff_ = hz;
        double c = 1.0 - hz * (2.0 * 3.14159) / fs_;
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
        coef_   = c;
        normal_ = (1.0 + c) / 2.0;
    }
    double process(double x) {
        const double w = x + coef_ * w1_;      // fi.pole(coef)
        const double y = normal_ * (w - w1_);  // fi.zero(1) * normal
        w1_ = w;
        return y;
    }
private:
    double fs_ = 48000.0, cutoff_ = 100.0, coef_ = 0.0, normal_ = 0.0, w1_ = 0.0;
};
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp
git commit -m "$(cat <<'EOF'
feat(dslar): OnePoleHip — Pd hip~ one-pole highpass (exact topology)

fi.pole(coef):fi.zero(1):*(normal), coef=clip(1-f*2*3.14159/SR,0,1). Pd's
literal pi so coef is bit-identical. doctest: unit-step first sample = normal;
DC is blocked.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `ControlLine` — Pd `line` control-rate ramp

**Files:**
- Modify: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `class ControlLine { void prepare(double fs); void reset(); void setRampMs(double ms); double process(double target); }` in `Seam::dslar` — a linear ramp toward `target` over `ms`, restart-from-current, emitted on a 20 ms grain staircase.

- [ ] **Step 1: Write the failing test**

Append to `tests/dslar_dsp_test.cpp`:
```cpp
// Reference Pd line: continuous v = sv + min(e/R,1)*(target-sv), restart from
// current on retarget, then a 20 ms grain staircase (sample-and-hold).
static std::vector<double> pd_line_reference(const std::vector<double>& in,
                                             double ms, double fs) {
    const double R = ms * fs / 1000.0;
    const int    G = (int)(20.0 * fs / 1000.0);
    std::vector<double> cont(in.size());
    double sv = 0.0, v = 0.0, prev = 0.0; int e = 0;
    for (size_t n = 0; n < in.size(); ++n) {
        const bool chg = (in[n] != prev);
        if (chg) { sv = v; e = 0; } else { e += 1; }
        v = sv + std::min((double)e / R, 1.0) * (in[n] - sv);
        cont[n] = v; prev = in[n];
    }
    std::vector<double> out(in.size());
    double held = 0.0;
    for (size_t n = 0; n < in.size(); ++n) {
        if ((int)(n % (size_t)G) == 0) held = cont[n];
        out[n] = held;
    }
    return out;
}

TEST_CASE("ControlLine matches the Pd line reference (step + mid-ramp retarget)") {
    const double fs = 44100.0, ms = 100.0;
    std::vector<double> in;
    for (int n = 0; n < 2000; ++n) in.push_back(1.0);   // step 0->1
    for (int n = 0; n < 7000; ++n) in.push_back(0.3);   // retarget 1->0.3 mid-ramp
    ControlLine ln; ln.prepare(fs); ln.setRampMs(ms); ln.reset();
    std::vector<double> ref = pd_line_reference(in, ms, fs);
    double md = 0.0;
    for (size_t n = 0; n < in.size(); ++n)
        md = std::max(md, std::fabs(ln.process(in[n]) - ref[n]));
    CHECK(md < 1e-9);
}

TEST_CASE("ControlLine reaches the target and holds it") {
    ControlLine ln; ln.prepare(44100.0); ln.setRampMs(100.0); ln.reset();
    double y = 0.0;
    for (int n = 0; n < 44100; ++n) y = ln.process(1.0);   // 1 s >> 100 ms ramp
    CHECK(y == doctest::Approx(1.0));
}
```

- [ ] **Step 2: Run to verify it FAILS**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `'ControlLine' was not declared`.

- [ ] **Step 3: Write the minimal implementation**

Insert into `dslar_dsp.h` before the closing namespace braces:
```cpp
// --- Pd x_time.c line: control-rate ramp, 20 ms grain staircase --------------
// v = setval + min(elapsed/R, 1)*(target-setval), restart-from-current on a new
// target (setval frozen to the current value at the change), sampled every 20 ms
// (DEFAULTLINEGRAIN) into a staircase. Same idiom as spd.line.
class ControlLine {
public:
    void prepare(double fs) {
        fs_    = (fs > 0.0) ? fs : 48000.0;
        grain_ = (int)(20.0 * fs_ / 1000.0);
        if (grain_ < 1) grain_ = 1;
        reset();
    }
    void reset() { v_ = 0.0; sv_ = 0.0; e_ = 0.0; prevTarget_ = 0.0; held_ = 0.0; n_ = 0; }
    void setRampMs(double ms) { ms_ = ms; }
    double process(double target) {
        const double R = ms_ * fs_ / 1000.0;
        const bool chg = (target != prevTarget_);
        if (chg) { sv_ = v_; e_ = 0.0; } else { e_ += 1.0; }
        const double frac = (R > 0.0) ? std::min(e_ / R, 1.0) : 1.0;
        v_ = sv_ + frac * (target - sv_);
        prevTarget_ = target;
        if (n_ % grain_ == 0) held_ = v_;    // pulse at n = 0, grain, 2*grain, ...
        ++n_;
        return held_;
    }
private:
    double fs_ = 48000.0, ms_ = 100.0, v_ = 0.0, sv_ = 0.0, e_ = 0.0,
           prevTarget_ = 0.0, held_ = 0.0;
    int grain_ = 882, n_ = 0;
};
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp
git commit -m "$(cat <<'EOF'
feat(dslar): ControlLine — Pd line control-rate ramp + 20 ms staircase

Linear tau/R interp with restart-from-current, sampled on the 20 ms grain into
a staircase. doctest verifies it float-exact vs a Pd line reference (step and
mid-ramp retarget), max|diff| < 1e-9.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `HannRms` — Pd `env~` Hann-weighted RMS, SR-adaptive window

**Files:**
- Modify: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`

**Interfaces:**
- Consumes: `pd::powtodb`, `pd::dbtorms`.
- Produces: `class HannRms { void prepare(double fs); void reset(); double process(double x); int window() const; }` in `Seam::dslar` — the control-rate Hann-weighted RMS (`spd.env : spd.dbtorms`), window `round(fs*2048/44100)`, period = window/2.

- [ ] **Step 1: Write the failing test**

Append to `tests/dslar_dsp_test.cpp`:
```cpp
TEST_CASE("HannRms: window is SR-adaptive round(fs*2048/44100)") {
    HannRms e44; e44.prepare(44100.0);
    CHECK(e44.window() == 2048);                          // exact at the design SR
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        HannRms e; e.prepare(fs);
        CHECK(e.window() == (int)std::lround(fs*2048.0/44100.0));
    }
}

TEST_CASE("HannRms of DC 0.5 settles to 0.5 (Hann-normalized RMS)") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        HannRms e; e.prepare(fs);
        double r = 0.0;
        for (int n = 0; n < 3*e.window(); ++n) r = e.process(0.5);
        CHECK(r == doctest::Approx(0.5).epsilon(1e-4));   // sum(hann)=1 -> RMS = |DC|
    }
}
```

- [ ] **Step 2: Run to verify it FAILS**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `'HannRms' was not declared`.

- [ ] **Step 3: Write the minimal implementation**

Insert into `dslar_dsp.h` before the closing namespace braces:
```cpp
// --- Pd d_ctl.c env~: Hann-weighted RMS-power envelope, control-rate hold -----
// result = sum_i hann[i] * x[n-i]^2, hann[i] = (1-cos(2*3.14159*i/np))/np
// (normalized, sum = 1). Emitted every `period` samples (Pd default np/2) and
// held between ticks; then powtodb -> dbtorms gives the Hann-weighted RMS.
// Ported as a ring buffer with the Hann sum computed at each capture and held:
// O(np/period) amortized per sample, float-identical to the overlap-add spec.
// SR-independence: np = round(fs*2048/44100), period = np/2, computed here.
class HannRms {
public:
    void prepare(double fs) {
        fs_      = (fs > 0.0) ? fs : 48000.0;
        npoints_ = (int)std::lround(fs_ * 2048.0 / 44100.0);
        if (npoints_ < 2) npoints_ = 2;
        period_  = npoints_ / 2;
        if (period_ < 1) period_ = 1;
        hann_.resize(npoints_);
        for (int i = 0; i < npoints_; ++i)
            hann_[i] = (1.0 - std::cos((2.0 * 3.14159 * i) / npoints_)) / npoints_;
        ring_.assign(npoints_, 0.0);
        reset();
    }
    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0);
        pos_ = 0; n_ = 0; heldDb_ = 0.0;
    }
    double process(double x) {
        ring_[pos_] = x;
        pos_ = (pos_ + 1) % npoints_;
        if (n_ % period_ == 0) {                 // capture instant (control rate)
            double acc = 0.0;
            int idx = (pos_ - 1 + npoints_) % npoints_;   // most recent sample = x[n-0]
            for (int i = 0; i < npoints_; ++i) {
                const double s = ring_[idx];
                acc += hann_[i] * s * s;                  // hann[i] weights x[n-i]
                idx = (idx - 1 + npoints_) % npoints_;
            }
            heldDb_ = pd::powtodb(acc);
        }
        ++n_;
        return pd::dbtorms(heldDb_);
    }
    int window() const { return npoints_; }
private:
    double fs_ = 48000.0; int npoints_ = 2048, period_ = 1024, pos_ = 0, n_ = 0;
    double heldDb_ = 0.0;
    std::vector<double> ring_, hann_;
};
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp
git commit -m "$(cat <<'EOF'
feat(dslar): HannRms — Pd env~ Hann-weighted RMS, SR-adaptive window

Ring-buffer Hann sum computed at each control-rate capture and held, then
powtodb->dbtorms. Window round(fs*2048/44100), period np/2 (SR-independence in
the deliverable). doctest: DC 0.5 -> RMS 0.5 at 44.1 and 96 kHz.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `DelayLine` — feedforward integer delay

**Files:**
- Modify: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `class DelayLine { void prepare(double fs); void reset(); void setDelayMs(double ms); double process(double x); }` in `Seam::dslar` — `de.delay`-equivalent integer delay, buffer sized for `DELMAXMS=200 ms @ SRMAX=192 kHz`, read offset `round(ms*fs/1000)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dslar_dsp_test.cpp`:
```cpp
TEST_CASE("DelayLine: integer-sample delay of an impulse") {
    DelayLine dl; dl.prepare(44100.0);
    dl.setDelayMs(1.0);                          // round(1*44.1) = 44 samples
    const int D = (int)(1.0 * 44100.0 / 1000.0 + 0.5);
    std::vector<double> out;
    out.push_back(dl.process(1.0));              // impulse at n=0
    for (int n = 1; n < 128; ++n) out.push_back(dl.process(0.0));
    CHECK(out[0] == doctest::Approx(0.0));
    CHECK(out[D] == doctest::Approx(1.0));       // impulse reappears at n = D
}

TEST_CASE("DelayLine: zero delay is a pass-through") {
    DelayLine dl; dl.prepare(44100.0);
    dl.setDelayMs(0.0);
    CHECK(dl.process(0.7) == doctest::Approx(0.7));
}
```

- [ ] **Step 2: Run to verify it FAILS**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `'DelayLine' was not declared`.

- [ ] **Step 3: Write the minimal implementation**

Insert into `dslar_dsp.h` before the closing namespace braces:
```cpp
// --- de.delay-equivalent feedforward integer delay ---------------------------
// LAR's named lines are single-writer/single-reader (tab1 loop, tab2 analysis
// tap). Buffer sized once for the worst case DELMAXMS @ SRMAX; the read offset
// adapts to fs at runtime. y = x @ round(ms*fs/1000).
class DelayLine {
public:
    static constexpr double kDelMaxMs = 200.0;
    static constexpr double kSrMax    = 192000.0;
    void prepare(double fs) {
        fs_   = (fs > 0.0) ? fs : 48000.0;
        size_ = (int)(kDelMaxMs * kSrMax / 1000.0) + 1;   // 38401
        buf_.assign(size_, 0.0);
        reset();
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); pos_ = 0; }
    void setDelayMs(double ms) {
        int d = (int)(ms * fs_ / 1000.0 + 0.5);
        if (d < 0) d = 0;
        if (d >= size_) d = size_ - 1;
        delay_ = d;
    }
    double process(double x) {
        buf_[pos_] = x;
        const int r = (pos_ - delay_ + size_) % size_;
        const double y = buf_[r];
        pos_ = (pos_ + 1) % size_;
        return y;
    }
private:
    double fs_ = 48000.0; int size_ = 0, pos_ = 0, delay_ = 0;
    std::vector<double> buf_;
};
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp
git commit -m "$(cat <<'EOF'
feat(dslar): DelayLine — feedforward integer delay (de.delay equivalent)

Buffer sized DELMAXMS@SRMAX, read offset round(ms*fs/1000) adapting to fs.
doctest: impulse reappears at D; zero delay is pass-through.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: `Larsen` — the feedforward LAR assembly + meters

**Files:**
- Modify: `plugins/dslar/source/dslar_dsp.h`
- Test: `tests/dslar_dsp_test.cpp`

**Interfaces:**
- Consumes: `OnePoleHip`, `ControlLine`, `HannRms`, `DelayLine`.
- Produces: `class Larsen` in `Seam::dslar` with `void prepare(double fs)`, `void reset()`, `double process(double x)`, meter getters `double measuredRms() const`, `double analysisGain() const`, and setters `setPower(bool)`, `setDrive(double)`, `setTarget(double)`, `setSteepness(double)`, `setSmoothingMs(double)`, `setLoopDelayMs(double)`, `setDecorrelationMs(double)`, `setOutput(double)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/dslar_dsp_test.cpp`:
```cpp
static Larsen makeLar(double fs) {
    Larsen L; L.prepare(fs);
    L.setPower(true);
    L.setDrive(1.0);
    L.setTarget(1.0);
    L.setSteepness(40.0);
    L.setSmoothingMs(200.0);
    L.setLoopDelayMs(50.0);
    L.setDecorrelationMs(20.0);
    L.setOutput(1.0);
    return L;
}

TEST_CASE("Larsen: DC 0.5 settles the loop gain g to 0.5^40 (homeostat), any SR") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Larsen L = makeLar(fs);
        // Feed long enough for the 2000 ms input fade + window + 200 ms smoothing.
        const int N = (int)(fs * 3.0);           // 3 s
        for (int n = 0; n < N; ++n) L.process(0.5);
        CHECK(L.measuredRms()   == doctest::Approx(0.5).epsilon(1e-3));
        CHECK(L.analysisGain()  == doctest::Approx(std::pow(0.5, 40.0)).epsilon(1e-2));
    }
}

TEST_CASE("Larsen: power off mutes the input fade (fx -> 0)") {
    Larsen L = makeLar(44100.0);
    L.setPower(false);
    double y = 0.0;
    for (int n = 0; n < 44100*3; ++n) y = L.process(0.5);
    CHECK(std::fabs(y) < 1e-9);
    CHECK(L.measuredRms() == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("Larsen: no NaN/Inf across parameter extremes") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Larsen L; L.prepare(fs);
        L.setPower(true); L.setDrive(4.0); L.setTarget(0.0);
        L.setSteepness(80.0); L.setSmoothingMs(1.0);
        L.setLoopDelayMs(200.0); L.setDecorrelationMs(200.0); L.setOutput(1.0);
        for (int n = 0; n < 20000; ++n) {
            double y = L.process(std::sin(0.01 * n));
            REQUIRE(std::isfinite(y));
        }
    }
}
```

- [ ] **Step 2: Run to verify it FAILS**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test
```
Expected: FAIL — `'Larsen' was not declared`.

- [ ] **Step 3: Write the minimal implementation**

Insert into `dslar_dsp.h` before the closing namespace braces:
```cpp
// --- LAR assembly: feedforward mono homeostatic processor --------------------
// fx = x * inputFade(power)                 (2000 ms anti-click fade)
// audio  = delay(tab1, drive * hip100(fx))
// r      = HannRms(delay(tab2, fx))         (control-rate Hann RMS)
// g      = smooth_tsmooth( |r - ref|^k )    (homeostat, then 20 ms staircase)
// y      = audio * g * output
// No internal feedback: the Larsen loop is acoustic and external.
class Larsen {
public:
    void prepare(double fs) {
        inputFade_.prepare(fs); inputFade_.setRampMs(2000.0);
        smooth_.prepare(fs);    smooth_.setRampMs(200.0);
        hip_.prepare(fs);       hip_.setCutoff(100.0);
        delayAudio_.prepare(fs);
        delayAnalysis_.prepare(fs);
        env_.prepare(fs);
        reset();
    }
    void reset() {
        inputFade_.reset(); smooth_.reset(); hip_.reset();
        delayAudio_.reset(); delayAnalysis_.reset(); env_.reset();
        rMeter_ = 0.0; gMeter_ = 0.0;
    }
    void setPower(bool on)            { power_ = on ? 1.0 : 0.0; }
    void setDrive(double d)           { drive_ = d; }
    void setTarget(double r)          { ref_ = r; }
    void setSteepness(double k)       { k_ = k; }
    void setSmoothingMs(double ms)    { smooth_.setRampMs(ms); }
    void setLoopDelayMs(double ms)    { delayAudio_.setDelayMs(ms); }
    void setDecorrelationMs(double ms){ delayAnalysis_.setDelayMs(ms); }
    void setOutput(double o)          { output_ = o; }

    double process(double x) {
        const double fade = inputFade_.process(power_);
        const double fx   = x * fade;
        double a = hip_.process(fx) * drive_;      // audio branch
        a = delayAudio_.process(a);
        const double sIn = delayAnalysis_.process(fx);
        const double r   = env_.process(sIn);      // Hann RMS (control rate)
        const double gRaw = std::pow(std::fabs(r - ref_), k_);
        const double g   = smooth_.process(gRaw);  // line(tsmooth)
        rMeter_ = r; gMeter_ = g;
        return a * g * output_;
    }
    double measuredRms()  const { return rMeter_; }
    double analysisGain() const { return gMeter_; }
private:
    ControlLine inputFade_, smooth_;
    OnePoleHip  hip_;
    DelayLine   delayAudio_, delayAnalysis_;
    HannRms     env_;
    double power_ = 0.0, drive_ = 1.0, ref_ = 1.0, k_ = 40.0, output_ = 1.0;
    double rMeter_ = 0.0, gMeter_ = 0.0;
};
```

- [ ] **Step 4: Run the test to verify it PASSES**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
c++ -std=c++17 -Itests -Iplugins/dslar/source tests/dslar_dsp_test.cpp -o /tmp/dslar_dsp_test && /tmp/dslar_dsp_test
```
Expected: PASS (all `dslar_dsp_test` cases).

- [ ] **Step 5: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source/dslar_dsp.h tests/dslar_dsp_test.cpp
git commit -m "$(cat <<'EOF'
feat(dslar): Larsen — feedforward LAR assembly + r/g meters

Composes inputFade(2000ms) -> {audio: hip100->drive->delay(tab1)} and
{analysis: delay(tab2)->HannRms->|r-ref|^k->smooth(tsmooth)}, y=audio*g*output.
No internal feedback. doctest: DC 0.5 -> g=0.5^40 at 44.1/96 kHz; power-off
mutes; no NaN across extremes.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: VST3 wiring — ids, processor, version, CMake, root registration (builds)

**Files:**
- Create: `plugins/dslar/source/dslar_ids.h`, `plugins/dslar/source/version.h`, `plugins/dslar/source/dslar_processor.h`, `plugins/dslar/source/dslar_processor.cpp`
- Create: `plugins/dslar/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root — add `add_subdirectory(plugins/dslar)`)

**Interfaces:**
- Consumes: `Seam::dslar::Larsen` (Task 7), `seam::meter::lin2norm` (Task 1).
- Produces: a buildable VST3 target `dslar` exposing 8 parameters (IDs 100–107) and 2 read-only meter parameters (IDs 200–201), mono 1-in/1-out.

- [ ] **Step 1: Write `dslar_ids.h`**

Create `plugins/dslar/source/dslar_ids.h`:
```cpp
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 14th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (ltburst=000C, ltglide=000D, dslar=000E).
static const Steinberg::FUID DSLARProcessorUID(
    0x5E4D000E, 0xB2C3D4E5, 0x44534C41, 0x52000000);   // "DSLA","R\0\0\0"

enum DSLARParams : Steinberg::Vst::ParamID {
    kParamPower       = 100,   // 0/1  system on/off (2000 ms anti-click fade)
    kParamDrive       = 101,   // 1..4 audio pre-gain
    kParamTarget      = 102,   // 0..1 homeostat reference
    kParamSteepness   = 103,   // 1..80 homeostat exponent
    kParamSmoothing   = 104,   // 1..1000 ms control smoothing
    kParamLoopDelay   = 105,   // 1..200 ms feedforward loop delay (tab1)
    kParamDecorr      = 106,   // 1..200 ms decorrelation tap (tab2)
    kParamOutput      = 107,   // 0..1 final VCA
    // Read-only display parameters, pushed from the audio thread:
    kParamMeterR      = 200,   // Hann RMS r, normalized [0,1] (floor -60 dBFS)
    kParamMeterG      = 201,   // loop gain g, normalized [0,1] (floor -60 dBFS)
};

static constexpr double kDriveMin = 1.0,   kDriveMax = 4.0,    kDriveDef = 1.0;
static constexpr double kTargetMin = 0.0,  kTargetMax = 1.0,   kTargetDef = 1.0;
static constexpr double kSteepMin = 1.0,   kSteepMax = 80.0,   kSteepDef = 40.0;
static constexpr double kSmoothMin = 1.0,  kSmoothMax = 1000.0, kSmoothDef = 200.0;
static constexpr double kTab1Min = 1.0,    kTab1Max = 200.0,   kTab1Def = 50.0;
static constexpr double kTab2Min = 1.0,    kTab2Max = 200.0,   kTab2Def = 20.0;
static constexpr double kOutMin = 0.0,     kOutMax = 1.0,      kOutDef = 1.0;
static constexpr double kMeterFloorDb = -60.0;

} // namespace Seam
```

- [ ] **Step 2: Write `version.h`**

Create `plugins/dslar/source/version.h`:
```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · DSLAR — Version and metadata
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "dslar.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM DSLAR – Di Scipio LAR feedforward homeostat (64Bit)"
#else
#define stringFileDescription   "SEAM DSLAR – Di Scipio LAR feedforward homeostat"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 3: Write `dslar_processor.h`**

Create `plugins/dslar/source/dslar_processor.h`:
```cpp
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "dslar_ids.h"
#include "dslar_dsp.h"

#include <atomic>

// FAUST REFERENCE (seam.discipio.lib): sds.lar — the feedforward mono LAR
// processor of Di Scipio's LAR.pd (the Larsen loop is acoustic, external).
// The DSP lives hand-written in the SDK-free dslar_dsp.h; this processor wires
// it to VST3 parameters and publishes the r/g meters (seam-ltm/CLAUDE.md: Faust
// is the spec, readable C++ is the deliverable).

namespace Seam {

class DSLARProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    DSLARProcessor();
    ~DSLARProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new DSLARProcessor();
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
    std::atomic<double> paramPower_{0.0};
    std::atomic<double> paramDrive_{kDriveDef};
    std::atomic<double> paramTarget_{kTargetDef};
    std::atomic<double> paramSteep_{kSteepDef};
    std::atomic<double> paramSmooth_{kSmoothDef};
    std::atomic<double> paramTab1_{kTab1Def};
    std::atomic<double> paramTab2_{kTab2Def};
    std::atomic<double> paramOutput_{kOutDef};

    dslar::Larsen larsen_;

    void applyParams();
    void readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** in, SampleType** out, int numSamples);
};

} // namespace Seam
```

- [ ] **Step 4: Write `dslar_processor.cpp`**

Create `plugins/dslar/source/dslar_processor.cpp`:
```cpp
#include "dslar_processor.h"
#include "dslar_ids.h"
#include "version.h"
#include "seam_meter.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

DSLARProcessor::DSLARProcessor() {}

static inline double denorm(double v, double lo, double hi) { return lo + v * (hi - lo); }
static inline double norm  (double p, double lo, double hi) { return (p - lo) / (hi - lo); }

tresult PLUGIN_API DSLARProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Input"),  SpeakerArr::kMono);
    addAudioOutput(STR16("Output"), SpeakerArr::kMono);

    auto add = [&](const TChar* name, ParamID id, double lo, double hi, double def,
                   int32 prec, const TChar* unit) {
        auto* p = new RangeParameter(name, id, unit, lo, hi, def, 0,
                                     ParameterInfo::kCanAutomate);
        p->setPrecision(prec);
        parameters.addParameter(p);
    };
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""), 0.0, 1.0, 0.0, 1,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
    add(STR16("Drive"),            kParamDrive,     kDriveMin,  kDriveMax,  kDriveDef,  2, STR16(""));
    add(STR16("Target"),           kParamTarget,    kTargetMin, kTargetMax, kTargetDef, 3, STR16(""));
    add(STR16("Steepness"),        kParamSteepness, kSteepMin,  kSteepMax,  kSteepDef,  1, STR16(""));
    add(STR16("Control smoothing"),kParamSmoothing, kSmoothMin, kSmoothMax, kSmoothDef, 0, STR16("ms"));
    add(STR16("Loop delay"),       kParamLoopDelay, kTab1Min,   kTab1Max,   kTab1Def,   1, STR16("ms"));
    add(STR16("Decorrelation"),    kParamDecorr,    kTab2Min,   kTab2Max,   kTab2Def,   1, STR16("ms"));
    add(STR16("Output"),           kParamOutput,    kOutMin,    kOutMax,    kOutDef,    3, STR16(""));

    parameters.addParameter(new RangeParameter(
        STR16("Meter R"), kParamMeterR, STR16(""), 0.0, 1.0, 0.0, 0,
        ParameterInfo::kIsReadOnly));
    parameters.addParameter(new RangeParameter(
        STR16("Meter G"), kParamMeterG, STR16(""), 0.0, 1.0, 0.0, 0,
        ParameterInfo::kIsReadOnly));

    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::terminate() { return SingleComponentEffect::terminate(); }

void DSLARProcessor::applyParams() {
    larsen_.setPower(paramPower_.load() >= 0.5);
    larsen_.setDrive(paramDrive_.load());
    larsen_.setTarget(paramTarget_.load());
    larsen_.setSteepness(paramSteep_.load());
    larsen_.setSmoothingMs(paramSmooth_.load());
    larsen_.setLoopDelayMs(paramTab1_.load());
    larsen_.setDecorrelationMs(paramTab2_.load());
    larsen_.setOutput(paramOutput_.load());
}

tresult PLUGIN_API DSLARProcessor::setActive(TBool state) {
    if (state) { larsen_.reset(); applyParams(); }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API DSLARProcessor::setupProcessing(ProcessSetup& setup) {
    larsen_.prepare(setup.sampleRate);
    applyParams();
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API DSLARProcessor::process(ProcessData& data) {
    readParameterChanges(data);
    applyParams();

    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0) {
        // Still publish the (idle) meters so the GUI stays live.
        if (auto* oc = data.outputParameterChanges) {
            int32 idx;
            if (auto* q = oc->addParameterData(kParamMeterR, idx)) { int32 o=0; q->addPoint(0, 0.0, o); }
            if (auto* q = oc->addParameterData(kParamMeterG, idx)) { int32 o=0; q->addPoint(0, 0.0, o); }
        }
        return kResultOk;
    }

    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32)
        processBlock<float>((float**)in, (float**)out, data.numSamples);
    else
        processBlock<double>((double**)in, (double**)out, data.numSamples);

    // Publish r/g meters (normalized, floor -60 dBFS).
    if (auto* oc = data.outputParameterChanges) {
        int32 idx;
        const double rN = seam::meter::lin2norm(larsen_.measuredRms(),  kMeterFloorDb);
        const double gN = seam::meter::lin2norm(larsen_.analysisGain(), kMeterFloorDb);
        if (auto* q = oc->addParameterData(kParamMeterR, idx)) { int32 o=0; q->addPoint(0, rN, o); }
        if (auto* q = oc->addParameterData(kParamMeterG, idx)) { int32 o=0; q->addPoint(0, gN, o); }
    }
    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API DSLARProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 1 || numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(ins[0]) != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API DSLARProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double vals[8];
    for (int i = 0; i < 8; ++i) if (!s.readDouble(vals[i])) return kResultFalse;
    paramPower_.store(std::clamp(vals[0], 0.0, 1.0));
    paramDrive_.store(std::clamp(vals[1], kDriveMin, kDriveMax));
    paramTarget_.store(std::clamp(vals[2], kTargetMin, kTargetMax));
    paramSteep_.store(std::clamp(vals[3], kSteepMin, kSteepMax));
    paramSmooth_.store(std::clamp(vals[4], kSmoothMin, kSmoothMax));
    paramTab1_.store(std::clamp(vals[5], kTab1Min, kTab1Max));
    paramTab2_.store(std::clamp(vals[6], kTab2Min, kTab2Max));
    paramOutput_.store(std::clamp(vals[7], kOutMin, kOutMax));

    auto setN = [&](ParamID id, double p, double lo, double hi) {
        if (auto* pr = parameters.getParameter(id)) pr->setNormalized(std::clamp(norm(p,lo,hi),0.0,1.0));
    };
    setN(kParamPower, paramPower_.load(), 0.0, 1.0);
    setN(kParamDrive, paramDrive_.load(), kDriveMin, kDriveMax);
    setN(kParamTarget, paramTarget_.load(), kTargetMin, kTargetMax);
    setN(kParamSteepness, paramSteep_.load(), kSteepMin, kSteepMax);
    setN(kParamSmoothing, paramSmooth_.load(), kSmoothMin, kSmoothMax);
    setN(kParamLoopDelay, paramTab1_.load(), kTab1Min, kTab1Max);
    setN(kParamDecorr, paramTab2_.load(), kTab2Min, kTab2Max);
    setN(kParamOutput, paramOutput_.load(), kOutMin, kOutMax);
    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double vals[8] = { paramPower_.load(), paramDrive_.load(), paramTarget_.load(),
                       paramSteep_.load(), paramSmooth_.load(), paramTab1_.load(),
                       paramTab2_.load(), paramOutput_.load() };
    for (int i = 0; i < 8; ++i) if (!s.writeDouble(vals[i])) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API DSLARProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "dslar.uidesc");
    return nullptr;
}

void DSLARProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (q->getParameterId()) {
            case kParamPower:     paramPower_.store(v >= 0.5 ? 1.0 : 0.0); break;
            case kParamDrive:     paramDrive_.store(denorm(v, kDriveMin, kDriveMax)); break;
            case kParamTarget:    paramTarget_.store(denorm(v, kTargetMin, kTargetMax)); break;
            case kParamSteepness: paramSteep_.store(denorm(v, kSteepMin, kSteepMax)); break;
            case kParamSmoothing: paramSmooth_.store(denorm(v, kSmoothMin, kSmoothMax)); break;
            case kParamLoopDelay: paramTab1_.store(denorm(v, kTab1Min, kTab1Max)); break;
            case kParamDecorr:    paramTab2_.store(denorm(v, kTab2Min, kTab2Max)); break;
            case kParamOutput:    paramOutput_.store(denorm(v, kOutMin, kOutMax)); break;
            default: break;
        }
    }
}

template <typename SampleType>
void DSLARProcessor::processBlock(SampleType** in, SampleType** out, int numSamples) {
    for (int s = 0; s < numSamples; ++s)
        out[0][s] = (SampleType) larsen_.process((double) in[0][s]);
}

template void DSLARProcessor::processBlock<float>(float**, float**, int);
template void DSLARProcessor::processBlock<double>(double**, double**, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::DSLARProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM DSLAR", Vst::kDistributable,
               "Fx", FULL_VERSION_STR, kVstVersionString,
               Seam::DSLARProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 5: Write the plugin `CMakeLists.txt`**

Create `plugins/dslar/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-dslar
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM DSLAR – Di Scipio LAR feedforward homeostatic processor"
)

set(dslar_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/dslar_ids.h
    source/dslar_dsp.h
    source/dslar_processor.cpp
    source/dslar_processor.h
    source/version.h
    resource/dslar.uidesc
)

set(target dslar)

smtg_add_vst3plugin(${target} ${dslar_sources})
smtg_target_configure_version_file(${target})

target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../_common)
target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/dslar.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.dslar"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

- [ ] **Step 6: Register the plugin in the root `CMakeLists.txt`**

In `CMakeLists.txt`, after the line `add_subdirectory(plugins/ltglide)`, add:
```cmake
add_subdirectory(plugins/dslar)
```

- [ ] **Step 7: Configure and build the plugin (and re-run the core tests through CMake)**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target dslar
cmake --build build --target dslar_dsp_test seam_meter_test
ctest --test-dir build -R "dslar_dsp_test|seam_meter_test" --output-on-failure
```
Expected: `dslar` links to a `.vst3` bundle; both tests pass. If the `resource/dslar.uidesc` file does not exist yet the resource copy step may warn — create the file in Task 9; if the build fails ONLY on the missing uidesc resource, proceed to Task 9 and rebuild there.

- [ ] **Step 8: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/source plugins/dslar/CMakeLists.txt CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(dslar): VST3 wiring — processor, ids, version, CMake (mono, meters)

Mono 1-in/1-out SingleComponentEffect around dslar::Larsen: 8 parameters
(Power, Drive, Target, Steepness, Control smoothing, Loop delay, Decorrelation,
Output) plus two kIsReadOnly meters (r, g) published via outputParameterChanges,
normalized by seam_meter at -60 dBFS. Registered in the root CMakeLists.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: GUI — `dslar.uidesc` (two columns + two meter bars)

**Files:**
- Create: `plugins/dslar/resource/dslar.uidesc`

**Interfaces:**
- Consumes: parameter tags 100–107 and meter tags 200–201 (Task 8).
- Produces: the loadable GUI; `createView` already references `"dslar.uidesc"`.

- [ ] **Step 1: Write `dslar.uidesc`**

Create `plugins/dslar/resource/dslar.uidesc`:
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
        <color name="TextDim" rgba="#888888ff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
        <color name="MeterFill" rgba="#c8a24aff"/>
    </colors>

    <template name="view" class="CViewContainer" origin="0, 0" size="460, 400"
              minSize="460, 400" maxSize="460, 400"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- Header -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM DSLAR" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextDim" text-alignment="center" title="Agostino Di Scipio · LAR" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextDim" text-alignment="center" title="feedforward homeostatic loop" transparent="true"/>

        <!-- Power -->
        <view class="CTextLabel" origin="40, 84" size="120, 18" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="Power" transparent="true"/>
        <view class="CCheckBox" origin="150, 82" size="60, 20" control-tag="Power"
              boxframe-color="TextDim" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>

        <!-- Column headers -->
        <view class="CTextLabel" origin="30, 112" size="200, 16" font="InfoFont"
              font-color="TextDim" text-alignment="center" title="— AUDIO PATH —" transparent="true"/>
        <view class="CTextLabel" origin="230, 112" size="200, 16" font="InfoFont"
              font-color="TextDim" text-alignment="center" title="— ANALYSIS —" transparent="true"/>

        <!-- Left column: Drive / Loop delay / Decorrelation -->
        <view class="CTextLabel" origin="30, 134" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Drive" transparent="true"/>
        <view class="CSlider" origin="30, 150" size="200, 18" control-tag="Drive"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="30, 170" size="200, 16" font="ValueFont" control-tag="Drive"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="2" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 192" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Loop delay (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 208" size="200, 18" control-tag="Loop delay"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="30, 228" size="200, 16" font="ValueFont" control-tag="Loop delay"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="1" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 250" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Decorrelation (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 266" size="200, 18" control-tag="Decorrelation"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="30, 286" size="200, 16" font="ValueFont" control-tag="Decorrelation"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="1" style-no-frame="true"/>

        <!-- Right column: Target / Steepness / Control smoothing / Output -->
        <view class="CTextLabel" origin="230, 134" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Target" transparent="true"/>
        <view class="CSlider" origin="230, 150" size="200, 18" control-tag="Target"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="230, 170" size="200, 16" font="ValueFont" control-tag="Target"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="3" style-no-frame="true"/>

        <view class="CTextLabel" origin="230, 192" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Steepness" transparent="true"/>
        <view class="CSlider" origin="230, 208" size="200, 18" control-tag="Steepness"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="230, 228" size="200, 16" font="ValueFont" control-tag="Steepness"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="1" style-no-frame="true"/>

        <view class="CTextLabel" origin="230, 250" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Control smoothing (ms)" transparent="true"/>
        <view class="CSlider" origin="230, 266" size="200, 18" control-tag="Control smoothing"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="230, 286" size="200, 16" font="ValueFont" control-tag="Control smoothing"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="230, 308" size="200, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Output" transparent="true"/>
        <view class="CSlider" origin="230, 324" size="200, 18" control-tag="Output"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="230, 344" size="200, 16" font="ValueFont" control-tag="Output"
              font-color="TextLight" text-alignment="center" transparent="true" value-precision="3" style-no-frame="true"/>

        <!-- Meters (read-only bars, MeterFill) -->
        <view class="CTextLabel" origin="30, 314" size="120, 14" font="InfoFont"
              font-color="TextDim" text-alignment="left" title="r  (Hann RMS)" transparent="true"/>
        <view class="CSlider" origin="30, 330" size="200, 12" control-tag="Meter R"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="MeterFill"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextLabel" origin="30, 350" size="120, 14" font="InfoFont"
              font-color="TextDim" text-alignment="left" title="g  (loop gain)" transparent="true"/>
        <view class="CSlider" origin="30, 366" size="200, 12" control-tag="Meter G"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="MeterFill"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
    </template>

    <control-tags>
        <control-tag name="Power"             tag="100"/>
        <control-tag name="Drive"             tag="101"/>
        <control-tag name="Target"            tag="102"/>
        <control-tag name="Steepness"         tag="103"/>
        <control-tag name="Control smoothing" tag="104"/>
        <control-tag name="Loop delay"        tag="105"/>
        <control-tag name="Decorrelation"     tag="106"/>
        <control-tag name="Output"            tag="107"/>
        <control-tag name="Meter R"           tag="200"/>
        <control-tag name="Meter G"           tag="201"/>
    </control-tags>
</vstgui-ui-description>
```

- [ ] **Step 2: Rebuild the plugin with the GUI**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build --target dslar
```
Expected: `dslar` builds and links; the `.vst3` bundle now contains `dslar.uidesc`.

- [ ] **Step 3: Verify the plugin validates and loads**

Run the SDK validator (bundled target `validator`) against the built plugin:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build --target validator
DSLAR=$(find build -name 'dslar.vst3' -type d | head -1)
"$(find build -name validator -type f -perm +111 | head -1)" "$DSLAR"
```
Expected: the validator reports the `SEAM DSLAR` class and finishes with `Result: all tests passed` (0 failures). If a host is preferred, load `dslar.vst3` in the DAW, confirm the two-column GUI opens with a moving `g` meter when audio passes and Power is on.

- [ ] **Step 4: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/resource/dslar.uidesc
git commit -m "$(cat <<'EOF'
feat(dslar): GUI — two-column layout + two read-only meter bars

House style (dark, Source Code Pro, cyan) plus a MeterFill color. AUDIO PATH
(Drive, Loop delay, Decorrelation) and ANALYSIS (Target, Steepness, Control
smoothing, Output) columns, Power on top, r and g meter bars at the bottom —
the suite's first graphical meter.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Study diary — Phase 4 (C++ port) section

**Files:**
- Modify: `plugins/dslar/doc/study/dslar-study.tex`
- Rebuild: `plugins/dslar/doc/study/dslar-study.pdf`

**Interfaces:**
- Produces: the Italian study diary records the C++ porting phase, consistent with the earlier phases.

- [ ] **Step 1: Add the Phase 4 section (Italian; one sentence per line)**

Insert before `\bibliographystyle{plain}` in `plugins/dslar/doc/study/dslar-study.tex`:
```latex
\section{Fase 4 — il plugin C++}\label{sec:fase4}
La specifica Faust congelata (\texttt{sds.lar}) diventa il contratto del porting a mano in C++, secondo la regola del progetto: Faust è la specifica, il C++ leggibile è il deliverable.
Il cuore \texttt{dslar\_dsp.h} è header-only e privo di SDK, quindi verificabile a sé con \texttt{doctest} contro gli stessi numeri d'oracolo della Fase 2--3.

Ogni oggetto Pd è portato come una piccola unità testata: i convertitori \texttt{powtodb}/\texttt{dbtorms} (\texttt{x\_acoustics.c}), il passa-alto a un polo \texttt{hip\textasciitilde} (\texttt{d\_filter.c}, topologia esatta con il $\pi$ letterale \num{3.14159}), la rampa control-rate \texttt{line} (\texttt{x\_time.c}, scala a gradini di \SI{20}{\milli\second}), e l'inviluppo RMS pesato Hann \texttt{env\textasciitilde} (\texttt{d\_ctl.c}).
L'\texttt{env} è reso come somma Hann su ring buffer calcolata alla cattura ogni \texttt{period} e tenuta tra le catture: stesso costo ammortizzato dell'overlap-add ma più leggibile, e float-identico perché la finestra Hann normalizzata somma a uno.
L'indipendenza dalla frequenza di campionamento vive nel deliverable: la finestra è dimensionata \texttt{round(fs*2048/44100)} in \texttt{prepare(fs)}, così il comportamento temporale resta invariante col variare di \(f_s\).

L'assemblaggio \texttt{Larsen} è una catena feedforward pura, senza retroazione interna, perché l'anello di Larsen è acustico ed esterno.
La verifica end-to-end mostra che una continua a \num{0.5} porta il guadagno d'anello \(g\) a \(0.5^{40}\) a \SI{44.1}{\kilo\hertz} e a \SI{96}{\kilo\hertz}, confermando insieme l'omeostato e l'indipendenza da \(f_s\).

Il plugin nasce col sistema di metering condiviso del suite (\texttt{\_common/seam\_meter.h}), di cui \texttt{dslar} è il primo consumatore.
Due barre in sola lettura mostrano \(r\) (RMS Hann, ingresso dell'omeostato) e \(g\) (guadagno d'anello all'uscita del \texttt{line}, fedele allo slider di Di Scipio), trasportate col meccanismo dei parametri read-only e normalizzate in dB a fondo scala \SI{-60}{\deci\bel FS}.
La resa ricca (una vista custom con scala in dB e la curva di trasferimento \(g=|r-1|^k\) col punto di lavoro vivo) è annotata come lavoro successivo.
```

- [ ] **Step 2: Rebuild the study PDF**

Run:
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/plugins/dslar/doc/study
make
```
Expected: `dslar-study.pdf` rebuilt with the new section (page count grows). If `make` reports a missing LaTeX tool, report which and stop — do not fake the PDF.

- [ ] **Step 3: Commit**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
git add plugins/dslar/doc/study/dslar-study.tex plugins/dslar/doc/study/dslar-study.pdf
git commit -m "$(cat <<'EOF'
docs(dslar): Phase 4 study diary — the C++ plugin port

Records the hand port of sds.lar (per-Pd-object tested units, ring-buffer Hann
env, SR-adaptive window), the feedforward assembly, the DC 0.5 -> g=0.5^40
end-to-end check at 44.1/96 kHz, and the shared seam_meter facility with the
two read-only meters (r, g). Custom meter + transfer curve noted as future.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes (already reconciled)

- **Spec coverage:** dslar design §Architecture → Tasks 2–9; §`dslar_dsp.h` components → Tasks 2–7; §Parameters → Task 8; §Metering (Phase A) → Task 1 (facility) + Task 8 (transport) + Task 9 (render); §GUI → Task 9; §Build & verification → Tasks 7 (core TDD), 8–9 (build + validator); §Deliverables → Task 1 (seam_meter.h), Tasks 2–9 (plugin), Task 10 (diary). Metering roadmap Phase A (`seam_meter.h` core: dB helpers + `LevelFollower`, Level + Gain) → Task 1; render decision (read-only slider now) → Task 9; deferred custom view/transfer curve → noted, not built.
- **Type consistency:** `Larsen` setters/getters used identically in Task 7 (definition) and Task 8 (calls: `setPower/setDrive/setTarget/setSteepness/setSmoothingMs/setLoopDelayMs/setDecorrelationMs/setOutput`, `measuredRms/analysisGain`). `seam::meter::lin2norm(x, floorDb)` defined in Task 1, called in Task 8. Parameter IDs 100–107 + 200–201 consistent across `dslar_ids.h` (Task 8) and `dslar.uidesc` control-tags (Task 9). `pd::powtodb/pd::dbtorms` defined Task 2, used by `HannRms` Task 5.
- **Placeholders:** none — every code step shows complete code; every run step shows the command and expected output.
- **Verified assumptions:** the ltburst/multipink patterns (factory, read-only publish, uidesc), the doctest harness, the tests/CMakeLists include-dir convention, the root CMake registration point, and the FUID next index were read from the repo before writing.
