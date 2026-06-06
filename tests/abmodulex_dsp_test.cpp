#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "abmodulex_dsp.h"

using namespace Seam::abmodulex;

TEST_CASE("abmodulex: unit LFU spreads equally to all ACN channels") {
    double a0,a1,a2,a3;
    encode(1.0, 0.0, 0.0, 0.0, a0,a1,a2,a3);
    CHECK(a0 == doctest::Approx(0.5));
    CHECK(a1 == doctest::Approx(0.5));
    CHECK(a2 == doctest::Approx(0.5));
    CHECK(a3 == doctest::Approx(0.5));
}

TEST_CASE("abmodulex is involutory (round-trips with itself / bamodulex)") {
    const double lfu=0.3, rfd=-0.7, rbu=0.1, lbd=0.9;
    double a0,a1,a2,a3;
    encode(lfu,rfd,rbu,lbd, a0,a1,a2,a3);
    double b0,b1,b2,b3;
    encode(a0,a1,a2,a3, b0,b1,b2,b3);
    CHECK(b0 == doctest::Approx(lfu));
    CHECK(b1 == doctest::Approx(rfd));
    CHECK(b2 == doctest::Approx(rbu));
    CHECK(b3 == doctest::Approx(lbd));
}
