#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "strx_processor.h"
#include "seam_meter.h"

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
// pair DIRECTLY to (screenX, screenY) with the view center as origin, x
// mirrored (so a LEFT-panned signal reads upper-left, the standard/Melda
// convention) and y flipped (VSTGUI y grows downward): screenX = cx - gx*scale
// (S, horizontal), screenY = cy - gy*scale (M, vertical). No 45-degree rotation is needed to
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
    static constexpr double kLabelFrac    = 1.10;    // L/R label placement vs. circle radius (>1 = outside the circle, in the corners)
    static constexpr double kReadoutH     = 16.0;    // bottom readout strip height (px)

    // --- Radial scale (log-dB, floored) ---
    static constexpr double kRadiusFloorDb = -48.0;  // signals at/below this floor sit at the circle's center

    // --- Needle smoothing (view-side EMA on the doubled angle) ---
    static constexpr double kNeedleSmooth = 0.05;    // per-repaint EMA coeff at ~30 Hz ~= ~0.5 s time constant

    StrxGoniometer(const VSTGUI::CRect& size, StrxProcessor* processor,
                   VSTGUI::CFontRef font,
                   const VSTGUI::CColor& labelColor, const VSTGUI::CColor& textColor,
                   const VSTGUI::CColor& trackColor, const VSTGUI::CColor& fillColor,
                   const VSTGUI::CColor& needleColor)
        : VSTGUI::CView(size), processor_(processor), font_(font),
          labelColor_(labelColor), textColor_(textColor),
          trackColor_(trackColor), fillColor_(fillColor), needleColor_(needleColor) {
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
        const CCoord dLabel = radius * kLabelFrac * 0.70710678; // label placement, inset from the axis line
        c->setFrameColor(labelColor_);
        c->drawLine(CPoint(center.x - d, center.y + d), CPoint(center.x + d, center.y - d)); // R: bottom-left <-> top-right
        c->drawLine(CPoint(center.x - d, center.y - d), CPoint(center.x + d, center.y + d)); // L: top-left <-> bottom-right
        if (font_) {
            c->setFont(font_);
            c->setFontColor(labelColor_);
            // Mirrored screen mapping (sx = cx - gx*scale): pure L (l>0, r=0):
            // gx=gy=+0.707l -> screen (cx-d, cy-d), top-left.
            // Pure R (r>0, l=0): gx=-0.707r, gy=+0.707r -> screen (cx+d, cy-d), top-right.
            c->drawString("L", CRect(center.x - dLabel - 8, center.y - dLabel - 14, center.x - dLabel + 8, center.y - dLabel), kCenterText);
            c->drawString("R", CRect(center.x + dLabel - 8, center.y - dLabel - 14, center.x + dLabel + 8, center.y - dLabel), kCenterText);
        }

        const Generation& newest = history_[head_];

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
                // Log-dB radial scale, floored at kRadiusFloorDb: the ANGLE
                // (direction) of (gx,gy) is preserved exactly; only the
                // radial distance is warped so attenuated signals stay near
                // the edge instead of shrinking linearly toward the center.
                // db2norm caps at 1.0, so points never exceed the circle —
                // no per-axis clamp needed (replaces the old [-1,1] clamp).
                const double gx = double(gen.gx[i]);      // raw S
                const double gy = double(gen.gy[i]);      // raw M
                const double rLin = std::hypot(gx, gy);
                const double rNorm = (rLin > 1e-6)
                    ? seam::meter::db2norm(seam::meter::lin2db(rLin, kRadiusFloorDb), kRadiusFloorDb)
                    : 0.0;                                 // rNorm in [0,1]
                const double scale = (rLin > 1e-6) ? (rNorm / rLin) : 0.0;
                const double sx = center.x - gx * radius * scale; // mirrored: L -> left
                const double sy = center.y - gy * radius * scale; // y up
                path->addRect(CRect(sx - kPointHalfPx, sy - kPointHalfPx,
                                     sx + kPointHalfPx, sy + kPointHalfPx));
            }
            c->setFillColor(pc);
            c->drawGraphicsPath(path, CDrawContext::kPathFilled);
            path->forget();
        }

        // Vector "needle": thin line through the center along the field's
        // principal axis (san.vectorangle -> newest.angleRad). Drawn after
        // the scatter so it stays legible against the decaying trail.
        // Display angle in the goniometer's (S=x, M=y) plane is
        // phi = angleRad + pi/4 (mono -> phi=90 deg = vertical,
        // anti-phase -> phi=0 deg = horizontal, matching the scatter).
        {
            const double phi = double(newest.angleRad) + M_PI / 4.0;
            // Smooth the DOUBLED angle as a unit vector (the needle is an
            // axis, angle mod pi) so the EMA never fights a 180-degree flip.
            const double c2 = std::cos(2.0 * phi);
            const double s2 = std::sin(2.0 * phi);
            needleCos2_ += kNeedleSmooth * (c2 - needleCos2_);
            needleSin2_ += kNeedleSmooth * (s2 - needleSin2_);
            const double phiS = 0.5 * std::atan2(needleSin2_, needleCos2_);
            const double nx = std::cos(phiS) * radius;
            const double ny = std::sin(phiS) * radius;
            c->setFrameColor(needleColor_);
            c->setLineWidth(1.0);
            // Mirrored endpoints (matches the flipped scatter mapping).
            c->drawLine(CPoint(center.x + nx, center.y + ny), CPoint(center.x - nx, center.y - ny));
        }

        // Readout: ANGLE / PANORAMA from the newest generation's scalars.
        char buf[64];
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
    VSTGUI::CColor labelColor_, textColor_, trackColor_, fillColor_, needleColor_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;

    std::array<Generation, kTrailFrames> history_{};
    int head_ = 0; // history_[head_] is the newest generation

    // Needle smoothing state (doubled-angle unit vector), default phi = pi/4.
    double needleCos2_ = 0.0;
    double needleSin2_ = 1.0;
};

} // namespace Seam
