//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — the FOOTER readout's data and copy (no VSTGUI)
//
// Three rows, so that a student sees the bell follow the cut:
//
//   W BELL 263 Hz · Q 0.88 · -4.0 dB
//   CUT 160 Hz
//   NORMAL  |  TEST W  |  TEST X · BYPASS · DIPOLE WITHOUT HIGH-PASS
//
// The numbers are the engine's, not the parameter's: the audio thread
// publishes Engine::cut() and Engine::bell() (the smoothed cut and the bell
// designed from it) after every block, so during a 20 ms glide the readout
// shows the cut actually in force, and the GUI thread never recomputes the
// table. Each field is a separate atomic, so a read can mix two consecutive
// blocks during a glide; the next idle repaints a consistent set, which is
// all a readout needs. The view is bastooge_readout_view.h; this header is
// apart so that the copy is testable without the SDK.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "bastooge_dsp.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace Seam::bastooge {

// Written by the audio thread (and by setActive), read by the GUI thread.
struct Readout {
    std::atomic<double> cutHz{st::kMk2gCutDefault};
    std::atomic<double> f0Hz{0.0};
    std::atomic<double> q{0.0};
    std::atomic<double> gainDb{0.0};
    std::atomic<int>    test{(int)Test::Normal};
    std::atomic<bool>   bypass{false};

    void publish(const Engine& e, Test t, bool by) {
        const st::Bell b = e.bell();
        cutHz.store(e.cut(), std::memory_order_relaxed);
        f0Hz.store(b.f0, std::memory_order_relaxed);
        q.store(b.q, std::memory_order_relaxed);
        gainDb.store(b.gainDb, std::memory_order_relaxed);
        test.store((int)t, std::memory_order_relaxed);
        bypass.store(by, std::memory_order_relaxed);
    }
};

// The three rows of the readout, as text (UTF-8; \xC2\xB7 is the middle dot).
inline void formatReadout(double cutHz, double f0, double q, double gainDb,
                          int test, bool bypass,
                          char* row1, std::size_t n1,
                          char* row2, std::size_t n2,
                          char* row3, std::size_t n3) {
    std::snprintf(row1, n1, "W BELL %ld Hz \xC2\xB7 Q %.2f \xC2\xB7 %+.1f dB",
                  std::lround(f0), q, gainDb);
    std::snprintf(row2, n2, "CUT %ld Hz", std::lround(cutHz));
    static const char* kNames[5] = {"NORMAL", "TEST W", "TEST X", "TEST Y", "TEST Z"};
    const char* name = (test >= 0 && test <= 4) ? kNames[test] : kNames[0];
    const bool dipole = test >= (int)Test::X && test <= (int)Test::Z;
    if (!bypass)
        std::snprintf(row3, n3, "%s", name);
    else if (dipole)   // the cones take the bass unloaded
        std::snprintf(row3, n3, "%s \xC2\xB7 BYPASS \xC2\xB7 DIPOLE WITHOUT HIGH-PASS", name);
    else
        std::snprintf(row3, n3, "%s \xC2\xB7 BYPASS", name);
}

}  // namespace Seam::bastooge
