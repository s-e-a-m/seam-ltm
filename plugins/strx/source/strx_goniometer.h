#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "strx_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// FAUST REFERENCE (seam.analyzers.lib): renders the san.vectorangle/panorama
// scalars and the sst.sdmx (seam.stereophony.lib) M/S pair that
// Seam::strx::Analyzer already computes (see strx_dsp.h AnalysisFrame::gx/gy,
// angleRad, panorama). This view adds no DSP of its own — it is a read-only,
// decaying-trail render of AnalysisFrame's goniometer point cloud.

namespace Seam {

// Second of strx's three custom views (Task 8 of 9): a Melda-style decaying
// L/R scatter (Lissajous/goniometer). AnalysisFrame::gx/gy already carry
// (S,M) = ((L-R)/sqrt2, (L+R)/sqrt2) — see strx_dsp.h. This view maps that
// pair DIRECTLY to (screenX, screenY) with the view center as origin and y
// flipped (VSTGUI y grows downward): screenX = cx + gx*scale (S, horizontal),
// screenY = cy - gy*scale (M, vertical). No 45-degree rotation is needed to
// get the classic goniometer look: because the DSP already outputs the M/S
// rotation, a mono signal (S=0, all gx==0) collapses to a perfectly vertical
// line, and an anti-phase signal (M=0, all gy==0) collapses to a perfectly
// horizontal line. The L and R channel axes fall out as the DIAGONALS
// (pure L: l=r-free, s=m => gx==gy; pure R: s=-m => gx==-gy), which is what
// classic goniometers show, and are drawn explicitly as reference axes.
//
// Polls StrxProcessor::latestFrame() — the GUI-thread cache described in
// strx_processor.h — on its own ~30 Hz CVSTGUITimer, independent of the
// StrxMeters (Task 7) and StrxSpectrum (Task 9) sibling views' timers.
class StrxGoniometer : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 33;        // ~30 Hz tick: push trail + repaint

    // --- Decaying trail (Melda-style) ---
    static constexpr int    kTrailFrames  = 8;       // ring depth (age 0 = newest)
    static constexpr double kBaseAlpha    = 200.0;   // newest generation alpha (0..255)
    static constexpr double kDecayFactor  = 0.55;    // per-age alpha multiplier

    // --- Layout / drawing ---
    static constexpr double kMarginFrac   = 0.06;    // circle inset as a fraction of the view
    static constexpr double kPointHalfPx  = 1.3;     // scatter point half-size (px)
    static constexpr double kAxisFrac     = 0.98;     // diagonal axis length vs. circle radius
    static constexpr double kReadoutH     = 16.0;    // bottom readout strip height (px)

