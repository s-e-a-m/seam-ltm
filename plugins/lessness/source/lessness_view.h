//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · lessness — what the detector shows, as text.
//
// lessness is a pass-through plugin that lets a person see what travels on
// the hub: who is registered, what each member is doing, which proposals are
// in the store and where each receiver stands on them, and the session sheet.
// This header computes the lines its views draw. It uses the sheet's own
// formatters (seam_hub_sheet.h), so the window and the sheet say the same
// things in the same words.
//
// Pure and SDK-free: no hub calls, no drawing. The lifecycle of a proposal
// for a receiver arrives through `LifecycleOf` (the client's lifecycle_of),
// so the tests need no dylib.
//
// GUI thread only: it reads snapshots the GUI thread took (members(),
// proposals(), sheet_status()); nothing here is safe to call from audio.
//
// Cites: seam_hub.h (types), seam_hub_sheet.h (who, stateObject, testWord),
// README "Lifecycle, per receiver" (why kDecisionsNote).
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub.h"
#include "seam_hub_sheet.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace Seam { namespace lessness {

using LifecycleOf = std::function<int32_t(uint32_t member, uint32_t epoch, uint64_t id)>;

// Where a receiver stands on a proposal is kept per registration epoch and
// per stone (README "Lifecycle, per receiver"). Two things therefore send its
// applied and ignored back to arrived in the proposals pane: a change of
// stone, and the host switching the receiver off and on, which the sheet
// folds into one `resumes` line but which is a new registration all the same
// (README "Open points"). The sheet keeps every decision. One line under the
// pane: at most kNoteChars, what the 600 px pane holds at Source Code Pro 11
// (0.6 em advance, 6.6 px a character).
inline constexpr int kNoteChars = 90;
inline const char* const kDecisionsNote =
    "a stone change or a host off-and-on resets a receiver's decisions; the sheet keeps them";

namespace detail {
inline const char* lifecycleWord(int32_t l) {
    switch (l) {
        case kSeamHubArrived: return "arrived";
        case kSeamHubShown:   return "shown";
        case kSeamHubApplied: return "applied";
        case kSeamHubIgnored: return "ignored";
        default:              return "none";
    }
}
inline bool hasPath(const SeamHubSheetStatus& s) {
    const std::string p = hubsheet::detail::oneLine(s.path, sizeof s.path);
    return p.find_first_not_of(' ') != std::string::npos;
}
inline bool isPending(int32_t l) { return l == kSeamHubArrived || l == kSeamHubShown; }
inline std::string memberWho(const SeamHubMember& m) {
    return hubsheet::who(hubsheet::detail::oneLine(m.name, sizeof m.name).c_str(), m.instance);
}
} // namespace detail

inline std::string memberLine(const SeamHubMember& m) {
    const bool stone = m.hasState && m.state.stoneId != 0;
    return hubsheet::detail::pad(detail::memberWho(m), 11) + " stone " +
           (stone ? std::to_string(m.state.stoneId) : std::string("-")) +
           "  says " + hubsheet::maskNames(m.says) + "  hears " + hubsheet::maskNames(m.hears);
}

inline std::string stateLine(const SeamHubMember& m) {
    return m.hasState ? hubsheet::stateObject(m.state) : std::string("no state");
}

struct Receiver { std::string who; int32_t lifecycle; };   // SeamHubLifecycle
struct ProposalRow {
    uint64_t id = 0; bool pending = false;   // some receiver at Arrived or Shown
    std::string head;
    std::vector<Receiver> receivers;         // every member with lifecycle != None, in member order
};

