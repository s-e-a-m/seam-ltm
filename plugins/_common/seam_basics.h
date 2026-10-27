//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_basics.h — the C++ side of basics.lib (ba) and of sma.tsos (seam.math.lib)
//
// FAUST REFERENCE (basics.lib, standard):
//   ba.tau2pole(tau) = 0 when |tau| < ma.EPSILON, else exp(-1/(tau*ma.SR));
//
// The pole of a one-pole smoother whose time constant is tau seconds. Moved
// here from seam_compressors.h (2026-10-02) when the choir's follower needed
// it too: a follower should not depend on a compressor.
//
// FAUST REFERENCE (seam.math.lib, sma.tsos):
//   tsos(t) = 331.3*sqrt(1 + t/273.15);   rsos = tsos(20);
//
// The speed of sound in m/s at t degrees Celsius.
// It is the temperature-aware companion of the fixed constants sma.isos
// (331.4) and sma.esos (344), and of Seam::kSpeedOfSoundInterior.
// Those stay untouched on purpose (issue #15, concert 2026-10-30).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cfloat>
#include <cmath>

namespace Seam {

inline double tau2pole(double tau, double fs) {
    return std::fabs(tau) < DBL_EPSILON ? 0.0 : std::exp(-1.0 / (tau * fs));
}

// Room temperature, the argument of sma.rsos.
constexpr double kRoomCelsius = 20.0;

inline double speedOfSound(double celsius) {
    return 331.3 * std::sqrt(1.0 + celsius / 273.15);
}

} // namespace Seam
