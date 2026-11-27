//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hub — the session sheet grammar (pure formatter)
//
// The sheet is a text file the hub writes, one event per line, readable by a
// person and by tools/hub-sheet.py. This header is the grammar's reference
// and its formatter; no I/O, no dylib dependency beyond seam_hub.h's types.
//
//   # SEAM hub sheet
//   # started: YYYY-MM-DD HH:MM:SS   pid: <pid>   hub: <SEAM_HUB_VERSION>
//   HH:MM:SS.mmm  <who:12>  <verb:8>  <object>
//     | <row>                          (continuation of the event above)
//
// Event line: local time of the unix-ms value, two spaces, who left-aligned
// in 12 (a longer who is kept whole), two spaces, verb left-aligned in 8,
// two spaces, object (free text, no newline). When the object is empty the
// line ends after the verb, with no trailing spaces.
// Continuation line: exactly "  | " then the row, no trailing spaces.
// Verbs: arrives leaves state says shows applies ignores sheet resumes present.
// who: "<name>#<instance>", or "hub" / "operator".
//
// Rows (columns padded with spaces, last column never padded):
//   band table  title  "band  hz        comp_db  level_db  settled"
//               row    band  = ISO 266 nominal name by row index (20, 25, 31.5 .. 1k,
//                              1.25k .. 20k, as strx prints them), left in 6
//                      hz    = "%.2f", left in 10
//                      comp  = "%+.1f", left in 9
//                      level = "%.1f", left in 10
//                      settled = "%.2f"
//               exactly bandCount rows (0..31): a withheld band has no row
//   sections    row    type word (peak|lowshelf|highshelf|unknown) left in 10,
//                      hz = "%7.1f", "  ", q = "%.2f", "  ", gain = "%+.1f"
//   state       pink and decoder: no rows; glide: one row
//               "<f0 %g> .. <f1 %g> Hz  T <%g> s  delta <%+g> s  sweep <n>  dirac <n>"
// Object texts (inline, after the verb). Every field the writer compares to
// decide that a state changed (sameState in seam_hub.cpp) is printed, so two
// state lines in a row never read the same:
//   band table  "band-table #<id>  stone <n>  [decoder <decoder>]  <count> bands
//                <%.1f> s  <%g> Hz  level <%.1f> dB  emitter <pink|glide|none>
//                [slots <n>]  [source "<text>"]"
//               (decoder part only when hasDecoder; slots only when nonzero;
//               source only when not empty, control characters as spaces)
//   sections    "sections #<id>  common|difference  <count>  rms <%.1f>
//                max <%.1f>  offset <%+.1f>  fit <%g>-<%g> Hz  room <%g> Hz
//                from <#id ...|->"
//   state       "<pink|glide|decoder>  stone <n>  <detail>  <%.1f> dB
//                <active|idle>  <%g> Hz"
//               pink detail    "slots <a>-<b>", or "slots <a>+0" when empty
//               glide detail   "pass <counter> start <sample>"
//               decoder detail "<decoder>"
//   <decoder>   "test <N|W|X|Y|Z>  cut <%g>  bypass <on|off>  model <n>
//                common <#id|->  difference <#id|->  piece <on|off>
//                sub <on|off> <%g> Hz"
//   resumes     "after <dt> ms": a leaves and an arrives of the same member with
//               the same masks, folded into one line (seam_hub_fold.h)
//   present     "says <mask>  hears <mask>", as arrives: a member already there
//               when a sheet is renewed (sheet_new), first in the new file
//   transitions "<type> #<id>[  (<detail>)]" for shows,
//               "<type> #<id>  on <who>[  (<detail>)]" for applies/ignores;
//               detail at most 31 characters, from the receiver
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace Seam { namespace hubsheet {

namespace detail {
inline std::string fmt(const char* f, double a) {
    char b[64]; std::snprintf(b, sizeof b, f, a); return b;
}
inline std::string pad(std::string s, size_t w) {
    if (s.size() < w) s.append(w - s.size(), ' ');
    return s;
}
inline const char* sectionWord(uint32_t t) {
    switch (t) {
        case kSeamHubPeak:      return "peak";
        case kSeamHubLowShelf:  return "lowshelf";
        case kSeamHubHighShelf: return "highshelf";
        default:                return "unknown";
    }
}
inline const char* bandName(int i) {
    static const char* k[SEAM_HUB_BANDS] = {
        "20","25","31.5","40","50","63","80","100","125","160","200","250",
        "315","400","500","630","800","1k","1.25k","1.6k","2k","2.5k","3.15k",
        "4k","5k","6.3k","8k","10k","12.5k","16k","20k"};
    return (i >= 0 && i < SEAM_HUB_BANDS) ? k[i] : "?";
}
inline int clampBands(int n) { return n < 0 ? 0 : (n > SEAM_HUB_BANDS ? SEAM_HUB_BANDS : n); }
inline const char* testWord(uint32_t t) {
    static const char* k[] = {"N", "W", "X", "Y", "Z"};
    return t < 5 ? k[t] : "?";
}
inline const char* onOff(uint32_t v) { return v ? "on" : "off"; }
inline std::string idRef(uint64_t id) { return id ? "#" + std::to_string(id) : std::string("-"); }
inline const char* kindWord(uint32_t k) {
    return k == kSeamHubPink ? "pink" : k == kSeamHubGlide ? "glide"
         : k == kSeamHubDecoder ? "decoder" : k == 0 ? "none" : "unknown";
}
// Free text from a fixed field, made safe for one line: bounded by the field
// (it may lack a NUL), control characters as spaces.
inline std::string oneLine(const char* p, size_t cap) {
    std::string s(p, strnlen(p, cap));
    for (char& c : s) if ((unsigned char)c < ' ' || c == 0x7f) c = ' ';
    return s;
}
// The decoder's setting, all of it: the state line and a table's object share it.
inline std::string decoderText(const SeamHubDecoder& d) {
    return std::string("test ") + testWord(d.test) + "  cut " + fmt("%g", d.cutHz) +
           "  bypass " + onOff(d.bypass) + "  model " + std::to_string(d.stoneOfModel) +
           "  common " + idRef(d.commonId) + "  difference " + idRef(d.differenceId) +
           "  piece " + onOff(d.pieceOn) + "  sub " + onOff(d.subOn) + " " + fmt("%g", d.subHz) + " Hz";
}
} // namespace detail

inline std::string header(const std::tm& start, int pid) {
    char b[160];
    std::snprintf(b, sizeof b,
        "# SEAM hub sheet\n# started: %04d-%02d-%02d %02d:%02d:%02d   pid: %d   hub: %u\n",
        start.tm_year + 1900, start.tm_mon + 1, start.tm_mday,
        start.tm_hour, start.tm_min, start.tm_sec, pid, (unsigned)SEAM_HUB_VERSION);
    return b;
}

// A says/hears mask as type names, comma-separated, "-" when empty.
inline std::string maskNames(uint32_t mask) {
    static const struct { uint32_t type; const char* name; } kNames[] = {
        {kSeamHubPink, "pink"}, {kSeamHubGlide, "glide"}, {kSeamHubDecoder, "decoder"},
        {kSeamHubBandTable, "band-table"}, {kSeamHubSections, "sections"}};
    std::string s;
    for (const auto& n : kNames)
        if (mask & SEAM_HUB_BIT(n.type)) { if (!s.empty()) s += ','; s += n.name; }
    return s.empty() ? "-" : s;
}

inline std::string who(const char* name, uint32_t instance) {
    return std::string(name) + "#" + std::to_string(instance);
}

// One event line, no trailing newline.
inline std::string event(int64_t unixMs, const std::string& w, const char* verb,
                         const std::string& object) {
    std::time_t t = (std::time_t)(unixMs / 1000);
    int ms = (int)(unixMs % 1000);
    if (ms < 0) { ms += 1000; --t; }
    std::tm tm{};
    localtime_r(&t, &tm);
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    std::string s = std::string(b) + "  " + detail::pad(w, 12) + "  ";
    if (object.empty()) { std::string v = verb; return s + v; }
    return s + detail::pad(verb, 8) + "  " + object;
}

inline std::string tableObject(uint64_t id, uint32_t stone, const SeamHubBandTable& t) {
    std::string s = "band-table #" + std::to_string(id) + "  stone " + std::to_string(stone);
    if (t.hasDecoder) s += "  decoder " + detail::decoderText(t.decoder);
    s += "  " + std::to_string(detail::clampBands(t.bandCount)) + " bands  " +
         detail::fmt("%.1f", t.integrationSec) + " s  " + detail::fmt("%g", t.sampleRate) + " Hz" +
         "  level " + detail::fmt("%.1f", t.sourceLevelDb) + " dB" +
         "  emitter " + detail::kindWord(t.emitterKind);
    if (t.emitterSlots) s += " slots " + std::to_string(t.emitterSlots);
    const std::string src = detail::oneLine(t.source, sizeof t.source);
    if (!src.empty()) s += "  source \"" + src + "\"";
    return s;
}

inline std::string sectionsObject(uint64_t id, const SeamHubSections& p) {
    std::string from;
    for (uint64_t f : p.fromIds) if (f) from += (from.empty() ? "" : " ") + detail::idRef(f);
    return "sections #" + std::to_string(id) + "  " +
           (p.block == kSeamHubBlockDifference ? "difference" : "common") + "  " +
           std::to_string(p.count) + "  rms " + detail::fmt("%.1f", p.residualRmsDb) +
           "  max " + detail::fmt("%.1f", p.residualMaxDb) +
           "  offset " + detail::fmt("%+.1f", p.offsetDb) +
           "  fit " + detail::fmt("%g", p.fitLoHz) + "-" + detail::fmt("%g", p.fitHiHz) + " Hz" +
           "  room " + detail::fmt("%g", p.roomLimitHz) + " Hz" +
           "  from " + (from.empty() ? std::string("-") : from);
}

inline std::string stateObject(const SeamHubState& s) {
    std::string o = std::string(s.kind == 0 ? "unknown" : detail::kindWord(s.kind)) +
                    "  stone " + std::to_string(s.stoneId);
    if (s.kind == kSeamHubPink) {
        const auto& p = s.u.pink;
        o += "  slots " + std::to_string(p.slotStart) +
             (p.slotCount > 0 ? "-" + std::to_string(p.slotStart + p.slotCount - 1) : std::string("+0"));
    } else if (s.kind == kSeamHubGlide) {
        o += "  pass " + std::to_string(s.u.glide.passCounter) +
             " start " + std::to_string(s.u.glide.passStartSample);
    } else if (s.kind == kSeamHubDecoder) {
        o += "  " + detail::decoderText(s.u.decoder);
    }
    o += "  " + detail::fmt("%.1f", s.levelDb) + " dB  " + (s.active ? "active" : "idle") +
         "  " + detail::fmt("%g", s.sampleRate) + " Hz";
    return o;
}

// Continuation lines, each "  | row\n".
inline std::string rows(const SeamHubBandTable& t) {
    std::string r = "  | band  hz        comp_db  level_db  settled\n";
    int n = detail::clampBands(t.bandCount);
    for (int i = 0; i < n; ++i) {
        const SeamHubBand& b = t.bands[i];
        r += "  | " + detail::pad(detail::bandName(i), 6) +
             detail::pad(detail::fmt("%.2f", b.hz), 10) +
             detail::pad(detail::fmt("%+.1f", b.compDb), 9) +
             detail::pad(detail::fmt("%.1f", b.levelDb), 10) +
             detail::fmt("%.2f", b.settled) + "\n";
    }
    return r;
}

inline std::string rows(const SeamHubSections& p) {
    std::string r;
    uint32_t n = p.count > SEAM_HUB_MAX_SECTIONS ? SEAM_HUB_MAX_SECTIONS : p.count;
    for (uint32_t i = 0; i < n; ++i) {
        const SeamHubSection& s = p.sections[i];
        char hz[32]; std::snprintf(hz, sizeof hz, "%7.1f", s.hz);
        r += "  | " + detail::pad(detail::sectionWord(s.type), 10) + hz + "  " +
             detail::fmt("%.2f", s.q) + "  " + detail::fmt("%+.1f", s.gainDb) + "\n";
    }
    return r;
}

inline std::string rows(const SeamHubState& s) {
    if (s.kind == kSeamHubGlide) {
        const auto& g = s.u.glide;
        return "  | " + detail::fmt("%g", g.f0) + " .. " + detail::fmt("%g", g.f1) + " Hz  T " +
               detail::fmt("%g", g.durationSec) + " s  delta " + detail::fmt("%+g", g.deltaSec) +
               " s  sweep " + std::to_string(g.sweepMode) + "  dirac " + std::to_string(g.diracMode) + "\n";
    }
    return "";
}

}} // namespace Seam::hubsheet
