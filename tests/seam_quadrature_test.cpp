#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_quadrature.h"
#include <cmath>

using namespace seam::quadrature;

TEST_CASE("allpass section has unit magnitude (phase-only)") {
    const double fs = 48000.0;
    for (double fHz : {20.0, 200.0, 2000.0, 18000.0}) {
        double w = 2.0 * M_PI * fHz / fs;
        double mag = allpassSectionMag(1000.0, 0.7071, fs, w);
        CHECK(mag == doctest::Approx(1.0).epsilon(1e-9));
    }
}

TEST_CASE("cascade phase sums section phases") {
    const double fs = 48000.0;
    const int M = 8;
    double freqs[M];
    for (int i = 0; i < M; ++i) freqs[i] = 20.0 * std::pow(1000.0, double(i)/(M-1));
    APSpec one[1]  = {{1000.0, 0.7071}};
    APSpec two[2]  = {{1000.0, 0.7071}, {1000.0, 0.7071}};
    double p1[M], p2[M];
    cascadePhase(one, 1, fs, freqs, M, p1);
    cascadePhase(two, 2, fs, freqs, M, p2);
    for (int i = 0; i < M; ++i) CHECK(p2[i] == doctest::Approx(2.0 * p1[i]).epsilon(1e-9));
}
