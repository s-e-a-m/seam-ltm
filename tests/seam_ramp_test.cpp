#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_ramp.h"
#include <cmath>

using Seam::LinearRamp;

TEST_CASE("reaches the target in round(seconds*fs) samples, exactly") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        LinearRamp r;
        r.setTarget(0.0, 0.0, fs); r.snap();
        r.setTarget(1.0, 0.025, fs);
        const long n = std::lround(0.025 * fs);
        for (long k = 0; k < n - 1; ++k) { r.next(); REQUIRE(r.active()); }
        CHECK(r.next() == 1.0);
        CHECK_FALSE(r.active());
        CHECK(r.next() == 1.0);
    }
}

TEST_CASE("linear: the midpoint is half way") {
    LinearRamp r;
    r.setTarget(100.0, 0.0, 1000.0); r.snap();
    r.setTarget(200.0, 1.0, 1000.0);
    for (int k = 0; k < 500; ++k) r.next();
    CHECK(std::fabs(r.value() - 150.0) < 1e-9);
}

TEST_CASE("a new target mid-ramp starts from the current value") {
    LinearRamp r;
    r.setTarget(0.0, 0.0, 1000.0); r.snap();
    r.setTarget(1.0, 1.0, 1000.0);
    for (int k = 0; k < 400; ++k) r.next();
    const double v = r.value();
    r.setTarget(0.0, 0.1, 1000.0);
    const double first = r.next();
    CHECK(first < v);
    CHECK(std::fabs((v - first) - v / 100.0) < 1e-12);
}

TEST_CASE("zero seconds is one sample") {
    LinearRamp r;
    r.setTarget(5.0, 0.0, 48000.0);
    CHECK(r.next() == 5.0);
}

TEST_CASE("snap jumps to the target") {
    LinearRamp r;
    r.setTarget(3.0, 10.0, 48000.0);
    r.snap();
    CHECK(r.value() == 3.0);
    CHECK_FALSE(r.active());
}
