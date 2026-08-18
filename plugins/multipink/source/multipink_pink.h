// SEAM-LTM · multipink_pink — the pinking filter, SDK-free so it can be judged.
//
// FAUST REFERENCE (noises.lib): no.pink_filter — the same coefficients, which
// GRAME documents as "original designed by invfreqz in Octave" (J.O. Smith,
// LAC-12) and which reach music-dsp from Paul Kellett, October 1999. Kellett
// wrote of them: "there is no algorithm behind those particular coefficients,
// just the general idea that it's a mixture of 1st order lowpasses with
// increasing cutoff frequency" -- they are hand-tuned for one sample rate.
//
// Being a fit made in the Z-PLANE, its corner frequencies are fractions of the
// sample rate: double fs and the -3 dB/octave region moves up an octave and
// the bottom falls off the fit. This is the defect measured on 2026-08-18,
// and the reason the response is exposed here analytically -- a tolerance of
// +/-0.25 dB cannot be judged by feeding the filter noise, because a noise
// measurement of a third-octave band carries more spread than that.
#pragma once

#include <cmath>
#include <complex>

namespace Seam { namespace multipink {

// y[n] = sum(kPinkB[k] x[n-k]) - sum(kPinkA[k] y[n-1-k])
inline constexpr double kPinkB[4] = {
     0.049922035, -0.095993537,  0.050612699, -0.004408786 };
inline constexpr double kPinkA[3] = {
    -2.494956002,  2.017265875, -0.522189400 };

// |H(f)| in dB at sample rate fs. Exact, so a 0.01 dB question has an answer.
inline double magnitudeDb(double f, double fs) {
    const std::complex<double> z = std::exp(std::complex<double>(0.0, -2.0 * M_PI * f / fs));
    const std::complex<double> z2 = z * z, z3 = z2 * z;
    const std::complex<double> num = kPinkB[0] + kPinkB[1]*z + kPinkB[2]*z2 + kPinkB[3]*z3;
    const std::complex<double> den = 1.0      + kPinkA[0]*z + kPinkA[1]*z2 + kPinkA[2]*z3;
    return 20.0 * std::log10(std::abs(num / den));
}

} } // namespace Seam::multipink
