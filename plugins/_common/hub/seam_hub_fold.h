//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hub — the fold of a host's leave-and-return (pure)
//
// Why. Reaper toggles a plugin off and on in the same millisecond (seen in the
// 2026-11-25 test in Reaper): the member says `leaves` and `arrives` with the
// same declaration, and the sheet reads as if the member had gone and come
// back. It has not; the host only bypassed it. A pair like that is folded into
// one line, `resumes  after <dt> ms`, at the time of the arrival
// (grammar: seam_hub_sheet.h).
//
// Pairing. A `leaves` pairs with the first later event of the same `who`
// ("name#instance": two instances are two members) when that event is an
// `arrives`, with the same `masks` (a different declaration is a real change
// and both lines stay), at most windowMs later.
//
// Why the window holds the whole tail and not the `leaves` alone. The sheet
// stays in order of arrival: while a `leaves` may still find its partner,
// writing the lines after it would put them before a line that, once
// folded, belongs after them. So fold() reports how many leading entries are
// ready, and everything from the first young unpaired `leaves` on waits.
// The writer thread (seam_hub.cpp) owns the queue and the clock; this header
// only decides.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub_sheet.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace Seam { namespace hubfold {

enum class Kind : uint8_t { Other, Arrives, Leaves };

struct Entry {
    std::string text;          // the event line and its rows, '\n'-separated
    int64_t     unixMs = 0;    // the event's time
    Kind        kind = Kind::Other;
    std::string who;           // "strx#1" for Arrives and Leaves, empty otherwise
    std::string masks;         // the member's "says ... hears ..." text, Arrives and Leaves
    bool        remembered = false;   // already copied into the recent lines
};

constexpr int64_t kFoldMs = 1000;

// Folds, in place, every Leaves followed by an Arrives with the same `who`
// and the same `masks`, at most windowMs later: the Leaves is erased and the
// Arrives becomes one `resumes` line at the Arrives' time, object
// "after <dt> ms". Returns how many leading entries are ready to write: all
// of them, unless some Leaves still without a partner is younger than
// windowMs (nowMs - unixMs < windowMs), in which case the count stops just
// before the first such Leaves. windowMs = 0 folds what pairs are present
// and returns q.size().
inline size_t fold(std::deque<Entry>& q, int64_t nowMs, int64_t windowMs) {
    for (size_t i = 0; i < q.size();) {
        if (q[i].kind != Kind::Leaves) { ++i; continue; }
        size_t j = i + 1;
        while (j < q.size() && !(q[j].kind != Kind::Other && q[j].who == q[i].who)) ++j;
        const bool pairs = j < q.size() && q[j].kind == Kind::Arrives &&
                           q[j].masks == q[i].masks &&
                           q[j].unixMs - q[i].unixMs >= 0 &&
                           q[j].unixMs - q[i].unixMs <= windowMs;
        if (!pairs) { ++i; continue; }
        const int64_t dt = q[j].unixMs - q[i].unixMs;
        q[j].text = hubsheet::event(q[j].unixMs, q[j].who, "resumes",
                                    "after " + std::to_string(dt) + " ms");
        q[j].kind = Kind::Other;
        q.erase(q.begin() + (std::ptrdiff_t)i);   // the next candidate is now at i
    }
    for (size_t i = 0; i < q.size(); ++i)
        if (q[i].kind == Kind::Leaves && nowMs - q[i].unixMs < windowMs) return i;
    return q.size();
}

}} // namespace Seam::hubfold
