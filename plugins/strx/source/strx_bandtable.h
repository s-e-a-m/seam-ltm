#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "strx_processor.h"
#include "strx_spectrum.h"
#include "strx_bands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// FAUST REFERENCE (seam.analyzers.lib): renders AnalysisFrame::bandComp, which
// Seam::strx::BandLevels computes as san.thirdoctave_levels_ab : san.pinkcomp
// (see strx_bands.h). This view adds no DSP of its own.

namespace Seam {

// The pink-measurement read-out: one bar per ISO 266 third-octave band, giving
// the dB to dial into the power amplifier. Shares StrxSpectrum's axis
// constants rather than repeating them, so the two views cannot drift apart —
// a band's bar sits under the same x as its line in the curve above.
class StrxBandTable : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // 10 Hz: the bands are slow

    static constexpr double kRangeDb  = 12.0;   // half-scale, top and bottom
    static constexpr double kLabelDb  = 2.0;    // print the value past this
    static constexpr double kLeftMargin  = StrxSpectrum::kLeftMargin;
    static constexpr double kRightMargin = StrxSpectrum::kRightMargin;
    static constexpr double kTopMargin   = 4.0;
    static constexpr double kBarsHeight  = 96.0;
    static constexpr double kValueRow    = 14.0;   // per-octave numbers
    static constexpr double kLabelRow    = 14.0;   // per-octave frequencies

