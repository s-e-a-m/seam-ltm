#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lr2xhgr_dsp.h"

using Seam::lr2xhgr::mix;

TEST_CASE("lr2xhgr mix: one gain scales BOTH banks (the sharing rule)") {
    // Zero divergence and zero rotation: mix reduces to the gained sum.
    const double la[4] = {0.0, 1.0, 0.0, 0.0};   // A1 only, left bank
    const double ra[4] = {0.0, 3.0, 0.0, 0.0};   // A1 only, right bank
    double out[4];
    mix(0.0, 0.0, 0.0, 0.0, /*g1*/0.5, /*g2*/1.0, /*g3*/1.0, la, ra, out);

    // Shared trim: (1 + 3) * 0.5 = 2.0. A per-bank bug (gain on L only) would
    // give 1*0.5 + 3 = 3.5.
    CHECK(out[1] == doctest::Approx(2.0));
    CHECK(out[0] == doctest::Approx(0.0));
}

TEST_CASE("lr2xhgr mix: A0 (W) is never gained, sums straight through") {
    const double la[4] = {2.0, 0.0, 0.0, 0.0};
    const double ra[4] = {5.0, 0.0, 0.0, 0.0};
    double out[4];
    mix(0.3, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, la, ra, out);
    CHECK(out[0] == doctest::Approx(7.0));   // 2 + 5, untouched by trims or rotation
}
