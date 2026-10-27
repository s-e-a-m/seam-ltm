//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — the two views the uidesc cannot describe
//
// RESET is not a parameter (stunedrev's button): a momentary parameter is
// lost when the host coalesces 0->1->0 into one point. The plugin is a
// SingleComponentEffect, so the view reaches the engine directly and asks
// it to empty the memory (a generation counter). The square is filled while
// the engine clears.
//
// The footer is the printed solution of the settings, read by a GUI timer:
//   -30 dB  A 201 · B 230 · C 260 · D 289 s       the estimated decay of each
//   -60 dB  A 355 · B ... · D 509 s               face after the input closes
//   first A 9.7 ms · last D 88.2 s                the table's ends (exact)
//   arena 579 MiB @ 96 kHz · ready                memory, rate, status
// The decay (inphirev_decay.h, a few ms per face) is computed here, on the
// GUI thread, only when the applied settings or the rate change; never in
// process(). The spec's sketch puts -30 and -60 dB on one line: at InfoFont
// that line is about 530 px, wider than the 400 px footer, so they take two.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "inphirev_dsp.h"
#include "inphirev_decay.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace Seam {

class InphirevResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    InphirevResetButton(const VSTGUI::CRect& size, inphirev::Engine* engine,
                        const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                        const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50, true);
    }
    ~InphirevResetButton() override { if (timer_) timer_->stop(); }

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
    inphirev::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool pressed_ = false;
};

class InphirevFooter : public VSTGUI::CView {
public:
    InphirevFooter(const VSTGUI::CRect& size, const inphirev::Engine* engine,
                   VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { refresh(); invalid(); }, 200, true);
        refresh();
    }
    ~InphirevFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char l1[128], l2[128], l3[128], l4[160];
        const inphirev::Status st = engine_->status();
        if (valid_) {
            std::snprintf(l1, sizeof l1, "\xE2\x88\x92" "30 dB  A %s \xC2\xB7 B %s \xC2\xB7 C %s \xC2\xB7 D %s s",
                          secs(decay_[0].t30).s, secs(decay_[1].t30).s,
                          secs(decay_[2].t30).s, secs(decay_[3].t30).s);
            std::snprintf(l2, sizeof l2, "\xE2\x88\x92" "60 dB  A %s \xC2\xB7 B %s \xC2\xB7 C %s \xC2\xB7 D %s s",
                          secs(decay_[0].t60).s, secs(decay_[1].t60).s,
                          secs(decay_[2].t60).s, secs(decay_[3].t60).s);
            std::snprintf(l3, sizeof l3, "first A %s \xC2\xB7 last D %s",
                          delayText(first_).s, delayText(last_).s);
        } else {
            std::snprintf(l1, sizeof l1, "\xE2\x88\x92" "30 dB  \xE2\x80\x94");
            std::snprintf(l2, sizeof l2, "\xE2\x88\x92" "60 dB  \xE2\x80\x94");
            std::snprintf(l3, sizeof l3, "first \xE2\x80\x94 \xC2\xB7 last \xE2\x80\x94");
        }
        const char* status =
              st == inphirev::Status::AllocFailed ? "allocation failed"
            : st == inphirev::Status::Unprepared  ? "inactive"
            : st == inphirev::Status::Clearing    ? "clearing\xE2\x80\xA6"
            : engine_->stoneId() == 0             ? "STONE id undeclared"
            :                                       "ready";
        const double khz = engine_->sampleRate() / 1000.0;
        std::snprintf(l4, sizeof l4, khz == std::floor(khz) ? "arena %.0f MiB @ %.0f kHz \xC2\xB7 %s"
                                                            : "arena %.0f MiB @ %.1f kHz \xC2\xB7 %s",
                      engine_->arenaBytes() / (1024.0 * 1024.0), khz, status);
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
        int id, j, n; double fs;
        bool operator==(const Key& o) const { return id == o.id && j == o.j && n == o.n && fs == o.fs; }
    };
    struct Text { char s[24]; };

    Key readKey() const {
        return { engine_->stoneId(), engine_->grid(), engine_->sections(), engine_->sampleRate() };
    }

    // The table is written by the audio thread only when a clearing begins
    // (and in prepare), before the settings are published; it is read here
    // only while the engine is ready and the settings and the count of
    // clearings are the same before and after the read. A torn read is
    // retried at the next tick.
    void refresh() {
        const inphirev::Status st = engine_->status();
        if (st == inphirev::Status::Unprepared || st == inphirev::Status::AllocFailed) { valid_ = false; return; }
        if (st != inphirev::Status::Ready) return;   // clearing: the last solution stays
        const Key k = readKey();
        if (valid_ && k == key_) return;
        const uint32_t clears = engine_->clears();
        if (k.n < 1 || k.n > inphirev::kMaxSections || k.fs <= 0.0) return;
        uint32_t d[inphirev::kFaces][inphirev::kMaxSections];
        for (int f = 0; f < inphirev::kFaces; ++f)
            for (int i = 0; i < k.n; ++i) d[f][i] = engine_->delay(f, i);
        if (!(readKey() == k) || engine_->clears() != clears ||
            engine_->status() != inphirev::Status::Ready) return;
        for (int f = 0; f < inphirev::kFaces; ++f)
            decay_[f] = inphirev::estimateDecay(d[f], k.n, k.fs);
        first_ = d[0][0] / k.fs;
        last_  = d[inphirev::kFaces - 1][k.n - 1] / k.fs;
        key_ = k;
        valid_ = true;
    }

    // Seconds: one decimal below 10 s, whole seconds above; beyond the
    // estimate's horizon (+inf) "> 3000".
    static Text secs(double t) {
        Text x;
        if (!std::isfinite(t))  std::snprintf(x.s, sizeof x.s, ">%.0f", inphirev::kHorizon);
        else if (t < 10.0)      std::snprintf(x.s, sizeof x.s, "%.1f", t);
        else                    std::snprintf(x.s, sizeof x.s, "%.0f", t);
        return x;
    }
    // A delay: ms below one second, seconds above.
    static Text delayText(double t) {
        Text x;
        if (t < 1.0) std::snprintf(x.s, sizeof x.s, "%.1f ms", t * 1000.0);
        else         std::snprintf(x.s, sizeof x.s, "%.1f s", t);
        return x;
    }

    const inphirev::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    Key key_ { 0, 0, 0, 0.0 };
    bool valid_ = false;
    inphirev::Decay decay_[inphirev::kFaces] {};
    double first_ = 0.0, last_ = 0.0;
};

} // namespace Seam
