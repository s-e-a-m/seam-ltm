//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_spectrum_view.h — N overlaid live spectra (VSTGUI)
//
// The drawing half of strx's spectrum (strx_spectrum.h) made generic: the
// same axes (20 Hz - 24 kHz log, -120 .. +6 dB), the same ISO 266 grid
// (third-octave hairlines, labelled octaves, alphas measured on screen in
// strx), and N curves read from a Seam::MultiSpectrum<N> (seam_spectrum.h),
// one colour and one legend label each. No measurement logic: no hold, no
// accumulation, no freezing — a live picture of what is sounding.
//
// The view is the single consumer of the analyser's triple buffer: a 30 Hz
// timer drains it into a cached frame (on the heap, 130 KB for four
// channels) and repaints only when a new frame arrived. Each curve keeps,
// per horizontal pixel, the loudest bin that falls on it: at 192 kHz 8193
// bins meet ~370 pixels, and a peak drawn as an average would vanish.
// Written 2026-10-09 for stunedrev.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "seam_spectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace Seam {

template <int N>
class SpectrumView : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 33;
    static constexpr double kFMin = 20.0, kFMax = 24000.0;   // Hz, strx's axis
    static constexpr double kDbMin = -120.0, kDbMax = 6.0;
    static constexpr double kLeftMargin = 30.0, kRightMargin = 4.0;
    static constexpr double kTopMargin = 4.0, kBottomMargin = 14.0;
    static constexpr double kLegendSwatch = 8.0;

    SpectrumView(const VSTGUI::CRect& size, MultiSpectrum<N>* source, VSTGUI::CFontRef font,
                 const VSTGUI::CColor& structure, const VSTGUI::CColor& text,
                 const std::array<VSTGUI::CColor, N>& colors,
                 const std::array<std::string, N>& labels)
        : CView(size), source_(source), font_(font), structure_(structure), text_(text),
          colors_(colors), labels_(labels), frame_(std::make_unique<SpectrumFrame<N>>()) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                if (source_ && source_->tryRead(*frame_)) invalid();
            }, kTimerMs, true);
    }
    ~SpectrumView() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        const CRect r = getViewSize();
        c->setDrawMode(kAntiAliasing);
        const CRect plot(r.left + kLeftMargin, r.top + kTopMargin,
                         r.right - kRightMargin, r.bottom - kBottomMargin);

        const double lo = std::log10(kFMin), span = std::log10(kFMax) - lo;
        auto xForFreq = [&](double f) -> CCoord {
            return plot.left + (std::log10(f) - lo) / span * plot.getWidth();
        };
        auto yForDb = [&](double db) -> CCoord {
            const double t = (std::clamp(db, kDbMin, kDbMax) - kDbMin) / (kDbMax - kDbMin);
            return plot.bottom - t * plot.getHeight();
        };
        if (font_) c->setFont(font_);

        // dB grid every 20 dB, labelled in the left gutter.
        CColor grid = structure_; grid.alpha = 70;
        for (double db = 0.0; db >= kDbMin; db -= 20.0) {
            const CCoord y = yForDb(db);
            c->setFrameColor(grid); c->setLineWidth(1.0);
            c->drawLine(CPoint(plot.left, y), CPoint(plot.right, y));
            char buf[8]; std::snprintf(buf, sizeof buf, "%.0f", db);
            c->setFontColor(text_);
            c->drawString(buf, CRect(r.left, y - 6, plot.left - 3, y + 6), kRightText);
        }

        // ISO 266 frequency grid: third-octave hairlines, labelled octaves.
        struct FLine { double hz; const char* label; };
        static constexpr FLine kLines[] = {
            {20, nullptr}, {25, nullptr}, {31.5, "31.5"}, {40, nullptr}, {50, nullptr},
            {63, "63"}, {80, nullptr}, {100, nullptr}, {125, "125"}, {160, nullptr},
            {200, nullptr}, {250, "250"}, {315, nullptr}, {400, nullptr}, {500, "500"},
            {630, nullptr}, {800, nullptr}, {1000, "1k"}, {1250, nullptr}, {1600, nullptr},
            {2000, "2k"}, {2500, nullptr}, {3150, nullptr}, {4000, "4k"}, {5000, nullptr},
            {6300, nullptr}, {8000, "8k"}, {10000, nullptr}, {12500, nullptr},
            {16000, "16k"}, {20000, nullptr},
        };
        for (const FLine& fl : kLines) {
            const CCoord x = xForFreq(fl.hz);
            CColor line = structure_; line.alpha = fl.label ? 110 : 55;
            c->setFrameColor(line); c->setLineWidth(1.0);
            c->drawLine(CPoint(x, plot.top), CPoint(x, plot.bottom));
            if (!fl.label) continue;
            CRect box(x - 15, plot.bottom + 1, x + 15, r.bottom);
            if (box.left  < plot.left) box.offset(plot.left - box.left, 0);
            if (box.right > r.right)   box.offset(r.right - box.right, 0);
            c->setFontColor(text_);
            c->drawString(fl.label, box, kCenterText);
        }
        c->setFrameColor(structure_); c->setLineWidth(1.0);
        c->drawRect(plot, kDrawStroked);

        // The curves, the first underneath: per pixel, the loudest bin on it.
        const SpectrumFrame<N>& fr = *frame_;
        const double fs = source_ ? source_->sampleRate() : 0.0;
        const int fft = fr.numBins > 1 ? 2 * (fr.numBins - 1) : 0;
        if (fft > 0 && fs > 0.0) {
            for (int ch = 0; ch < N; ++ch) {
                CGraphicsPath* path = c->createGraphicsPath();
                if (!path) continue;
                bool started = false;
                long px = -1; double xAt = 0.0; float peak = -200.0f;
                auto emit = [&]() {
                    const CPoint p(xAt, yForDb(peak));
                    if (!started) { path->beginSubpath(p); started = true; } else path->addLine(p);
                };
                for (int k = 1; k < fr.numBins; ++k) {
                    const double f = k * fs / fft;
                    if (f < kFMin) continue;
                    if (f > kFMax) break;
                    const double x = xForFreq(f);
                    const long p = (long)std::floor(x);
                    if (p != px) {
                        if (px >= 0) emit();
                        px = p; xAt = x; peak = fr.db[ch][k];
                    } else {
                        peak = std::max(peak, fr.db[ch][k]);
                    }
                }
                if (px >= 0) emit();
                if (started) {
                    c->setFrameColor(colors_[(size_t)ch]);
                    c->setLineWidth(1.25);
                    c->drawGraphicsPath(path, CDrawContext::kPathStroked);
                }
                path->forget();
            }
        }

        // Legend, top right: a swatch and a label per curve.
        CCoord lx = plot.right - 4;
        for (int ch = N - 1; ch >= 0; --ch) {
            const CCoord w = 10.0 + 7.0 * (CCoord)labels_[(size_t)ch].size();
            lx -= w + kLegendSwatch + 8;
            const CCoord ly = plot.top + 5;
            c->setFillColor(colors_[(size_t)ch]);
            c->drawRect(CRect(lx, ly, lx + kLegendSwatch, ly + kLegendSwatch), kDrawFilled);
            c->setFontColor(text_);
            c->drawString(labels_[(size_t)ch].c_str(),
                          CRect(lx + kLegendSwatch + 3, ly - 3, lx + kLegendSwatch + 3 + w, ly + 11), kLeftText);
        }
        setDirty(false);
    }

private:
    MultiSpectrum<N>* source_;
    VSTGUI::CFontRef font_;
    VSTGUI::CColor structure_, text_;
    std::array<VSTGUI::CColor, N> colors_;
    std::array<std::string, N> labels_;
    std::unique_ptr<SpectrumFrame<N>> frame_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
