//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_analyzers.h — the C++ side of analyzers.lib (an)
//
// FAUST REFERENCE (analyzers.lib, standard):
//   amp_follower(rel) = abs : env with {
//       p = ba.tau2pole(rel);
//       env(x) = x * (1.0 - p) : (+ : max(x,_)) ~ *(p);
//   };
//
// e[n] = max(|x[n]|, (1-p)|x[n]| + p e[n-1]): an immediate attack, a
// release of rel seconds at any rate. Callers that let it decay into
// silence wrap process in seam_denormals.h. Written 2026-10-02 for SSCDO#2's
// choir.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_basics.h"
#include <algorithm>
#include <cmath>

namespace Seam {

class AmpFollower {
public:
    void prepare(double fs, double rel) { p_ = tau2pole(rel, fs); }

    double tick(double x) {
        const double a = std::fabs(x);
        y_ = std::max(a, a * (1.0 - p_) + p_ * y_);
        return y_;
    }

    void   reset()       { y_ = 0.0; }
    double value() const { return y_; }
    bool   hasSubnormalState() const { return std::fpclassify(y_) == FP_SUBNORMAL; }

private:
    double p_ = 0.0, y_ = 0.0;
};

} // namespace Seam
