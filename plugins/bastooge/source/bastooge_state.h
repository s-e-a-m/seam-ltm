//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — the parameters between the threads, and the state
//
// Three plain values in atomics, as in abmodulex: process() stores what the
// host's queue brings, setState stores a recalled preset from the UI thread.
// On disk they are three little-endian doubles, [cutHz, test, bypass], under
// the suite's append-only contract (seam_state.h): a short blob keeps the
// defaults for the fields it lacks.
//
// Every value is clamped on the way in, from automation and from state
// alike. The Engine indexes a table by the Test, so a test outside 0..4
// must never reach it: it becomes Normal, the safe reading of a blob that
// makes no sense. A cut outside 100..250 Hz is clamped to the range; a NaN
// anywhere restores that field's default.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "bastooge_dsp.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace Seam::bastooge {

constexpr double kCutMin = st::kMk2gCutMin;          // 100 Hz
constexpr double kCutMax = st::kMk2gCutMax;          // 250 Hz
constexpr double kCutDefault = st::kMk2gCutDefault;  // 160 Hz
constexpr int kTestSteps = 4;                        // five entries: Normal, W, X, Y, Z

// The cut slider: equal travel, equal ratio. fc = 100 * 2.5^n.
inline double cutFromNormalized(double n) {
    if (!(n == n)) return kCutDefault;
    n = std::max(0.0, std::min(1.0, n));
    return kCutMin * std::pow(kCutMax / kCutMin, n);
}
inline double cutToNormalized(double hz) {
    if (!(hz == hz)) hz = kCutDefault;
    hz = std::max(kCutMin, std::min(kCutMax, hz));
    return std::log(hz / kCutMin) / std::log(kCutMax / kCutMin);
}
inline double clampCut(double hz) {
    if (!(hz == hz)) return kCutDefault;
    return std::max(kCutMin, std::min(kCutMax, hz));
}

// A list index (0..4) to a Test. Anything else, NaN included, is Normal.
inline Test testFromIndex(double v) {
    if (!(v >= -0.5 && v < kTestSteps + 0.5)) return Test::Normal;
    return (Test)(int)std::lround(v);
}
// The SDK's own reading of a list parameter (futils.h FromNormalized, used by
// StringListParameter and the COptionMenu): equal bins of 1/5, the last one
// closed at 1. Rounding n * 4 instead would show one entry and run another
// for off-grid automation (0.15 shows Normal but would run W).
inline Test testFromNormalized(double n) {
    if (!(n >= 0.0 && n <= 1.0)) return Test::Normal;
    return (Test)std::min(kTestSteps, (int)(n * (kTestSteps + 1)));
}
inline double testToNormalized(Test t) { return (double)(int)t / kTestSteps; }

class ParamBox {
public:
    void storeCut(double hz)     { cut_.store(clampCut(hz)); }
    void storeTest(Test t)       { test_.store((int)testFromIndex((double)(int)t)); }
    void storeBypass(bool on)    { bypass_.store(on); }

    double cut() const  { return cut_.load(); }
    Test   test() const { return (Test)test_.load(); }
    bool   bypass() const { return bypass_.load(); }

private:
    std::atomic<double> cut_{kCutDefault};
    std::atomic<int>    test_{(int)Test::Normal};
    std::atomic<bool>   bypass_{false};
};

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    s.writeDouble(box.cut());
    s.writeDouble((double)(int)box.test());
    s.writeDouble(box.bypass() ? 1.0 : 0.0);
}

// Returns how many fields the blob held; a missing field is stored as its default.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[3] = {kCutDefault, (double)(int)Test::Normal, 0.0};
    const int n = Seam::readStateDoubles(state, v, 3);
    box.storeCut(v[0]);
    box.storeTest(testFromIndex(v[1]));
    box.storeBypass(v[2] >= 0.5);   // a NaN compares false: off
    return n;
}

}  // namespace Seam::bastooge
