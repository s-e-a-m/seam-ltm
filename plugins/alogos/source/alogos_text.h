//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — the strings the window prints (SDK-free)
//
// Two kinds of text, kept out of the VSTGUI and VST3 code so that the tests
// read exactly what the window shows:
//
//   the parameters' display   distance: the snapped metres the engine uses;
//                             sections: the effective count, "(max N)" when
//                             the count asked is above N_max;
//   the footer's lines        the decay per face, the table's ends with the
//                             effective sections and N_max, the arena and
//                             the status.
//
// The parameters' display is computed from the controller's values of the
// other settings (the snap depends on id, scale and density; N_max on scale,
// density and the snapped distance), with the engine's own functions, so the
// window and the engine agree on every setting.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "alogos_dsp.h"
#include "alogos_decay.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace alogos {

// ── The parameters' display ─────────────────────────────────────────────

// The grid point the engine takes for a distance asked (Engine::describe).
inline double snappedMetres(int scale, int densityIdx, int stoneId, double metres) {
    if (!(metres == metres)) metres = kDefaultMetres;
    const Scale sc = (Scale)std::clamp(scale, 0, 3);
    const int D = density(std::clamp(densityIdx, 0, 2));
    const int id = std::clamp(stoneId, 0, kMaxStoneId);
    return gridMetres(sc, D, id, snapGrid(sc, D, id, std::clamp(metres, kMinMetres, kMaxMetres)));
}

// N_max at the snapped distance.
inline int sectionsMaxFor(int scale, int densityIdx, int stoneId, double metres) {
    const Scale sc = (Scale)std::clamp(scale, 0, 3);
    const int D = density(std::clamp(densityIdx, 0, 2));
    return nMax(sc, D, snappedMetres(scale, densityIdx, stoneId, metres));
}

// "3.330 m": the snapped metres, three decimals.
inline void distanceText(char* s, std::size_t size, int scale, int densityIdx, int stoneId, double metres) {
    std::snprintf(s, size, "%.3f m", snappedMetres(scale, densityIdx, stoneId, metres));
}

// "17", or "19 (max 19)" when the count asked (30) is above N_max.
inline void sectionsText(char* s, std::size_t size, int asked, int scale, int densityIdx,
                         int stoneId, double metres) {
    asked = std::clamp(asked, 1, kMaxSections);
    const int mx = sectionsMaxFor(scale, densityIdx, stoneId, metres);
    if (asked > mx) std::snprintf(s, size, "%d (max %d)", mx, mx);
    else            std::snprintf(s, size, "%d", asked);
}

// ── The footer ──────────────────────────────────────────────────────────

// The table's ends as the footer prints them: face A's first section and
// face D's last attached one, in samples, exactly the engine's delays.
struct Readout {
    uint32_t first, last;
    int n, nMax;
    double fs;
};

// Read while the engine is ready (the view checks the settings and the count
// of clearings around the call; see AlogosFooter::refresh).
inline Readout readout(const Engine& e) {
    const int n = std::clamp(e.sections(), 1, kMaxSections);
    return { e.delay(0, 0), e.delay(kFaces - 1, n - 1), n, e.sectionsMax(), e.sampleRate() };
}

struct Text { char s[24]; };

// Seconds: one decimal below 10 s, whole seconds above; beyond the
// estimate's horizon (+inf) ">3000".
inline Text secs(double t) {
    Text x;
    if (!std::isfinite(t))  std::snprintf(x.s, sizeof x.s, ">%.0f", kHorizon);
    else if (t < 10.0)      std::snprintf(x.s, sizeof x.s, "%.1f", t);
    else                    std::snprintf(x.s, sizeof x.s, "%.0f", t);
    return x;
}

// A delay: ms below one second, seconds above.
inline Text delayText(double t) {
    Text x;
    if (t < 1.0) std::snprintf(x.s, sizeof x.s, "%.1f ms", t * 1000.0);
    else         std::snprintf(x.s, sizeof x.s, "%.1f s", t);
    return x;
}

// "−30 dB  A 201 · B 230 · C 260 · D 289 s" (db = 30 or 60).
inline void decayLine(char* s, std::size_t size, int db, const Decay (&d)[kFaces]) {
    const bool t30 = db == 30;
    std::snprintf(s, size, "\xE2\x88\x92" "%d dB  A %s \xC2\xB7 B %s \xC2\xB7 C %s \xC2\xB7 D %s s", db,
                  secs(t30 ? d[0].t30 : d[0].t60).s, secs(t30 ? d[1].t30 : d[1].t60).s,
                  secs(t30 ? d[2].t30 : d[2].t60).s, secs(t30 ? d[3].t30 : d[3].t60).s);
}

// "first A 9.7 ms · last D 88.2 s · N 17 · max 19".
inline void tableLine(char* s, std::size_t size, const Readout& r) {
    if (r.fs <= 0.0) { std::snprintf(s, size, "first \xE2\x80\x94 \xC2\xB7 last \xE2\x80\x94"); return; }
    std::snprintf(s, size, "first A %s \xC2\xB7 last D %s \xC2\xB7 N %d \xC2\xB7 max %d",
                  delayText(r.first / r.fs).s, delayText(r.last / r.fs).s, r.n, r.nMax);
}

inline const char* statusText(Status st, int stoneId) {
    switch (st) {
        case Status::AllocFailed: return "allocation failed";
        case Status::Unprepared:  return "inactive";
        case Status::Clearing:    return "clearing\xE2\x80\xA6";
        case Status::Allocating:  return "allocating\xE2\x80\xA6";
        case Status::Ready:       break;
    }
    return stoneId == 0 ? "STONE id undeclared" : "ready";
}

// "arena 579 MiB @ 96 kHz · ready". While allocating (and after a failed
// allocation) the figure is the arena still held, the one the engine had
// before the setting that asked for a new one: the new arena is counted
// only once the audio thread has taken it.
inline void arenaLine(char* s, std::size_t size, std::size_t bytes, double fs, const char* status) {
    const double khz = fs / 1000.0, mib = bytes / (1024.0 * 1024.0);
    char a[24], r[24];
    std::snprintf(a, sizeof a, mib < 10.0 ? "%.1f" : "%.0f", mib);     // a small setting is not "0"
    std::snprintf(r, sizeof r, khz == std::floor(khz) ? "%.0f" : "%.1f", khz);
    std::snprintf(s, size, "arena %s MiB @ %s kHz \xC2\xB7 %s", a, r, status);
}

} // namespace alogos
