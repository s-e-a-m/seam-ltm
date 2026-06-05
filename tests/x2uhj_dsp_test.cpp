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
