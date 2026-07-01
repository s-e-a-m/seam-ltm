# ltglide Phase 3b-i — Glissando Tone-Burst Generator — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `ltglide`, a standalone mono VST3 that plays a deterministic glissando of Linkwitz shaped tone-bursts bracketed by head/tail Dirac markers, driven by a loop + manual-trigger transport.

**Architecture:** A new plugin cloned from `ltburst`'s structure. An SDK-free header `ltglide_dsp.h` holds three units: a `SweepFreq` map (progress → Hz), a `GlissBurst` retriggered-grain engine (C++ port of the Faust `slw.glissburst`, passo/gap), and a `GlideTransport` state machine that owns the per-sample progress `p` and the pass timeline. The `SingleComponentEffect` processor feeds `SweepFreq(p)` into `GlissBurst`, applies the dBFS Level gain, and emits Dirac impulses at pass boundaries.

**Tech Stack:** C++17, VST3 SDK 3.x (`SingleComponentEffect`), VSTGUI (`.uidesc`), CMake, doctest (SDK-free unit tests under `tests/`).

## Global Constraints

- Hand-written C++ DSP only; Faust is the spec, never `faust -lang cpp` in `source/` (seam-ltm/CLAUDE.md).
- FAUST REFERENCE comment block at the top of the processor header, citing `seam.linkwitz.lib`.
- SDK-free DSP core in a header-only file, unit-tested with doctest (pattern: `ltburst_dsp.h` + `tests/ltburst_dsp_test.cpp`).
- VSTGUI live editing disabled suite-wide (`VSTGUI_LIVE_EDITING=0`); ship a finished GUI (seam-ltm/CLAUDE.md — every plugin ships a GUI).
- Mono output bus only; reject any non-mono arrangement (pattern: `ltburst`).
- VST3 SDK at `../vst3sdk`, overridable via `-DSEAM_VST3SDK_DIR=...`; the workspace SDK is at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- `N` (burst cycle count) is fixed at 5 (canonical Linkwitz); not a parameter.
- Target FUID: `0x5E4D000D` (13th plugin; ltburst = `0x5E4D000C`).
- Numerical parity target vs the Faust reference: ≈ 1e-13 (as achieved for `ShapedBurst`).
- Peer-aware synchronisation and the receiver plugin are OUT OF SCOPE (Phase 3b-ii).

## Faust reference (the spec being ported)

From `seam.linkwitz.lib` (Approach A, canonical, stable Faust):

```faust
sweepfreq(f0,f1,smode,p) = select2(smode, f0 + (f1-f0)*p, f0*pow(f1/f0, p));

glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u) * win
with {
    phase = grain : _,!;
    fg    = grain : !,_;
    grain = loop ~ (_,_)
    with {
        loop(pphase,pfg) = nphase, nfg
        with {
            den    = max(20.0, pfg);
            Tg     = select2(dmode, max(delta, N/den), N/den + delta);
            inc    = 1.0/max(1.0, Tg*ma.SR);
            adv    = pphase + inc;
            start  = 1 - 1';
            onset  = (adv >= 1.0) | start;
            nphase = adv - floor(adv);
            nfg    = select2(onset, pfg, fsig);
        };
    };
    den = max(20.0, fg);
    Tg  = select2(dmode, max(delta, N/den), N/den + delta);
    u   = fg * phase * Tg;
    win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N));
};
```

Semantics recovered for the C++ port:
- `select2(0,A,B)=A`, `select2(1,A,B)=B`. So `dmode` 0 = passo → `Tg = max(delta, N/den)`; `dmode` 1 = gap → `Tg = N/den + delta`.
- `nfg = select2(onset, pfg, fsig)`: no onset → hold previous `pfg`; onset → latch `fsig`.
- The feedback carries the PREVIOUS sample's `(phase, fg)`; the output stage uses the CURRENT `(phase, fg)`.
- `start = 1 - 1'` is 1 only at sample 0, forcing the first onset so `fsig` is latched immediately.
- `u = fg·phase·Tg` = (cycles/sec)·(fraction of grain)·(sec/grain) = cycles since onset.

## File structure

- `plugins/ltglide/source/ltglide_dsp.h` — SDK-free: `Seam::ltglide::SweepFreq` (free fn), `GlissBurst`, `GlideTransport`.
- `plugins/ltglide/source/ltglide_ids.h` — FUID + `ParamID` enum + range constants.
- `plugins/ltglide/source/ltglide_processor.h` — `LTGLIDEProcessor` (FAUST REFERENCE block).
- `plugins/ltglide/source/ltglide_processor.cpp` — IAudioProcessor wiring + factory.
- `plugins/ltglide/source/version.h` — version/company strings.
- `plugins/ltglide/resource/ltglide.uidesc` — VSTGUI editor.
- `plugins/ltglide/CMakeLists.txt` — plugin target.
- `plugins/ltglide/doc/study/…`, `plugins/ltglide/doc/ltglide-validation.md` — docs.
- `tests/ltglide_dsp_test.cpp` — doctest for `SweepFreq` + `GlissBurst` + `GlideTransport`.
- Modify `CMakeLists.txt` (root) — `add_subdirectory(plugins/ltglide)`.
- Modify `tests/CMakeLists.txt` — register `ltglide_dsp_test`.

`ltburst` is not modified: `ltglide` consumes none of `ShapedBurst`, so no shared `linkwitz_dsp.h` is factored now (YAGNI; revisit if a third consumer appears).

---

### Task 1: SweepFreq + GlissBurst DSP core

**Files:**
- Create: `plugins/ltglide/source/ltglide_dsp.h`
- Create: `tests/ltglide_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a `ltglide_dsp_test` target)

**Interfaces:**
- Produces:
  - `double Seam::ltglide::SweepFreq(double f0, double f1, int smode, double p)` — `smode` 0 linear, 1 exponential.
  - `class Seam::ltglide::GlissBurst` with:
    - `static constexpr int kN = 5;`
    - `void prepare(double fs);`
    - `void reset();`
    - `void setDelta(double sec);` / `void setDmode(int dmode);` (0 passo, 1 gap)
    - `double process(double fsig);` — one sample; advances the grain engine, latching `fsig` at onsets.
    - `double heldFrequency() const;` — the currently latched grain frequency `fg` (for tests and the future receiver).
    - `double grainPhase() const;` — the grain ramp `phase ∈ [0,1)`.

- [ ] **Step 1: Write the DSP header**

Create `plugins/ltglide/source/ltglide_dsp.h`:

```cpp
// SEAM-LTM · ltglide — SDK-free DSP core (header-only, unit-testable).
//
// Glissando of Linkwitz shaped tone-bursts: an external progress p in [0,1]
// maps (via SweepFreq) to a carrier frequency f0->f1; GlissBurst retriggers a
// grain at each onset, latches the swept frequency for the whole grain
// (sample-and-hold), and shapes N=5 carrier cycles with a Hann window.
//
// FAUST REFERENCE (seam.linkwitz.lib):
//   sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p));
//   glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u)*win with { ... };  // Approach A
#pragma once
#include <cmath>
#include <algorithm>

