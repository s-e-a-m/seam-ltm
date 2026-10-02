//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the parameters between the threads (SDK-free)
//
// The three controls live here as normalized values in atomics, as in LMO
// and stunedrev: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. Neither touches
// the SDK's Parameter objects from the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
#include <algorithm>
#include <atomic>

namespace delrm {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Distance, Output };
constexpr int kNumParams = 3;

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double distanceToNormalized(double mt) { return clamp01(mt / kMaxMetres); }
inline double normalizedToDistance(double n)  { return clamp01(n) * kMaxMetres; }

inline double defaultNormalized(Param p) {
    return p == Param::Distance ? distanceToNormalized(kDefaultMetres) : 0.0;
}

struct Plain {
    bool   power;
    double metres, output;
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
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setDistance(p.metres);
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace delrm
