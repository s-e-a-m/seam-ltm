//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ltglide — single-shot launch button.
//
// With LOOP off, GlideTransport's Idle state has no exit but `if (loop_)
// beginPass()`, so ltglide is silent forever. This is the missing door.
//
// It owns no VST3 parameter and drives none. dslar's reset button is "UI-only"
// in that it owns no parameter of its own, but it still drives six real ones
// through the host; SHOT has no such road, because trigger() is internal
// transport state and there is nothing for a host to automate. Two atomics on
// the processor are the whole path — which also makes it immune to the
// momentary-button coalescing problem, since there is no parameter to coalesce.
//
// Lit for the pass's whole duration (~32 s: head Dirac + 5 s lead + 20 s sweep
// + 5 s tail + tail Dirac), so the panel says whether it is measuring.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"

#include "ltglide_processor.h"

namespace Seam {

class LtglideShotButton : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // lit-state poll; not animation

    LtglideShotButton(const VSTGUI::CRect& size, LTGLIDEProcessor* processor,
                      const VSTGUI::CColor& idleColor, const VSTGUI::CColor& litColor)
        : VSTGUI::CView(size), processor_(processor),
          idleColor_(idleColor), litColor_(litColor) {
        setWantsFocus(false);
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) {
                const bool r = processor_ && processor_->transportRunning();
                if (r != lit_) { lit_ = r; invalid(); }
            }, kTimerMs, /*doStart*/true);
    }

    ~LtglideShotButton() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
    }

    void draw(VSTGUI::CDrawContext* c) override {
        c->setDrawMode(VSTGUI::kAntiAliasing);
        const VSTGUI::CRect r = getViewSize();
        c->setFillColor(lit_ ? litColor_ : idleColor_);
        c->drawRect(r, VSTGUI::kDrawFilled);
        c->setFrameColor(litColor_);
        c->setLineWidth(1);
        c->drawRect(r, VSTGUI::kDrawStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override {
        if (buttons.isLeftButton() && processor_ && getViewSize().pointInside(where)) {
            processor_->requestShot();
            return VSTGUI::kMouseEventHandled;
        }
        return VSTGUI::kMouseEventNotHandled;
    }

private:
    LTGLIDEProcessor* processor_ = nullptr;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    VSTGUI::CColor idleColor_, litColor_;
    bool lit_ = false;
};

} // namespace Seam
