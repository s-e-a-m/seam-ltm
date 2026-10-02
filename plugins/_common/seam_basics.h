//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_basics.h — the C++ side of basics.lib (ba)
//
// FAUST REFERENCE (basics.lib, standard):
//   ba.tau2pole(tau) = 0 when |tau| < ma.EPSILON, else exp(-1/(tau*ma.SR));
//
// The pole of a one-pole smoother whose time constant is tau seconds. Moved
// here from seam_compressors.h (2026-10-02) when the choir's follower needed
// it too: a follower should not depend on a compressor.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cfloat>
#include <cmath>

namespace Seam {

inline double tau2pole(double tau, double fs) {
    return std::fabs(tau) < DBL_EPSILON ? 0.0 : std::exp(-1.0 / (tau * fs));
}

} // namespace Seam
