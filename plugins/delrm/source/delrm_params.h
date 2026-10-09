//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the parameters between the threads (SDK-free)
//
// The thirteen controls live here as normalized values in atomics, as in LMO
// and stunedrev: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. Neither touches
// the SDK's Parameter objects from the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace delrm {

// The state's order on disk (append-only, never reordered).
// The compressor's five sliders for channel 2, then the five for channel 4.
enum class Param : int { Power = 0, Distance, Output,
                         Drive2, Thresh2, Ratio2, Attack2, Release2,
                         Drive4, Thresh4, Ratio4, Attack4, Release4 };
constexpr int kNumParams = 13;
constexpr int kDynFields = 5;              // drive, threshold, ratio, attack, release

// Drive and threshold are linear in dB; ratio, attack and release are
// logarithmic: equal travel, equal ratio.
// Drive up to +60 dB (2026-10-09, GS in the host): the triple product is
// cubic, so a quiet input sits far below any threshold, and +40 dB was the
// whole travel. The score's +20 dB sits at one third.
constexpr double kDriveMaxDb = 60.0;
constexpr double kThreshMinDb = -60.0;
constexpr double kRatioMax = 40.0;
constexpr double kAttackMin = 0.001, kAttackMax = 1.0;      // s
constexpr double kReleaseMin = 0.005, kReleaseMax = 5.0;    // s

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double distanceToNormalized(double mt) { return clamp01(mt / kMaxMetres); }
inline double normalizedToDistance(double n)  { return clamp01(n) * kMaxMetres; }
inline double logToPlain(double n, double lo, double hi)      { return lo * std::pow(hi / lo, clamp01(n)); }
inline double logToNormalized(double v, double lo, double hi) { return clamp01(std::log(v / lo) / std::log(hi / lo)); }

// Field f (0-4) of a Dynamics, the slider's plain value <-> normalized.
inline double dynToPlain(int f, double n) {
    switch (f) {
        case 0:  return clamp01(n) * kDriveMaxDb;
        case 1:  return kThreshMinDb * (1.0 - clamp01(n));
        case 2:  return logToPlain(n, 1.0, kRatioMax);
        case 3:  return logToPlain(n, kAttackMin, kAttackMax);
        default: return logToPlain(n, kReleaseMin, kReleaseMax);
    }
}
inline double dynToNormalized(int f, double v) {
    switch (f) {
        case 0:  return clamp01(v / kDriveMaxDb);
        case 1:  return clamp01(1.0 - v / kThreshMinDb);
        case 2:  return logToNormalized(v, 1.0, kRatioMax);
        case 3:  return logToNormalized(v, kAttackMin, kAttackMax);
        default: return logToNormalized(v, kReleaseMin, kReleaseMax);
    }
}
inline double& dynField(Dynamics& d, int f) {
    double* x[kDynFields] = { &d.driveDb, &d.threshDb, &d.ratio, &d.attack, &d.release };
    return *x[f];
}
inline double dynField(const Dynamics& d, int f) { Dynamics c = d; return dynField(c, f); }

inline bool isDyn(Param p) { return (int)p >= (int)Param::Drive2; }
inline int  dynRow(Param p)   { return ((int)p - (int)Param::Drive2) / kDynFields; }   // 0 = ch 2, 1 = ch 4
inline int  dynFieldOf(Param p) { return ((int)p - (int)Param::Drive2) % kDynFields; }

inline double defaultNormalized(Param p) {
    if (isDyn(p)) return dynToNormalized(dynFieldOf(p), dynField(Dynamics{}, dynFieldOf(p)));
    return p == Param::Distance ? distanceToNormalized(kDefaultMetres) : 0.0;
}

struct Plain {
    bool     power;
    double   metres, output;
    Dynamics dyn[2];                       // channel 2, channel 4
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power  = normalized(Param::Power) >= 0.5;
        x.metres = normalizedToDistance(normalized(Param::Distance));
        x.output = clamp01(normalized(Param::Output));
        for (int i = (int)Param::Drive2; i < kNumParams; ++i) {
            const Param p = (Param)i;
            // A slider on its default plays the score's value itself: the
            // round trip through log and pow gives 30 ms back one ulp off.
            const double n = normalized(p);
            const int f = dynFieldOf(p);
            dynField(x.dyn[dynRow(p)], f) = n == defaultNormalized(p) ? dynField(Dynamics{}, f) : dynToPlain(f, n);
        }
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setDistance(p.metres);
    e.setOutput(p.output);
    e.setPower(p.power);
    e.setDynamics(0, p.dyn[0]);
    e.setDynamics(1, p.dyn[1]);
}

} // namespace delrm
