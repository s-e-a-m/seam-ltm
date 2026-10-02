#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_analyzers.h"
#include "ref/seam_analyzers_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

// The same input as follow.dsp: sin(2*pi*48*n/SR), off from n = SR.
static std::vector<double> followed(double fs, int n) {
    Seam::AmpFollower f; f.prepare(fs, 1.5);
    std::vector<double> y((size_t)n);
    for (int k = 0; k < n; ++k)
        y[(size_t)k] = f.tick(k < (int)fs ? std::sin(2 * M_PI * 48 * k / fs) : 0.0);
    return y;
}

template <int N>
static double relErr(const std::vector<double>& y, int skip, const double (*ref)[N]) {
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < N; ++k) {
        e = std::max(e, std::fabs(y[(size_t)(skip + k)] - ref[0][k]));
        pk = std::max(pk, std::fabs(ref[0][k]));
    }
    return e / pk;
}

TEST_CASE("equals an.amp_follower(1.5) around the stop and a second later") {
    auto y = followed(96000.0, 192000 + 1024);
    CHECK(relErr<2048>(y, 95000, analyzersref::kFollowStop96) < 1e-12);
    CHECK(relErr<1024>(y, 191000, analyzersref::kFollowLate96) < 1e-12);
}

TEST_CASE("the attack is immediate") {
    Seam::AmpFollower f; f.prepare(96000.0, 1.5);
    CHECK(f.tick(-0.7) == 0.7);
}

TEST_CASE("the release is exp(-1/1.5) after one second, at any rate") {
    for (double fs : {48000.0, 96000.0}) {
        Seam::AmpFollower f; f.prepare(fs, 1.5);
        f.tick(1.0);
        for (int k = 0; k < (int)fs; ++k) f.tick(0.0);
        CHECK(f.value() == doctest::Approx(std::exp(-1.0 / 1.5)).epsilon(1e-4));
    }
}
