#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "dslar_ids.h"

#include <cmath>

namespace Seam {

// UI-only "reset to Di Scipio defaults" button.
//
// Deliberately NOT a VST3 parameter: a momentary fire-on-click parameter is
// unreliable because the host may coalesce the 0->1->0 transition within a
// single process block and drop the click. Instead the click drives a pure-GUI
// gesture that ramps the six "character" parameters back to their LAR.pd
// defaults over kRampMs via the edit controller's begin/perform/endEdit.
//
// Power and Output are left untouched, so a reset during a live (acoustic)
// Larsen session neither silences the system nor moves the master level — it
// snaps only the homeostat's behaviour back to Di Scipio's baseline. The ramp
// echoes LAR's own spd.line idiom, where every control glides rather than jumps.
//
// Visual: a small square. Empty at rest; filled azure while the reset ramps;
// empties again when the ramp completes.
class DslarResetButton : public VSTGUI::CView {
public:
    static constexpr double   kRampMs = 1000.0;  // glide duration (tunable)
    static constexpr uint32_t kTickMs = 20;      // timer period (Pd 20 ms grain)
    static constexpr double   kBoxPx  = 12.0;    // drawn box size, matched to CCheckBox

    struct Target { Steinberg::Vst::ParamID tag; double plainDefault; };

    DslarResetButton(const VSTGUI::CRect& size, Steinberg::Vst::EditController* controller,
                     const VSTGUI::CColor& frame, const VSTGUI::CColor& idleFill,
                     const VSTGUI::CColor& activeFill)
        : VSTGUI::CView(size), controller_(controller),
          frame_(frame), idle_(idleFill), active_(activeFill) {}

    ~DslarResetButton() override { stopRamp(/*commitDefaults*/true); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const bool lit = pressed_ || resetting_;
        const CRect vs = getViewSize();
        // Match the Power CCheckBox box exactly: a small square at viewLeft+1,
        // vertically centred (see CCheckBox::draw). Empty (bg fill) at rest;
        // azure fill while the reset ramps.
        const CCoord box = kBoxPx;
        CRect r(0, 0, box, box);
        r.offset(vs.left + 1.0, vs.top + std::ceil((vs.getHeight() - box) / 2.0));
        c->setDrawMode(kAntiAliasing);
        c->setFrameColor(frame_);
        c->setFillColor(lit ? active_ : idle_);
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawFilledAndStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& /*where*/,
                                          const VSTGUI::CButtonState& buttons) override {
        using namespace VSTGUI;
        if (resetting_ || !buttons.isLeftButton()) return kMouseEventNotHandled;
        pressed_ = true;
        invalid();
        return kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& /*buttons*/) override {
        using namespace VSTGUI;
        if (!pressed_) return kMouseEventNotHandled;
        pressed_ = false;
        if (getViewSize().pointInside(where)) startRamp();  // cancel if released outside
        invalid();
        return kMouseEventHandled;
    }

private:
    void startRamp() {
        if (!controller_ || resetting_) return;
        for (int i = 0; i < kN; ++i) {
            start_[i]  = controller_->getParamNormalized(kParams[i].tag);
            target_[i] = controller_->plainParamToNormalized(kParams[i].tag,
                                                             kParams[i].plainDefault);
            controller_->beginEdit(kParams[i].tag);
        }
        resetting_ = true;
        elapsedMs_ = 0.0;
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { onTick(); }, kTickMs, /*doStart*/true);
        invalid();
    }

    void onTick() {
        elapsedMs_ += static_cast<double>(kTickMs);
        double frac = elapsedMs_ / kRampMs;
        if (frac > 1.0) frac = 1.0;
        for (int i = 0; i < kN; ++i) {
            const double v = start_[i] + frac * (target_[i] - start_[i]);
            controller_->setParamNormalized(kParams[i].tag, v);  // move slider
            controller_->performEdit(kParams[i].tag, v);         // move DSP (via host)
        }
        invalid();
        if (frac >= 1.0) stopRamp(/*commitDefaults*/false);      // already at target
    }

    // End the edit gesture. commitDefaults snaps to the target first — used only
    // if the view is destroyed mid-ramp, so we never leave a partial reset with
    // an edit gesture still open.
    void stopRamp(bool commitDefaults) {
        if (timer_) { timer_->stop(); timer_->forget(); timer_ = nullptr; }
        if (!resetting_) return;
        for (int i = 0; i < kN; ++i) {
            if (commitDefaults) {
                controller_->setParamNormalized(kParams[i].tag, target_[i]);
                controller_->performEdit(kParams[i].tag, target_[i]);
            }
            controller_->endEdit(kParams[i].tag);
        }
        resetting_ = false;
    }

    static constexpr int kN = 6;
    static constexpr Target kParams[kN] = {
        { kParamDrive,     kDriveDef  },
        { kParamTarget,    kTargetDef },
        { kParamSteepness, kSteepDef  },
        { kParamSmoothing, kSmoothDef },
        { kParamLoopDelay, kTab1Def   },
        { kParamDecorr,    kTab2Def   },
    };

    Steinberg::Vst::EditController* controller_ = nullptr;
    VSTGUI::CColor frame_, idle_, active_;
    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    bool   pressed_   = false;
    bool   resetting_ = false;
    double elapsedMs_ = 0.0;
    double start_[kN]  = {0.0};
    double target_[kN] = {0.0};
};

} // namespace Seam
