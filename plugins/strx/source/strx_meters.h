#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "strx_processor.h"
#include "seam_meter.h"

#include <algorithm>
#include <cstdio>

// FAUST REFERENCE (seam.analyzers.lib / seam.stereophony.lib): renders the
// san.correlation/width scalars and the sst.sdmx M/S levels that
// Seam::strx::Analyzer already computes (see strx_dsp.h). This view adds no
// DSP of its own — it is a read-only render of AnalysisFrame, reusing
// seam::meter::db2norm (seam_meter.h) for the dB->[0,1] bar-height mapping,
// the same normalization dslar's r/g meters use.

namespace Seam {

// First of strx's three custom views (Task 7 of 9): five vertical bars —
// In L, In R, M, S (dBFS, floored at kFloorDb) and Width (frame.width,
// 0 = mono at the bar's bottom to 1 = fully decorrelated at the top). The
// Width bar's fill tints "inv" (anti-phase, frame.correlation < 0) instead
// of the normal fill color.
//
// Polls StrxProcessor::latestFrame() — the GUI-thread cache described in
// strx_processor.h — on its own ~30 Hz CVSTGUITimer. Tasks 8/9 add sibling
// views (goniometer, spectrum) that poll the same cache independently, so
// each view owns its own timer/paint cadence without starving the others.
class StrxMeters : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 33;    // ~30 Hz repaint
    static constexpr double   kFloorDb = -60.0; // matches AnalysisFrame's floor
    static constexpr double   kMonoWidthEps = 0.02; // "mono" label threshold

    StrxMeters(const VSTGUI::CRect& size, StrxProcessor* processor,
               VSTGUI::CFontRef font,
               const VSTGUI::CColor& labelColor, const VSTGUI::CColor& textColor,
               const VSTGUI::CColor& trackColor, const VSTGUI::CColor& fillColor,
               const VSTGUI::CColor& invColor)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          labelColor_(labelColor), textColor_(textColor),
          trackColor_(trackColor), fillColor_(fillColor), invColor_(invColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, kTimerMs, /*doStart*/true);
    }

    ~StrxMeters() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        static const Seam::strx::AnalysisFrame kEmptyFrame{};
        const Seam::strx::AnalysisFrame& frame =
            processor_ ? processor_->latestFrame() : kEmptyFrame;

        const CRect r = getViewSize();
        if (font_) c->setFont(font_);
        c->setDrawMode(kAntiAliasing);

        static constexpr int kNumBars = 5;
        static const char* kLabels[kNumBars] = { "L", "R", "M", "S", "W" };
        const float dbValues[4] = { frame.inL, frame.inR, frame.mid, frame.side };

        const CCoord labelH  = 14.0;  // top row: bar name
        const CCoord valueH  = 12.0;  // bottom row: dB / mono-wide-inv readout
        const CCoord gap     = 6.0;
        const CCoord top     = r.top + labelH;
        const CCoord bottom  = r.bottom - valueH;
        const CCoord barAreaH = std::max(0.0, bottom - top);
        const CCoord barW = std::max(1.0, (r.getWidth() - gap * (kNumBars + 1)) / kNumBars);

        for (int i = 0; i < kNumBars; ++i) {
            const CCoord x0 = r.left + gap + i * (barW + gap);
            const CRect track(x0, top, x0 + barW, bottom);

            // Track background.
            c->setFillColor(trackColor_);
            c->drawRect(track, kDrawFilled);

            double norm;
            bool invTint = false;
            char valStr[16];
            if (i < 4) {
                norm = seam::meter::db2norm(dbValues[i], kFloorDb);
                std::snprintf(valStr, sizeof valStr, "%.1f", dbValues[i]);
            } else {
                norm = frame.width; // already [0,1], 0 = mono
                invTint = frame.correlation < 0.f;
                std::snprintf(valStr, sizeof valStr, "%s",
                    frame.width < kMonoWidthEps ? "mono" : (invTint ? "inv" : "wide"));
            }
            norm = norm < 0.0 ? 0.0 : (norm > 1.0 ? 1.0 : norm);

            const CCoord fillH = barAreaH * norm;
            if (fillH > 0.0) {
                const CRect fill(x0, bottom - fillH, x0 + barW, bottom);
                c->setFillColor(invTint ? invColor_ : fillColor_);
                c->drawRect(fill, kDrawFilled);
            }

            // Frame outline over the fill.
            c->setFrameColor(labelColor_);
            c->setLineWidth(1.0);
            c->drawRect(track, kDrawStroked);

            // Label above, value below.
            c->setFontColor(textColor_);
            c->drawString(kLabels[i], CRect(x0, r.top, x0 + barW, top), kCenterText);
            c->drawString(valStr, CRect(x0, bottom, x0 + barW, bottom + valueH), kCenterText);
        }
        setDirty(false);
    }

private:
    StrxProcessor* processor_ = nullptr;
    VSTGUI::CFontRef font_ = nullptr;
    VSTGUI::CColor labelColor_, textColor_, trackColor_, fillColor_, invColor_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
};

} // namespace Seam
