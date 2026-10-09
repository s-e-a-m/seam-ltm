//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the parameters between the threads (SDK-free)
//
// POWER, output, 1/f, Q and release as normalized values in atomics, as in
// LMO, stunedrev and delRM: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. RESET is not a
// parameter (choir_views.h).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_dsp.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace choir {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Output, Pink, Q, Release };
constexpr int kNumParams = 5;

// Q and release are logarithmic: equal travel, equal ratio.
constexpr double kQMin = 10.0,   kQMax = 1000.0;
constexpr double kRelMin = 0.05, kRelMax = 10.0;     // s

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double logToPlain(double n, double lo, double hi)      { return lo * std::pow(hi / lo, clamp01(n)); }
inline double logToNormalized(double v, double lo, double hi) { return clamp01(std::log(v / lo) / std::log(hi / lo)); }
inline double qToPlain(double n)             { return logToPlain(n, kQMin, kQMax); }
inline double qToNormalized(double q)        { return logToNormalized(q, kQMin, kQMax); }
inline double releaseToPlain(double n)       { return logToPlain(n, kRelMin, kRelMax); }
inline double releaseToNormalized(double r)  { return logToNormalized(r, kRelMin, kRelMax); }

// The score's choir: 1/f off, Q 350, release 1.5 s (sdt.choir(350, 1.5)).
inline double defaultNormalized(Param p) {
    switch (p) {
        case Param::Q:       return qToNormalized(Config().q);
        case Param::Release: return releaseToNormalized(Config().release);
        default:             return 0.0;
    }
}

struct Plain { bool power; double output; bool pink; double q, release; };

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }
    Plain plain() const {
        return { normalized(Param::Power) >= 0.5, clamp01(normalized(Param::Output)),
                 normalized(Param::Pink) >= 0.5,
                 qToPlain(normalized(Param::Q)), releaseToPlain(normalized(Param::Release)) };
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setOutput(p.output);
    e.setPower(p.power);
    e.setPink(p.pink);
    e.setQ(p.q);
    e.setRelease(p.release);
}

} // namespace choir
