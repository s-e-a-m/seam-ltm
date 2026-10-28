//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — the two views the uidesc cannot describe
//
// RESET is not a parameter (stunedrev's button): a momentary parameter is
// lost when the host coalesces 0->1->0 into one point. The plugin is a
// SingleComponentEffect, so the view reaches the engine directly and asks
// it to empty the memory (a generation counter). The square is filled while
// the engine clears.
//
// The footer is the printed solution of the settings, read by a GUI timer:
//   -30 dB  A 201 · B 230 · C 260 · D 289 s       the estimated decay of each
//   -60 dB  A 355 · B ... · D 509 s               face with the setting's g
//   first A 9.7 ms · last D 54.6 s · N 17 · max 19   the table's ends (exact),
//                                                 effective sections and N_max
//   arena 343 MiB @ 96 kHz · ready                memory, rate, status
// The decay (alogos_decay.h) is computed here, on the GUI thread, only when
// the applied settings or the rate change; never in process(). The text of
// every line comes from alogos_text.h, which the engine test reads.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "alogos_dsp.h"
#include "alogos_decay.h"
#include "alogos_text.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace Seam {

class AlogosResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    AlogosResetButton(const VSTGUI::CRect& size, alogos::Engine* engine,
                      const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                      const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50, true);
    }
    ~AlogosResetButton() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const bool lit = pressed_ || (engine_ && engine_->clearing());
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
    alogos::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool pressed_ = false;
};

class AlogosFooter : public VSTGUI::CView {
public:
    AlogosFooter(const VSTGUI::CRect& size, const alogos::Engine* engine,
                 VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { refresh(); invalid(); }, 200, true);
        refresh();
    }
    ~AlogosFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char l1[128], l2[128], l3[128], l4[160];
        if (valid_) {
            alogos::decayLine(l1, sizeof l1, 30, decay_);
            alogos::decayLine(l2, sizeof l2, 60, decay_);
            alogos::tableLine(l3, sizeof l3, readout_);
        } else {
            std::snprintf(l1, sizeof l1, "\xE2\x88\x92" "30 dB  \xE2\x80\x94");
            std::snprintf(l2, sizeof l2, "\xE2\x88\x92" "60 dB  \xE2\x80\x94");
            std::snprintf(l3, sizeof l3, "first \xE2\x80\x94 \xC2\xB7 last \xE2\x80\x94");
        }
        // While allocating, the arena printed is the one still held (see arenaLine).
        alogos::arenaLine(l4, sizeof l4, engine_->arenaBytes(), engine_->sampleRate(),
                          alogos::statusText(engine_->status(), engine_->stoneId()));
        const CRect vs = getViewSize();
        c->setFont(font_);
        c->setFontColor(color_);
        const char* lines[4] = { l1, l2, l3, l4 };
        for (int i = 0; i < 4; ++i)
            c->drawString(lines[i], CRect(vs.left, vs.top + 18 * i, vs.right, vs.top + 18 * i + 16), kLeftText);
        setDirty(false);
    }

private:
    struct Key {
        int id, n, scale, dn, gain; double metres, fs;
        bool operator==(const Key& o) const {
            return id == o.id && n == o.n && scale == o.scale && dn == o.dn && gain == o.gain &&
                   metres == o.metres && fs == o.fs;
        }
    };

    Key readKey() const {
        return { engine_->stoneId(), engine_->sections(), engine_->scale(), engine_->densityIdx(),
                 engine_->gain(), engine_->metres(), engine_->sampleRate() };
    }

    // The table is published by the audio thread only while the output is
    // silent (a clearing or an allocation, and in prepare), or unchanged when
    // only the readouts move; it is read here only while the engine is ready
    // and the settings and the count of clearings are the same before and
    // after the read. A torn read is retried at the next tick.
    void refresh() {
        const alogos::Status st = engine_->status();
        if (st == alogos::Status::Unprepared || st == alogos::Status::AllocFailed) { valid_ = false; return; }
        if (st != alogos::Status::Ready) return;   // clearing, allocating: the last solution stays
        const Key k = readKey();
        if (valid_ && k == key_) return;
        const uint32_t clears = engine_->clears();
        if (k.n < 1 || k.n > alogos::kMaxSections || k.fs <= 0.0) return;
        uint32_t d[alogos::kFaces][alogos::kMaxSections];
        for (int f = 0; f < alogos::kFaces; ++f)
            for (int i = 0; i < k.n; ++i) d[f][i] = engine_->delay(f, i);
        const alogos::Readout r = alogos::readout(*engine_);
        if (!(readKey() == k) || engine_->clears() != clears ||
            engine_->status() != alogos::Status::Ready) return;
        const double g = alogos::gainValue((alogos::Gain)k.gain);
        for (int f = 0; f < alogos::kFaces; ++f)
            decay_[f] = alogos::estimateDecay(d[f], k.n, k.fs, g);
        readout_ = r;
        key_ = k;
        valid_ = true;
    }

    const alogos::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    Key key_ { 0, 0, 0, 0, 0, 0.0, 0.0 };
    bool valid_ = false;
    alogos::Decay decay_[alogos::kFaces] {};
    alogos::Readout readout_ { 0, 0, 0, 0, 0.0 };
};

} // namespace Seam
