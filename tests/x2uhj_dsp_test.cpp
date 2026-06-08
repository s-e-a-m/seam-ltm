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

TEST_CASE("UHJEncoder: W-only input produces nonzero L,R and zero Q") {
    UHJEncoder enc; enc.prepare(48000.0);
    // AmbiX ACN W-only (omni). W contributes to Sigma (both L and R).
    // Z=0, so Q must be zero. W does not enter Q path.
    double in[4] = {1.0, 0.0, 0.0, 0.0};
    double L,R,T,Q;
    // Settle the filters.
    for (int i = 0; i < 4096; ++i) enc.process(in, L, R, T, Q);
    CHECK(L != doctest::Approx(0.0));   // Sigma path active
    CHECK(R != doctest::Approx(0.0));   // Sigma path active
    CHECK(Q == doctest::Approx(0.0));   // Z=0 -> Q=0
}

TEST_CASE("UHJEncoder: Q channel carries only Z (scaled, filtered)") {
    UHJEncoder enc; enc.prepare(48000.0);
    double inZ[4]  = {0,0,1.0,0};   // ACN Z only
    double inXY[4] = {0,1.0,0,1.0}; // ACN Y and X, no Z
    double L,R,T,Q, Lo,Ro,To,Qo;
    for (int i = 0; i < 4096; ++i) {
        enc.process(inZ, L, R, T, Q);
    }
    UHJEncoder enc2; enc2.prepare(48000.0);
    for (int i = 0; i < 4096; ++i) {
        enc2.process(inXY, Lo, Ro, To, Qo);
    }
    CHECK(std::abs(Q) > 1e-6);     // Z drives Q
    CHECK(std::abs(Qo) < 1e-9);    // X,Y do not drive Q
}

#include "seam_quadrature.h"
TEST_CASE("UHJEncoder retains a converged design at several rates") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        UHJEncoder enc;
        enc.prepare(fs);
        const auto& d = enc.design();
        CHECK(d.converged);
        CHECK(d.maxErrorDeg < 2.5);
    }
}