    StrxBandTable(const VSTGUI::CRect& size, StrxProcessor* processor,
                  VSTGUI::CFontRef font,
                  const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                  const VSTGUI::CColor& barColor)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          structureColor_(structureColor), textColor_(textColor), barColor_(barColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                if (processor_) {
                    const auto& d = processor_->calbusWatch().poll();
                    // A sweep is not a pink field: the table is meaningless
                    // while ltglide is running, and says so rather than
                    // drawing numbers nobody should act on.
                    live_ = d.available && d.firstActive >= 0 && !d.glide;
                }
                invalid();
            }, kTimerMs, /*doStart*/true);
    }

    ~StrxBandTable() override {
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
        c->setDrawMode(kAntiAliasing);
        if (font_) c->setFont(font_);

        const CRect bars(r.left + kLeftMargin, r.top + kTopMargin,
                          r.right - kRightMargin, r.top + kTopMargin + kBarsHeight);
        const double zeroY = (bars.top + bars.bottom) * 0.5;
        const double pxPerDb = (bars.getHeight() * 0.5) / kRangeDb;

        static const double kLogFMin = std::log10(StrxSpectrum::kFMin);
        static const double kLogFMax = std::log10(StrxSpectrum::kFMax);
        const double logSpan = kLogFMax - kLogFMin;
        auto xForFreq = [&](double f) -> CCoord {
            return bars.left + (std::log10(f) - kLogFMin) / logSpan * bars.getWidth();
        };
        auto yForDb = [&](double db) -> CCoord {
            return zeroY - std::clamp(db, -kRangeDb, kRangeDb) * pxPerDb;
        };

        // --- Scale: zero line solid, +/-6 and +/-12 as faint guides. ---
        // Same brightness ladder as the spectrum grid above (Structure with
        // tiered alpha), so the two panes read as one instrument.
        auto guide = [&](double db, uint8_t alpha) {
            CColor col = structureColor_; col.alpha = alpha;
            c->setFrameColor(col);
            c->setLineWidth(1.0);
            const CCoord y = yForDb(db);
            c->drawLine(CPoint(bars.left, y), CPoint(bars.right, y));
            char buf[8];
            std::snprintf(buf, sizeof buf, "%+.0f", db);
            c->setFontColor(textColor_);
            c->drawString(buf, CRect(r.left, y - 6, bars.left - 3, y + 6), kRightText);
        };
        guide(+12.0, 55); guide(+6.0, 55); guide(-6.0, 55); guide(-12.0, 55);
        {
            CColor col = structureColor_; col.alpha = 170;
            c->setFrameColor(col);
            c->setLineWidth(1.0);
            c->drawLine(CPoint(bars.left, zeroY), CPoint(bars.right, zeroY));
            c->setFontColor(textColor_);
            c->drawString("0", CRect(r.left, zeroY - 6, bars.left - 3, zeroY + 6), kRightText);
        }

        if (!live_) {
            c->setFontColor(structureColor_);
            c->drawString("NO PINK SOURCE — the table reads a stationary pink field",
                           CRect(bars.left + 4, zeroY - 16, bars.right, zeroY - 3), kLeftText);
            return;
        }

        // Only the bands this sample rate can measure. At 44.1 and 48 kHz the
        // 20 kHz band's passband runs into Nyquist and its reading is an
        // artefact of the rate (-3.0 dB at 44.1 kHz, measured), so it is not
        // drawn at all rather than drawn wrong.
        const int nb = std::clamp(frame.bandCount, 0, strx::kNumBands);

        // --- One bar per band, from the zero line towards the correction. ---
        // A bar is drawn dim until its own band has had three time constants
        // to converge: the 20 Hz band needs half a minute, the 1 kHz band a
        // second, and a table that hid that difference would invite acting on
        // numbers that are still moving.
        for (int i = 0; i < nb; ++i) {
            const double fl = strx::bandFl(i);
            const double fu = strx::bandFuNominal(i);
            const CCoord x0 = xForFreq(fl) + 1.0;
            const CCoord x1 = std::min(xForFreq(fu) - 1.0, bars.right);
            if (x1 <= x0) continue;
            const double v = frame.bandComp[i];
            const CCoord y = yForDb(v);
            CColor col = barColor_;
            col.alpha = (uint8_t)std::lround(60.0 + 195.0 * std::clamp(frame.bandSettle[i], 0.f, 1.f));
            c->setFillColor(col);
            const CRect bar(x0, std::min(y, zeroY), x1, std::max(y, zeroY));
            c->drawRect(bar, kDrawFilled);
        }

        // --- Print the value at the tip of each excursion's PEAK band. ---
        // Not every band past the threshold: at 17 px a bar is narrower than
        // "+4.1", and a bump spans three bands, so labelling them all would
        // overlap. One number per feature, on the band that leads it.
        for (int i = 0; i < nb; ++i) {
            const float v = frame.bandComp[i];
            if (std::fabs(v) < kLabelDb) continue;
            if (frame.bandSettle[i] < 0.8f) continue;
            const float lo = (i > 0)    ? std::fabs(frame.bandComp[i-1]) : 0.f;
            const float hi = (i < nb-1) ? std::fabs(frame.bandComp[i+1]) : 0.f;
            if (std::fabs(v) < lo || std::fabs(v) < hi) continue;
            char buf[8];
            std::snprintf(buf, sizeof buf, "%+.1f", v);
            const CCoord x = xForFreq(strx::bandFc(i));
            const CCoord y = yForDb(v);
            CRect box(x - 16, (v >= 0 ? y - 14 : y + 1), x + 16, (v >= 0 ? y - 1 : y + 14));
            if (box.left  < bars.left)  box.offset(bars.left  - box.left,  0);
            if (box.right > r.right)    box.offset(r.right    - box.right, 0);
            c->setFontColor(textColor_);
            c->drawString(buf, box, kCenterText);
        }

        // --- Per-octave readout + frequency labels, on the spectrum's grid. ---
        struct OctBand { int index; const char* label; };
        static constexpr OctBand kOct[] = {
            {  2, "31.5" }, {  5, "63"  }, {  8, "125" }, { 11, "250" }, { 14, "500" },
            { 17, "1k"   }, { 20, "2k"  }, { 23, "4k"  }, { 26, "8k"  }, { 29, "16k" },
        };
        const CCoord valueTop = bars.bottom + 1;
        const CCoord labelTop = valueTop + kValueRow;
        for (const OctBand& ob : kOct) {
            if (ob.index >= nb) continue;
            const CCoord x = xForFreq(strx::bandFc(ob.index));
            auto place = [&](CCoord top, CCoord bot) {
                CRect box(x - 16, top, x + 16, bot);
                if (box.left  < bars.left) box.offset(bars.left - box.left,  0);
                if (box.right > r.right)   box.offset(r.right   - box.right, 0);
                return box;
            };
            char buf[8];
            std::snprintf(buf, sizeof buf, "%+.1f", frame.bandComp[ob.index]);
            c->setFontColor(textColor_);
            c->drawString(buf, place(valueTop, valueTop + kValueRow - 1), kCenterText);
            CColor dim = textColor_; dim.alpha = 160;
            c->setFontColor(dim);
            c->drawString(ob.label, place(labelTop, labelTop + kLabelRow - 1), kCenterText);
        }
    }

private:
    StrxProcessor* processor_ = nullptr;
    VSTGUI::CFontRef font_ = nullptr;
    VSTGUI::CColor structureColor_, textColor_, barColor_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    bool live_ = false;
};

} // namespace Seam
