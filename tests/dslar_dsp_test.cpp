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

TEST_CASE("OnePoleHip: coef follows Pd 1-f*2*3.14159/SR; blocks DC") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    // Feed DC 1.0; a highpass settles its output toward 0.
    double y = 0.0;
    for (int n = 0; n < 44100; ++n) y = hp.process(1.0);
    CHECK(std::fabs(y) < 1e-3);
}

TEST_CASE("OnePoleHip: first sample of a unit step equals normal") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    const double coef   = std::min(1.0, std::max(0.0, 1.0 - 100.0*(2.0*3.14159)/44100.0));
    const double normal = (1.0 + coef) / 2.0;
    // w0 = 1 + coef*0 = 1 ; y0 = normal*(w0 - 0) = normal.
    CHECK(hp.process(1.0) == doctest::Approx(normal));
}
