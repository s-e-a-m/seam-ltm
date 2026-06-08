// SEAM reusable quadrature design engine (SDK-free, header-only).
// Designs an all-pass quadrature pair (H_R, H_I) whose phase difference holds
// -90 degrees across a band, by a minimax phase fit at the actual sample rate.
// Ported from the Python harness design_quadrature_perfs.py.
#pragma once
#include <cmath>
#include <complex>

namespace seam { namespace quadrature {

struct APSpec { double f; double Q; };

// Coefficients (a1, a2) of one RBJ all-pass biquad at (f, Q, fs).
inline void allpassCoeffs(double f, double Q, double fs, double& a1, double& a2) {
    const double w0 = 2.0 * M_PI * f / fs;
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double n = 1.0 + alpha;
    a1 = -2.0 * std::cos(w0) / n;
    a2 = (1.0 - alpha) / n;
}

// Complex response of one all-pass biquad at digital angular frequency w.
inline std::complex<double> allpassSectionResponse(double f, double Q, double fs, double w) {
    double a1, a2;
    allpassCoeffs(f, Q, fs, a1, a2);
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = a2 + a1 * z1 + z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
    return num / den;
}

inline double allpassSectionMag(double f, double Q, double fs, double w) {
    return std::abs(allpassSectionResponse(f, Q, fs, w));
}

inline double allpassSectionPhase(double f, double Q, double fs, double w) {
    return std::arg(allpassSectionResponse(f, Q, fs, w));
}

// Cascade phase: sum of per-section unwrapped phases across the frequency grid.
// Matches numpy's per-section unwrap-then-sum in design_quadrature_perfs.py.
inline void cascadePhase(const APSpec* secs, int n, double fs,
                         const double* freqs, int M, double* out) {
    for (int i = 0; i < M; ++i) out[i] = 0.0;
    for (int s = 0; s < n; ++s) {
        double prev = 0.0, offset = 0.0;
        for (int i = 0; i < M; ++i) {
            const double w = 2.0 * M_PI * freqs[i] / fs;
            double ph = allpassSectionPhase(secs[s].f, secs[s].Q, fs, w);
            if (i > 0) {
                double d = ph - prev;
                while (d >  M_PI) { offset -= 2.0 * M_PI; d -= 2.0 * M_PI; }
                while (d < -M_PI) { offset += 2.0 * M_PI; d += 2.0 * M_PI; }
            }
            prev = ph;
            out[i] += ph + offset;
        }
    }
}

}} // namespace
