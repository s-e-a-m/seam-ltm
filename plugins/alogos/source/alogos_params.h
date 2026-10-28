//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — the parameters between the threads (SDK-free)
//
// The seven controls live here as normalized values in atomics: process()
// stores what the host's queues bring and reads the box; setState stores a
// recalled preset from the UI thread.
//
// Every control maps normalized <-> plain through a clamp, so a value out of
// range (a corrupt or future session: scale 7, density 3, g 9, distance -1
// or 40 m, sections 0 or 500) lands on the nearest valid step and never
// indexes outside a table. A NaN falls back to the default.
//
// Sections are stored as asked (1..128): the limit N_max depends on scale
// and distance and is applied by the engine, not here, so a round trip
// through a scale with a smaller N_max gives the count back.
//
// Plain.scale, density and gain are 0-based menu indices, as taken by
// alogos::Engine::setSettings (density index 0, 1, 2 <-> D = 1, 2, 4).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "alogos_dsp.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace alogos {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, StoneId, Distance, Sections, Scale, Density, Gain };
constexpr int kNumParams = 7;

constexpr int kStoneIdSteps  = kMaxStoneId;        // 8 steps -> 9 values ("?" + 1..8)
constexpr int kSectionsMin   = 1;
constexpr int kSectionsSteps = kMaxSections - 1;   // 127 steps -> 128 values
constexpr int kScaleSteps    = 3;                  // 4 values
constexpr int kDensitySteps  = 2;                  // 3 values
constexpr int kGainSteps     = 4;                  // 5 values
constexpr double kMetresSpan = kMaxMetres - kMinMetres;   // 5 m

inline double clamp01(double x) { return std::min(1.0, std::max(0.0, x)); }

// The SDK's stepped toPlain: min(steps, floor(norm * (steps + 1))).
// -1 for a NaN, which the callers turn into the default.
inline int steppedFloor(double norm, int steps) {
    if (std::isnan(norm)) return -1;
    return std::min(steps, (int)(clamp01(norm) * (steps + 1)));
}

inline double stoneIdToNormalized(int id)   { return (double)id / kStoneIdSteps; }
inline double sectionsToNormalized(int n)   { return (double)(n - kSectionsMin) / kSectionsSteps; }
inline double scaleToNormalized(int i)      { return (double)i / kScaleSteps; }
inline double densityToNormalized(int i)    { return (double)i / kDensitySteps; }
inline double gainToNormalized(int i)       { return (double)i / kGainSteps; }
inline double distanceToNormalized(double m) { return (m - kMinMetres) / kMetresSpan; }

inline int normalizedToStoneId(double norm) {
    const int v = steppedFloor(norm, kStoneIdSteps);
    return v < 0 ? 0 : v;
}
inline int normalizedToSections(double norm) {
    const int v = steppedFloor(norm, kSectionsSteps);
    return v < 0 ? kDefaultSections : kSectionsMin + v;
}
inline int normalizedToScale(double norm) {
    const int v = steppedFloor(norm, kScaleSteps);
    return v < 0 ? 0 : v;
}
inline int normalizedToDensity(double norm) {
    const int v = steppedFloor(norm, kDensitySteps);
    return v < 0 ? 0 : v;
}
inline int normalizedToGain(double norm) {
    const int v = steppedFloor(norm, kGainSteps);
    return v < 0 ? 0 : v;
}
// Continuous: metres = 1 + 5 * norm, clamped to [1, 6]; NaN -> 3.33 m.
inline double normalizedToDistance(double norm) {
    if (std::isnan(norm)) return kDefaultMetres;
    return kMinMetres + kMetresSpan * clamp01(norm);
}

inline double defaultNormalized(Param p) {
    switch (p) {
        case Param::Distance: return distanceToNormalized(kDefaultMetres);
        case Param::Sections: return sectionsToNormalized(kDefaultSections);
        default:              return 0.0;   // Power off, id "?", phi, D = 1, g = 1/phi
    }
}

struct Plain {
    bool   power;
    int    stoneId;
    double metres;
    int    sections, scale, density, gain;
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    // The processor and the engine read only plain(); normalized() and store()
    // are for the host's queues, the state and the editor.
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power    = normalized(Param::Power) >= 0.5;
        x.stoneId  = normalizedToStoneId(normalized(Param::StoneId));
        x.metres   = normalizedToDistance(normalized(Param::Distance));
        x.sections = normalizedToSections(normalized(Param::Sections));
        x.scale    = normalizedToScale(normalized(Param::Scale));
        x.density  = normalizedToDensity(normalized(Param::Density));
        x.gain     = normalizedToGain(normalized(Param::Gain));
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

} // namespace alogos
