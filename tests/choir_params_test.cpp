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