namespace Seam { namespace ltglide {

// progress p in [0,1] -> frequency (smode 0 = linear, 1 = exponential).
inline double SweepFreq(double f0, double f1, int smode, double p) {
    return (smode == 0) ? f0 + (f1 - f0) * p
                        : f0 * std::pow(f1 / f0, p);
}

// Retriggered-grain glissando burst engine (port of Faust slw.glissburst, A).
class GlissBurst {
public:
    static constexpr int    kN      = 5;      // canonical Linkwitz cycle count
    static constexpr double kFloorHz = 20.0;  // guards N/fg while fg is still 0

    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; reset(); }
    void reset() { phase_ = 0.0; fg_ = 0.0; started_ = false; }

    void setDelta(double sec) { delta_ = (sec > 0.0) ? sec : 0.0; }
    void setDmode(int dmode)  { dmode_ = (dmode != 0) ? 1 : 0; }  // 0 passo, 1 gap

    double heldFrequency() const { return fg_; }
    double grainPhase()    const { return phase_; }

    // One sample. fsig is the continuous swept frequency (Hz) for this sample.
    inline double process(double fsig) {
        // --- recursive grain engine: carries previous (phase_, fg_) ---
        const double pphase = phase_;
        const double pfg    = fg_;
        const double denP   = std::max(kFloorHz, pfg);
        const double TgP    = periodSec(denP);
        const double inc    = 1.0 / std::max(1.0, TgP * fs_);
        const double adv    = pphase + inc;
        const bool   start  = !started_;                 // 1 - 1' : true at sample 0
        started_ = true;
        const bool   onset  = (adv >= 1.0) || start;
        phase_ = adv - std::floor(adv);
        fg_    = onset ? fsig : pfg;                      // hold; latch at onset

        // --- output stage: uses the CURRENT (phase_, fg_) ---
        const double den = std::max(kFloorHz, fg_);
        const double Tg  = periodSec(den);
        const double u   = fg_ * phase_ * Tg;             // cycles since onset
        const double win = (u < (double)kN)
            ? 0.5 - 0.5 * std::cos(2.0 * M_PI * u / (double)kN)
            : 0.0;
        return std::sin(2.0 * M_PI * u) * win;
    }

private:
    inline double periodSec(double den) const {
        // passo: onset-fixed max(delta, N/f); gap: gap-fixed N/f + delta.
        return (dmode_ == 0) ? std::max(delta_, (double)kN / den)
                             : (double)kN / den + delta_;
    }

    double fs_      = 48000.0;
    double delta_   = 0.3;
    int    dmode_   = 1;       // gap by default
    double phase_   = 0.0;     // grain ramp [0,1)
    double fg_      = 0.0;     // latched grain frequency (Hz)
    bool   started_ = false;
};

}} // namespace Seam::ltglide
```

- [ ] **Step 2: Write the failing test**

Create `tests/ltglide_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ltglide_dsp.h"
#include <cmath>

using namespace Seam::ltglide;

TEST_CASE("SweepFreq endpoints and midpoints") {
    // exponential (smode 1): geometric — midpoint is the geometric mean.
    CHECK(SweepFreq(20000, 20, 1, 0.0) == doctest::Approx(20000.0));
    CHECK(SweepFreq(20000, 20, 1, 1.0) == doctest::Approx(20.0));
    CHECK(SweepFreq(20000, 20, 1, 0.5) == doctest::Approx(std::sqrt(20000.0 * 20.0)));
    // linear (smode 0): arithmetic — midpoint is the arithmetic mean.
    CHECK(SweepFreq(100, 200, 0, 0.0) == doctest::Approx(100.0));
    CHECK(SweepFreq(100, 200, 0, 1.0) == doctest::Approx(200.0));
    CHECK(SweepFreq(100, 200, 0, 0.5) == doctest::Approx(150.0));
}

static GlissBurst makeGap(double f, double delta) {
    GlissBurst g;
    g.prepare(48000.0);
    g.setDelta(delta);
    g.setDmode(1);   // gap
    g.reset();
    (void)f;
    return g;
}

TEST_CASE("constant-frequency gap mode: onset spacing equals N/f + delta") {
    // f=1000, N=5, delta=0.3 -> Tg = 5/1000 + 0.3 = 0.305 s -> 14640 samples @48k.
    // (This equals the ltburst fixed-burst period for f0=1000, dwell=300 ms.)
    GlissBurst g = makeGap(1000.0, 0.3);
    const double f = 1000.0;
    // Sample 0 forces the first onset -> held freq becomes f immediately.
    g.process(f);
    CHECK(g.heldFrequency() == doctest::Approx(1000.0));
    // Find the next onset by watching heldFrequency latch again after a change.
    int firstOnset = 0;                 // sample 0 was an onset
    int nextOnset = -1;
    for (int n = 1; n < 40000; ++n) {
        double before = g.grainPhase();
        g.process(f);
        double after = g.grainPhase();
        if (after < before) { nextOnset = n; break; }  // ramp wrapped -> onset
    }
    REQUIRE(nextOnset > 0);
    CHECK((nextOnset - firstOnset) == 14640);
}

TEST_CASE("burst starts on a zero crossing") {
    GlissBurst g = makeGap(1000.0, 0.3);
    CHECK(g.process(1000.0) == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("sample-and-hold: frequency is latched for the whole grain") {
    GlissBurst g = makeGap(1000.0, 0.3);
    g.process(2000.0);                          // sample 0 onset latches 2000
    CHECK(g.heldFrequency() == doctest::Approx(2000.0));
    // Feed a different frequency; it must NOT take effect until the next onset.
    for (int n = 0; n < 100; ++n) g.process(500.0);
    CHECK(g.heldFrequency() == doctest::Approx(2000.0));
}

TEST_CASE("amplitude never exceeds unity") {
    GlissBurst g = makeGap(1000.0, 0.3);
    double peak = 0.0;
    for (int n = 0; n < 14640; ++n) peak = std::max(peak, std::fabs(g.process(1000.0)));
    CHECK(peak <= 1.0 + 1e-9);
    CHECK(peak > 0.5);
}

TEST_CASE("no NaN/Inf across a swept parameter range") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (int dmode : {0, 1}) {
            for (double delta : {0.05, 0.3, 1.0}) {
                GlissBurst g;
                g.prepare(fs); g.setDelta(delta); g.setDmode(dmode); g.reset();
                for (int n = 0; n < 20000; ++n) {
                    double p = (double)n / 20000.0;
                    double f = SweepFreq(20000.0, 20.0, 1, p);
                    double y = g.process(f);
                    REQUIRE(std::isfinite(y));
                }
            }
        }
    }
}
```

- [ ] **Step 3: Register the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(ltglide_dsp_test
    ltglide_dsp_test.cpp
)
target_include_directories(ltglide_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/ltglide/source
)
target_compile_features(ltglide_dsp_test PRIVATE cxx_std_17)
add_test(NAME ltglide_dsp_test COMMAND ltglide_dsp_test)
```

