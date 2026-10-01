//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the two views the uidesc cannot describe
//
// RESET is not a parameter: a momentary parameter is lost when the host
// coalesces 0->1->0 into one point (ltglide, Reaper). The plugin is a
// SingleComponentEffect, so the view reaches the engine directly and asks
// it to empty the memory (a generation counter). The square is filled while
// the engine clears.
//
// The footer draws what the engine reports, read from its atomics by a GUI
// timer: the energy centroid of each line, the arena and the status.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "stunedrev_dsp.h"

#include <cmath>
#include <cstdio>

namespace Seam {

class StunedrevResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    StunedrevResetButton(const VSTGUI::CRect& size, stunedrev::Engine* engine,
                         const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                         const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50, true);
    }
    ~StunedrevResetButton() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const bool lit = pressed_ || (engine_ && engine_->status() == stunedrev::Status::Clearing);
        const CRect vs = getViewSize();
        CRect r(0, 0, kBoxPx, kBoxPx);
        r.offset(vs.left + 1.0, vs.top + std::ceil((vs.getHeight() - kBoxPx) / 2.0));
        c->setDrawMode(kAntiAliasing);
        c->setFrameColor(frame_);
        c->setFillColor(lit ? active_ : idle_);
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
    stunedrev::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool pressed_ = false;
};

class StunedrevFooter : public VSTGUI::CView {
public:
    StunedrevFooter(const VSTGUI::CRect& size, const stunedrev::Engine* engine,
                    VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 200, true);
    }
    ~StunedrevFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char l1[192], l2[128];
        const stunedrev::Status st = engine_->status();
        if (st == stunedrev::Status::Ready || st == stunedrev::Status::Clearing) {
            std::snprintf(l1, sizeof l1,
                "memory  \xE2\x88\x9A" "2 %.0f s \xC2\xB7 \xCF\x86 %.0f s \xC2\xB7 e %.0f s \xC2\xB7 \xCF\x80 %.0f s",
                engine_->centroidSeconds(0), engine_->centroidSeconds(1),
                engine_->centroidSeconds(2), engine_->centroidSeconds(3));
        } else {
            std::snprintf(l1, sizeof l1, "memory  \xE2\x80\x94");
        }
        const char* status = st == stunedrev::Status::Ready ? "ready"
                           : st == stunedrev::Status::Clearing ? "clearing\xE2\x80\xA6"
                           : st == stunedrev::Status::AllocFailed ? "allocation failed" : "inactive";
        std::snprintf(l2, sizeof l2, "arena %.0f MiB @ %.1f kHz \xC2\xB7 %s",
                      engine_->arenaBytes() / (1024.0 * 1024.0), engine_->sampleRate() / 1000.0, status);
        const CRect vs = getViewSize();
        c->setFont(font_);
        c->setFontColor(color_);
        c->drawString(l1, CRect(vs.left, vs.top, vs.right, vs.top + 16), kLeftText);
        c->drawString(l2, CRect(vs.left, vs.top + 18, vs.right, vs.top + 34), kLeftText);
        setDirty(false);
    }

private:
    const stunedrev::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
