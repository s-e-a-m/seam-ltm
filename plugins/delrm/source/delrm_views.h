//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the footer view (VSTGUI)
//
// One line, written by delrm_footer.h: D in milliseconds, in samples and the rate, read
// from the engine's atomics by a GUI timer, as stunedrev's centroids. The
// prime changing with the rate is visible here.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
#include "delrm_footer.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include <cstdio>

namespace Seam {

class DelrmFooter : public VSTGUI::CView {
public:
    DelrmFooter(const VSTGUI::CRect& size, const delrm::Engine* engine,
                VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 200, true);
    }
    ~DelrmFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char line[128];
        delrm::formatDelayLine(line, sizeof line, engine_->delaySamples(), engine_->sampleRate());
        c->setFont(font_);
        c->setFontColor(color_);
        c->drawString(line, getViewSize(), kLeftText);
        setDirty(false);
    }

private:
    const delrm::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