- [ ] **Step 4: Configure, build and run the test**

Run:
```bash
cmake -S . -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target ltglide_dsp_test
./build/tests/ltglide_dsp_test
```
Expected: all test cases pass (0 failed).

- [ ] **Step 5: Commit**

```bash
git add plugins/ltglide/source/ltglide_dsp.h tests/ltglide_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(ltglide): SDK-free SweepFreq + GlissBurst DSP core + doctest"
```

---

### Task 2: GlideTransport state machine

**Files:**
- Modify: `plugins/ltglide/source/ltglide_dsp.h` (add `GlideTransport`)
- Modify: `tests/ltglide_dsp_test.cpp` (add transport cases)

**Interfaces:**
- Produces `class Seam::ltglide::GlideTransport`:
  - `enum class Kind { Silence, Dirac, Glide };`
  - `struct Tick { Kind kind; double p; };` — `p ∈ [0,1)` valid only when `kind == Glide`.
  - `void prepare(double fs);`
  - `void setSweepSeconds(double t);` — glissando duration.
  - `void setLoop(bool on);`
  - `void trigger();` — begin one pass (ignored if a pass is already running).
  - `Tick process();` — advance one sample.
  - `bool running() const;`
  - Fixed timing constants: `static constexpr double kLeadSec = 5.0, kTailSec = 5.0, kWaitSec = 2.0;`
- Consumes: nothing (pure logic).

Pass timeline: `Dirac(1) → Lead(5 s silence) → Glide(t, p:0→1) → Tail(5 s silence) → Dirac(1) → [loop ? Wait(2 s) → restart : Idle]`. When `loop` is on and the transport is Idle, a pass starts automatically.

- [ ] **Step 1: Add GlideTransport to the header**

Insert before the closing `}}` of `plugins/ltglide/source/ltglide_dsp.h`:

```cpp
// Pass transport: owns the progress p and the Dirac-bracketed pass timeline.
class GlideTransport {
public:
    enum class Kind { Silence, Dirac, Glide };
    struct Tick { Kind kind; double p; };

    static constexpr double kLeadSec = 5.0;
    static constexpr double kTailSec = 5.0;
    static constexpr double kWaitSec = 2.0;

    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        leadN_ = (long)std::llround(kLeadSec * fs_);
        tailN_ = (long)std::llround(kTailSec * fs_);
        waitN_ = (long)std::llround(kWaitSec * fs_);
        recomputeGlide();
        state_ = State::Idle; counter_ = 0;
    }
    void setSweepSeconds(double t) { tSec_ = (t > 0.0) ? t : 1.0; recomputeGlide(); }
    void setLoop(bool on) { loop_ = on; }
    void trigger() { if (state_ == State::Idle) beginPass(); }
    bool running() const { return state_ != State::Idle; }

    Tick process() {
        switch (state_) {
            case State::Idle:
                if (loop_) beginPass();
                else return { Kind::Silence, 0.0 };
                return process();                 // fall into the fresh pass
            case State::HeadDirac:
                state_ = State::Lead; counter_ = 0;
                return { Kind::Dirac, 0.0 };
            case State::Lead:
                if (++counter_ >= leadN_) { state_ = State::Glide; counter_ = 0; }
                return { Kind::Silence, 0.0 };
            case State::Glide: {
                double p = (glideN_ > 0) ? (double)counter_ / (double)glideN_ : 0.0;
                if (++counter_ >= glideN_) { state_ = State::Tail; counter_ = 0; }
                return { Kind::Glide, p };
            }
            case State::Tail:
                if (++counter_ >= tailN_) { state_ = State::TailDirac; counter_ = 0; }
                return { Kind::Silence, 0.0 };
            case State::TailDirac:
                state_ = loop_ ? State::Wait : State::Idle; counter_ = 0;
                return { Kind::Dirac, 0.0 };
            case State::Wait:
                if (++counter_ >= waitN_) { state_ = State::Idle; counter_ = 0; }
                return { Kind::Silence, 0.0 };
        }
        return { Kind::Silence, 0.0 };
    }

private:
    enum class State { Idle, HeadDirac, Lead, Glide, Tail, TailDirac, Wait };
    void recomputeGlide() { glideN_ = (long)std::llround(tSec_ * fs_); if (glideN_ < 1) glideN_ = 1; }
    void beginPass() { state_ = State::HeadDirac; counter_ = 0; }

    double fs_ = 48000.0, tSec_ = 20.0;
    long leadN_ = 0, tailN_ = 0, waitN_ = 0, glideN_ = 0, counter_ = 0;
    bool loop_ = false;
    State state_ = State::Idle;
};
```

- [ ] **Step 2: Write the failing tests**

Append to `tests/ltglide_dsp_test.cpp`:

```cpp
TEST_CASE("transport is silent when idle and not looping") {
    GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(1.0);
    for (int n = 0; n < 1000; ++n) {
        auto tick = t.process();
        CHECK(tick.kind == GlideTransport::Kind::Silence);
    }
    CHECK(t.running() == false);
}

TEST_CASE("triggered pass has the exact Dirac/silence/glide timeline") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);                 // glideN = 48000
    t.trigger();

    // Sample 0: head Dirac.
    auto s0 = t.process();
    CHECK(s0.kind == GlideTransport::Kind::Dirac);

    // Next 5 s: silence (lead).
    const long leadN = 5 * 48000;
    for (long n = 0; n < leadN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Next 1 s: glide, p rising from 0 toward 1.
    auto g0 = t.process();
    CHECK(g0.kind == GlideTransport::Kind::Glide);
    CHECK(g0.p == doctest::Approx(0.0));
    const long glideN = 48000;
    GlideTransport::Tick gLast = g0;
    for (long n = 1; n < glideN; ++n) gLast = t.process();
    CHECK(gLast.kind == GlideTransport::Kind::Glide);
    CHECK(gLast.p < 1.0);
    CHECK(gLast.p > 0.99);

    // Next 5 s: silence (tail).
    const long tailN = 5 * 48000;
    for (long n = 0; n < tailN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Tail Dirac, then idle (no loop).
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    CHECK(t.process().kind == GlideTransport::Kind::Silence);
    CHECK(t.running() == false);
}

TEST_CASE("loop restarts automatically with a wait gap") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(true);
    // First tick from Idle+loop must be the head Dirac of pass 1.
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    // Drain the pass: lead + glide + tail.
    long body = 5 * 48000 + 48000 + 5 * 48000;
    for (long n = 0; n < body; ++n) t.process();
    // Tail Dirac.
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    // Wait (2 s) then a new head Dirac.
    long waitN = 2 * 48000;
    for (long n = 0; n < waitN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);   // pass 2 begins
}
```

