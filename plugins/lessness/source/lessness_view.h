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
// Cites: seam_hub.h (types), seam_hub_sheet.h (who, stateObject, testWord,
// idRef), README "Lifecycle, per receiver" (why kStoneNote).
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

// A receiver's decisions belong to its current stone: see the README.
inline const char* const kStoneNote =
    "a receiver that changes stone no longer shows the decisions it took; the sheet keeps them";

namespace detail {
// The hub prints masks as type names; same words as seam_hub.cpp, joined by
// spaces here, "-" when empty.
inline std::string maskNames(uint32_t mask) {
    static const struct { uint32_t type; const char* name; } kNames[] = {
        {kSeamHubPink, "pink"}, {kSeamHubGlide, "glide"}, {kSeamHubDecoder, "decoder"},
        {kSeamHubBandTable, "band-table"}, {kSeamHubSections, "sections"}};
    std::string s;
    for (const auto& n : kNames)
        if (mask & SEAM_HUB_BIT(n.type)) { if (!s.empty()) s += ' '; s += n.name; }
    return s.empty() ? "-" : s;
}
inline const char* lifecycleWord(int32_t l) {
    switch (l) {
        case kSeamHubArrived: return "arrived";
        case kSeamHubShown:   return "shown";
        case kSeamHubApplied: return "applied";
        case kSeamHubIgnored: return "ignored";
        default:              return "none";
    }
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
           "  says " + detail::maskNames(m.says) + "  hears " + detail::maskNames(m.hears);
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
            "  " + std::to_string((long long)std::llround(b.integrationSec)) + " s";
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

inline std::string sheetLine(const SeamHubSheetStatus& s, bool available) {
    if (!available) return "hub unavailable";
    const std::string path = hubsheet::detail::oneLine(s.path, sizeof s.path);
    if (path.empty()) return "no session";
    std::string out = tildePath(path, std::getenv("HOME")) + "  ";
    if (s.writing) return out + "writing";
    return out + "folder unreachable  " + std::to_string(s.pendingLines) + " lines held  " +
           std::to_string(s.droppedLines) + " dropped";
}

inline bool sheetAlarm(const SeamHubSheetStatus& s, bool available) {
    return !available || (s.path[0] != '\0' && s.writing == 0);
}

}} // namespace Seam::lessness
