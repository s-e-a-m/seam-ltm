#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_rotation.h"
#include <cmath>

using Seam::rotateYPR;
using Seam::gainRotateYPR;

TEST_CASE("gainRotateYPR: gains hit A1/A2/A3, A0 fixed (zero rotation)") {
    double o0, o1, o2, o3;
    gainRotateYPR(0.5, 1.0, 0.25, 0.0, 0.0, 0.0,
                  1.0, 2.0, 3.0, 4.0, o0, o1, o2, o3);
    CHECK(o0 == doctest::Approx(1.0));   // A0 untouched
    CHECK(o1 == doctest::Approx(1.0));   // 2 * 0.5
    CHECK(o2 == doctest::Approx(3.0));   // 3 * 1.0
    CHECK(o3 == doctest::Approx(1.0));   // 4 * 0.25
}

TEST_CASE("gainRotateYPR: gain precedes rotation (yaw = pi/2 diverges the two orders)") {
    const double yaw = M_PI / 2.0;
    const double g1 = 0.5, g2 = 1.0, g3 = 0.25;
    const double a0 = 1.0, a1 = 2.0, a2 = 3.0, a3 = 4.0;

    // Reference built in the CORRECT order: gain first, then rotate.
    double r0, r1, r2, r3;
    rotateYPR(yaw, 0.0, 0.0, a0, a1 * g1, a2 * g2, a3 * g3, r0, r1, r2, r3);

    double o0, o1, o2, o3;
    gainRotateYPR(g1, g2, g3, yaw, 0.0, 0.0, a0, a1, a2, a3, o0, o1, o2, o3);

    CHECK(o0 == doctest::Approx(r0));
    CHECK(o1 == doctest::Approx(r1));
    CHECK(o2 == doctest::Approx(r2));
    CHECK(o3 == doctest::Approx(r3));

    // Pin the concrete numbers too: rotateYPR(pi/2) maps (a0,a1,a2,a3) ->
    // (a0, -a3, a2, a1), so gain-first gives (1, -a3*g3, a2*g2, a1*g1).
    CHECK(o1 == doctest::Approx(-4.0 * 0.25)); // -1.0
    CHECK(o3 == doctest::Approx( 2.0 * 0.5));  //  1.0
}