- [ ] **Step 3: Build and run**

Run:
```bash
cmake --build build --target ltglide_dsp_test
./build/tests/ltglide_dsp_test
```
Expected: all cases pass (Task 1 + Task 2).

- [ ] **Step 4: Commit**

```bash
git add plugins/ltglide/source/ltglide_dsp.h tests/ltglide_dsp_test.cpp
git commit -m "feat(ltglide): GlideTransport state machine (loop + trigger) + doctest"
```

---

### Task 3: Plugin scaffold (ids, version, CMake, silent processor)

**Files:**
- Create: `plugins/ltglide/source/ltglide_ids.h`
- Create: `plugins/ltglide/source/version.h`
- Create: `plugins/ltglide/source/ltglide_processor.h`
- Create: `plugins/ltglide/source/ltglide_processor.cpp`
- Create: `plugins/ltglide/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root) — add `add_subdirectory(plugins/ltglide)` after `plugins/ltburst`.

**Interfaces:**
- Produces `Seam::LTGLIDEProcessorUID`, `Seam::LTGLIDEParams`, and the `Seam::LTGLIDEProcessor` class. At the end of this task the plugin builds and passes the validator while emitting silence (DSP wired in Task 4).

- [ ] **Step 1: Write `ltglide_ids.h`**

```cpp
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 13th plugin in the SEAM-LTM suite (ltburst = 0x5E4D000C).
static const Steinberg::FUID LTGLIDEProcessorUID(
    0x5E4D000D, 0xB2C3D4E5, 0x4C54474C, 0x49444500);   // "LTGL","IDE\0"

enum LTGLIDEParams : Steinberg::Vst::ParamID {
    kParamLevel   = 100,   // -60 … 0 dBFS       (linear taper)
    kParamF0      = 101,   // 20 … 20000 Hz      (log) sweep start
    kParamF1      = 102,   // 20 … 20000 Hz      (log) sweep end
    kParamSmode   = 103,   // 0 linear / 1 exponential
    kParamDmode   = 104,   // 0 passo / 1 gap
    kParamDelta   = 105,   // grain spacing, seconds
    kParamT       = 106,   // sweep duration, seconds
    kParamTrigger = 107,   // momentary: rising edge starts one pass
    kParamLoop    = 108,   // toggle: continuous passes
};

// Level (dBFS, linear taper — carrier/peak amplitude; also the Dirac ceiling).
static constexpr double kLevelMinDb     = -60.0;
static constexpr double kLevelMaxDb     =   0.0;
static constexpr double kLevelDefaultDb = -20.0;

// Sweep endpoints (Hz, log taper).
static constexpr double kFreqMinHz = 20.0;
static constexpr double kFreqMaxHz = 20000.0;
static constexpr double kF0DefaultHz = 20000.0;   // start: acuto
static constexpr double kF1DefaultHz = 20.0;      // end:   grave

// Grain spacing (seconds, linear).
static constexpr double kDeltaMinSec     = 0.02;
static constexpr double kDeltaMaxSec     = 2.0;
static constexpr double kDeltaDefaultSec = 0.3;

// Sweep duration (seconds, linear).
static constexpr double kTMinSec     = 2.0;
static constexpr double kTMaxSec     = 120.0;
static constexpr double kTDefaultSec = 20.0;

} // namespace Seam
```

- [ ] **Step 2: Write `version.h`**

```cpp
#pragma once
#include "pluginterfaces/base/fplatform.h"

#define MAJOR_VERSION_STR "0"
#define MAJOR_VERSION_INT 0
#define SUB_VERSION_STR "1"
#define SUB_VERSION_INT 1
#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR "1"

#define FULL_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR "." BUILD_NUMBER_STR

#define stringOriginalFilename "ltglide.vst3"
#define stringFileDescription "SEAM LTGLIDE — Linkwitz glissando shaped tone-burst generator"
#define stringCompanyName "SEAM"
#define stringCompanyWeb "https://s-e-a-m.github.io"
#define stringCompanyEmail "mailto:grammaton@me.com"
#define stringLegalCopyright "© 2026 SEAM"
#define stringLegalTrademarks "VST is a trademark of Steinberg Media Technologies GmbH"
```

- [ ] **Step 3: Write `ltglide_processor.h`**

```cpp
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "ltglide_ids.h"
#include "ltglide_dsp.h"

#include <atomic>

// FAUST REFERENCE (seam.linkwitz.lib):
//
//   sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p));
//   glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u)*win with {
//       grain = loop ~ (_,_) with { ... onset -> latch fsig ... };
//       u = fg*phase*Tg; win = (u<N)*(0.5-0.5*cos(2*ma.PI*u/N)); };  // N=5
//
// This plugin re-implements the glissando generator by hand in C++ (project
// convention — seam-ltm/CLAUDE.md). The DSP lives in the SDK-free header
// ltglide_dsp.h (SweepFreq + GlissBurst + GlideTransport); this processor owns
// the progress p, feeds SweepFreq(p) into GlissBurst, applies the dBFS Level
// gain, and emits head/tail Dirac markers, over a mono output bus.

namespace Seam {

class LTGLIDEProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LTGLIDEProcessor();
    ~LTGLIDEProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new LTGLIDEProcessor();
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
    std::atomic<double> paramLevelDb_{kLevelDefaultDb};
    std::atomic<double> paramF0Hz_{kF0DefaultHz};
    std::atomic<double> paramF1Hz_{kF1DefaultHz};
    std::atomic<int>    paramSmode_{1};                 // exponential
    std::atomic<int>    paramDmode_{1};                 // gap
    std::atomic<double> paramDeltaSec_{kDeltaDefaultSec};
    std::atomic<double> paramTSec_{kTDefaultSec};
    std::atomic<bool>   paramLoop_{false};

    // Trigger edge detection (a rising edge on kParamTrigger begins one pass).
    double prevTrigger_ = 0.0;
    std::atomic<bool> triggerPending_{false};