inline std::string proposalHead(const SeamHubProposal& p) {
    const std::string author = hubsheet::who(
        hubsheet::detail::oneLine(p.authorName, sizeof p.authorName).c_str(), p.authorInstance);
    std::time_t t = (std::time_t)(p.postedUnixMs / 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    char hm[8]; std::snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    std::string s;
    if (p.type == kSeamHubBandTable) {
        const SeamHubBandTable& b = p.u.table;
        s = "band-table #" + std::to_string(p.id) + "  " + author + "  stone " + std::to_string(p.stoneId) +
            "  " + (b.hasDecoder ? hubsheet::detail::testWord(b.decoder.test) : "-") + "  " + hm +
            "  " + (std::isfinite(b.integrationSec) && std::fabs(b.integrationSec) < 1e15
                 ? std::to_string((long long)std::llround(b.integrationSec)) : std::string("-")) + " s";
    } else if (p.type == kSeamHubSections) {
        const SeamHubSections& x = p.u.sections;
        s = "sections #" + std::to_string(p.id) + "  " + author + "  stone " + std::to_string(p.stoneId) +
            "  " + (x.block == kSeamHubBlockDifference ? "difference" : "common") + "  " + hm +
            "  " + std::to_string(x.count) + " sections";
    } else {
        s = "proposal #" + std::to_string(p.id) + "  " + author + "  stone " + std::to_string(p.stoneId) +
            "  " + hm;
    }
    return s;
}

// Pending first, then the rest; newest (highest id) first within each group.
inline std::vector<ProposalRow> proposalRows(const SeamHubProposal* p, int32_t n,
                                             const SeamHubMember* m, int32_t k,
                                             const LifecycleOf& of) {
    std::vector<ProposalRow> rows;
    if (!p || n <= 0) return rows;
    for (int32_t i = 0; i < n; ++i) {
        ProposalRow r;
        r.id = p[i].id;
        r.head = proposalHead(p[i]);
        for (int32_t j = 0; m && j < k; ++j) {
            const int32_t l = of ? of(m[j].member, m[j].epoch, p[i].id) : (int32_t)kSeamHubNone;
            if (l == kSeamHubNone) continue;
            r.receivers.push_back({detail::memberWho(m[j]), l});
            if (detail::isPending(l)) r.pending = true;
        }
        rows.push_back(std::move(r));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const ProposalRow& a, const ProposalRow& b) {
        if (a.pending != b.pending) return a.pending;
        return a.id > b.id;
    });
    return rows;
}

inline std::string receiversLine(const ProposalRow& r) {
    if (r.receivers.empty()) return "heard by nobody now";
    std::string s;
    for (const Receiver& x : r.receivers)
        s += (s.empty() ? "" : "  ") + x.who + " " + detail::lifecycleWord(x.lifecycle);
    return s;
}

// $HOME prefix -> "~", only at a path-component boundary.
inline std::string tildePath(const std::string& path, const char* home) {
    if (!home || !*home) return path;
    const size_t n = std::strlen(home);
    if (path.compare(0, n, home) == 0 && (path.size() == n || path[n] == '/'))
        return "~" + path.substr(n);
    return path;
}

// The sheet's states as sheet_status() reports them (seam_hub.cpp, Sheet):
//   no path                      no session (the writer clears the path when a
//                                session ends with nothing held, publishEnded);
//   path, writing                the sheet is being written;
//   path, not writing, held > 0  the folder is unreachable and lines wait:
//                                the alarm (spec 2026-11-25, the red mark is
//                                for an unreachable folder);
//   path, not writing, held 0    not open, nothing waiting. It still occurs:
//                                for an instant between begin() and open() on
//                                every new sheet (start, renew, a return in
//                                the grace), and while a sheet whose folder
//                                failed at its first open has nothing to
//                                write. No alarm, because nothing is at risk
//                                and a transient must not flash red; the
//                                first line enqueued makes it held > 0, and
//                                the alarm then rises if the folder is gone.
inline std::string sheetLine(const SeamHubSheetStatus& s, bool available) {
    if (!available) return "hub unavailable";
    const std::string path = hubsheet::detail::oneLine(s.path, sizeof s.path);
    if (!detail::hasPath(s)) return "no session";
    const std::string shown = tildePath(path, std::getenv("HOME"));
    if (s.writing) return shown + "  writing";
    if (s.pendingLines == 0) return shown + "  not open, nothing held";
    // The alarm's counts lead: a long path (a /Volumes one) runs off the
    // pane's right edge, and what gets clipped must be the path, not them.
    return "folder unreachable  " + std::to_string(s.pendingLines) + " lines held  " +
           std::to_string(s.droppedLines) + " dropped  " + shown;
}

inline bool sheetAlarm(const SeamHubSheetStatus& s, bool available) {
    return !available || (detail::hasPath(s) && s.writing == 0 && s.pendingLines > 0);
}

// The footer while a folder chosen with FOLDER waits for the next sheet.
// NEW SHEET is named only while a sheet is being written: outside a session,
// or while lines are held after the grace, the hub ignores sheet_new()
// (seam_hub.cpp, writerMain; README "Open points"), so the hint would name a
// button that does nothing. While lines are held the held count stays, with
// its alarm: the unreachable folder matters more than the next one.
inline std::string footerLine(const SeamHubSheetStatus& s, bool available, const std::string& nextFolder) {
    if (!available || nextFolder.empty()) return sheetLine(s, available);
    if (detail::hasPath(s) && s.writing == 0 && s.pendingLines > 0) return sheetLine(s, available);
    const std::string next = "next sheet in " + tildePath(nextFolder, std::getenv("HOME"));
    return s.writing ? next + " \xC2\xB7 NEW SHEET to start it" : next;
}

}} // namespace Seam::lessness
