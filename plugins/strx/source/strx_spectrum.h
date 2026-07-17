#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "strx_processor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// FAUST REFERENCE (seam.analyzers.lib: an.mth_octave_spectral_level, the
// idiomatic SR-independent filterbank equivalent) / seam_fft.h Welch: renders
// the AnalysisFrame::specM/specS Welch magnitude curves that
// Seam::strx::Analyzer already computes (see strx_dsp.h). This view adds no
// DSP of its own — it is a read-only render of AnalysisFrame.

namespace Seam {

// Third of strx's three custom views (Task 9 of 9): overlaid M/S Welch
// spectra on a log-frequency x-axis (20 Hz - 20 kHz) and a dB y-axis
// (-120 .. +6 dB). bin k -> Hz uses fftSize = 2*(numBins-1) (derived from the
// frame, since numBins is fixed by strx_dsp.h's kFftSize) and
// StrxProcessor::sampleRate() (config, not per-frame data — safe to read
// directly on the GUI thread, unlike AnalysisFrame contents).
//
// Polls StrxProcessor::latestFrame() — the GUI-thread cache described in
// strx_processor.h — on its own ~30 Hz CVSTGUITimer, independent of the
// StrxMeters (Task 7) and StrxGoniometer (Task 8) sibling views' timers.
class StrxSpectrum : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 33;   // ~30 Hz repaint

    // --- Axis ranges ---
    static constexpr double kFMin  = 20.0;     // Hz, x-axis left edge
    static constexpr double kFMax  = 20000.0;  // Hz, x-axis right edge
    static constexpr double kDbMin = -120.0;   // y-axis bottom
    static constexpr double kDbMax = 6.0;      // y-axis top

    // --- Layout ---
    static constexpr double kLeftMargin   = 30.0;  // dB-axis label gutter
    static constexpr double kRightMargin  = 4.0;
    static constexpr double kTopMargin    = 4.0;
    static constexpr double kBottomMargin = 14.0;  // freq-axis label strip
    static constexpr double kLegendSwatch = 8.0;