    // DSP.
    ltglide::GlissBurst    glide_;
    ltglide::GlideTransport transport_;
    double prevGainLin_ = 0.0;

    double computeGainLin() const;
    void   readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples);
};

} // namespace Seam
```

- [ ] **Step 4: Write `ltglide_processor.cpp` (silent scaffold)**

This step wires parameters, buses, state, factory, and a silent `processBlock`. Task 4 replaces `processBlock` with the DSP; everything else is final here.

```cpp
#include "ltglide_processor.h"
#include "ltglide_ids.h"
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

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

// Logarithmic frequency parameter (equal normalized travel = equal octaves).
class LogRangeParameter : public Steinberg::Vst::RangeParameter {
public:
    LogRangeParameter(const TChar* title, ParamID tag, const TChar* units,
                      ParamValue minPlain, ParamValue maxPlain,
                      ParamValue defaultValuePlain,
                      int32 stepCount = 0,
                      int32 flags = ParameterInfo::kCanAutomate)
        : RangeParameter(title, tag, units, minPlain, maxPlain,
                         defaultValuePlain, stepCount, flags)
    {
        const ParamValue n = LogRangeParameter::toNormalized(defaultValuePlain);
        info.defaultNormalizedValue = n;
        setNormalized(n);
    }
    ParamValue toPlain(ParamValue norm) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return lo * std::pow(hi / lo, norm);
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return std::log(plain / lo) / std::log(hi / lo);
    }
};

static inline double linNormToPlain(double v, double lo, double hi) { return v * (hi - lo) + lo; }
static inline double linPlainToNorm(double p, double lo, double hi) { return (p - lo) / (hi - lo); }
static inline double logNormToPlain(double v, double lo, double hi) { return lo * std::pow(hi / lo, v); }
static inline double logPlainToNorm(double p, double lo, double hi) { return std::log(p / lo) / std::log(hi / lo); }

LTGLIDEProcessor::LTGLIDEProcessor() {}

tresult PLUGIN_API LTGLIDEProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioOutput(STR16("Output"), SpeakerArr::kMono);

    auto* lv = new RangeParameter(STR16("Level"), kParamLevel, STR16("dBFS"),
        kLevelMinDb, kLevelMaxDb, kLevelDefaultDb, 0, ParameterInfo::kCanAutomate);
    lv->setPrecision(1);
    parameters.addParameter(lv);

    auto* f0 = new LogRangeParameter(STR16("F0"), kParamF0, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, kF0DefaultHz, 0, ParameterInfo::kCanAutomate);
    f0->setPrecision(0);
    parameters.addParameter(f0);

    auto* f1 = new LogRangeParameter(STR16("F1"), kParamF1, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, kF1DefaultHz, 0, ParameterInfo::kCanAutomate);
    f1->setPrecision(0);
    parameters.addParameter(f1);

    auto* sm = new StringListParameter(STR16("Sweep"), kParamSmode);
    sm->appendString(STR16("linear"));
    sm->appendString(STR16("exponential"));
    sm->setNormalized(1.0);   // exponential default
    parameters.addParameter(sm);

    auto* dm = new StringListParameter(STR16("Timing"), kParamDmode);
    dm->appendString(STR16("passo"));
    dm->appendString(STR16("gap"));
    dm->setNormalized(1.0);   // gap default
    parameters.addParameter(dm);

    auto* dl = new RangeParameter(STR16("Delta"), kParamDelta, STR16("s"),
        kDeltaMinSec, kDeltaMaxSec, kDeltaDefaultSec, 0, ParameterInfo::kCanAutomate);
    dl->setPrecision(2);
    parameters.addParameter(dl);

    auto* tt = new RangeParameter(STR16("Sweep Time"), kParamT, STR16("s"),
        kTMinSec, kTMaxSec, kTDefaultSec, 0, ParameterInfo::kCanAutomate);
    tt->setPrecision(1);
    parameters.addParameter(tt);

    auto* tg = new RangeParameter(STR16("Trigger"), kParamTrigger, nullptr,
        0, 1, 0, 1, ParameterInfo::kCanAutomate);
    parameters.addParameter(tg);

    auto* lp = new RangeParameter(STR16("Loop"), kParamLoop, nullptr,
        0, 1, 0, 1, ParameterInfo::kCanAutomate);
    parameters.addParameter(lp);

    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LTGLIDEProcessor::setActive(TBool state) {
    if (state) {
        glide_.setDelta(paramDeltaSec_.load());
        glide_.setDmode(paramDmode_.load());
        glide_.reset();
        transport_.setSweepSeconds(paramTSec_.load());
        transport_.setLoop(paramLoop_.load());
        prevGainLin_ = computeGainLin();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API LTGLIDEProcessor::setupProcessing(ProcessSetup& setup) {
    glide_.prepare(setup.sampleRate);
    glide_.setDelta(paramDeltaSec_.load());
    glide_.setDmode(paramDmode_.load());
    transport_.prepare(setup.sampleRate);
    transport_.setSweepSeconds(paramTSec_.load());
    transport_.setLoop(paramLoop_.load());
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API LTGLIDEProcessor::process(ProcessData& data) {
    readParameterChanges(data);
    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;

    glide_.setDelta(paramDeltaSec_.load());
    glide_.setDmode(paramDmode_.load());
    transport_.setSweepSeconds(paramTSec_.load());
    transport_.setLoop(paramLoop_.load());
    if (triggerPending_.exchange(false)) transport_.trigger();

    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32)
        processBlock<float>((float**)out, numChannels, data.numSamples);
    else
        processBlock<double>((double**)out, numChannels, data.numSamples);
    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LTGLIDEProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 0 || numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LTGLIDEProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double level = kLevelDefaultDb, f0 = kF0DefaultHz, f1 = kF1DefaultHz;
    double delta = kDeltaDefaultSec, t = kTDefaultSec;
    int32 smode = 1, dmode = 1, loop = 0;
    if (!s.readDouble(level)) return kResultFalse;
    if (!s.readDouble(f0))    return kResultFalse;
    if (!s.readDouble(f1))    return kResultFalse;
    if (!s.readInt32(smode))  return kResultFalse;
    if (!s.readInt32(dmode))  return kResultFalse;
    if (!s.readDouble(delta)) return kResultFalse;
    if (!s.readDouble(t))     return kResultFalse;
    if (!s.readInt32(loop))   return kResultFalse;

    paramLevelDb_.store(std::clamp(level, kLevelMinDb, kLevelMaxDb));
    paramF0Hz_.store(std::clamp(f0, kFreqMinHz, kFreqMaxHz));
    paramF1Hz_.store(std::clamp(f1, kFreqMinHz, kFreqMaxHz));
    paramSmode_.store((smode != 0) ? 1 : 0);
    paramDmode_.store((dmode != 0) ? 1 : 0);
    paramDeltaSec_.store(std::clamp(delta, kDeltaMinSec, kDeltaMaxSec));
    paramTSec_.store(std::clamp(t, kTMinSec, kTMaxSec));
    paramLoop_.store(loop != 0);

    if (auto* p = parameters.getParameter(kParamLevel))
        p->setNormalized(linPlainToNorm(paramLevelDb_.load(), kLevelMinDb, kLevelMaxDb));
    if (auto* p = parameters.getParameter(kParamF0))
        p->setNormalized(std::clamp(logPlainToNorm(paramF0Hz_.load(), kFreqMinHz, kFreqMaxHz), 0.0, 1.0));
    if (auto* p = parameters.getParameter(kParamF1))
        p->setNormalized(std::clamp(logPlainToNorm(paramF1Hz_.load(), kFreqMinHz, kFreqMaxHz), 0.0, 1.0));
    if (auto* p = parameters.getParameter(kParamSmode))
        p->setNormalized(paramSmode_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamDmode))
        p->setNormalized(paramDmode_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamDelta))
        p->setNormalized(linPlainToNorm(paramDeltaSec_.load(), kDeltaMinSec, kDeltaMaxSec));
    if (auto* p = parameters.getParameter(kParamT))
        p->setNormalized(linPlainToNorm(paramTSec_.load(), kTMinSec, kTMaxSec));
    if (auto* p = parameters.getParameter(kParamLoop))
        p->setNormalized(paramLoop_.load() ? 1.0 : 0.0);

    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    if (!s.writeDouble(paramLevelDb_.load())) return kResultFalse;
    if (!s.writeDouble(paramF0Hz_.load()))    return kResultFalse;
    if (!s.writeDouble(paramF1Hz_.load()))    return kResultFalse;
    if (!s.writeInt32(paramSmode_.load()))    return kResultFalse;
    if (!s.writeInt32(paramDmode_.load()))    return kResultFalse;
    if (!s.writeDouble(paramDeltaSec_.load()))return kResultFalse;
    if (!s.writeDouble(paramTSec_.load()))    return kResultFalse;
    if (!s.writeInt32(paramLoop_.load() ? 1 : 0)) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API LTGLIDEProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "ltglide.uidesc");
    return nullptr;
}

