#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_params.h"
#include <cmath>

using namespace delrm;

TEST_CASE("the defaults: power off, 7.291 m, output 0") {
    ParamBox b;
    const Plain p = b.plain();
    CHECK_FALSE(p.power);
    CHECK(std::fabs(p.metres - 7.291) < 1e-12);
    CHECK(p.output == 0.0);
}

TEST_CASE("the distance maps linearly over 0-30 m and round-trips") {
    for (double mt : {0.0, 0.001, 7.291, 15.0, 30.0})
        CHECK(std::fabs(normalizedToDistance(distanceToNormalized(mt)) - mt) < 1e-12);
}

TEST_CASE("host values off the range clamp") {
    ParamBox b;
    b.store(Param::Distance, -0.2);
    CHECK(b.plain().metres == 0.0);
    b.store(Param::Distance, 1.3);
    CHECK(b.plain().metres == 30.0);
    b.store(Param::Output, 1.7);
    CHECK(b.plain().output == 1.0);
}

TEST_CASE("applyTo sets the engine's distance, output and power") {
    Engine e;
    REQUIRE(e.prepare(96000.0));
    ParamBox b;
    b.store(Param::Distance, distanceToNormalized(10.0));
    applyTo(b.plain(), e);
    const Seam::PrimeSieve s(sieveBound(96000.0));
    CHECK(e.delaySamples() == delayFor(10.0, 96000.0, s));
}
