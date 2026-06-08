#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_quadrature.h"
#include <cmath>

using namespace Seam::quadrature;

TEST_CASE("allpass section has unit magnitude (phase-only)") {
    const double fs = 48000.0;
    for (double fHz : {20.0, 200.0, 2000.0, 18000.0}) {
        double w = 2.0 * M_PI * fHz / fs;
        double mag = allpassSectionMag(1000.0, 0.7071, fs, w);
        CHECK(mag == doctest::Approx(1.0).epsilon(1e-9));
    }
}

TEST_CASE("cascade phase sums distinct section phases") {
    const double fs = 48000.0;
    const int M = 8;
    double freqs[M];
    for (int i = 0; i < M; ++i) freqs[i] = 20.0 * std::pow(1000.0, double(i)/(M-1));
    APSpec a[1]    = {{300.0,  0.5}};
    APSpec b[1]    = {{4000.0, 0.8}};
    APSpec both[2] = {{300.0,  0.5}, {4000.0, 0.8}};
    double pa[M], pb[M], pab[M];
    cascadePhase(a,    1, fs, freqs, M, pa);
    cascadePhase(b,    1, fs, freqs, M, pb);
    cascadePhase(both, 2, fs, freqs, M, pab);
    for (int i = 0; i < M; ++i) CHECK(pab[i] == doctest::Approx(pa[i] + pb[i]).epsilon(1e-9));
}
