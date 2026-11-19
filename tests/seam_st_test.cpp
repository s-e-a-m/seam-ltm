#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_st.h"
#include <cmath>

using namespace Seam::st;

// The 16 rows of the table, fc = 100 * 2.5^(k/15), as in seam.st.lib.
static const double kF0[16] = {
    222.279, 231.823, 241.847, 252.38, 263.457, 275.11, 287.379, 300.304,
    313.929, 328.302, 343.474, 359.499, 376.439, 394.357, 413.322, 433.407};
static const double kQ[16] = {
    0.836531, 0.847386, 0.858033, 0.868433, 0.878556, 0.888353, 0.897807, 0.906876,
    0.915531, 0.923743, 0.931485, 0.938721, 0.945439, 0.951616, 0.957251, 0.962313};
static const double kG[16] = {
    -4.98392, -4.72051, -4.46347, -4.21306, -3.96957, -3.73324, -3.50434, -3.28311,
    -3.06976, -2.86449, -2.66749, -2.47886, -2.29875, -2.12721, -1.9643, -1.81001};

TEST_CASE("mk2g table: grid points reproduce the rows") {
    for (int k = 0; k < kMk2gRows; ++k) {
        const double fc = 100.0 * std::pow(2.5, k / 15.0);
        const Bell b = mk2gBell(fc);
        INFO("row " << k);
        CHECK(std::fabs(b.f0 - kF0[k]) < 1e-9);
        CHECK(std::fabs(b.q - kQ[k]) < 1e-12);
        CHECK(std::fabs(b.gainDb - kG[k]) < 1e-12);
    }
}

TEST_CASE("mk2g table: clamps and monotone") {
    const Bell lo = mk2gBell(50.0), hi = mk2gBell(400.0);
    CHECK(lo.f0 == doctest::Approx(kF0[0]).epsilon(1e-12));
    CHECK(lo.q == doctest::Approx(kQ[0]).epsilon(1e-12));
    CHECK(lo.gainDb == doctest::Approx(kG[0]).epsilon(1e-12));
    CHECK(hi.f0 == doctest::Approx(kF0[15]).epsilon(1e-12));
    CHECK(hi.q == doctest::Approx(kQ[15]).epsilon(1e-12));
    CHECK(hi.gainDb == doctest::Approx(kG[15]).epsilon(1e-12));
    Bell prev = mk2gBell(100.0);
    for (int i = 1; i < 1000; ++i) {
        const Bell b = mk2gBell(100.0 + 150.0 * i / 999.0);
        CHECK(b.f0 > prev.f0);
        CHECK(b.gainDb > prev.gainDb);
        prev = b;
    }
}

TEST_CASE("mk2g table: trial points") {
    const Bell b = mk2gBell(kMk2gCutDefault);
    CHECK(std::fabs(std::log2(b.f0 / 310.0)) < 1.0 / 3.0);
    CHECK(std::fabs(b.gainDb + 3.1) < 1.0);
}
