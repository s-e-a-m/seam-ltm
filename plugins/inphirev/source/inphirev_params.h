//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — the parameters between the threads (SDK-free)
//
// The four controls live here as normalized values in atomics, as in
// stunedrev: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread.
//
// Every stepped control maps normalized <-> plain through a clamp, so a
// value out of range (a corrupt or future session: id 9, j 20, sections 0
// or 40) lands on the nearest valid step and never indexes outside the
// delay table. A NaN falls back to the default.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "inphirev_dsp.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace inphirev {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, StoneId, Grid, Sections };
constexpr int kNumParams = 4;

constexpr int kStoneIdSteps  = kMaxStoneId;        // 8 steps -> 9 values ("?" + 1..8)
constexpr int kGridSteps     = kGridMax;           // 14 steps -> 15 values
constexpr int kSectionsMin   = 1;
constexpr int kSectionsSteps = kMaxSections - 1;   // 17 steps -> 18 values

inline double clamp01(double x) { return std::min(1.0, std::max(0.0, x)); }

// The SDK's stepped toPlain: min(steps, floor(norm * (steps + 1))).
inline int steppedFloor(double norm, int steps) {
    if (std::isnan(norm)) return -1;
    return std::min(steps, (int)(clamp01(norm) * (steps + 1)));
}

inline double stoneIdToNormalized(int id)       { return (double)id / kStoneIdSteps; }
inline double gridToNormalized(int j)           { return (double)j / kGridSteps; }
inline double sectionsToNormalized(int n)       { return (double)(n - kSectionsMin) / kSectionsSteps; }

// StoneId is a StringListParameter, whose entries sit at idx/steps exactly:
// rounded, as multipink reads its own (index = norm * steps + 0.5).
inline int normalizedToStoneId(double norm) {
    if (std::isnan(norm)) return 0;
    return (int)(clamp01(norm) * kStoneIdSteps + 0.5);
}
inline int normalizedToGrid(double norm) {
    const int v = steppedFloor(norm, kGridSteps);
    return v < 0 ? kDefaultGrid : v;
}
inline int normalizedToSections(double norm) {
    const int v = steppedFloor(norm, kSectionsSteps);
    return v < 0 ? kDefaultSections : kSectionsMin + v;
}

inline double defaultNormalized(Param p) {
    switch (p) {
        case Param::StoneId:  return stoneIdToNormalized(0);
        case Param::Grid:     return gridToNormalized(kDefaultGrid);
        case Param::Sections: return sectionsToNormalized(kDefaultSections);
        default:              return 0.0;   // Power off
    }
}

struct Plain {
    bool power;
    int  stoneId, grid, sections;
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power    = normalized(Param::Power) >= 0.5;
        x.stoneId  = normalizedToStoneId(normalized(Param::StoneId));
        x.grid     = normalizedToGrid(normalized(Param::Grid));
        x.sections = normalizedToSections(normalized(Param::Sections));
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

} // namespace inphirev