    StrxGoniometer(const VSTGUI::CRect& size, StrxProcessor* processor,
                   VSTGUI::CFontRef font,
                   const VSTGUI::CColor& labelColor, const VSTGUI::CColor& textColor,
                   const VSTGUI::CColor& trackColor, const VSTGUI::CColor& fillColor)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          labelColor_(labelColor), textColor_(textColor),
          trackColor_(trackColor), fillColor_(fillColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { pushGeneration(); invalid(); }, kTimerMs, /*doStart*/true);
    }

    ~StrxGoniometer() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        const CRect r = getViewSize();
        c->setDrawMode(kAntiAliasing);

        // Background.
        c->setFillColor(trackColor_);
        c->drawRect(r, kDrawFilled);

        const CCoord plotBottom = r.bottom - kReadoutH;
        const CCoord side = std::min(r.getWidth(), plotBottom - r.top);
        const CCoord margin = side * kMarginFrac;
        const CCoord radius = std::max(1.0, side * 0.5 - margin);
        const CPoint center((r.left + r.right) * 0.5, (r.top + plotBottom) * 0.5);

        // Bounding circle.
        c->setFrameColor(labelColor_);
        c->setLineWidth(1.0);
        c->drawEllipse(CRect(center.x - radius, center.y - radius,
                              center.x + radius, center.y + radius), kDrawStroked);

        // Light grid: S (horizontal) and M (vertical) crosshair.
        CColor grid = labelColor_;
        grid.alpha = 70;
        c->setFrameColor(grid);
        c->drawLine(CPoint(center.x - radius, center.y), CPoint(center.x + radius, center.y));
        c->drawLine(CPoint(center.x, center.y - radius), CPoint(center.x, center.y + radius));

        // L/R diagonal axes: pure-L is gx==gy, pure-R is gx==-gy.
        const CCoord d = radius * kAxisFrac * 0.70710678; // projected onto each screen axis
        c->setFrameColor(labelColor_);
        c->drawLine(CPoint(center.x - d, center.y + d), CPoint(center.x + d, center.y - d)); // L: bottom-left <-> top-right
        c->drawLine(CPoint(center.x - d, center.y - d), CPoint(center.x + d, center.y + d)); // R: top-left <-> bottom-right
        if (font_) {
            c->setFont(font_);
            c->setFontColor(labelColor_);
            // Pure L (l>0, r=0): gx=gy=+0.707l -> screen (cx+d, cy-d), top-right.
            // Pure R (r>0, l=0): gx=-0.707r, gy=+0.707r -> screen (cx-d, cy-d), top-left.
            c->drawString("L", CRect(center.x + d - 8, center.y - d - 14, center.x + d + 8, center.y - d), kCenterText);
            c->drawString("R", CRect(center.x - d - 8, center.y - d - 14, center.x - d + 8, center.y - d), kCenterText);
        }

        // Decaying point cloud: oldest generation first so the newest paints on top.
        for (int age = kTrailFrames - 1; age >= 0; --age) {
            const int idx = (head_ + age) % kTrailFrames;
            const Generation& gen = history_[idx];
            if (gen.numPoints <= 0) continue;

            const double alpha = kBaseAlpha * std::pow(kDecayFactor, double(age));
            if (alpha < 1.0) continue;
            CColor pc = fillColor_;
            pc.alpha = (uint8_t) std::min(255.0, std::max(0.0, alpha));

            CGraphicsPath* path = c->createGraphicsPath();
            if (!path) continue;
            for (int i = 0; i < gen.numPoints; ++i) {
                // Clamp (S,M) to the unit range so |value|=1 lands on the circle
                // edge: full-scale mono gives gy=√2 and anti-phase gives gx=√2,
                // which would otherwise map ~41% past the radius and square off
                // against the rectangular view bounds. (View-side only — the DSP
                // in strx_dsp.h keeps the true unnormalized (S,M).)
                const double gxc = std::clamp(double(gen.gx[i]), -1.0, 1.0);
                const double gyc = std::clamp(double(gen.gy[i]), -1.0, 1.0);
                const double sx = center.x + gxc * radius;
                const double sy = center.y - gyc * radius; // y up
                path->addRect(CRect(sx - kPointHalfPx, sy - kPointHalfPx,
                                     sx + kPointHalfPx, sy + kPointHalfPx));
            }
            c->setFillColor(pc);
            c->drawGraphicsPath(path, CDrawContext::kPathFilled);
            path->forget();
        }

        // Readout: ANGLE / PANORAMA from the newest generation's scalars.
        char buf[64];
        const Generation& newest = history_[head_];
        std::snprintf(buf, sizeof buf, "ANGLE %+.0f\xC2\xB0   PANORAMA %+.0f%%",
                      newest.angleRad * (180.0 / M_PI), newest.panorama * 100.0);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        c->drawString(buf, CRect(r.left, plotBottom, r.right, r.bottom), kCenterText);

        setDirty(false);
    }

private:
    struct Generation {
        int numPoints = 0;
        float gx[Seam::strx::AnalysisFrame::kMaxPoints];
        float gy[Seam::strx::AnalysisFrame::kMaxPoints];
        float angleRad = 0.f;
        float panorama = 0.f;
    };

    void pushGeneration() {
        static const Seam::strx::AnalysisFrame kEmptyFrame{};
        const Seam::strx::AnalysisFrame& frame =
            processor_ ? processor_->latestFrame() : kEmptyFrame;

        head_ = (head_ + kTrailFrames - 1) % kTrailFrames; // newest slot moves back one
        Generation& gen = history_[head_];
        const int n = std::min(frame.numPoints, Seam::strx::AnalysisFrame::kMaxPoints);
        gen.numPoints = n;
        for (int i = 0; i < n; ++i) { gen.gx[i] = frame.gx[i]; gen.gy[i] = frame.gy[i]; }
        gen.angleRad = frame.angleRad;
        gen.panorama = frame.panorama;
    }

    StrxProcessor* processor_ = nullptr;
    VSTGUI::CFontRef font_ = nullptr;
    VSTGUI::CColor labelColor_, textColor_, trackColor_, fillColor_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;

    std::array<Generation, kTrailFrames> history_{};
    int head_ = 0; // history_[head_] is the newest generation
};

} // namespace Seam
