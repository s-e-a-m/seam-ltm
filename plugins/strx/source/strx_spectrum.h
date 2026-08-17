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
#include <cstdint>
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
                 const VSTGUI::CColor& colorM, const VSTGUI::CColor& colorS)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          structureColor_(structureColor), textColor_(textColor),
          colorM_(colorM), colorS_(colorS) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                if (processor_) {
                    const auto& d = processor_->calbusWatch().poll();
                    glide_  = d.glide;
                    active_ = d.available && d.firstActive >= 0;   // any emitter sounding
                }
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

        // --- Frequency grid: three tiers of reference lines + labels. ---
        // The values are the ISO 266 nominal centre frequencies, so the axis
        // speaks the same vocabulary as the octave / third-octave EQ that
        // corrects a STONE. Brightness carries the hierarchy: third-octave
        // hairlines to read where a feature of the curve actually sits,
        // octave lines for the bands one describes out loud, decade lines as
        // the coarse anchor. 1 kHz is both an octave centre and a decade, so
        // it takes the decade brightness and keeps its label.
        //
        // Only the octave tier is labelled. The plot is ~526 px over three
        // decades, which puts octave labels ~53 px apart — comfortable — but
        // "100" would land 17 px from "125" and "10k" the same distance from
        // "8k". The decade tier reads as a brightness accent instead of
        // fighting its neighbours for the label strip.
        // The third-octave alpha was raised from 25 to 38 after rendering the
        // grid at true size: 25/255 of Structure over BgDark separates the
        // hairline from the background by ~10 levels of grey, which is a line
        // that exists in the code and not on the screen.
        enum FTier { kThird = 0, kOctave = 1, kDecade = 2 };
        static constexpr uint8_t kTierAlpha[] = { 38, 70, 110 };
        struct FLine { double hz; FTier tier; const char* label; };
        static constexpr FLine kFreqLines[] = {
            {    20.0, kThird,  nullptr }, {    25.0, kThird,  nullptr },
            {    31.5, kOctave, "31.5"  }, {    40.0, kThird,  nullptr },
            {    50.0, kThird,  nullptr }, {    63.0, kOctave, "63"    },
            {    80.0, kThird,  nullptr }, {   100.0, kDecade, nullptr },
            {   125.0, kOctave, "125"   }, {   160.0, kThird,  nullptr },
            {   200.0, kThird,  nullptr }, {   250.0, kOctave, "250"   },
            {   315.0, kThird,  nullptr }, {   400.0, kThird,  nullptr },
            {   500.0, kOctave, "500"   }, {   630.0, kThird,  nullptr },
            {   800.0, kThird,  nullptr }, {  1000.0, kDecade, "1k"    },
            {  1250.0, kThird,  nullptr }, {  1600.0, kThird,  nullptr },
            {  2000.0, kOctave, "2k"    }, {  2500.0, kThird,  nullptr },
            {  3150.0, kThird,  nullptr }, {  4000.0, kOctave, "4k"    },
            {  5000.0, kThird,  nullptr }, {  6300.0, kThird,  nullptr },
            {  8000.0, kOctave, "8k"    }, { 10000.0, kDecade, nullptr },
            { 12500.0, kThird,  nullptr }, { 16000.0, kOctave, "16k"   },
            { 20000.0, kThird,  nullptr },
        };
        for (const FLine& fl : kFreqLines) {
            const CCoord x = xForFreq(fl.hz);
            CColor line = structureColor_;
            line.alpha = kTierAlpha[fl.tier];
            c->setFrameColor(line);
            c->setLineWidth(1.0);
            c->drawLine(CPoint(x, plot.top), CPoint(x, plot.bottom));
            if (!fl.label) continue;
            // Centred on its line, except where that would push the box past
            // the plot — at 31.5 Hz it would otherwise reach into the gutter
            // the dB labels own.
            CRect box(x - 15, plot.bottom + 1, x + 15, r.bottom);
            if (box.left  < plot.left)  box.offset(plot.left  - box.left,  0);
            if (box.right > plot.right) box.offset(plot.right - box.right, 0);
            c->setFontColor(structureColor_);
            c->drawString(fl.label, box, kCenterText);
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

        // "Last measure wins" (GS decision, 2026-07-21): while ANY emitter is
        // sounding, draw the live measurement and remember exactly what was
        // drawn. The moment nothing is sounding any more — the loop stopped,
        // or the pink observation that took over the bus stops in turn — the
        // view freezes on that remembered snapshot instead of falling back to
        // whatever the (possibly stale, possibly pre-takeover) live analysis
        // frame contains. A pink takeover itself invalidates the interrupted
        // glide session at the DSP level (see pinkTakeover() in
        // strx_calbus_digest.h), so the accumulation this snapshot could
        // ever capture is always the one that was actually on screen.
        if (active_) {
            const bool haveAcc = frame.accPasses > 0;
            const float* primaryM = glide_ ? (haveAcc ? frame.accM : frame.holdM) : frame.specM;
            const float* primaryS = glide_ ? (haveAcc ? frame.accS : frame.holdS) : frame.specS;
            drawCurve(primaryM, frame.numBins, colorM_, /*alpha*/255);
            drawCurve(primaryS, frame.numBins, colorS_, /*alpha*/255);
            if (glide_) {
                drawCurve(frame.specM, frame.numBins, colorM_, /*alpha*/90);
                drawCurve(frame.specS, frame.numBins, colorS_, /*alpha*/90);
            }

            // Snapshot exactly what was just drawn (cheap: only refreshed on
            // a repaint that actually had live data). GUI thread only, no
            // locking needed.
            const int nb = std::min(frame.numBins, Seam::strx::AnalysisFrame::kNumBins);
            std::copy(primaryM, primaryM + nb, heldPrimaryM_);
            std::copy(primaryS, primaryS + nb, heldPrimaryS_);
            heldHasLive_ = glide_;
            if (heldHasLive_) {
                std::copy(frame.specM, frame.specM + nb, heldLiveM_);
                std::copy(frame.specS, frame.specS + nb, heldLiveS_);
            }
            heldNumBins_ = nb;
            heldValid_   = true;
        } else if (heldValid_) {
            // Nothing sounding, but a measure was shown before: freeze it.
            drawCurve(heldPrimaryM_, heldNumBins_, colorM_, /*alpha*/255);
            drawCurve(heldPrimaryS_, heldNumBins_, colorS_, /*alpha*/255);
            if (heldHasLive_) {
                drawCurve(heldLiveM_, heldNumBins_, colorM_, /*alpha*/90);
                drawCurve(heldLiveS_, heldNumBins_, colorS_, /*alpha*/90);
            }
            if (font_) {
                c->setFontColor(structureColor_);
                c->drawString("HELD", CRect(plot.left + 2, plot.top + 2, plot.left + 44, plot.top + 14),
                               kLeftText);
            }
        } else {
            // Fresh instance, nothing sounding yet, no snapshot to fall back
            // on: today's plain live rendering.
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
    VSTGUI::CColor structureColor_, textColor_, colorM_, colorS_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    // Set by the timer poll (GUI thread), read by draw() (GUI thread). Copied
    // out of CalbusWatch::poll()'s returned digest immediately — never store
    // the reference itself, which aliases the watch's internal state and
    // would silently observe a later snapshot (see strx_calbus_watch.h).
    bool glide_  = false;
    bool active_ = false;   // any emitter (glide or pink) is sounding

    // "Last measure wins" snapshot (see draw()): the curves last drawn while
    // active_ was true, so the view can freeze on them once nothing is
    // sounding any more instead of reverting to a stale or unrelated live
    // frame. heldHasLive_ is true only when the held primary curve was a
    // glide accumulation/hold (the faint live overlay only ever accompanies
    // that case).
    bool  heldValid_   = false;
    bool  heldHasLive_ = false;
    int   heldNumBins_ = 0;
    float heldPrimaryM_[Seam::strx::AnalysisFrame::kNumBins] = {};
    float heldPrimaryS_[Seam::strx::AnalysisFrame::kNumBins] = {};
    float heldLiveM_[Seam::strx::AnalysisFrame::kNumBins] = {};
    float heldLiveS_[Seam::strx::AnalysisFrame::kNumBins] = {};
};

} // namespace Seam
