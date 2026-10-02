//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the two views the uidesc cannot describe
//
// RESET is not a parameter (a momentary parameter is lost when the host
// coalesces 0->1->0; ltglide, Reaper): the view reaches the engine directly
// and raises its generation counter, served at the next block. The square
// is filled while pressed.
//
// ChoirGrid: four rows (channels) of 16 bars (bands). Each bar is the block
// peak of the band's envelope divided by Q, the amplitude of the input's
// partial there, in dBFS from -80 to 0, read from 64 relaxed atomics by a
// 30 Hz timer. Row labels carry f and a; the last line carries Q, the
// release, the noise block and the rate. An inactive band is an empty
// grey frame.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_dsp.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Seam {

class ChoirResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    ChoirResetButton(const VSTGUI::CRect& size, choir::Engine* engine,
                     const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                     const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {}

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const CRect vs = getViewSize();
        CRect r(0, 0, kBoxPx, kBoxPx);
        r.offset(vs.left + 1.0, vs.top + std::ceil((vs.getHeight() - kBoxPx) / 2.0));
        c->setDrawMode(kAntiAliasing);
        c->setFrameColor(frame_);
        c->setFillColor(pressed_ ? active_ : idle_);
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawFilledAndStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState& b) override {
        if (!b.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
        pressed_ = true; invalid();
        return VSTGUI::kMouseEventHandled;
    }
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState&) override {
        if (!pressed_) return VSTGUI::kMouseEventNotHandled;
        pressed_ = false;
        if (engine_ && getViewSize().pointInside(where)) engine_->requestReset();
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

private:
    choir::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    bool pressed_ = false;
};

class ChoirGrid : public VSTGUI::CView {
public:
    static constexpr double kLabelW = 110.0, kRowH = 32.0, kAxisH = 14.0, kLineH = 16.0;
    static constexpr double kFloorDb = -80.0;

    ChoirGrid(const VSTGUI::CRect& size, const choir::Engine* engine, VSTGUI::CFontRef font,
              const VSTGUI::CColor& text, const VSTGUI::CColor& track,
              const VSTGUI::CColor& fill, const VSTGUI::CColor& structure)
        : CView(size), engine_(engine), font_(font),
          text_(text), track_(track), fill_(fill), structure_(structure) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 33, true);
    }
    ~ChoirGrid() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const CRect vs = getViewSize();
        const choir::Config& cfg = engine_->config();
        const double pitch = (vs.getWidth() - kLabelW) / choir::kBands;
        const double barW = std::floor(pitch * 0.66);
        c->setFont(font_);
        c->setFontColor(text_);
        c->setDrawMode(kAntiAliasing);
        c->setLineWidth(1.0);
        char s[96];
        for (int ch = 0; ch < choir::kChannels; ++ch) {
            const double top = vs.top + ch * kRowH;
            std::snprintf(s, sizeof s, "%d  %g Hz  a %g", ch + 1, cfg.f[(size_t)ch], cfg.a[(size_t)ch]);
            c->drawString(s, CRect(vs.left, top, vs.left + kLabelW, top + kRowH - 4), kLeftText);
            for (int k = 0; k < choir::kBands; ++k) {
                const double x = vs.left + kLabelW + k * pitch;
                const CRect slot(x, top + 3, x + barW, top + kRowH - 3);
                const float v = engine_->display().load(ch, k);
                if (v < 0.0f) {                                   // inactive band
                    c->setFrameColor(structure_);
                    c->drawRect(slot, kDrawStroked);
                    continue;
                }
                c->setFillColor(track_);
                c->drawRect(slot, kDrawFilled);
                const double db = 20.0 * std::log10(std::max(1e-12, (double)v));
                const double frac = std::min(1.0, std::max(0.0, (db - kFloorDb) / -kFloorDb));
                if (frac > 0.0) {
                    CRect bar = slot;
                    bar.top = slot.bottom - frac * slot.getHeight();
                    c->setFillColor(fill_);
                    c->drawRect(bar, kDrawFilled);
                }
            }
        }
        const double axisTop = vs.top + choir::kChannels * kRowH;
        for (int k = 0; k < choir::kBands; k += 1) {
            if (k != 0 && k != 3 && k != 7 && k != 11 && k != 15) continue;
            const double x = vs.left + kLabelW + k * pitch;
            std::snprintf(s, sizeof s, "%d", k + 1);
            c->drawString(s, CRect(x - 4, axisTop, x + barW + 4, axisTop + kAxisH), kCenterText);
        }
        const double fs = engine_->sampleRate();
        if (fs > 0.0)
            std::snprintf(s, sizeof s, "Q %g \xC2\xB7 release %g s \xC2\xB7 noise block 3 \xC2\xB7 @ %g kHz",
                          cfg.q, cfg.release, fs / 1000.0);
        else
            std::snprintf(s, sizeof s, "Q %g \xC2\xB7 release %g s \xC2\xB7 noise block 3 \xC2\xB7 inactive",
                          cfg.q, cfg.release);
        c->drawString(s, CRect(vs.left, axisTop + kAxisH + 2, vs.right, axisTop + kAxisH + 2 + kLineH), kLeftText);
        setDirty(false);
    }

private:
    const choir::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor text_, track_, fill_, structure_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
