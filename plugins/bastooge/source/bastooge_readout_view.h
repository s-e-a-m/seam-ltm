//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — the FOOTER readout view
//
// Draws the three rows of bastooge_readout.h (the filters in force and the
// mode), centred, in InfoFont/TextLight. The path is the x2uhj readout's: the
// view holds a pointer to the Readout the processor owns, which the audio
// thread fills with the engine's cut() and bell(), and polls it on idle.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "bastooge_readout.h"

#include <cmath>

namespace Seam::bastooge {

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
        char rows[3][96];
        formatReadout(shown_.cut, shown_.f0, shown_.q, shown_.g, shown_.test, shown_.bypass,
                      rows[0], sizeof rows[0], rows[1], sizeof rows[1], rows[2], sizeof rows[2]);
        if (font_) c->setFont(font_);
        c->setFontColor(color_);
        const CRect r = getViewSize();
        const CCoord rowH = 16.0;
        for (int i = 0; i < 3; ++i)
            c->drawString(rows[i], CRect(r.left, r.top + i * rowH, r.right, r.top + (i + 1) * rowH),
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
