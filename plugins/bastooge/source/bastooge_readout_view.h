//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — the FOOTER readout: the filters in force, the mode
//
// Two lines, so that a student sees the bell follow the cut:
//
//   W BELL 263 Hz · Q 0.88 · -4.0 dB    CUT 160 Hz
//   NORMAL  |  TEST W  |  TEST X · BYPASS · DIPOLE WITHOUT HIGH-PASS
//
// The numbers are the engine's, not the parameter's: the audio thread
// publishes Engine::cut() and Engine::bell() (the smoothed cut and the bell
// designed from it) after every block, so during a 20 ms glide the readout
// shows the cut actually in force, and the GUI thread never recomputes the
// table. The path is the x2uhj readout's: the view holds a pointer to state
// the processor owns and polls it on idle. Here each field is a separate
// atomic, so a read can mix two consecutive blocks during a glide; the next
// idle repaints a consistent pair, which is all a readout needs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "bastooge_dsp.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

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

// The two lines of the readout, as text. Free so that the copy is one
// function, whatever draws it.
inline void formatReadout(double cutHz, double f0, double q, double gainDb,
                          int test, bool bypass,
                          char* line1, size_t n1, char* line2, size_t n2) {
    std::snprintf(line1, n1, "W BELL %ld Hz \xC2\xB7 Q %.2f \xC2\xB7 %+.1f dB    CUT %ld Hz",
                  std::lround(f0), q, gainDb, std::lround(cutHz));
    static const char* kNames[5] = {"NORMAL", "TEST W", "TEST X", "TEST Y", "TEST Z"};
    const char* name = (test >= 0 && test <= 4) ? kNames[test] : kNames[0];
    const bool dipole = test >= (int)Test::X && test <= (int)Test::Z;
    if (!bypass)
        std::snprintf(line2, n2, "%s", name);
    else if (dipole)
        std::snprintf(line2, n2, "%s \xC2\xB7 BYPASS \xC2\xB7 DIPOLE WITHOUT HIGH-PASS", name);
    else
        std::snprintf(line2, n2, "%s \xC2\xB7 BYPASS", name);
}

class ReadoutView : public VSTGUI::CView {
public:
    ReadoutView(const VSTGUI::CRect& size, const Readout* r,
                VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : VSTGUI::CView(size), readout_(r), font_(font), color_(color) {
        if (font_) font_->remember();
        setWantsIdle(true);
    }
    ~ReadoutView() override { if (font_) font_->forget(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        if (!readout_) return;
        shown_ = snapshot();
        char l1[96], l2[96];
        formatReadout(shown_.cut, shown_.f0, shown_.q, shown_.g, shown_.test, shown_.bypass,
                      l1, sizeof l1, l2, sizeof l2);
        if (font_) c->setFont(font_);
        c->setFontColor(color_);
        const CRect r = getViewSize();
        const CCoord rowH = 14.0;
        c->drawString(l1, CRect(r.left, r.top, r.right, r.top + rowH), kCenterText);
        c->drawString(l2, CRect(r.left, r.top + rowH + 4.0, r.right, r.top + 2.0 * rowH + 4.0),
                      kCenterText);
        setDirty(false);
    }

    void onIdle() override {
        if (readout_ && !(snapshot() == shown_)) invalid();
    }

private:
    struct Snap {
        double cut = -1.0, f0 = 0.0, q = 0.0, g = 0.0;
        int test = -1;
        bool bypass = false;
        // Compared as the readout prints them, so a glide's last 1e-4 Hz
        // does not repaint text that does not change.
        bool operator==(const Snap& o) const {
            return std::lround(cut) == std::lround(o.cut) && std::lround(f0) == std::lround(o.f0) &&
                   std::lround(q * 100.0) == std::lround(o.q * 100.0) &&
                   std::lround(g * 10.0) == std::lround(o.g * 10.0) &&
                   test == o.test && bypass == o.bypass;
        }
    };
    Snap snapshot() const {
        Snap s;
        s.cut = readout_->cutHz.load(std::memory_order_relaxed);
        s.f0 = readout_->f0Hz.load(std::memory_order_relaxed);
        s.q = readout_->q.load(std::memory_order_relaxed);
        s.g = readout_->gainDb.load(std::memory_order_relaxed);
        s.test = readout_->test.load(std::memory_order_relaxed);
        s.bypass = readout_->bypass.load(std::memory_order_relaxed);
        return s;
    }

    const Readout* readout_;
    VSTGUI::CFontRef font_;
    VSTGUI::CColor color_;
    Snap shown_;
};

}  // namespace Seam::bastooge
