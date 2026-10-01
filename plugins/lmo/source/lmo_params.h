//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — the parameters between the threads (SDK-free)
//
// The five controls live here as normalized values in atomics. process()
// stores what the host's queues bring and reads the box; setState stores
// a recalled preset from the UI thread. Neither touches the SDK's
// Parameter objects from the audio thread: Parameter::setNormalized
// notifies the editor synchronously (a lock, and VSTGUI redraws off the
// main thread), which is why multipink and ltglide keep atomics too.
//
// A recall stores the values one by one while process() may read the box
// between any two stores. glide is the time of the NEXT move of f, so it
// must land before f: kRecallOrder says so, and lmo_params_test checks
// every interleaving. applyTo() hands the values to the engine in the same
// order, glide first.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "lmo_dsp.h"
#include <atomic>

namespace lmo {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Frequency, Glide, Delta, Volume };
constexpr int kNumParams = 5;

constexpr double kFMin      = 20.0;
constexpr double kFMax      = 1500.0;   // the committed .dsp range
constexpr double kFDefault  = 48.0;     // cue 0
constexpr double kGlideMax  = 300.0;    // s
constexpr double kDeltaMax  = 50.0;     // Hz

// The order in which a recall stores the values: glide before f. (The
// disk order, f before glide, failed lmo_params_test after the f store: a
// process() there started the recalled f on the OLD glide, 120 s.)
constexpr Param kRecallOrder[kNumParams] = {
    Param::Glide, Param::Frequency, Param::Delta, Param::Volume, Param::Power };

inline double defaultNormalized(Param p) {
    return p == Param::Frequency ? (kFDefault - kFMin) / (kFMax - kFMin) : 0.0;
}

struct Plain {
    bool   power;
    double f, glide, delta, volume;
};

class ParamBox {
public:
    ParamBox() {
        for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i));
    }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power  = normalized(Param::Power) >= 0.5;
        x.f      = kFMin + normalized(Param::Frequency) * (kFMax - kFMin);
        x.glide  = normalized(Param::Glide) * kGlideMax;
        x.delta  = normalized(Param::Delta) * kDeltaMax;
        x.volume = normalized(Param::Volume);
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

// glide first: it is the time of the next move of f.
inline void applyTo(const Plain& p, Engine& e) {
    e.setGlide(p.glide);
    e.setFrequency(p.f);
    e.setDelta(p.delta);
    e.setVolume(p.volume);
    e.setPower(p.power);
}

} // namespace lmo