    StrxSpectrum(const VSTGUI::CRect& size, StrxProcessor* processor,
                 VSTGUI::CFontRef font,
                 const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                 const VSTGUI::CColor& trackColor,
                 const VSTGUI::CColor& colorM, const VSTGUI::CColor& colorS)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          structureColor_(structureColor), textColor_(textColor), trackColor_(trackColor),
          colorM_(colorM), colorS_(colorS) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                glide_ = processor_->calbusWatch().poll().glide;
                invalid();
            }, kTimerMs, /*doStart*/true);
    }

    ~StrxSpectrum() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        static const Seam::strx::AnalysisFrame kEmptyFrame{};
        const Seam::strx::AnalysisFrame& frame =
            processor_ ? processor_->latestFrame() : kEmptyFrame;
        const double fs = processor_ ? processor_->sampleRate() : 48000.0;

        const CRect r = getViewSize();
        c->setDrawMode(kAntiAliasing);

        const CRect plot(r.left + kLeftMargin, r.top + kTopMargin,
                          r.right - kRightMargin, r.bottom - kBottomMargin);

        static const double kLogFMin = std::log10(kFMin);
        static const double kLogFMax = std::log10(kFMax);
        const double logSpan = kLogFMax - kLogFMin;
        auto xForFreq = [&](double f) -> CCoord {
            const double t = (std::log10(f) - kLogFMin) / logSpan;
            return plot.left + t * plot.getWidth();
        };
        auto yForDb = [&](double db) -> CCoord {
            const double dbc = std::clamp(db, kDbMin, kDbMax);
            const double t = (dbc - kDbMin) / (kDbMax - kDbMin);
            return plot.bottom - t * plot.getHeight();
        };

        if (font_) c->setFont(font_);

        // --- dB grid (horizontal lines every 20 dB) + left-axis labels. ---
        CColor grid = structureColor_;
        grid.alpha = 70;
        static constexpr double kDbLines[] = { 0.0, -20.0, -40.0, -60.0, -80.0, -100.0, -120.0 };
        for (double db : kDbLines) {
            const CCoord y = yForDb(db);
            c->setFrameColor(grid);
            c->setLineWidth(1.0);
            c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
            char buf[8];
            std::snprintf(buf, sizeof buf, "%.0f", db);
            c->setFontColor(structureColor_);
            c->drawString(buf, CRect(r.left, y - 6, plot.left - 3, y + 6), kRightText);
        }

        // --- Frequency grid (decade lines) + bottom-axis labels. ---
        struct FLine { double hz; const char* label; };
        static constexpr FLine kFreqLines[] = {
            { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" },
        };
        for (const FLine& fl : kFreqLines) {
            const CCoord x = xForFreq(fl.hz);
            c->setFrameColor(grid);
            c->setLineWidth(1.0);
            c->drawLine(CPoint(x, plot.top), CPoint(x, plot.bottom));
            c->setFontColor(structureColor_);
            c->drawString(fl.label, CRect(x - 16, plot.bottom + 1, x + 16, r.bottom), kCenterText);
        }

        // Plot border.
        c->setFrameColor(structureColor_);
        c->setLineWidth(1.0);
        c->drawRect(plot, kDrawStroked);

        // --- M/S curves. ---
        const int fftSize = (frame.numBins > 1) ? 2 * (frame.numBins - 1) : 0;
        auto drawCurve = [&](const float* spec, int numBins, CColor color, uint8_t alpha) {
            if (fftSize <= 0 || fs <= 0.0) return;
            CGraphicsPath* path = c->createGraphicsPath();
            if (!path) return;
            bool started = false;
            for (int k = 0; k < numBins; ++k) {
                const double f = k * fs / double(fftSize);
                if (f < kFMin) continue;
                if (f > kFMax) break;
                const CPoint p(xForFreq(f), yForDb(spec[k]));
                if (!started) { path->beginSubpath(p); started = true; }
                else path->addLine(p);
            }
            if (started) {
                color.alpha = alpha;
                c->setFrameColor(color);
                c->setLineWidth(1.5);
                c->drawGraphicsPath(path, CDrawContext::kPathStroked);
            }
            path->forget();
        };

        // Pink noise is stationary, so one averaged curve per channel says
        // everything. A sweep is not: it presents ONE frequency at a time, so
        // the average would smear a moving peak. The hold accumulates the
        // response as the sweep descends (each bin is excited once), and the
        // live curve — fast now, tau 100 ms — shows where the sweep IS.
        if (glide_) {
            drawCurve(frame.holdM, frame.numBins, colorM_, /*alpha*/255);
            drawCurve(frame.holdS, frame.numBins, colorS_, /*alpha*/255);
            drawCurve(frame.specM, frame.numBins, colorM_, /*alpha*/90);
            drawCurve(frame.specS, frame.numBins, colorS_, /*alpha*/90);
        } else {
            drawCurve(frame.specM, frame.numBins, colorM_, /*alpha*/255);
            drawCurve(frame.specS, frame.numBins, colorS_, /*alpha*/255);
        }

        // --- Legend: "M" / "S" swatches, top-right corner of the plot. ---
        if (font_) {
            const CCoord lx = plot.right - 56.0;
            const CCoord ly = plot.top + 2.0;
            c->setFillColor(colorM_);
            c->drawRect(CRect(lx, ly, lx + kLegendSwatch, ly + kLegendSwatch), kDrawFilled);
            c->setFontColor(textColor_);
            c->drawString("M", CRect(lx + kLegendSwatch + 2, ly - 3, lx + 22, ly + 11), kLeftText);

            const CCoord sx = lx + 26.0;
            c->setFillColor(colorS_);
            c->drawRect(CRect(sx, ly, sx + kLegendSwatch, ly + kLegendSwatch), kDrawFilled);
            c->drawString("S", CRect(sx + kLegendSwatch + 2, ly - 3, sx + 22, ly + 11), kLeftText);
        }

        setDirty(false);
    }

private:
    StrxProcessor* processor_ = nullptr;
    VSTGUI::CFontRef font_ = nullptr;
    VSTGUI::CColor structureColor_, textColor_, trackColor_, colorM_, colorS_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    // Set by the timer poll (GUI thread), read by draw() (GUI thread). Copied
    // out of CalbusWatch::poll()'s returned digest immediately — never store
    // the reference itself, which aliases the watch's internal state and
    // would silently observe a later snapshot (see strx_calbus_watch.h).
    bool glide_ = false;
};

} // namespace Seam
