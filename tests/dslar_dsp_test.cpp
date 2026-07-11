#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "dslar_dsp.h"
#include <cmath>

using namespace Seam::dslar;

TEST_CASE("pd::powtodb — Pd 100 dB-offset power->dB, clamped") {
    CHECK(pd::powtodb(1.0)  == doctest::Approx(100.0));   // 100 + 10*log10(1)
    CHECK(pd::powtodb(0.01) == doctest::Approx(80.0));    // 100 + 10*log10(0.01)
    CHECK(pd::powtodb(0.0)  == doctest::Approx(0.0));      // silence -> 0
    CHECK(pd::powtodb(0.25) == doctest::Approx(93.9794000867));
}

TEST_CASE("pd::dbtorms — inverse converter, clamped") {
    CHECK(pd::dbtorms(100.0) == doctest::Approx(1.0));
    CHECK(pd::dbtorms(80.0)  == doctest::Approx(0.1));
    CHECK(pd::dbtorms(0.0)   == doctest::Approx(0.0));
    // dbtorms(powtodb(0.25)) == sqrt(0.25) == 0.5 (the dB round-trip cancels)
    CHECK(pd::dbtorms(pd::powtodb(0.25)) == doctest::Approx(0.5));
}
