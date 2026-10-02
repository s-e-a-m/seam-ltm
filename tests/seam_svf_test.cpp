#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_svf.h"
#include "ref/seam_svf_ref.h"
#include <algorithm>
#include <cmath>

using Seam::SvfBandpass;

template <int N>
static double impulseRelErr(double fs, double f, double q, const double (*ref)[N]) {
    SvfBandpass b; b.design(fs, f, q);
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < N; ++k) {
        const double y = b.tick(k == 0 ? 1.0 : 0.0);
        e = std::max(e, std::fabs(y - ref[0][k]));
        pk = std::max(pk, std::fabs(ref[0][k]));
    }
    return e / pk;
}

TEST_CASE("equals fi.svf.bp(48, 350) at 96 kHz, impulse response") {
    CHECK(impulseRelErr<2048>(96000.0, 48.0, 350.0, svfref::kSvf48) < 1e-13);
}

TEST_CASE("equals fi.svf.bp(1000, 0.7) at 48 kHz, impulse response") {
    CHECK(impulseRelErr<512>(48000.0, 1000.0, 0.7, svfref::kSvf1k) < 1e-13);
}

TEST_CASE("peak gain is q at the centre") {
    const double fs = 48000.0, f = 1000.0, q = 0.7;
    SvfBandpass b; b.design(fs, f, q);
    double pk = 0.0;
    for (int n = 0; n < (int)fs; ++n) {
        const double y = b.tick(std::sin(2.0 * M_PI * f * n / fs));
        if (n > fs / 2) pk = std::max(pk, std::fabs(y));
    }
    CHECK(pk == doctest::Approx(q).epsilon(1e-4));
}

TEST_CASE("reset empties the state") {
    SvfBandpass b; b.design(96000.0, 48.0, 350.0);
    for (int n = 0; n < 1000; ++n) b.tick(1.0);
    b.reset();
    CHECK(b.tick(0.0) == 0.0);
}
