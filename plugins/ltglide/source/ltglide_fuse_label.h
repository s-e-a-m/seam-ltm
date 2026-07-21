//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ltglide — step-fuse threshold label.
//
// In step timing mode the onset stride is max(delta, N/f) (GlissBurst::
// periodSec(), ltglide_dsp.h). Below f* = N/delta = 5/delta Hz the delta
// floor stops binding (N/f exceeds it) and the grain train fuses: separated
// pips become a continuous micro-stepped glissando (measured: with
// delta=0.02 the morphology visibly changes around f~=250 Hz, matching
// 5/0.02 = 250). Gap mode has no such floor (its period is always N/f+delta,
// so grains never touch), hence this label only makes sense -- and is only
// drawn -- in step mode. See the "Step-fuse threshold" subsection in
// plugins/ltglide/doc/ltglide-validation.md for the full derivation.
//
// Polls the processor's Delta/Timing parameters on a CVSTGUITimer (same
// read-only GUI-thread pattern as strx's status/meter views and the SHOT
// button this view replaces): the timer just invalidates, draw() recomputes
// f* fresh on every paint.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include "ltglide_processor.h"

#include <cstdio>

namespace Seam {

class LtglideFuseLabel : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 250;   // text, not animation

    LtglideFuseLabel(const VSTGUI::CRect& size, LTGLIDEProcessor* processor,
                      VSTGUI::CFontRef font, const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), processor_(processor),
          font_(font), textColor_(textColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, kTimerMs, /*doStart*/true);
    }

    ~LtglideFuseLabel() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        VSTGUI::CView::draw(c);
        if (!processor_ || processor_->dmode() != 0) {    // 0 = step, 1 = gap
            setDirty(false);
            return;                                       // gap mode: draw nothing
        }
        c->setDrawMode(VSTGUI::kAntiAliasing);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        const double delta = processor_->deltaSec();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "STEP FUSES < %.0f Hz",
                      (delta > 0.0) ? ((double)Seam::ltglide::GlissBurst::kN / delta) : 0.0);
        c->drawString(buf, getViewSize(), VSTGUI::kCenterText);
        setDirty(false);
    }

private:
    LTGLIDEProcessor*      processor_ = nullptr;
    VSTGUI::CVSTGUITimer*  timer_ = nullptr;
    VSTGUI::CFontRef       font_ = nullptr;
    VSTGUI::CColor         textColor_;
};

} // namespace Seam
