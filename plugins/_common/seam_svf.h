//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_svf.h — the C++ side of fi.svf (filters.lib), band-pass
//
// FAUST REFERENCE (filters.lib, standard, Andrew Simper's SVF):
//   svf(T,F,Q,G) = tick ~ (_,_) : !,!,si.dot(3, mix)
//   with { tick(ic1eq, ic2eq, v0) = 2*v1 - ic1eq, 2*v2 - ic2eq, v0, v1, v2
//          with { v1 = ic1eq + g*(v0-ic2eq) : /(1 + g*(g+k));
//                 v2 = ic2eq + g*v1; };
//          g = tan(F*ma.PI/ma.SR); k = 1/Q; };
//   bp(f,q) = svf(1, f, q, 0);          // mix = 0, 1, 0: the output is v1
//
// The bilinear transform, prewarped at f, of H(s) = s/(s^2 + s/q + 1): peak
// gain q at f. Above fs/2 tan turns negative and the filter is unstable; the
// caller keeps f below it (the choir designs at min(fc, 19999) Hz).
// Callers that let it ring into silence wrap process in seam_denormals.h.
// Written 2026-10-02 for SSCDO#2's choir.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class SvfBandpass {
public:
    void design(double fs, double f, double q) {
        g_ = std::tan(f * M_PI / fs);
        k_ = 1.0 / q;
        d_ = 1.0 + g_ * (g_ + k_);
    }

    double tick(double x) {
        const double v1 = (ic1_ + g_ * (x - ic2_)) / d_;
        const double v2 = ic2_ + g_ * v1;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return v1;
    }

    void reset() { ic1_ = ic2_ = 0.0; }

    bool hasSubnormalState() const {
        return std::fpclassify(ic1_) == FP_SUBNORMAL || std::fpclassify(ic2_) == FP_SUBNORMAL;
    }

private:
    double g_ = 0.0, k_ = 1.0, d_ = 1.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

} // namespace Seam