double LTGLIDEProcessor::computeGainLin() const {
    return std::pow(10.0, paramLevelDb_.load() / 20.0);
}

void LTGLIDEProcessor::readParameterChanges(ProcessData& data) {
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
            case kParamLevel: paramLevelDb_.store(std::clamp(linNormToPlain(v, kLevelMinDb, kLevelMaxDb), kLevelMinDb, kLevelMaxDb)); break;
            case kParamF0:    paramF0Hz_.store(std::clamp(logNormToPlain(v, kFreqMinHz, kFreqMaxHz), kFreqMinHz, kFreqMaxHz)); break;
            case kParamF1:    paramF1Hz_.store(std::clamp(logNormToPlain(v, kFreqMinHz, kFreqMaxHz), kFreqMinHz, kFreqMaxHz)); break;
            case kParamSmode: paramSmode_.store(v >= 0.5 ? 1 : 0); break;
            case kParamDmode: paramDmode_.store(v >= 0.5 ? 1 : 0); break;
            case kParamDelta: paramDeltaSec_.store(std::clamp(linNormToPlain(v, kDeltaMinSec, kDeltaMaxSec), kDeltaMinSec, kDeltaMaxSec)); break;
            case kParamT:     paramTSec_.store(std::clamp(linNormToPlain(v, kTMinSec, kTMaxSec), kTMinSec, kTMaxSec)); break;
            case kParamTrigger: {
                double now = v;
                if (prevTrigger_ < 0.5 && now >= 0.5) triggerPending_.store(true);
                prevTrigger_ = now;
            } break;
            case kParamLoop:  paramLoop_.store(v >= 0.5); break;
        }
    }
}

template <typename SampleType>
void LTGLIDEProcessor::processBlock(SampleType** outputs, int numChannels, int numSamples) {
    // Scaffold: silence. Task 4 replaces this body with the DSP.
    for (int s = 0; s < numSamples; ++s)
        for (int c = 0; c < numChannels; ++c)
            outputs[c][s] = (SampleType)0;
    prevGainLin_ = computeGainLin();
}

