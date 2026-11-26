//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · lessness — the three panes and the footer's sheet line.
//
// Each view polls the hub on its own CVSTGUITimer, on the GUI thread, as
// strx's status line does (strx_status.h). The difference is where the hub
// is read: strx reads it in draw(), here every snapshot is taken in the timer
// and turned into lines there, and draw() only paints the lines it was left.
// A repaint is asked for only when the lines changed, so an idle hub costs
// four snapshots a second and no painting.
//
// 250 ms is the sheet writer's tick: nothing on the hub reaches the sheet
// faster, so polling faster would show nothing new.
//
// The text is InfoFont (Source Code Pro Light 11, a monospace face), so the
// sheet's columns line up here as they do in the file. Lines longer than the
// pane are clipped at its edge, never wrapped: a wrapped sheet line would
// read as two.
//
// What the lines say is decided in lessness_view.h, pure and tested; this
// file only fetches, fits and paints.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include "lessness_view.h"
#include "seam_hub_client.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Seam {

// A pane of monospace lines, refreshed by its own timer.
class LessnessPane : public VSTGUI::CView {
public:
    static constexpr uint32_t       kTimerMs = 250;   // the writer's tick
    static constexpr VSTGUI::CCoord kLineH   = 14.0;  // 11 px text, 3 px leading

    LessnessPane(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                 const VSTGUI::CColor& textColor, bool bottomAligned)
        : VSTGUI::CView(size), font_(font), textColor_(textColor), bottom_(bottomAligned) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { refresh(); }, kTimerMs, /*doStart*/true);
    }

    ~LessnessPane() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    // The first snapshot as soon as the view is on screen, not a tick later.
    bool attached(VSTGUI::CView* parent) override {
        const bool r = VSTGUI::CView::attached(parent);
        refresh();
        return r;
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        const CRect r = getViewSize();
        ConcatClip clip(*c, r);            // clipped, never wrapped
        c->setDrawMode(kAntiAliasing);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        const int n = (int)lines_.size();
        CCoord y = bottom_ ? r.bottom - n * kLineH : r.top;
        for (const std::string& line : lines_) {
            if (!line.empty())
                c->drawString(line.c_str(), CRect(r.left, y, r.right, y + kLineH), kLeftText);
            y += kLineH;
        }
        setDirty(false);
    }

protected:
    // GUI thread, from the timer: the lines the pane should show now, at most
    // rows() of them. Read the hub here, never in draw().
    virtual std::vector<std::string> snapshot() = 0;

    int rows() const { return (int)(getViewSize().getHeight() / kLineH); }

private:
    void refresh() {
        std::vector<std::string> next = snapshot();
        if (next != lines_) {
            lines_.swap(next);
            invalid();
        }
    }

    VSTGUI::CVSTGUITimer*    timer_ = nullptr;
    VSTGUI::CFontRef         font_ = nullptr;
    VSTGUI::CColor           textColor_;
    bool                     bottom_ = false;
    std::vector<std::string> lines_;
};

namespace lessness {

// How many two-line entries fit in `rows` rows when there are `count`: all
// of them, or as many as leave one row for "+N more". `more` is N, or 0.
inline int fitPairs(int count, int rows, int& more) {
    more = 0;
    if (count * 2 <= rows) return count;
    const int shown = rows > 0 ? (rows - 1) / 2 : 0;
    more = count - shown;
    return shown;
}

} // namespace lessness

// — MEMBERS —: each member's line, and under it, indented, its state.
class LessnessMembers : public LessnessPane {
public:
    LessnessMembers(const VSTGUI::CRect& size, VSTGUI::CFontRef font, const VSTGUI::CColor& text)
        : LessnessPane(size, font, text, /*bottomAligned*/false) {}

protected:
    std::vector<std::string> snapshot() override {
        HubClient& hub = HubClient::instance();
        if (!hub.available()) return {"hub unavailable"};
        const int32_t n = hub.members(members_, SEAM_HUB_MAX_MEMBERS);
        if (n <= 0) return {"no members"};
        int more = 0;
        const int shown = lessness::fitPairs(n, rows(), more);
        std::vector<std::string> out;
        for (int i = 0; i < shown; ++i) {
            out.push_back(lessness::memberLine(members_[i]));
            out.push_back("  " + lessness::stateLine(members_[i]));
        }
        if (more > 0) out.push_back("+" + std::to_string(more) + " more members");
        return out;
    }

private:
    SeamHubMember members_[SEAM_HUB_MAX_MEMBERS];
};

