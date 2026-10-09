#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_params.h"

using namespace choir;

TEST_CASE("defaults: POWER off, output 0") {
    ParamBox b;
    CHECK_FALSE(b.plain().power);
    CHECK(b.plain().output == 0.0);
}

TEST_CASE("plain values from normalized ones") {
    ParamBox b;
    b.store(Param::Power, 0.6); b.store(Param::Output, 0.25);
    CHECK(b.plain().power);
    CHECK(b.plain().output == 0.25);
    b.store(Param::Output, 1.7);
    CHECK(b.plain().output == 1.0);
}

TEST_CASE("applyTo sets the engine's targets") {
    Engine e; e.prepare(96000.0); e.reset();
    ParamBox b; b.store(Param::Power, 1.0); b.store(Param::Output, 0.5);
    applyTo(b.plain(), e);
    e.reset();
    CHECK(e.gain() == 0.5);
}

TEST_CASE("defaults: 1/f off, the score's Q 350 and release 1.5 s") {
    ParamBox b;
    CHECK_FALSE(b.plain().pink);
    CHECK(b.plain().q == doctest::Approx(350.0).epsilon(1e-12));
    CHECK(b.plain().release == doctest::Approx(1.5).epsilon(1e-12));
}

TEST_CASE("Q and release are logarithmic over their ranges") {
    ParamBox b;
    b.store(Param::Q, 0.0);       CHECK(b.plain().q == doctest::Approx(10.0));
    b.store(Param::Q, 0.5);       CHECK(b.plain().q == doctest::Approx(100.0));
    b.store(Param::Q, 1.0);       CHECK(b.plain().q == doctest::Approx(1000.0));
    b.store(Param::Q, 1.4);       CHECK(b.plain().q == doctest::Approx(1000.0));
    b.store(Param::Release, 0.0); CHECK(b.plain().release == doctest::Approx(0.05));
    b.store(Param::Release, 1.0); CHECK(b.plain().release == doctest::Approx(10.0));
    for (double q : {10.0, 37.0, 350.0, 1000.0}) CHECK(qToPlain(qToNormalized(q)) == doctest::Approx(q));
    for (double r : {0.05, 0.3, 1.5, 10.0}) CHECK(releaseToPlain(releaseToNormalized(r)) == doctest::Approx(r));
}

TEST_CASE("applyTo sets 1/f, Q and release") {
    Engine e; e.prepare(48000.0); e.reset();
    ParamBox b;
    b.store(Param::Pink, 1.0); b.store(Param::Q, 0.5); b.store(Param::Release, 1.0);
    applyTo(b.plain(), e);
    e.reset();
    CHECK(e.pinkMix() == 1.0);
    CHECK(e.q() == doctest::Approx(100.0));
    CHECK(e.releaseTime() == doctest::Approx(10.0));
}
