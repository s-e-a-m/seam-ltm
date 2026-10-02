#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_basics.h"
#include <cmath>

TEST_CASE("tau2pole is exp(-1/(tau*fs))") {
    CHECK(Seam::tau2pole(1.5, 96000.0) == std::exp(-1.0 / (1.5 * 96000.0)));
    CHECK(Seam::tau2pole(0.03, 48000.0) == std::exp(-1.0 / (0.03 * 48000.0)));
}

TEST_CASE("tau2pole is 0 for a time constant below epsilon, as ba.tau2pole") {
    CHECK(Seam::tau2pole(0.0, 96000.0) == 0.0);
    CHECK(Seam::tau2pole(1e-20, 96000.0) == 0.0);
    CHECK(Seam::tau2pole(-1e-20, 96000.0) == 0.0);
}
