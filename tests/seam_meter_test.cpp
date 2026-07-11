#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_meter.h"
#include <cmath>

using namespace seam::meter;

TEST_CASE("lin2db: unity is 0 dB, silence floors, half is ~-6 dB") {
    CHECK(lin2db(1.0)   == doctest::Approx(0.0));
    CHECK(lin2db(0.0)   == doctest::Approx(-60.0));      // floor, never -inf
    CHECK(lin2db(0.5)   == doctest::Approx(-6.0205999));
    CHECK(lin2db(1e-9)  == doctest::Approx(-60.0));      // below floor clamps
}

TEST_CASE("db2norm maps [floor,0] to [0,1] and clamps") {
    CHECK(db2norm(0.0)    == doctest::Approx(1.0));
    CHECK(db2norm(-60.0)  == doctest::Approx(0.0));
    CHECK(db2norm(-30.0)  == doctest::Approx(0.5));
    CHECK(db2norm(6.0)    == doctest::Approx(1.0));      // above 0 clamps to 1
    CHECK(db2norm(-90.0)  == doctest::Approx(0.0));      // below floor clamps to 0
}

TEST_CASE("db2norm: degenerate floor (floorDb>=0) returns defined 0/1, not NaN") {
    CHECK(db2norm(-30.0, 0.0) == doctest::Approx(0.0));
    CHECK(db2norm(6.0, 0.0)   == doctest::Approx(1.0));
    CHECK(std::isfinite(db2norm(-30.0, 0.0)));
}

TEST_CASE("lin2norm composes lin2db and db2norm; norm2db inverts") {
    CHECK(lin2norm(1.0) == doctest::Approx(1.0));
    CHECK(lin2norm(0.0) == doctest::Approx(0.0));
    CHECK(norm2db(0.5)  == doctest::Approx(-30.0));
}

TEST_CASE("LevelFollower(RMS) of a DC 0.5 settles to 0.5") {
    LevelFollower f;
    f.prepare(48000.0, LevelFollower::Mode::Rms, 10.0);
    double v = 0.0;
    for (int i = 0; i < 48000; ++i) v = f.feed(0.5);
    CHECK(v == doctest::Approx(0.5).epsilon(1e-3));
}

TEST_CASE("LevelFollower(Peak) tracks the absolute value") {
    LevelFollower f;
    f.prepare(48000.0, LevelFollower::Mode::Peak, 1.0);
    double v = 0.0;
    for (int i = 0; i < 48000; ++i) v = f.feed(-0.8);
    CHECK(v == doctest::Approx(0.8).epsilon(1e-3));
}
