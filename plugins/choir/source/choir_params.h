//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the parameters between the threads (SDK-free)
//
// POWER and output as normalized values in atomics, as in LMO, stunedrev
// and delRM: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. RESET is not a
// parameter (choir_views.h).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_dsp.h"
#include <algorithm>
#include <atomic>

namespace choir {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Output };
constexpr int kNumParams = 2;

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double defaultNormalized(Param) { return 0.0; }

struct Plain { bool power; double output; };

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }
    Plain plain() const { return { normalized(Param::Power) >= 0.5, clamp01(normalized(Param::Output)) }; }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace choir