// — PROPOSALS —: the whole store, pending first, each as its head and, under
// it, where each receiver stands; kStoneNote always on the pane's last line.
class LessnessProposals : public LessnessPane {
public:
    LessnessProposals(const VSTGUI::CRect& size, VSTGUI::CFontRef font, const VSTGUI::CColor& text)
        : LessnessPane(size, font, text, /*bottomAligned*/false),
          proposals_((size_t)SEAM_HUB_STORE) {}

protected:
    std::vector<std::string> snapshot() override {
        HubClient& hub = HubClient::instance();
        if (!hub.available()) return {"hub unavailable"};
        const int32_t k = hub.members(members_, SEAM_HUB_MAX_MEMBERS);
        const int32_t n = hub.proposals(0, proposals_.data(), SEAM_HUB_STORE);
        const auto table = lessness::proposalRows(
            proposals_.data(), n, members_, k,
            [&hub](uint32_t m, uint32_t e, uint64_t id) { return hub.lifecycleOf(m, e, id); });

        const int body = rows() - 1;           // the last row is the note's
        std::vector<std::string> out;
        if (table.empty()) {
            out.push_back("no proposals in the store");
        } else {
            int more = 0;
            const int shown = lessness::fitPairs((int)table.size(), body, more);
            for (int i = 0; i < shown; ++i) {
                out.push_back(table[(size_t)i].head);
                out.push_back("  " + lessness::receiversLine(table[(size_t)i]));
            }
            if (more > 0) out.push_back("+" + std::to_string(more) + " more proposals");
        }
        while ((int)out.size() < body) out.emplace_back();
        out.push_back(lessness::kStoneNote);
        return out;
    }

private:
    SeamHubMember                members_[SEAM_HUB_MAX_MEMBERS];
    std::vector<SeamHubProposal> proposals_;   // the store's size, allocated once
};

// — SHEET —: the newest recent lines that fit, the newest at the bottom.
class LessnessLines : public LessnessPane {
public:
    LessnessLines(const VSTGUI::CRect& size, VSTGUI::CFontRef font, const VSTGUI::CColor& text)
        : LessnessPane(size, font, text, /*bottomAligned*/true) {}

protected:
    std::vector<std::string> snapshot() override {
        // In null mode recentLines() is "", and the footer says why.
        const std::string all = HubClient::instance().recentLines();
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < all.size()) {
            size_t end = all.find('\n', start);
            if (end == std::string::npos) end = all.size();
            std::string line = all.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = end + 1;
        }
        const int fit = rows();
        if ((int)lines.size() > fit)
            lines.erase(lines.begin(), lines.end() - fit);
        return lines;
    }
};

// FOOTER: where the sheet is written and whether it is, in white, after a
// red square when it is not (sheetAlarm). While a folder chosen with FOLDER
// is waiting for the next sheet, the line says so instead, until the sheet's
// path is inside that folder.
class LessnessSheet : public VSTGUI::CView {
public:
    static constexpr uint32_t       kTimerMs = 250;
    static constexpr VSTGUI::CCoord kSquare  = 10.0;
    static constexpr VSTGUI::CCoord kGap     = 8.0;

    LessnessSheet(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                  const VSTGUI::CColor& textColor, const VSTGUI::CColor& alarmColor,
                  std::shared_ptr<std::string> nextFolder)
        : VSTGUI::CView(size), font_(font), textColor_(textColor), alarmColor_(alarmColor),
          nextFolder_(std::move(nextFolder)) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { refresh(); }, kTimerMs, /*doStart*/true);
    }

    ~LessnessSheet() override {
        if (timer_) { timer_->stop(); timer_->forget(); }
        if (font_) font_->forget();
    }

    bool attached(VSTGUI::CView* parent) override {
        const bool r = VSTGUI::CView::attached(parent);
        refresh();
        return r;
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        const CRect r = getViewSize();
        ConcatClip clip(*c, r);
        c->setDrawMode(kAntiAliasing);
        CCoord x = r.left;
        if (alarm_) {
            const CCoord top = r.top + (r.getHeight() - kSquare) / 2.0;
            c->setFillColor(alarmColor_);
            c->drawRect(CRect(x, top, x + kSquare, top + kSquare), kDrawFilled);
            x += kSquare + kGap;
        }
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        c->drawString(text_.c_str(), CRect(x, r.top, r.right, r.bottom), kLeftText);
        setDirty(false);
    }

private:
    void refresh() {
        HubClient& hub = HubClient::instance();
        const bool available = hub.available();
        const SeamHubSheetStatus status = hub.sheetStatus();
        std::string text = lessness::sheetLine(status, available);
        if (available && nextFolder_ && !nextFolder_->empty()) {
            const std::string path = hubsheet::detail::oneLine(status.path, sizeof status.path);
            const std::string& folder = *nextFolder_;
            if (path.compare(0, folder.size() + 1, folder + "/") == 0)
                nextFolder_->clear();      // the sheet has moved there: done
            else
                text = "next sheet in " + lessness::tildePath(folder, std::getenv("HOME")) +
                       " \xC2\xB7 NEW SHEET to start it";
        }
        const bool alarm = lessness::sheetAlarm(status, available);
        if (text != text_ || alarm != alarm_) {
            text_ = std::move(text);
            alarm_ = alarm;
            invalid();
        }
    }

    VSTGUI::CVSTGUITimer*        timer_ = nullptr;
    VSTGUI::CFontRef             font_ = nullptr;
    VSTGUI::CColor               textColor_, alarmColor_;
    std::shared_ptr<std::string> nextFolder_;
    std::string                  text_;
    bool                         alarm_ = false;
};

} // namespace Seam
