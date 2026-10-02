//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the parameters between the threads (SDK-free)
//
// The seven controls live here as normalized values in atomics, as in LMO:
// process() stores what the host's queues bring and reads the box; setState
// stores a recalled preset from the UI thread. Neither touches the SDK's
// Parameter objects from the audio thread (Parameter::setNormalized notifies
// the editor synchronously). No control depends on another, so a recall may
// store them in any order.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "stunedrev_dsp.h"
#include <algorithm>
#include <atomic>

namespace stunedrev {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, T1, T2, T3, T4, Input, Output };
constexpr int kNumParams = 7;
constexpr int kTimeSteps = kTMax - kTMin;   // 99, the RangeParameter's stepCount

inline double timeToNormalized(int ms) { return (double)(ms - kTMin) / kTimeSteps; }

// The SDK's RangeParameter::toPlain for a stepped parameter, so that the
// millisecond the DSP uses is the one the host displays, also for a host
// value between two steps.
inline int normalizedToTime(double norm) {
    norm = std::min(1.0, std::max(0.0, norm));
    return kTMin + std::min(kTimeSteps, (int)(norm * (kTimeSteps + 1)));
}

inline double defaultNormalized(Param p) {
    const int i = (int)p - (int)Param::T1;
    return (i >= 0 && i < kLines) ? timeToNormalized(kDefaultTimes[i]) : 0.0;
}

struct Plain {
    bool   power;
    int    t[kLines];
    double input, output;
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power = normalized(Param::Power) >= 0.5;
        for (int j = 0; j < kLines; ++j) x.t[j] = normalizedToTime(normalized((Param)((int)Param::T1 + j)));
        x.input  = normalized(Param::Input);
        x.output = normalized(Param::Output);
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    for (int j = 0; j < kLines; ++j) e.setTime(j, p.t[j]);
    e.setInput(p.input);
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace stunedrev
