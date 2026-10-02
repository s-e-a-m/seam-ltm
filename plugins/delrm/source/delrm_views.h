//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the footer view (VSTGUI)
//
// One line: D in milliseconds and in samples at the session's rate, read
// from the engine's atomics by a GUI timer, as stunedrev's centroids. The
// prime changing with the rate is visible here.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
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
        const double fs = engine_->sampleRate();
        if (fs > 0.0) {
            const unsigned d = engine_->delaySamples();
            std::snprintf(line, sizeof line, "D  %.2f ms \xC2\xB7 %u samples @ %.1f kHz",
                          1000.0 * d / fs, d, fs / 1000.0);
        } else {
            std::snprintf(line, sizeof line, "D  \xE2\x80\x94  inactive");
        }
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
