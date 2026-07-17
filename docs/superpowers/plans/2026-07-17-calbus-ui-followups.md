# calbus UI Follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make strx readable and useful during a real calibration pass, give both emitters the controls the room needs, and stop the bus from vanishing on ARM machines.

**Architecture:** The DSP cores stay SDK-free and unit-tested; the bus stays a GUI-thread-only concern for strx, reaching the audio thread through two atomics read once per `process()` block. A pure digest function over a bus snapshot becomes the single source of truth that both the status line and the spectrum read, so they cannot disagree about which emitter is sounding.

**Tech Stack:** C++17, VST3 SDK (`SingleComponentEffect`, `VST3EditorDelegate`), VSTGUI (`CView`, `CVSTGUITimer`, `COptionMenu`), CMake + Xcode, doctest.

Design doc: `docs/superpowers/specs/2026-07-17-calbus-ui-followups-design.md`
Prior spec (the bus itself): `docs/superpowers/specs/2026-07-16-calbus-peer-aware-bus-design.md`

## Global Constraints

- Hand-written C++17 only. Faust is the spec, never a code generator (`CLAUDE.md`).
- VST3 SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`; pass `-DSEAM_VST3SDK_DIR=...`.
- Build generator must be Xcode: `-G Xcode`.
- **Build per-target, never `ALL_BUILD`**, and configure the test tree with `-DSEAM_BUILD_PLUGINS=OFF`. The last build tree to compile a plugin owns its `~/Library/Audio/Plug-Ins/VST3` symlink; a test tree that builds plugins hijacks the links the DAW loads.
- Code, comments and commit messages in English.
- **Do NOT use ThreadSanitizer on this project** — it models the fences and stays silent while the bus code provably corrupts data. Use behavioural probes.
- strx's audio thread must NEVER touch the calbus. The bus is read on the GUI thread only; it reaches the DSP through atomics.
- strx has ZERO automatable VST3 parameters, by design. Do not add any.
- `stoneId` range is 0–8, 0 = undeclared, rendered `STONE ?`. Declared by hand, never inferred.
- Calbus handles are opaque tokens: `SEAM_CALBUS_NO_HANDLE`, never a range comparison, never index with one.
- No error path may prevent a plugin from processing audio.
- Palette: `BgDark #292c2f`, `TextLight #fcfbfd`, `TextDim #888888`, `Structure #888888` (new), `SliderTrack #444444`, `SliderActive #4a9ec8`, `MeterFill #c8a24a`, `MeterInv #c04040`.
- Validator on every touched plugin: failure count must stay 0.

---

### Task 1: Universal `libseamcalbus`

**Files:**
- Modify: `plugins/_common/calbus/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing in code. The dylib gains `arm64`/`arm64e` slices.

**Why this is first:** it is one line, it is independent of everything else, and it is what lets the work leave this machine.

- [ ] **Step 1: Confirm the defect**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
lipo -archs build-release/VST3/Release/multipink.vst3/Contents/MacOS/multipink
lipo -archs ~/Library/Application\ Support/SEAM/libseamcalbus.dylib
```

Expected: the plugin prints `x86_64 arm64`; the dylib prints `x86_64` only. That mismatch means the bus cannot load on an ARM machine at all.

- [ ] **Step 2: Apply the SDK's own universal-binary function**

In `plugins/_common/calbus/CMakeLists.txt`, immediately after `target_include_directories(seam_calbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`, add:

```cmake
# The VST3 targets are built universal by the SDK (SMTG_BUILD_UNIVERSAL_BINARY,
# default ON -> XCODE_ATTRIBUTE_OSX_ARCHITECTURES "x86_64;arm64;arm64e").
# seam_calbus is a plain add_library, so it would otherwise inherit
# CMAKE_OSX_ARCHITECTURES, which the root CMakeLists pins to the host — leaving
# an x86_64-only dylib that an arm64 host cannot dlopen. The plugins would then
# load and play while strx read "calbus unavailable" and the bus silently did
# not exist.
#
# Call the SDK's own public function rather than hand-writing the property, so
# the dylib and the plugins cannot drift apart if the SDK's policy changes.
if(APPLE)
    smtg_target_setup_universal_binary(seam_calbus)
endif()
```

- [ ] **Step 3: Rebuild and verify all four slices**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target seam_calbus --config Release
lipo -archs build-release/lib/Release/libseamcalbus.dylib
lipo -archs ~/Library/Application\ Support/SEAM/libseamcalbus.dylib
```

Expected: both print `x86_64 arm64 arm64e` (or at minimum `x86_64 arm64`). If the POST_BUILD copy did not refresh the installed one, report it — the install rule is what feeds the DAW.

If `smtg_target_setup_universal_binary` is not defined at that point in the configure, report it rather than working around it: it means the SDK module has not been included yet and the fix belongs elsewhere in the CMake ordering.

- [ ] **Step 4: Confirm the bus still works**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm/build-test && ctest -R calbus -C Release --output-on-failure
```

Expected: 3/3 pass. (If `build-test` does not exist: `cmake -B build-test -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DSEAM_BUILD_TESTS=ON -DSEAM_BUILD_PLUGINS=OFF`.)

- [ ] **Step 5: Commit**

```bash
git add plugins/_common/calbus/CMakeLists.txt
git commit -m "build(calbus): build libseamcalbus as a universal binary

The plugins are x86_64+arm64 (the SDK builds them universal) while our own
add_library inherited CMAKE_OSX_ARCHITECTURES, pinned to the host. On any ARM
machine the plugins would load natively and the dylib would not load at all —
strx would read 'calbus unavailable' and the bus would silently not exist.
Uses the SDK's own smtg_target_setup_universal_binary so the two cannot drift."
```

---

### Task 2: `Welch` max-hold and runtime τ

**Files:**
- Modify: `plugins/_common/seam_fft.h`
- Test: `tests/seam_fft_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces, on `seam::fft::Welch` — used by Task 3:
  - `void setEmaTau(double tau)` — recomputes the EMA coefficient in place; does not touch the ring, the hold, or the window.
  - `void resetHold()` — clears the max-hold to silence.
  - `const float* holdDb() const` — `numBins()` floats, dB, floored at -120.
  - Existing `magnitudeDb()`, `reset()`, `prepare()`, `push()`, `hasNewFrame()`, `numBins()` keep their meaning. `reset()` now also clears the hold.

- [ ] **Step 1: Write the failing tests**

Append to `tests/seam_fft_test.cpp`:

```cpp
// A full-scale sine at an exact bin centre, pushed long enough for the EMA to
// settle, then silence. The EMA decays toward the floor; the hold must not.
static void pushSine(seam::fft::Welch& w, double fs, double freq, int nSamples) {
    for (int i = 0; i < nSamples; ++i)
        w.push(float(std::sin(2.0 * M_PI * freq * double(i) / fs)));
}
static void pushSilence(seam::fft::Welch& w, int nSamples) {
    for (int i = 0; i < nSamples; ++i) w.push(0.0f);
}

TEST_CASE("Welch max-hold keeps the peak after the tone stops") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 0.05, fs);              // short tau so the EMA decays fast

    const int    bin  = 64;
    const double freq = double(bin) * fs / double(n);   // exact bin centre
    pushSine(w, fs, freq, n * 20);

    const float peakEma  = w.magnitudeDb()[bin];
    const float peakHold = w.holdDb()[bin];
    CHECK(peakEma  > -6.0f);             // full-scale sine reads ~0 dBFS
    CHECK(peakHold > -6.0f);

    pushSilence(w, n * 40);
    CHECK(w.magnitudeDb()[bin] < -60.0f);   // EMA decayed to the floor
    CHECK(w.holdDb()[bin] == peakHold);     // hold did NOT move
}

