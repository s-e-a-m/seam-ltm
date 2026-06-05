#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "x2uhj_dsp.h"

#include <complex>
#include <cmath>

using namespace Seam::x2uhj;

// Evaluate steady-state magnitude of one AllpassSection at frequency f.
static double sectionMagnitude(double f, double Q, double fs, double probe) {
    AllpassSection s; s.set(f, Q, fs);
    // Drive with a sinusoid; measure output RMS / input RMS after settling.
    const int N = 8192; double inSum = 0, outSum = 0;
    for (int n = 0; n < N; ++n) {
        double x = std::sin(2.0 * M_PI * probe * n / fs);
        double y = s.process(x);
        if (n > N/2) { inSum += x*x; outSum += y*y; }
    }
    return std::sqrt(outSum / inSum);
}

TEST_CASE("AllpassSection has unit magnitude") {
    CHECK(sectionMagnitude(1000.0, 0.5, 48000.0, 1000.0) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(sectionMagnitude(1000.0, 0.5, 48000.0,  300.0) == doctest::Approx(1.0).epsilon(0.01));
}

#include "x2uhj_coeffs.h"

// Measure phase of a real sinusoid through a cascade by cross-correlation.
static double cascadePhaseDeg(const APSpec* spec, int n, double f, double fs) {
    AllpassSection s[8];
    for (int i = 0; i < n; ++i) s[i].set(spec[i].f, spec[i].Q, fs);
    const int N = 16384; double cosAcc = 0, sinAcc = 0;
    for (int k = 0; k < N; ++k) {
        double x = std::cos(2.0 * M_PI * f * k / fs);
        double y = x;
        for (int i = 0; i < n; ++i) y = s[i].process(y);
        if (k > N/2) {
            cosAcc += y * std::cos(2.0 * M_PI * f * k / fs);
            sinAcc += y * std::sin(2.0 * M_PI * f * k / fs);
        }
    }
    return std::atan2(-sinAcc, cosAcc) * 180.0 / M_PI;
}

TEST_CASE("ambixToFuMa reorders ACN and scales W by 1/sqrt(2)") {
    // AmbiX ACN input: [W, Y, Z, X]
    double in[4] = {1.0, 2.0, 3.0, 4.0};
    double out[4];
    ambixToFuMa(in, out);                 // -> [W/√2, X, Y, Z]
    CHECK(out[0] == doctest::Approx(1.0 / std::sqrt(2.0)));
    CHECK(out[1] == doctest::Approx(4.0));   // X (ACN 3)
    CHECK(out[2] == doctest::Approx(2.0));   // Y (ACN 1)
    CHECK(out[3] == doctest::Approx(3.0));   // Z (ACN 2)
}

TEST_CASE("Quadrature pair is ~90 deg apart across the band") {
    const double fs = 48000.0;
    for (double f : {100.0, 500.0, 1000.0, 5000.0, 10000.0}) {
        double pr = cascadePhaseDeg(kHR, kNumSections, f, fs);
        double pi = cascadePhaseDeg(kHI, kNumSections, f, fs);
        double diff = std::fmod(pi - pr + 540.0, 360.0) - 180.0;
        CHECK(std::abs(std::abs(diff) - 90.0) < 5.0);
    }
}
