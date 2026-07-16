//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — calibration-bus status line (calbus Task 5).
//
// The one user-visible feature of the calibration bus, and its diagnostic:
// it answers "which emitter am I hearing?" — the question the pool alone
// cannot answer, because the pool tracks slot OWNERSHIP and four loaded
// multipink instances all own their slots while only one is sounding.
//
// Reads the bus from the GUI thread ONLY, on its own CVSTGUITimer, mirroring
// StrxMeters/StrxGoniometer/StrxSpectrum: the timer just invalidates, and
// draw() reads current state fresh on every paint. strx's audio thread never
// touches the bus — that arrives with a later spec, and the seqlock in
// seam_calbus.h is already built for it.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include "seam_calbus_client.h"

#include <cstdio>
#include <string>

namespace Seam {

class StrxStatusLine : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // ~10 Hz: text, not animation

    StrxStatusLine(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                   const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), font_(font), textColor_(textColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, kTimerMs, /*doStart*/true);
    }

    ~StrxStatusLine() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        VSTGUI::CView::draw(c);
        c->setDrawMode(VSTGUI::kAntiAliasing);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        const std::string text = compose();
        c->drawString(text.c_str(), getViewSize(), VSTGUI::kLeftText);
        setDirty(false);
    }

private:
    static void appendStone(char* buf, size_t n, uint32_t stoneId) {
        if (stoneId >= 1 && stoneId <= 8) std::snprintf(buf, n, "STONE %u", stoneId);
        else                              std::snprintf(buf, n, "STONE ?");
    }

    // recs[i].kind discriminates the union: only read the arm that matches.
    // r.u.glide.passStartSample < 0 has exactly one contracted meaning (no
    // valid host continuous clock, seam_calbus.h:82) — it never signals
    // anything about whether the pass has ended.
    static std::string describe(const SeamCalbusRecord& r) {
        char stone[16];
        appendStone(stone, sizeof(stone), r.stoneId);
        char buf[192];
        if (r.kind == (uint32_t)kSeamCalbusPink) {
            std::snprintf(buf, sizeof(buf), "multipink \xC2\xB7 %s \xC2\xB7 slot %d-%d \xC2\xB7 %.1f dB",
                          stone, r.u.pink.slotStart,
                          r.u.pink.slotStart + r.u.pink.slotCount - 1, r.levelDb);
        } else {
            char clock[24];
            if (r.u.glide.passStartSample < 0)
                std::snprintf(clock, sizeof(clock), "no host clock");
            else
                std::snprintf(clock, sizeof(clock), "T=%.0fs", r.u.glide.durationSec);
            std::snprintf(buf, sizeof(buf),
                          "ltglide \xC2\xB7 %s \xC2\xB7 pass %llu \xC2\xB7 %.0f\xE2\x86\x92%.0f Hz \xC2\xB7 %s",
                          stone, (unsigned long long)r.u.glide.passCounter,
                          r.u.glide.f0, r.u.glide.f1, clock);
        }
        // (UTF-8 byte escapes above for the middle-dot/arrow glyphs — keeps
        // the source ASCII-clean regardless of editor/terminal encoding.)
        return buf;
    }

    static std::string compose() {
        auto& client = CalbusClient::instance();
        if (!client.available()) return "calbus unavailable";

        SeamCalbusRecord recs[SEAM_CALBUS_MAX_SLOTS];
        const int32_t n = client.snapshot(recs, SEAM_CALBUS_MAX_SLOTS);
        if (n == 0) return "calbus: no emitter";

        // One emitter sounds at a time by method (see the design doc), so the
        // first active record is the answer. Registered-but-silent emitters
        // are reported as a count, which is how you notice that the multipink
        // you meant to un-mute is still muted.
        int registered = 0;
        for (int32_t i = 0; i < n; ++i) {
            if (!recs[i].active) { ++registered; continue; }
            return describe(recs[i]);
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "calbus: %d idle, none sounding", registered);
        return buf;
    }

    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    VSTGUI::CFontRef      font_ = nullptr;
    VSTGUI::CColor        textColor_;
};

} // namespace Seam
