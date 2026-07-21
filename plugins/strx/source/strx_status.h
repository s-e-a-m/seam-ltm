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
// draw() reads current state fresh on every paint. compose() reads the
// digest CalbusWatch caches on the processor (strx_calbus_watch.h) — the
// same digest the spectrum view uses — never its own walk of the snapshot.
// strx's audio thread still never touches the bus directly: what process()
// needs (glide mode, hold-reset) reaches it through two atomics the watch
// writes, read once per block.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include "strx_calbus_watch.h"
#include "strx_processor.h"

#include <cstdio>
#include <string>

namespace Seam {

class StrxStatusLine : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // ~10 Hz: text, not animation

    StrxStatusLine(const VSTGUI::CRect& size, StrxProcessor* processor,
                   VSTGUI::CFontRef font, const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), processor_(processor), font_(font), textColor_(textColor) {
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
            // sweepMode: 0 linear, 1 exponential. GS chased a "wrong curve"
            // for an hour before this was visible on the line at all.
            const char* mode = (r.u.glide.sweepMode == 1) ? "exp" : "lin";
            std::snprintf(buf, sizeof(buf),
                          "ltglide \xC2\xB7 %s \xC2\xB7 pass %llu \xC2\xB7 %.0f\xE2\x86\x92%.0f Hz \xC2\xB7 %s \xC2\xB7 %s",
                          stone, (unsigned long long)r.u.glide.passCounter,
                          r.u.glide.f0, r.u.glide.f1, mode, clock);
        }
        // (UTF-8 byte escapes above for the middle-dot/arrow glyphs — keeps
        // the source ASCII-clean regardless of editor/terminal encoding.)
        return buf;
    }

    // Reads the ONE digest CalbusWatch caches for both this view and the
    // spectrum (strx_calbus_digest.h) — never its own walk of the snapshot,
    // so the two views cannot name different emitters at the same instant.
    std::string compose() {
        if (!processor_) return "calbus unavailable";
        Seam::strx::CalbusWatch& watch = processor_->calbusWatch();
        const Seam::strx::CalbusDigest& d = watch.poll();
        if (!d.available) return "calbus unavailable";
        if (d.count == 0) return "calbus: no emitter";

        // One emitter sounds at a time BY METHOD (see the design doc), so the
        // first active record is the headline answer. But "by method" is an
        // operator discipline, not something the bus enforces, and the most
        // likely slip in the room is un-muting one STONE while forgetting to
        // mute another. Silently naming only the first active record would
        // hide exactly that mistake during the session that depends on this
        // line catching it — so the digest counts ALL active records, not
        // just the idle ones, and flags it when more than one is sounding.
        if (d.firstActive < 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "calbus: %d idle, none sounding", d.idleCount);
            return buf;
        }
        std::string text = describe(watch.records()[d.firstActive]);
        if (d.activeCount > 1) {
            // Kept compact on purpose, not out of necessity: at 560px/11px
            // (Source Code Pro Light, ~6.6 px/char) the longest record is the
            // ltglide branch with a "no host clock" clock field (longer than
            // any realistic "T=%.0fs"), an unresolved STONE ("STONE ?"), and
            // now the sweepMode word this describe() also appends — e.g.
            // "ltglide · STONE ? · pass 9999 · 20000→20 Hz · exp · no host
            // clock" is 65 glyphs; plus this flag (~10 glyphs) that is ~75
            // glyphs (~495px), leaving ~65px of headroom on the 560px view —
            // tighter than before the sweepMode word (was ~124px), but still
            // positive, and still room enough for a second emitter's name if
            // we ever wanted one. The flag stays a count because "who else"
            // matters less than "someone else is sounding": that's the fact
            // this line exists to surface (see the comment above on the
            // un-mute slip).
            char extra[16];
            std::snprintf(extra, sizeof(extra), " \xC2\xB7 +%d more", d.activeCount - 1);
            text += extra;
        }
        return text;
    }

    StrxProcessor*         processor_ = nullptr;
    VSTGUI::CVSTGUITimer*  timer_ = nullptr;
    VSTGUI::CFontRef       font_ = nullptr;
    VSTGUI::CColor         textColor_;
};

} // namespace Seam