TEST_CASE("Welch resetHold clears the hold and leaves the EMA alone") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 2.0, fs);

    const int bin = 64;
    pushSine(w, fs, double(bin) * fs / double(n), n * 20);
    CHECK(w.holdDb()[bin] > -6.0f);

    const float emaBefore = w.magnitudeDb()[bin];
    w.resetHold();
    CHECK(w.holdDb()[bin] == -120.0f);          // hold cleared
    CHECK(w.magnitudeDb()[bin] == emaBefore);   // EMA untouched
}

TEST_CASE("Welch setEmaTau changes the decay rate and nothing else") {
    const double fs = 48000.0;
    const int    n  = 1024;
    const int    bin = 64;
    const double freq = double(bin) * fs / double(n);

    // Same excitation, same silence, two different taus -> the SHORT tau must
    // have decayed further. This is the property glide mode depends on.
    seam::fft::Welch slow, fast;
    slow.prepare(n, 2.0, fs);
    fast.prepare(n, 2.0, fs);
    fast.setEmaTau(0.1);                        // switch at runtime

    pushSine(slow, fs, freq, n * 20);
    pushSine(fast, fs, freq, n * 20);
    pushSilence(slow, n * 10);
    pushSilence(fast, n * 10);

    CHECK(fast.magnitudeDb()[bin] < slow.magnitudeDb()[bin] - 6.0f);

    // The hold is independent of tau: both saw the same peak.
    CHECK(slow.holdDb()[bin] == doctest::Approx(fast.holdDb()[bin]).epsilon(0.01));
}

TEST_CASE("Welch reset clears the hold too") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 2.0, fs);
    pushSine(w, fs, double(64) * fs / double(n), n * 20);
    CHECK(w.holdDb()[64] > -6.0f);
    w.reset();
    CHECK(w.holdDb()[64] == -120.0f);
}
```

Make sure `<cmath>` is included in the test file (for `std::sin` and `M_PI`); add it if it is not already there.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target seam_fft_test --config Release
```

Expected: FAIL to compile — `no member named 'holdDb' in 'seam::fft::Welch'`.

- [ ] **Step 3: Implement**

In `plugins/_common/seam_fft.h`, class `Welch`:

In `prepare()`, immediately after `magLin_.assign(bins_, 0.0f);`, add:

```cpp
        holdLin_.assign(bins_, 0.0f);
        holdDb_.assign(bins_, -120.0f);
```

Add these public methods after `prepare()`:

```cpp
    // Change the EMA time constant in place. Used to switch between the slow
    // average that reads pink noise well (tau = 2 s) and the fast one a swept
    // tone needs (tau = 0.1 s): a sweep presents ONE frequency at a time, so a
    // slow average sees a moving peak and smears it.
    void setEmaTau(double tau) {
        const double dt = double(hop_) / fs_;
        ema_ = (tau > 0.0) ? std::exp(-dt / tau) : 0.0;
    }

    // Clear the max-hold without disturbing the EMA, the ring, or the window.
    void resetHold() {
        std::fill(holdLin_.begin(), holdLin_.end(), 0.0f);
        std::fill(holdDb_.begin(), holdDb_.end(), -120.0f);
    }

    // Per-bin maximum power ever seen since the last resetHold(), in dB.
    // Always computed — it costs one compare per bin per frame (~23 frames/s
    // at hop 2048 / 48 kHz), so there is no mode to get wrong.
    const float* holdDb() const { return holdDb_.data(); }
```

In `reset()`, after the existing `std::fill` calls, add:

```cpp
        std::fill(holdLin_.begin(), holdLin_.end(), 0.0f);
        std::fill(holdDb_.begin(), holdDb_.end(), -120.0f);
```

In `runFrame()`, inside the `for (int k = 0; k < bins_; ++k)` loop, immediately after `const double p = (re*re + im*im) * winNorm_;`, add:

```cpp
            if (p > holdLin_[k]) {                      // max-hold on raw power
                holdLin_[k] = float(p);
                const double hdb = 10.0 * std::log10(p > 1e-12 ? p : 1e-12);
                holdDb_[k] = float(hdb < -120.0 ? -120.0 : hdb);
            }
```

Add to the member list, beside `magLin_, magDb_`:

```cpp
    std::vector<float> holdLin_, holdDb_;
```

