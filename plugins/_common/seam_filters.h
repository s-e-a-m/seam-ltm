//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_filters.h — the C++ side of seam.filters.lib (sfi)
//
// FAUST REFERENCE (seam.filters.lib):
//   leakyint(fc) = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR));
//
// The leaky integrator, normalised to time: the integral of the input in
// seconds, which forgets below fc. y[n] = x[n]/fs + a*y[n-1], a =
// exp(-2*pi*fc/fs). Above fc its gain is 1/(2*pi*f) at every rate; below fc
// it levels off at about 1/(2*pi*fc), so a DC offset cannot grow without
// bound as it does in fi.integrator. Written in Faust 2026-09-29 for
// SSCDO#2's delRM, whose sdt.delrmint scales it by 96000; the scaling is the
// caller's, not the library's.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class LeakyIntegrator {
public:
    void prepare(double fs, double fc) {
        invFs_ = 1.0 / fs;
        a_ = std::exp(-2.0 * 3.141592653589793 * fc / fs);
        reset();
    }
    void   reset()       { y_ = 0.0; }
    double pole() const  { return a_; }

    double tick(double x) {
        y_ = x * invFs_ + a_ * y_;
        return y_;
    }

private:
    double invFs_ = 1.0 / 96000.0, a_ = 0.0, y_ = 0.0;
};

} // namespace Seam