template void LTGLIDEProcessor::processBlock<float>(float**, int, int);
template void LTGLIDEProcessor::processBlock<double>(double**, int, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::LTGLIDEProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM LTGLIDE", Vst::kDistributable,
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::LTGLIDEProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 5: Write `plugins/ltglide/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-ltglide
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM LTGLIDE – Linkwitz glissando shaped tone-burst generator"
)

set(ltglide_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/ltglide_ids.h
    source/ltglide_dsp.h
    source/ltglide_processor.cpp
    source/ltglide_processor.h
    source/version.h
    resource/ltglide.uidesc
)

set(target ltglide)

smtg_add_vst3plugin(${target} ${ltglide_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/ltglide.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.ltglide"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

Note: the `.uidesc` referenced here is created in Task 5. To keep this task's build self-contained, create a minimal placeholder `plugins/ltglide/resource/ltglide.uidesc` now (Step 6) and finalise it in Task 5.

- [ ] **Step 6: Minimal placeholder `resource/ltglide.uidesc`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <template name="view" size="300, 420" background-color="~ BlackCColor" minSize="300, 420" maxSize="300, 420">
    <view class="CTextLabel" origin="0, 0" size="300, 420" title="ltglide" font-color="~ WhiteCColor"/>
  </template>
</vstgui-ui-description>
```

- [ ] **Step 7: Register in the root CMakeLists**

Add after the `add_subdirectory(plugins/ltburst)` line in the root `CMakeLists.txt`:

```cmake
add_subdirectory(plugins/ltglide)
```

- [ ] **Step 8: Build and validate**

Run:
```bash
cmake -S . -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target ltglide
./build/bin/validator "$(find build -name ltglide.vst3 -maxdepth 4 | head -1)"
```
Expected: build succeeds; validator reports all tests passed (0 failures); 9 parameters and a mono output bus present.

- [ ] **Step 9: Commit**

```bash
git add plugins/ltglide CMakeLists.txt
git commit -m "feat(ltglide): VST3 scaffold — ids, params, mono bus, silent processor"
```

---

### Task 4: Wire the DSP into the processor (audio output)

**Files:**
- Modify: `plugins/ltglide/source/ltglide_processor.cpp` (`processBlock` body only)

**Interfaces:**
- Consumes: `ltglide::SweepFreq`, `ltglide::GlissBurst::process`, `ltglide::GlideTransport::process`/`Tick`/`Kind` (Tasks 1–2); `computeGainLin()` (Task 3).
- Produces: audible output — the transport drives a Dirac-bracketed glissando; `processBlock` maps each `Tick` to a sample.

- [ ] **Step 1: Replace the `processBlock` body**

Replace the scaffold `processBlock` in `ltglide_processor.cpp` with:

```cpp
template <typename SampleType>
void LTGLIDEProcessor::processBlock(SampleType** outputs, int numChannels, int numSamples) {
    const double targetGain = computeGainLin();
    const double startGain  = prevGainLin_;
    const double gainStep   = (numSamples > 0) ? (targetGain - startGain) / numSamples : 0.0;

    const double f0 = paramF0Hz_.load();
    const double f1 = paramF1Hz_.load();
    const int    sm = paramSmode_.load();

    for (int s = 0; s < numSamples; ++s) {
        const double g = startGain + gainStep * s;      // per-block gain ramp
        double y = 0.0;
        auto tick = transport_.process();
        switch (tick.kind) {
            case ltglide::GlideTransport::Kind::Dirac:
                y = 1.0;                                 // unit impulse (scaled by g)
                break;
            case ltglide::GlideTransport::Kind::Glide: {
                const double f = ltglide::SweepFreq(f0, f1, sm, tick.p);
                y = glide_.process(f);
                break;
            }
            case ltglide::GlideTransport::Kind::Silence:
            default:
                y = 0.0;
                break;
        }
        const SampleType out = (SampleType)(y * g);
        for (int c = 0; c < numChannels; ++c)
            outputs[c][s] = out;
    }
    prevGainLin_ = targetGain;
}
```

Note on determinism: `GlideTransport` enters `Glide` fresh each pass, but `GlissBurst`'s feedback state persists. Reset it at each pass so every pass is identical. Add, at the top of `Kind::Glide` handling, a reset on the first glide sample of a pass — detect it via `tick.p == 0.0`:

```cpp
            case ltglide::GlideTransport::Kind::Glide: {
                if (tick.p == 0.0) glide_.reset();       // deterministic per-pass start
                const double f = ltglide::SweepFreq(f0, f1, sm, tick.p);
                y = glide_.process(f);
                break;
            }
```

(Use this second form; it supersedes the first `Kind::Glide` block above.)

- [ ] **Step 2: Build**

Run:
```bash
cmake --build build --target ltglide
```
Expected: builds with no warnings/errors.

- [ ] **Step 3: Validate and sanity-check output**

Run:
```bash
./build/bin/validator "$(find build -name ltglide.vst3 -maxdepth 4 | head -1)"
```
Expected: all validator tests pass. The "silence flags"/process tests confirm the plugin produces finite output.

- [ ] **Step 4: Commit**

```bash
git add plugins/ltglide/source/ltglide_processor.cpp
git commit -m "feat(ltglide): drive glissando + Dirac markers from the transport"
```

---

### Task 5: Finished VSTGUI editor

**Files:**
- Modify: `plugins/ltglide/resource/ltglide.uidesc` (replace placeholder with the finished editor)

**Interfaces:**
- Consumes: parameter tags 100–108 (Task 3) — every control's `control-tag` must match a `ParamID`.
- Produces: a finished, non-editable GUI (logo + controls + Trigger button + Loop toggle).

- [ ] **Step 1: Write the finished `ltglide.uidesc`**

Model the structure on `plugins/ltburst/resource/ltglide.uidesc`'s sibling (`ltburst.uidesc`): native-size logo (`CView` draws bitmaps at native size), `SourceCodePro-Light` font, one row per parameter with a `CTextLabel` + a control and a value read-out. Define `control-tag`s bound to the `ParamID`s:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <bitmaps>
    <bitmap name="seam_logo" path="seam_logo.png"/>
  </bitmaps>
  <fonts>
    <font name="scp" font-name="Source Code Pro Light" size="12"/>
  </fonts>
  <control-tags>
    <control-tag name="Level"   tag="100"/>
    <control-tag name="F0"      tag="101"/>
    <control-tag name="F1"      tag="102"/>
    <control-tag name="Sweep"   tag="103"/>
    <control-tag name="Timing"  tag="104"/>
    <control-tag name="Delta"   tag="105"/>
    <control-tag name="Time"    tag="106"/>
    <control-tag name="Trigger" tag="107"/>
    <control-tag name="Loop"    tag="108"/>
  </control-tags>
  <template name="view" size="300, 460" background-color="~ BlackCColor" minSize="300, 460" maxSize="300, 460">
    <view class="CView" origin="30, 16" size="240, 77" bitmap="seam_logo"/>
    <!-- Level -->
    <view class="CTextLabel" origin="20, 110" size="90, 20" title="Level"  font="scp" font-color="~ WhiteCColor"/>
    <view class="CSlider" control-tag="Level" origin="110, 110" size="130, 20"/>
    <view class="CTextEdit" control-tag="Level" origin="245, 110" size="45, 20" font="scp" value-precision="1"/>
    <!-- F0 -->
    <view class="CTextLabel" origin="20, 140" size="90, 20" title="F0"     font="scp" font-color="~ WhiteCColor"/>
    <view class="CSlider" control-tag="F0" origin="110, 140" size="130, 20"/>
    <view class="CTextEdit" control-tag="F0" origin="245, 140" size="45, 20" font="scp" value-precision="0"/>
    <!-- F1 -->
    <view class="CTextLabel" origin="20, 170" size="90, 20" title="F1"     font="scp" font-color="~ WhiteCColor"/>
    <view class="CSlider" control-tag="F1" origin="110, 170" size="130, 20"/>
    <view class="CTextEdit" control-tag="F1" origin="245, 170" size="45, 20" font="scp" value-precision="0"/>
    <!-- Sweep shape -->
    <view class="CTextLabel" origin="20, 200" size="90, 20" title="Sweep"  font="scp" font-color="~ WhiteCColor"/>
    <view class="COptionMenu" control-tag="Sweep" origin="110, 200" size="180, 20" font="scp"/>
    <!-- Timing -->
    <view class="CTextLabel" origin="20, 230" size="90, 20" title="Timing" font="scp" font-color="~ WhiteCColor"/>
    <view class="COptionMenu" control-tag="Timing" origin="110, 230" size="180, 20" font="scp"/>
    <!-- Delta -->
    <view class="CTextLabel" origin="20, 260" size="90, 20" title="Delta"  font="scp" font-color="~ WhiteCColor"/>
    <view class="CSlider" control-tag="Delta" origin="110, 260" size="130, 20"/>
    <view class="CTextEdit" control-tag="Delta" origin="245, 260" size="45, 20" font="scp" value-precision="2"/>
    <!-- Sweep time -->
    <view class="CTextLabel" origin="20, 290" size="90, 20" title="Time"   font="scp" font-color="~ WhiteCColor"/>
    <view class="CSlider" control-tag="Time" origin="110, 290" size="130, 20"/>
    <view class="CTextEdit" control-tag="Time" origin="245, 290" size="45, 20" font="scp" value-precision="1"/>
    <!-- Transport -->
    <view class="COnOffButton" control-tag="Trigger" origin="20, 330" size="120, 26" title="Trigger"/>
    <view class="COnOffButton" control-tag="Loop"    origin="160, 330" size="120, 26" title="Loop"/>
  </template>
</vstgui-ui-description>
```

- [ ] **Step 2: Build and validate**

Run:
```bash
cmake --build build --target ltglide
./build/bin/validator "$(find build -name ltglide.vst3 -maxdepth 4 | head -1)"
```
Expected: build succeeds; validator passes; every `control-tag` resolves to a registered parameter (100–108).

- [ ] **Step 3: Commit**

```bash
git add plugins/ltglide/resource/ltglide.uidesc
git commit -m "feat(ltglide): finished VSTGUI editor (sweep, timing, transport)"
```

---

### Task 6: Study diary §6 + validation note

**Files:**
- Create: `plugins/ltglide/doc/ltglide-validation.md`
- Create/extend: the Italian study diary. `ltglide` has no diary yet; create `plugins/ltglide/doc/study/ltglide-study.tex` OR add a section to the existing `plugins/ltburst/doc/study/ltburst-study.tex`. Since `ltglide` is a distinct object, create its own minimal diary file with one section on the port.

**Interfaces:**
- Consumes: the implemented DSP (Tasks 1–2) and transport for the narrative.
- Produces: documentation only (no code).

- [ ] **Step 1: Write `plugins/ltglide/doc/ltglide-validation.md`**

Document, in English (per CLAUDE.md — only the study diary is Italian):
- the doctest coverage (SweepFreq endpoints, GlissBurst onset spacing = N/f+delta, sample-and-hold latching, amplitude bound, NaN sweep; GlideTransport timeline and loop);
- the validator result (all tests passed, 9 params, mono bus);
- the constant-frequency parity anchor: gap mode at f=1000, delta=0.3 gives onset spacing 14640 samples = the ltburst fixed-burst period (P=305 cycles), tying GlissBurst back to the shipped `ShapedBurst`.

Include the exact commands to reproduce:
```bash
cmake -S . -B build -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target ltglide_dsp_test ltglide
./build/tests/ltglide_dsp_test
./build/bin/validator "$(find build -name ltglide.vst3 -maxdepth 4 | head -1)"
```

- [ ] **Step 2: Write the Italian study diary section**

Create `plugins/ltglide/doc/study/ltglide-study.tex` (one sentence per line; affirmative voice). Cover:
- il glissato come generalizzazione del burst fisso: uno sweep esterno alimenta un sample-and-hold per grana, così ogni burst resta a frequenza singola;
- il motore a grana retriggerata (Approach A): il ramp azzera a ogni onset, `fg` latcha `fsig` all'onset, carrier e finestra derivano dal ramp alla frequenza latchata;
- le due modalità di timing (passo onset-fisso / gap gap-fisso) e la guardia `max(20, fg)`;
- il transport (Dirac testa/coda, lead/tail 5 s, loop + trigger) e perché il loop serve all'averaging del ricevitore (fase 3b-ii);
- rimanda misura/spettri/analisi alla fase ricevitore.

The `.tex` need not build a PDF in this task (no `make` gate); if a `Makefile` is copied from `ltburst/doc/study`, ensure `make` succeeds, otherwise leave the `.tex` as source.

- [ ] **Step 3: Commit**

```bash
git add plugins/ltglide/doc
git commit -m "docs(ltglide): validation note + Italian study diary (grain engine + transport)"
```

---

## Self-Review

**Spec coverage:**
- Standalone operative generator → Tasks 3–4 (silent scaffold + DSP wiring). ✓
- No peer-aware sync / receiver → explicitly out of scope; not implemented. ✓
- New plugin, not a mode of ltburst → Task 3 (own ids/FUID/CMake); ltburst untouched. ✓
- No routing / stone index → mono bus only, no routing params. ✓
- Shared DSP core → DEVIATION: ltglide consumes none of ShapedBurst, so no shared header is factored (YAGNI). Flagged to the user at handoff. ✓ (documented)
- SweepFreq + GlissBurst (passo/gap, sample-and-hold) → Task 1. ✓
- Transport (loop + manual trigger), Dirac head/tail ±5 s, p owned by transport → Task 2 + Task 4. ✓
- Parameters Level/f0/f1/smode/dmode/delta/t, N fixed 5 → Task 3 ids + Task 5 GUI. ✓
- GUI consistent with ltburst → Task 5. ✓
- doc/study §6 (Italian) + doc/math deferred to end of Phase 3 → Task 6 (study + validation; math doc deferred). ✓
- Verification gates (doctest parity, ltburst still passes, validator, recorded structure, mono) → doctest Tasks 1–2, validator Tasks 3–5; ltburst untouched so its doctest is unaffected; recorded-structure check is manual (noted). ✓

**Placeholder scan:** The Task 3 `.uidesc` is an intentional minimal placeholder, finalised in Task 5 — this is a staged deliverable, not an unfilled TODO. No other placeholders.

**Type consistency:** `SweepFreq(double,double,int,double)`, `GlissBurst::process(double)`, `GlissBurst::heldFrequency()/grainPhase()`, `GlideTransport::Tick{Kind,double}` and `GlideTransport::process()` are used identically across Tasks 1, 2, 4. ParamIDs 100–108 match between `ltglide_ids.h` (Task 3), `readParameterChanges`/`setState` (Task 3), and the `.uidesc` control-tags (Task 5).

## Notes for the implementer

- The `RangeParameter` momentary/toggle pattern (stepCount 1) for Trigger/Loop mirrors on/off controls; the processor edge-detects Trigger in `readParameterChanges` and defers `transport_.trigger()` to the audio thread via `triggerPending_`.
- `StringListParameter` for Sweep/Timing renders named choices ("linear/exponential", "passo/gap") and normalises to 0/1, read as `int` in the processor.
- Keep `M_PI` (suite-consistent with ltburst/hilbert); if a Windows/MSVC build is ever added, introduce a named `2π` constant.
- The `find … ltglide.vst3` command locates the built bundle regardless of the generator's output layout; adjust `-maxdepth` if the tree is deeper.
