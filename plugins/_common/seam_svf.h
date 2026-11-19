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

    // A new q at the same centre: k = 1/q is the only term that moves, so no
    // tan. The TPT structure stays stable while its coefficients change, so
    // q may glide while the filter rings (the choir's live Q).
    void setQ(double q) {
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

// ──────────────────────────────────────────────────────────────────────────
// Svf — the high-pass, all-pass and bell modes of fi.svf (Faust 2.88,
// filters.lib, environment `svf`), one tick per sample.
//
// FAUST REFERENCE (filters.lib, svf): the tick of SvfBandpass above, then
//   out = m0*v0 + m1*v1 + m2*v2, with k = 1/Q, except the bell: k = 1/(Q*A),
//   A = 10^(G/40). Mix rows (m0, m1, m2):
//     hp   (1, -k, -1)
//     ap   (1, -2k, 0)
//     bell (1, k*(A*A-1), 0)
// The operation order follows Faust, division by (1+g*(g+k)) included, so
// the impulse response matches the Faust one to ~1e-15.
//
// One deliberate difference: a state magnitude below 1e-30 is flushed to
// exactly zero inside tick (Faust does not). A decaying filter then never
// parks in the subnormal range, with or without an FTZ guard around the
// caller; the deviation from Faust stays ~1e-30, far below any audible or
// A/B-gate level (1e-12).
//
// design() may be called while the filter runs: the TPT structure stays
// stable when its coefficients change.
// ──────────────────────────────────────────────────────────────────────────
class Svf {
public:
    enum class Type { Highpass, Allpass, Bell };

    void design(double fs, Type t, double f, double q, double gainDb = 0.0) {
        g_ = std::tan(f * M_PI / fs);
        double k = 1.0 / q;
        if (t == Type::Bell) {
            const double a = std::pow(10.0, gainDb / 40.0);
            k = 1.0 / (q * a);
            m1_ = k * (a * a - 1.0);
            m2_ = 0.0;
        } else if (t == Type::Allpass) {
            m1_ = -2.0 * k;
            m2_ = 0.0;
        } else {
            m1_ = -k;
            m2_ = -1.0;
        }
        k_ = k;
    }

    double tick(double x) {
        const double v1 = (ic1_ + g_ * (x - ic2_)) / (1.0 + g_ * (g_ + k_));
        const double v2 = ic2_ + g_ * v1;
        ic1_ = flush(2.0 * v1 - ic1_);
        ic2_ = flush(2.0 * v2 - ic2_);
        return 1.0 * x + m1_ * v1 + m2_ * v2;
    }

    void reset() { ic1_ = ic2_ = 0.0; }

    bool isSubnormal() const {
        return std::fpclassify(ic1_) == FP_SUBNORMAL || std::fpclassify(ic2_) == FP_SUBNORMAL;
    }

private:
    static double flush(double v) { return std::fabs(v) < 1e-30 ? 0.0 : v; }

    double g_ = 0.0, k_ = 1.0, m1_ = 0.0, m2_ = 0.0;
    double ic1_ = 0.0, ic2_ = 0.0;
};

} // namespace Seam
