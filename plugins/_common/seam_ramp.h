//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_ramp.h — a linear ramp anchored in time
//
// The semantics of Pure Data's `line`: a new target starts from wherever
// the ramp is, and is reached in a duration given in SECONDS, converted to
// samples at the session's rate — so a 120 s glissando lasts 120 s at any
// rate. Unlike `line`, which Pd updates once per 64-sample block, this ramp
// steps every sample and lands on the target exactly (no accumulated
// rounding left at the end).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class LinearRamp {
public:
    void setTarget(double target, double seconds, double fs) {
        target_ = target;
        long n = std::lround(seconds * fs);
        if (n < 1) n = 1;
        remaining_ = n;
        inc_ = (target_ - value_) / (double)n;
    }

    void snap() { value_ = target_; remaining_ = 0; inc_ = 0.0; }

    double next() {
        if (remaining_ > 0) {
            if (--remaining_ == 0) value_ = target_;
            else                   value_ += inc_;
        }
        return value_;
    }

    double value()  const { return value_; }
    double target() const { return target_; }
    bool   active() const { return remaining_ > 0; }

private:
    double value_ = 0.0, target_ = 0.0, inc_ = 0.0;
    long remaining_ = 0;
};

} // namespace Seam