(Extend the existing `std::vector<float> win_, ring_, scratch_, magLin_, magDb_;` declaration or add a second one — match the file's style.)

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target seam_fft_test --config Release
./build-test/tests/Release/seam_fft_test
```

Expected: all cases pass, including the pre-existing ones.

- [ ] **Step 5: Verify the tests by mutation**

Each new test must be able to fail. Break the code deliberately, confirm RED, revert, confirm GREEN. Report all four results.

1. Change the hold to `holdLin_[k] = float(p);` unconditionally (no `if`) → "keeps the peak after the tone stops" must go RED.
2. Make `resetHold()` a no-op → "resetHold clears the hold" must go RED.
3. Make `setEmaTau()` a no-op → "setEmaTau changes the decay rate" must go RED.
4. Remove the hold clear from `reset()` → "reset clears the hold too" must go RED.

A test that cannot fail is worse than no test. This project has already shipped three tests that passed for the wrong reason.

- [ ] **Step 6: Commit**

```bash
git add plugins/_common/seam_fft.h tests/seam_fft_test.cpp
git commit -m "feat(fft): Welch max-hold and runtime EMA tau

A swept tone presents one frequency at a time, so the 2 s average that reads
pink noise well smears a sweep into a moving bump. Max-hold accumulates the
response as the sweep descends; it costs one compare per bin per frame, so it
is computed always and the caller decides whether to draw it. setEmaTau lets
the caller switch to a fast average when a sweep is what is sounding."
```

---

### Task 3: `Analyzer` — hold arrays and glide mode

**Files:**
- Modify: `plugins/strx/source/strx_dsp.h`
- Test: `tests/strx_dsp_test.cpp`

**Interfaces:**
- Consumes: Task 2's `Welch::setEmaTau`, `Welch::resetHold`, `Welch::holdDb`.
- Produces, used by Tasks 4 and 6:
  - `AnalysisFrame::holdM[kNumBins]`, `AnalysisFrame::holdS[kNumBins]` — dB, floored at -120.
  - `void Analyzer::setGlideMode(bool on)` — idempotent; switches both Welch taus between `kPinkTauSec` (2.0) and `kGlideTauSec` (0.1).
  - `void Analyzer::resetHold()` — clears both Welch holds.
  - `bool Analyzer::glideMode() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/strx_dsp_test.cpp` (match the namespace qualification the file's existing cases use):

```cpp
TEST_CASE("Analyzer publishes a max-hold that survives silence") {
    Seam::strx::Analyzer a;
    a.prepare(48000.0);

    // A loud burst, then silence. The live spectrum decays; the hold does not.
    std::vector<float> loud(8192), quiet(8192, 0.0f);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    a.analyze(loud.data(), loud.data(), int(loud.size()));
    const Seam::strx::AnalysisFrame& f1 = a.frame();
    int peakBin = 0;
    for (int k = 1; k < f1.numBins; ++k)
        if (f1.holdM[k] > f1.holdM[peakBin]) peakBin = k;
    const float peakHold = f1.holdM[peakBin];
    CHECK(peakHold > -40.0f);

    for (int i = 0; i < 12; ++i) a.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    const Seam::strx::AnalysisFrame& f2 = a.frame();
    CHECK(f2.specM[peakBin] < peakHold - 6.0f);   // live decayed
    CHECK(f2.holdM[peakBin] == peakHold);         // hold held
}

TEST_CASE("Analyzer resetHold clears the published hold") {
    Seam::strx::Analyzer a;
    a.prepare(48000.0);
    std::vector<float> loud(8192);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    a.analyze(loud.data(), loud.data(), int(loud.size()));
    int peakBin = 0;
    for (int k = 1; k < a.frame().numBins; ++k)
        if (a.frame().holdM[k] > a.frame().holdM[peakBin]) peakBin = k;
    CHECK(a.frame().holdM[peakBin] > -40.0f);

    a.resetHold();
    std::vector<float> quiet(8192, 0.0f);
    a.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    CHECK(a.frame().holdM[peakBin] == -120.0f);
}

TEST_CASE("Analyzer glide mode is idempotent and switches the live decay") {
    // Same excitation and same silence in the two modes: glide's fast tau must
    // decay further. This is the property the swept display depends on.
    std::vector<float> loud(8192), quiet(8192, 0.0f);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    Seam::strx::Analyzer pink, glide;
    pink.prepare(48000.0);
    glide.prepare(48000.0);
    CHECK_FALSE(pink.glideMode());
    glide.setGlideMode(true);
    glide.setGlideMode(true);          // idempotent: must not reset anything
    CHECK(glide.glideMode());

    pink.analyze(loud.data(), loud.data(), int(loud.size()));
    glide.analyze(loud.data(), loud.data(), int(loud.size()));
    int peakBin = 0;
    for (int k = 1; k < pink.frame().numBins; ++k)
        if (pink.frame().holdM[k] > pink.frame().holdM[peakBin]) peakBin = k;

    for (int i = 0; i < 4; ++i) {
        pink.analyze(quiet.data(), quiet.data(), int(quiet.size()));
        glide.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    }
    CHECK(glide.frame().specM[peakBin] < pink.frame().specM[peakBin] - 6.0f);

    glide.setGlideMode(false);
    CHECK_FALSE(glide.glideMode());
}
```

Ensure `<cmath>` and `<vector>` are included in the test file.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target strx_dsp_test --config Release
```

Expected: FAIL to compile — `no member named 'holdM' in 'Seam::strx::AnalysisFrame'`.

- [ ] **Step 3: Implement**

In `plugins/strx/source/strx_dsp.h`, in `struct AnalysisFrame`, after `float specS[kNumBins] = {};`:

```cpp
    // Per-bin max since the last resetHold(). Meaningful when a swept emitter
    // is sounding: each bin is excited once as the sweep passes, so holding the
    // maximum draws the response curve over one pass. Always published; the
    // view decides whether to draw it.
    float holdM[kNumBins] = {};  // dB
    float holdS[kNumBins] = {};  // dB
```

In `class Analyzer`, add these constants next to `kFftSize` in the private section:

```cpp
    static constexpr double kPinkTauSec  = 2.0;   // slow average: reads pink noise smoothly
    static constexpr double kGlideTauSec = 0.1;   // fast average: tracks a sweep's moving tone
```

Add these public methods after `reset()`:

```cpp
    // Switch the live average between the pink and glide time constants.
    // Idempotent: repeated calls with the same value do nothing, so the
    // processor can call this every block without disturbing the analysis.
    void setGlideMode(bool on) {
        if (on == glide_) return;
        glide_ = on;
        const double tau = on ? kGlideTauSec : kPinkTauSec;
        welchM_.setEmaTau(tau);
        welchS_.setEmaTau(tau);
    }
    bool glideMode() const { return glide_; }

    // Clear the max-hold. Driven by the calibration bus: a new pass starts a
    // new measurement, so the previous pass's curve must not linger.
    void resetHold() { welchM_.resetHold(); welchS_.resetHold(); }
```

Add the member beside `fs_`:

```cpp
    bool glide_ = false;
```

In `prepare()`, replace the two Welch prepare lines so the tau follows the mode rather than being hardcoded:

```cpp
        const double tau = glide_ ? kGlideTauSec : kPinkTauSec;
        welchM_.prepare(kFftSize, tau, fs_);
        welchS_.prepare(kFftSize, tau, fs_);
```

In `analyze()`, extend the spectra copy loop:

```cpp
        fr.numBins = welchM_.numBins();
        const float* mM = welchM_.magnitudeDb();
        const float* mS = welchS_.magnitudeDb();
        const float* hM = welchM_.holdDb();
        const float* hS = welchS_.holdDb();
        for (int k = 0; k < fr.numBins; ++k) {
            fr.specM[k] = mM[k]; fr.specS[k] = mS[k];
            fr.holdM[k] = hM[k]; fr.holdS[k] = hS[k];
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target strx_dsp_test --config Release
./build-test/tests/Release/strx_dsp_test
```

Expected: all cases pass, including the pre-existing ones.

- [ ] **Step 5: Verify by mutation**

1. Make `resetHold()` a no-op → "resetHold clears the published hold" must go RED.
2. Make `setGlideMode` always use `kPinkTauSec` → "glide mode ... switches the live decay" must go RED.
3. Stop copying `hM`/`hS` into the frame → "publishes a max-hold" must go RED.

Revert each and confirm GREEN. Report all three.

- [ ] **Step 6: Commit**

```bash
git add plugins/strx/source/strx_dsp.h tests/strx_dsp_test.cpp
git commit -m "feat(strx): publish max-hold spectra and a glide analysis mode

AnalysisFrame carries holdM/holdS alongside the live spectra (16 -> 33 KB per
frame, ~1 MB/s at 30 Hz). setGlideMode switches the live average between 2 s
(pink) and 100 ms (sweep) and is idempotent, so the processor can call it every
block. resetHold is what a new pass will drive."
```

---

### Task 4: Bus digest and the cached watch

**Files:**
- Create: `plugins/strx/source/strx_calbus_digest.h`
- Create: `plugins/strx/source/strx_calbus_watch.h`
- Modify: `plugins/strx/source/strx_status.h` (use the digest instead of its own walk)
- Modify: `plugins/strx/source/strx_processor.h`, `plugins/strx/source/strx_processor.cpp`
- Test: `tests/strx_calbus_digest_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SeamCalbusRecord`, `SEAM_CALBUS_MAX_SLOTS`, `kSeamCalbusPink`, `kSeamCalbusGlide` (from `plugins/_common/calbus/seam_calbus.h`); `Seam::CalbusClient` (from `seam_calbus_client.h`); Task 3's `Analyzer::setGlideMode`/`resetHold`.
- Produces, used by Tasks 5 and 6:
  - `struct Seam::strx::CalbusDigest { bool available; int32_t count; int32_t firstActive; int32_t activeCount; int32_t idleCount; bool glide; uint64_t passCounter; };`
  - `CalbusDigest Seam::strx::digest(const SeamCalbusRecord* recs, int32_t n, bool available);` — pure, SDK-free, testable.
  - `class Seam::strx::CalbusWatch` with `const CalbusDigest& poll();` and `const SeamCalbusRecord* records() const;`
  - On `StrxProcessor`: `CalbusWatch& calbusWatch();`

- [ ] **Step 1: Write the failing test**

Create `tests/strx_calbus_digest_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_calbus_digest.h"

using Seam::strx::digest;

static SeamCalbusRecord pink(uint32_t stone, bool active, int32_t slotStart) {
    SeamCalbusRecord r{};
    r.kind = kSeamCalbusPink;
    r.stoneId = stone;
    r.active = active ? 1u : 0u;
    r.levelDb = -23.0;
    r.u.pink.slotStart = slotStart;
    r.u.pink.slotCount = 4;
    return r;
}
static SeamCalbusRecord glide(uint32_t stone, bool active, uint64_t pass) {
    SeamCalbusRecord r{};
    r.kind = kSeamCalbusGlide;
    r.stoneId = stone;
    r.active = active ? 1u : 0u;
    r.levelDb = -20.0;
    r.u.glide.passCounter = pass;
    r.u.glide.passStartSample = 12345;
    return r;
}

TEST_CASE("digest of an unavailable bus reports nothing") {
    const Seam::strx::CalbusDigest d = digest(nullptr, 0, /*available*/false);
    CHECK_FALSE(d.available);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest with no records") {
    SeamCalbusRecord recs[1];
    const Seam::strx::CalbusDigest d = digest(recs, 0, true);
    CHECK(d.available);
    CHECK(d.count == 0);
    CHECK(d.firstActive == -1);
    CHECK(d.idleCount == 0);
}

TEST_CASE("digest counts idle records and finds none sounding") {
    SeamCalbusRecord recs[3] = { pink(1,false,0), pink(2,false,4), pink(3,false,8) };
    const Seam::strx::CalbusDigest d = digest(recs, 3, true);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK(d.idleCount == 3);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest names the first active record and counts the rest") {
    SeamCalbusRecord recs[4] = { pink(1,false,0), pink(2,true,4), pink(3,true,8), pink(4,false,12) };
    const Seam::strx::CalbusDigest d = digest(recs, 4, true);
    CHECK(d.firstActive == 1);      // index of STONE 2
    CHECK(d.activeCount == 2);      // the collision the status line must flag
    CHECK(d.idleCount == 2);
    CHECK_FALSE(d.glide);           // pink is sounding, not a sweep
}

TEST_CASE("digest reports glide and its pass counter only when glide is the first active") {
    SeamCalbusRecord recs[2] = { pink(1,false,0), glide(2,true,7) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 1);
    CHECK(d.glide);
    CHECK(d.passCounter == 7u);
}

TEST_CASE("an idle glide does not put the spectrum in glide mode") {
    // The spectrum follows the SOUNDING emitter. A registered-but-idle ltglide
    // must not switch the analysis away from the pink average.
    SeamCalbusRecord recs[2] = { pink(1,true,0), glide(2,false,7) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest follows the same record the status line names") {
    // Both views read this one digest, so they cannot disagree about which
    // emitter is "the" emitter when the by-method rule is violated.
    SeamCalbusRecord recs[2] = { glide(1,true,3), pink(2,true,0) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK(d.glide);
    CHECK(d.passCounter == 3u);
    CHECK(d.activeCount == 2);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target strx_calbus_digest_test --config Release
```

Expected: FAIL — the target does not exist yet.

- [ ] **Step 3: Write the digest**

Create `plugins/strx/source/strx_calbus_digest.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — one reading of a calibration-bus snapshot.
//
// Both the status line and the spectrum need to know which emitter is
// sounding. Two independent walks of the snapshot would be duplicated logic
// that can drift, and — worse — could name different emitters at the same
// instant when more than one is active, so the line would say one thing while
// the spectrum measured another. This is the single reading both use.
//
// Pure and SDK-free, so it can be unit-tested without a host or the dylib.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus.h"

#include <cstdint>

namespace Seam { namespace strx {

struct CalbusDigest {
    bool     available   = false;  // the dylib loaded and the version matched
    int32_t  count       = 0;      // records in the snapshot
    int32_t  firstActive = -1;     // index of the sounding emitter, or -1
    int32_t  activeCount = 0;      // how many are sounding (>1 = method violated)
    int32_t  idleCount   = 0;      // registered but silent
    bool     glide       = false;  // the FIRST ACTIVE record is a sweep
    uint64_t passCounter = 0;      // that sweep's pass number (0 when not glide)
};

// `recs` may be null when count is 0. `available` comes from the client.
inline CalbusDigest digest(const SeamCalbusRecord* recs, int32_t n, bool available) {
    CalbusDigest d;
    d.available = available;
    if (!available || !recs || n <= 0) return d;
    d.count = n;
    for (int32_t i = 0; i < n; ++i) {
        if (!recs[i].active) { ++d.idleCount; continue; }
        if (d.firstActive < 0) d.firstActive = i;
        ++d.activeCount;
    }
    if (d.firstActive >= 0 && recs[d.firstActive].kind == (uint32_t)kSeamCalbusGlide) {
        d.glide = true;
        d.passCounter = recs[d.firstActive].u.glide.passCounter;
    }
    return d;
}

}} // namespace Seam::strx
```

- [ ] **Step 4: Add the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(strx_calbus_digest_test
    strx_calbus_digest_test.cpp
)
target_include_directories(strx_calbus_digest_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_compile_features(strx_calbus_digest_test PRIVATE cxx_std_17)
add_test(NAME strx_calbus_digest_test COMMAND strx_calbus_digest_test)
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-test -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DSEAM_BUILD_TESTS=ON -DSEAM_BUILD_PLUGINS=OFF
cmake --build build-test --target strx_calbus_digest_test --config Release
./build-test/tests/Release/strx_calbus_digest_test
```

Expected: 7 cases pass.

- [ ] **Step 6: Write the watch**

Create `plugins/strx/source/strx_calbus_watch.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — the GUI thread's cached view of the calibration bus.
//
// GUI THREAD ONLY. strx's audio thread must never touch the bus (calbus
// Spec 2's contract); what the DSP needs reaches it through the processor's
// atomics, which this watch writes.
//
// Two views want the bus now: the status line at ~10 Hz and the spectrum at
// ~30 Hz. Two independent snapshots would show different instants and could
// disagree about which emitter is "the" emitter. This watch snapshots at most
// once per kMinIntervalMs and hands both views the SAME digest — the same
// lesson reference_lockfree_spsc_triplebuffer records for the analysis frame,
// applied to the bus.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus_client.h"
#include "strx_calbus_digest.h"

#include <atomic>
#include <chrono>

namespace Seam { namespace strx {

class CalbusWatch {
public:
    static constexpr int kMinIntervalMs = 80;   // finer than the 10 Hz status line

    // `glideOut` and `holdEpochOut` are the processor's atomics, read by the
    // audio thread once per process() block.
    CalbusWatch(std::atomic<bool>& glideOut, std::atomic<uint32_t>& holdEpochOut)
        : glide_(glideOut), holdEpoch_(holdEpochOut) {}

    // GUI thread. Re-snapshots at most every kMinIntervalMs; otherwise returns
    // the cached digest, so two views polling at different rates see one state.
    const CalbusDigest& poll() {
        const auto now = std::chrono::steady_clock::now();
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();
        if (primed_ && ms < kMinIntervalMs) return digest_;
        last_   = now;
        primed_ = true;

        auto& client = CalbusClient::instance();
        const bool avail = client.available();
        const int32_t n = avail ? client.snapshot(recs_, SEAM_CALBUS_MAX_SLOTS) : 0;
        digest_ = digest(recs_, n, avail);

        // Drive the DSP. setGlideMode is idempotent, so publishing every poll
        // costs nothing; the hold epoch only moves when a new pass starts.
        glide_.store(digest_.glide, std::memory_order_relaxed);
        if (digest_.glide && digest_.passCounter != lastPass_) {
            lastPass_ = digest_.passCounter;
            holdEpoch_.fetch_add(1, std::memory_order_relaxed);
        }
        return digest_;
    }

    const SeamCalbusRecord* records() const { return recs_; }

private:
    std::atomic<bool>&     glide_;
    std::atomic<uint32_t>& holdEpoch_;
    SeamCalbusRecord       recs_[SEAM_CALBUS_MAX_SLOTS] = {};
    CalbusDigest           digest_;
    uint64_t               lastPass_ = 0;
    bool                   primed_   = false;
    std::chrono::steady_clock::time_point last_{};
};

}} // namespace Seam::strx
```

- [ ] **Step 7: Wire the processor**

In `plugins/strx/source/strx_processor.h`, add the include beside the others:

```cpp
#include "strx_calbus_watch.h"
```

Add to the private members:

```cpp
    // GUI -> DSP. Written by the CalbusWatch on the GUI thread, read by
    // process() once per block. The bus itself is never touched from audio.
    std::atomic<bool>     specGlide_{false};
    std::atomic<uint32_t> holdEpoch_{0};
    uint32_t              lastHoldEpoch_ = 0;
    Seam::strx::CalbusWatch calbusWatch_{specGlide_, holdEpoch_};
```

Add to the public section:

```cpp
    // GUI thread only. The one cached bus reading both custom views share.
    Seam::strx::CalbusWatch& calbusWatch() { return calbusWatch_; }
```

Make sure `<atomic>` is included.

In `plugins/strx/source/strx_processor.cpp`, in `process()`, immediately before the call that feeds the analyzer, add:

```cpp
    // Apply what the GUI read from the bus. Once per block, never per sample,
    // and never by touching the bus from this thread.
    analyzer_.setGlideMode(specGlide_.load(std::memory_order_relaxed));
    const uint32_t he = holdEpoch_.load(std::memory_order_relaxed);
    if (he != lastHoldEpoch_) { lastHoldEpoch_ = he; analyzer_.resetHold(); }
```

(Use the analyzer member's actual name as it appears in the file.)

- [ ] **Step 8: Make the status line use the digest**

In `plugins/strx/source/strx_status.h`, replace the include of the client with the watch, take a `StrxProcessor*` in the constructor exactly as the sibling views do, and rewrite `compose()` to use `processor_->calbusWatch().poll()` and `records()` instead of its own `CalbusClient::instance()` call and its own walk. The rendered strings must not change:

- not available → `calbus unavailable`
- `count == 0` → `calbus: no emitter`
- `firstActive < 0` → `calbus: %d idle, none sounding` with `idleCount`
- otherwise `describe(records()[firstActive])`, plus ` · +%d more` with `activeCount - 1` when `activeCount > 1`

Keep `describe()` and `appendStone()` exactly as they are — `passStartSample < 0` still renders `no host clock` and nothing else.

Update the `createCustomView` branch in `strx_processor.cpp` to pass `this` to `StrxStatusLine`, matching the neighbouring branches' idiom.

- [ ] **Step 9: Build and run the validator**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target strx --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/strx.vst3
cd build-test && ctest -C Release --output-on-failure
```

Expected: validator 0 failures; all tests pass.

- [ ] **Step 10: Commit**

```bash
git add plugins/strx/source tests/strx_calbus_digest_test.cpp tests/CMakeLists.txt
git commit -m "feat(strx): one cached bus reading for both views

The status line and the spectrum both need to know which emitter is sounding.
Two independent snapshots would show different instants and could name
different emitters when more than one is active — the line saying one thing
while the spectrum measured another. digest() is the single pure reading, unit
tested without a host; CalbusWatch caches it and drives the two atomics the
audio thread reads. The bus stays GUI-thread-only."
```

---

### Task 5: strx layout and visual language

**Files:**
- Modify: `plugins/strx/resource/strx.uidesc`
- Modify: `plugins/strx/source/strx_goniometer.h`
- Modify: `plugins/strx/source/strx_meters.h`
- Modify: `plugins/strx/source/strx_spectrum.h`
- Modify: `plugins/strx/source/strx_processor.cpp` (view sizes in `createCustomView`)

**Interfaces:**
- Consumes: nothing new.
- Produces: the geometry Task 6 draws into — the spectrum view is `560 × 240` at `(20, 416)`.

- [ ] **Step 1: Add the `Structure` colour**

In `plugins/strx/resource/strx.uidesc`, inside `<colors>`, after the `TextDim` entry:

```xml
        <color name="Structure" rgba="#888888ff"/>
```

`TextDim` stays: other plugins use it as a text colour, and retiring it is a question for the future guideline, not this pilot. `Structure` exists because after this change nothing in strx draws *text* with `#888888` — it draws circles, grids and axes, and a colour named for a role it no longer has is how the next reader gets misled.

- [ ] **Step 2: Rewrite the template geometry**

Replace the `editor` template's opening tag and all its views in `plugins/strx/resource/strx.uidesc` with:

```xml
    <template name="editor" class="CViewContainer" origin="0, 0" size="600, 770"
              minSize="600, 770" maxSize="600, 770"
              background-color="BgDark" background-color-draw-style="filled">

        <view class="CTextLabel" origin="0, 14" size="600, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM STRX" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="600, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="STEREO M/S Analyser" transparent="true"/>

        <!-- Top row, two columns. Each view splits internally into three bands:
             labels 0-18, plot 18-278, values 278-300. The plot band is 260 px in
             both and at the same absolute y (88-348), so the circle and the bars
             are the same height and align without anyone imposing it. -->
        <view class="CView" origin="20, 70" size="260, 300" custom-view-name="StrxGoniometer"
              tooltip="Decaying M/S scatter (Lissajous) with Angle/Panorama readout"/>
        <view class="CView" origin="310, 70" size="270, 300" custom-view-name="StrxMeters"
              tooltip="In L, In R, M, S levels (dBFS) and Width (correlation-tinted)"/>

        <!-- Calibration-bus status line. 560 px at 11 px Source Code Pro is
             ~84 characters; the longest record
             ("ltglide · STONE ? · pass 7 · 20000→20 Hz · no host clock", 56)
             fits with room for the "· +N more" collision flag. At the old
             300 px it was cut at 45. -->
        <view class="CView" origin="20, 380" size="560, 26" custom-view-name="StrxStatus"/>

        <!-- Spectrum, spanning both columns: 560 px nearly doubles the
             horizontal resolution of the 20 Hz-20 kHz log axis. -->
        <view class="CView" origin="20, 416" size="560, 240" custom-view-name="StrxSpectrum"
              tooltip="M/S spectra: Welch average for pink, max-hold + live for a sweep"/>

        <view class="CView" origin="180, 670" size="240, 77" bitmap="logo"/>
    </template>
```

- [ ] **Step 3: Update the view sizes in `createCustomView`**

In `plugins/strx/source/strx_processor.cpp`, the `CRect` passed to each view must match the uidesc:

- `StrxGoniometer` → `VSTGUI::CRect(0, 0, 260, 300)`
- `StrxMeters` → `VSTGUI::CRect(0, 0, 270, 300)`
- `StrxSpectrum` → `VSTGUI::CRect(0, 0, 560, 240)`
- `StrxStatusLine` → `VSTGUI::CRect(0, 0, 560, 26)`

In each branch, fetch the new colour beside the existing ones:

```cpp
        VSTGUI::CColor structure = VSTGUI::kGreyCColor;
        if (description) description->getColor("Structure", structure);
```

and pass it where the views currently receive their `TextDim`-derived `label` colour. Every string in every view now uses the `TextLight`-derived colour; `structure` is only for the circle, the grid, the axes and the bar tracks. Match each branch's existing idiom for fetching colours rather than inventing a new one.

- [ ] **Step 4: Remove the grey backdrops**

In `plugins/strx/source/strx_goniometer.h`, delete the two lines in `draw()` that fill the whole view with the track colour:

```cpp
        c->setFillColor(trackColor_);
        c->drawRect(r, kDrawFilled);
```

Do the same in `strx_meters.h` and `strx_spectrum.h` if they carry the equivalent fill. `BgDark` then shows through and the plugin background becomes the objects' background. With no backdrop there are no frames: what delimits each plot is its own geometry — the circle, the bar tracks, the axes.

Leave the bar *tracks* alone: those are `SliderTrack` and they are the meters' scale, not a backdrop.

- [ ] **Step 5: Move the text out of the plots**

Each of the three views now reserves bands rather than drawing text over its plot.

`strx_goniometer.h`: the plot band is `y = 18 .. 278` of the view — centre the circle there and size its radius to that band, not to the whole view. Draw `L` and `R` in the label band (`y = 0 .. 18`), and `ANGLE …  PANORAMA …` in the value band (`y = 278 .. 300`). Both in `TextLight`; the circle and the radial grid in `structure`.

`strx_meters.h`: the bars occupy `y = 18 .. 278` — the same band, so they are exactly as tall as the circle. `kLabels` (`L R M S W`) go in the label band, the per-bar value strings in the value band, both in `TextLight`. Tracks stay `SliderTrack`, fills stay `MeterFill` / `MeterInv`.

`strx_spectrum.h`: its axis labels stay inside its plot frame — there they are part of the graph, not labels on the panel. Only the backdrop changes, plus the grid moving to `structure`.

This is the whole of "labels outside the frames" and "meters as tall as the goniometer": today the text lives inside the plot area and eats it from within, which is why the bars never reach the circle's height.

- [ ] **Step 6: Build and verify**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target strx --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/strx.vst3
```

Expected: build succeeds, validator 0 failures.

Report that the in-host visual check is pending GS: whether the `#888888` grid now competes with the scatter. Removing the `#444444` backdrop raises the grid's contrast from roughly 2:1 to roughly 4:1 without any colour changing, so the same grid will read more strongly than before. If it does compete, the fix is to darken `Structure` toward `#666666`, not to bring the backdrop back. Do NOT pre-emptively darken it — that is GS's call on his screen.

- [ ] **Step 7: Commit**

```bash
git add plugins/strx/resource/strx.uidesc plugins/strx/source
git commit -m "feat(strx): 600x770 layout, no backdrops, one white point for text

Dimensions emerge from the existing quadrants: 20 + 260 + 30 + 270 + 20 = 600.
Each top view splits into label/plot/value bands with the plot band at the same
absolute y in both, so the circle and the bars align without being told to —
the text used to live inside the plot and eat it from within. The status line
goes from 45 to 84 characters, so the longest record stops being truncated. The
spectrum goes from 310 to 560 px. Backdrops removed; every string is TextLight;
structure keeps its dim grey under the new name Structure."
```

---

### Task 6: Spectrum — max-hold and live curves

**Files:**
- Modify: `plugins/strx/source/strx_spectrum.h`
- Modify: `plugins/strx/source/strx_processor.cpp` (pass the processor to the view if it does not already receive it)

**Interfaces:**
- Consumes: Task 3's `AnalysisFrame::holdM/holdS`; Task 4's `StrxProcessor::calbusWatch()` and `CalbusDigest::glide`.
- Produces: nothing downstream.

- [ ] **Step 1: Draw hold and live when a sweep is sounding**

In `plugins/strx/source/strx_spectrum.h`, in the timer callback, poll the watch so the view knows what is sounding — the poll is rate-limited inside the watch, so calling it from a 30 Hz timer costs one snapshot per 80 ms:

```cpp
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                glide_ = processor_->calbusWatch().poll().glide;
                invalid();
            }, kTimerMs, /*doStart*/true);
```

with `bool glide_ = false;` as a member.

In `draw()`, after the existing frame read, render according to `glide_`:

```cpp
        // Pink noise is stationary, so one averaged curve per channel says
        // everything. A sweep is not: it presents ONE frequency at a time, so
        // the average would smear a moving peak. The hold accumulates the
        // response as the sweep descends (each bin is excited once), and the
        // live curve — fast now, tau 100 ms — shows where the sweep IS.
        if (glide_) {
            drawCurve(c, fr.holdM, fr.numBins, midColor_,  /*alpha*/255);
            drawCurve(c, fr.holdS, fr.numBins, sideColor_, /*alpha*/255);
            drawCurve(c, fr.specM, fr.numBins, midColor_,  /*alpha*/90);
            drawCurve(c, fr.specS, fr.numBins, sideColor_, /*alpha*/90);
        } else {
            drawCurve(c, fr.specM, fr.numBins, midColor_,  /*alpha*/255);
            drawCurve(c, fr.specS, fr.numBins, sideColor_, /*alpha*/255);
        }
```

Factor the existing per-curve drawing into a `drawCurve(CDrawContext*, const float* db, int numBins, CColor, uint8_t alpha)` helper rather than repeating the path-building code four times. Keep the existing dB→y and log-frequency→x mapping exactly as it is: it is calibrated (a full-scale sine reads 0 dBFS) and out of scope here.

The legend currently reads `M` `S`. Leave it: the hold and the live curve are the same two channels, distinguished by weight, and a four-entry legend would cost more room than it explains.

- [ ] **Step 2: Build and verify**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target strx --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/strx.vst3
cd build-test && ctest -C Release --output-on-failure
```

Expected: validator 0 failures; all tests pass.

There is no automated test for this step: the decision logic it depends on (`digest`, the hold, the tau switch) is unit-tested in Tasks 2–4, and what remains here is drawing, which only the host can show. Say so in the report rather than writing a test that asserts nothing.

- [ ] **Step 3: Commit**

```bash
git add plugins/strx/source/strx_spectrum.h plugins/strx/source/strx_processor.cpp
git commit -m "feat(strx): max-hold + live spectrum when a sweep is sounding

Driven by the bus: when the sounding emitter is a glide, the hold draws the
response curve as the sweep descends and the live curve shows where the sweep
is. Pink noise is stationary and keeps the single averaged curve it always had."
```

---

### Task 7: ltglide SHOT button

**Files:**
- Create: `plugins/ltglide/source/ltglide_shot_button.h`
- Modify: `plugins/ltglide/source/ltglide_ids.h` (view name tag)
- Modify: `plugins/ltglide/source/ltglide_processor.h`, `plugins/ltglide/source/ltglide_processor.cpp`
- Modify: `plugins/ltglide/resource/ltglide.uidesc`
- Test: `tests/ltglide_dsp_test.cpp`

**Interfaces:**
- Consumes: `GlideTransport::trigger()`, `GlideTransport::running()` (already public in `ltglide_dsp.h`).
- Produces: `Seam::LtglideShotButton`; on `LTGLIDEProcessor`: `void requestShot()` and `bool transportRunning() const`.

- [ ] **Step 1: Write the failing test**

`trigger()` exists but nothing has ever exercised its guard. Append to `tests/ltglide_dsp_test.cpp` (match the file's namespace idiom):

```cpp
TEST_CASE("GlideTransport::trigger fires exactly one pass with LOOP off") {
    using GT = Seam::ltglide::GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    // With LOOP off the transport is silent forever until triggered: Idle's
    // only exit is `if (loop_) beginPass()`. This is why SHOT exists.
    for (int i = 0; i < 1000; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 0u);

    t.trigger();
    CHECK(t.running());
    CHECK(t.passCount() == 1u);

    // Run the pass out; with LOOP off it must stop and stay stopped.
    const long total = (long)((GT::kLeadSec + 2.0 + GT::kTailSec) * 48000.0) + 16;
    for (long i = 0; i < total; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 1u);
    for (int i = 0; i < 1000; ++i) t.process();
    CHECK(t.passCount() == 1u);          // no second pass appeared
}

TEST_CASE("GlideTransport::trigger during a pass does not restart it") {
    using GT = Seam::ltglide::GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    t.trigger();
    CHECK(t.passCount() == 1u);
    for (long i = 0; i < 48000; ++i) t.process();   // 1 s into the pass
    CHECK(t.running());

    t.trigger();                                    // must be ignored
    CHECK(t.passCount() == 1u);

    // A truncated-and-restarted pass would still publish a passCounter and an
    // anchor, and Spec 3 would average it as if it were whole. The guard is
    // what stops that.
    for (long i = 0; i < 48000; ++i) t.process();
    CHECK(t.running());
    CHECK(t.passCount() == 1u);
}
```

- [ ] **Step 2: Run to verify it fails or passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target ltglide_dsp_test --config Release
./build-test/tests/Release/ltglide_dsp_test
```

These may PASS immediately: `trigger()` already exists and its guard is already correct. That is fine and expected — the point is that nothing covered it. Verify by mutation that they can fail: remove the `if (state_ == State::Idle)` guard from `trigger()` and confirm "does not restart it" goes RED; revert and confirm GREEN. Report both.

- [ ] **Step 3: Commit the test**

```bash
git add tests/ltglide_dsp_test.cpp
git commit -m "test(ltglide): cover trigger()'s single-shot behaviour and its guard

trigger() has existed since the transport was written and only the plan ever
called it. Its Idle guard is what stops a re-press from truncating a pass that
would still publish a passCounter and an anchor for Spec 3 to average."
```

- [ ] **Step 4: Add the processor's two atomics**

In `plugins/ltglide/source/ltglide_processor.h`, add to the private members:

```cpp
    // SHOT button <-> DSP. No VST3 parameter is involved in either direction:
    // trigger() is internal transport state, not something a host can automate,
    // and a parameterless path is also immune to the momentary-button
    // coalescing problem. This works because SingleComponentEffect makes the
    // processor and the controller the same object, so the view can reach here
    // directly — exactly as strx's views read the analyzer.
    std::atomic<bool> shotRequest_{false};
    std::atomic<bool> transportRunning_{false};
```

and to the public section:

```cpp
    // GUI thread: fire one pass. Ignored by the transport unless it is idle.
    void requestShot() { shotRequest_.store(true, std::memory_order_relaxed); }
    // GUI thread: is a pass running right now? Drives the button's lit state.
    bool transportRunning() const { return transportRunning_.load(std::memory_order_relaxed); }
```

In `plugins/ltglide/source/ltglide_processor.cpp`, in `process()`, immediately after `readParameterChanges(data);`:

```cpp
    // Consume the GUI's shot request. trigger() no-ops unless the transport is
    // idle, so a press during a pass, or with LOOP on, does nothing.
    if (shotRequest_.exchange(false, std::memory_order_relaxed)) transport_.trigger();
```

and at the end of `processBlock()`, beside the existing `busAnchor_` edge handling:

```cpp
    transportRunning_.store(transport_.running(), std::memory_order_relaxed);
```

Ensure `<atomic>` is included.

- [ ] **Step 5: Write the button view**

Add the view name tag to `plugins/ltglide/source/ltglide_ids.h`:

```cpp
// Custom-view name tag (matches resource/ltglide.uidesc custom-view-name).
static const char* kViewShot = "LtglideShot";
```

Create `plugins/ltglide/source/ltglide_shot_button.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ltglide — single-shot launch button.
//
// With LOOP off, GlideTransport's Idle state has no exit but `if (loop_)
// beginPass()`, so ltglide is silent forever. This is the missing door.
//
// It owns no VST3 parameter and drives none. dslar's reset button is "UI-only"
// in that it owns no parameter of its own, but it still drives six real ones
// through the host; SHOT has no such road, because trigger() is internal
// transport state and there is nothing for a host to automate. Two atomics on
// the processor are the whole path — which also makes it immune to the
// momentary-button coalescing problem, since there is no parameter to coalesce.
//
// Lit for the pass's whole duration (~32 s: head Dirac + 5 s lead + 20 s sweep
// + 5 s tail + tail Dirac), so the panel says whether it is measuring.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"

#include "ltglide_processor.h"

namespace Seam {

class LtglideShotButton : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // lit-state poll; not animation

    LtglideShotButton(const VSTGUI::CRect& size, LTGLIDEProcessor* processor,
                      const VSTGUI::CColor& idleColor, const VSTGUI::CColor& litColor)
        : VSTGUI::CView(size), processor_(processor),
          idleColor_(idleColor), litColor_(litColor) {
        setWantsFocus(false);
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                const bool r = processor_ && processor_->transportRunning();
                if (r != lit_) { lit_ = r; invalid(); }
            }, kTimerMs, /*doStart*/true);
    }

    ~LtglideShotButton() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
    }

    void draw(VSTGUI::CDrawContext* c) override {
        c->setDrawMode(VSTGUI::kAntiAliasing);
        const VSTGUI::CRect r = getViewSize();
        c->setFillColor(lit_ ? litColor_ : idleColor_);
        c->drawRect(r, VSTGUI::kDrawFilled);
        c->setFrameColor(litColor_);
        c->setLineWidth(1);
        c->drawRect(r, VSTGUI::kDrawStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override {
        if (buttons.isLeftButton() && processor_ && getViewSize().pointInside(where)) {
            processor_->requestShot();
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    LTGLIDEProcessor* processor_ = nullptr;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    VSTGUI::CColor idleColor_, litColor_;
    bool lit_ = false;
};

} // namespace Seam
```

If `LTGLIDEProcessor` does not already derive from `VSTGUI::VST3EditorDelegate` and override `createCustomView`, add that exactly as `StrxProcessor` does (`strx_processor.h`/`.cpp`), then add the branch:

```cpp
    if (name && std::string(name) == kViewShot) {
        VSTGUI::CColor idle = VSTGUI::kGreyCColor, lit = VSTGUI::kBlueCColor;
        if (description) {
            description->getColor("SliderTrack", idle);
            description->getColor("SliderActive", lit);
        }
        return new Seam::LtglideShotButton(VSTGUI::CRect(0, 0, 14, 14), this, idle, lit);
    }
```

- [ ] **Step 6: Add the button to the uidesc**

In `plugins/ltglide/resource/ltglide.uidesc`, on the same row as the LOOP checkbox, mirroring dslar's reset-button row (`dslar.uidesc:41-44` — a 14×14 custom view with a `KnobLabelFont` label to its right).

LOOP sits at `origin="95, 606" size="110, 20"`, so it occupies x 95–205 and y 606–626. SHOT goes to its right, vertically centred on the same row. Insert immediately after the LOOP checkbox's closing tag:

```xml
        <!-- Single-shot launch. With LOOP off the transport has no exit from
             Idle, so this is the only way to fire one pass. Lit for the pass's
             whole duration (~32 s). -->
        <view class="CView" origin="220, 609" size="14, 14" custom-view-name="LtglideShot"
              tooltip="Fire one pass (LOOP off). Lit while the pass runs."/>
        <view class="CTextLabel" origin="238, 607" size="46, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="SHOT" transparent="true"/>
```

x 220–234 for the box and 238–284 for the label leaves a 16 px right margin in the 300 px template, and 609–623 centres the 14 px box on LOOP's 20 px row.

- [ ] **Step 7: Build and run the validator**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target ltglide --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/ltglide.vst3
```

Expected: build succeeds, validator 0 failures.

- [ ] **Step 8: Commit**

```bash
git add plugins/ltglide
git commit -m "feat(ltglide): SHOT button for a single pass

With LOOP off the transport had no exit from Idle, so ltglide was silent
forever and trigger() — public since the transport was written — was reachable
only from the unit test. The button owns no VST3 parameter and drives none:
trigger() is internal transport state, so two atomics on the processor are the
whole path, which also sidesteps momentary-button coalescing. Lit for the
pass's duration; a press during a pass is ignored, because a truncated pass
would still publish a passCounter for Spec 3 to average."
```

---

### Task 8: STONE control on both emitters

**Files:**
- Modify: `plugins/multipink/resource/multipink.uidesc`
- Modify: `plugins/ltglide/resource/ltglide.uidesc`

**Interfaces:**
- Consumes: `kParamStoneId` (103 on multipink, 109 on ltglide), already registered as a 9-entry `StringListParameter` by the calbus work.
- Produces: nothing downstream.

This is the fix for `STONE ?`: the parameter exists and is automatable, but with no control it is reachable only through the host's generic parameter list.

- [ ] **Step 1: multipink**

In `plugins/multipink/resource/multipink.uidesc`, add the control tag inside `<control-tags>`:

```xml
        <control-tag name="StoneId"    tag="103"/>
```

and the control, mirroring the `Reference` block at lines 31-35 exactly (that is the file's established way of presenting a `StringListParameter`).

The MUTE checkbox ends at y=242 and the slot badge starts at y=252, so there is no room: the STONE block goes in between and everything below shifts down by 50 px. Insert immediately after the MUTE checkbox's closing tag:

```xml
        <!-- STONE identity for the calibration bus. Declared by hand, never
             inferred from the pool slot: with four STONEs in the room, an
             instance that guesses is an instance that calibrates the wrong
             power amp. Default "?" = undeclared, which strx renders "STONE ?". -->
        <view class="CTextLabel" origin="20, 252" size="260, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="STONE" transparent="true"/>
        <view class="COptionMenu" origin="80, 272" size="140, 20" control-tag="StoneId"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>
```

Then shift the three rows below it down by 50 px, and grow the template:

| View | old y | new y |
|---|---|---|
| Slot badge row (`CTextLabel` ×3 + `CParamDisplay` ×2, currently y=252) | 252 | 302 |
| Status row (`CTextLabel` + `CParamDisplay` `PoolStatus`, currently y=272) | 272 | 322 |
| Logo | 295 | 345 |

Template header becomes:

```xml
    <template name="editor" class="CViewContainer" origin="0, 0" size="300, 450"
              minSize="300, 450" maxSize="300, 450"
```

(Logo at 345 + 77 = 422, leaving a 28 px bottom margin.) Keep `background-color` and the rest of the opening tag exactly as they are.

- [ ] **Step 2: ltglide**

In `plugins/ltglide/resource/ltglide.uidesc`, add the control tag:

```xml
        <control-tag name="StoneId"    tag="109"/>
```

and the same `CTextLabel` + `COptionMenu` pair below the LOOP/SHOT row. ltglide has no slot to infer an identity from, and its chain identity lives in the host routing, which the receiver cannot read — so it is declared by hand, exactly like multipink's.

Insert after the SHOT label added in Task 7:

```xml
        <!-- STONE identity for the calibration bus. ltglide has no slot to be
             inferred from and the receiver cannot read the host's routing, so
             this is declared by hand. Default "?" = undeclared. -->
        <view class="CTextLabel" origin="20, 640" size="260, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="STONE" transparent="true"/>
        <view class="COptionMenu" origin="80, 660" size="140, 20" control-tag="StoneId"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>
```

Then move the logo from `origin="30, 646"` to `origin="30, 700"` and grow the template:

```xml
    <template name="editor" class="CViewContainer" origin="0, 0" size="300, 800"
              minSize="300, 800" maxSize="300, 800"
```

(Logo at 700 + 77 = 777, leaving a 23 px bottom margin.) Keep the rest of the opening tag as it is.

- [ ] **Step 3: Build and run both validators**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target multipink --config Release
cmake --build build-release --target ltglide --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/multipink.vst3
./build-release/bin/Release/validator ./build-release/VST3/Release/ltglide.vst3
```

Expected: both build; both validators 0 failures.

- [ ] **Step 4: Commit**

```bash
git add plugins/multipink/resource/multipink.uidesc plugins/ltglide/resource/ltglide.uidesc
git commit -m "feat(multipink,ltglide): STONE control in the GUI

kParamStoneId has existed since the calbus work but had no control, so it was
reachable only through the host's generic parameter list and strx always read
'STONE ?'. A COptionMenu, mirroring the Reference list multipink already
presents that way."
```

---

## In-host verification (GS, after Task 8)

No automated test replaces this. In Reaper:

1. Load multipink, set **STONE 2** from its new control, un-mute. strx reads `multipink · STONE 2 · slot 0-3 · -23.0 dB`, **untruncated**.
2. Load a second multipink, un-mute both. The line flags `· +N more` — impossible to see before, since the flag sat past the 45-char cut.
3. Swap for ltglide, LOOP **off**. Press **SHOT**: one pass fires, the button stays azure for its duration, and the spectrum's max-hold draws the response as the sweep descends while the live curve shows where it is. At the next SHOT the hold clears.
4. Press SHOT during a pass: nothing happens.
5. **Read the tail of the ltglide line.** `T=20s` means Reaper supplies `kContTimeValid` and Spec 3 has its anchor. `no host clock` for a pass started while the transport rolls means Reaper never supplies it, and Spec 3 must rest on `projectTimeSamples` — "always valid" per `ivstprocesscontext.h:124`, but it jumps on loops and relocation. Record which; do not work around it here.
6. **Visual:** does the `#888888` grid now compete with the scatter? Removing the `#444444` backdrop raises its contrast from ~2:1 to ~4:1 without any colour changing. If it does, darken `Structure` toward `#666666` — do not bring the backdrop back.

## Notes for the implementer

**The bus is an observer.** No change here may make a plugin's audio depend on it. strx must still analyse, multipink must still make noise, and ltglide must still sweep when the dylib is absent.

**strx's audio thread never touches the bus.** The watch runs on the GUI thread and writes two atomics; `process()` reads them once per block. If a change makes the audio thread call `CalbusClient`, the change is wrong.

**Do not touch** the seqlock, the epoch/handle packing, `publish()`'s wait-freedom, the record layout, `BusAnchor`, or the spectrum's dB/log-frequency calibration. Each was verified experimentally and is settled.
