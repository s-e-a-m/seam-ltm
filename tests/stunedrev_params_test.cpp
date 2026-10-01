#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_params.h"
#include <cmath>

using namespace stunedrev;

TEST_CASE("defaults: the Pd patch's times, POWER off, input and output at 0") {
    ParamBox box;
    const Plain p = box.plain();
    CHECK_FALSE(p.power);
    for (int j = 0; j < kLines; ++j) CHECK(p.t[j] == kDefaultTimes[j]);
    CHECK(p.input == 0.0);
    CHECK(p.output == 0.0);
}

TEST_CASE("every millisecond survives normalized and back") {
    for (int ms = kTMin; ms <= kTMax; ++ms) CHECK(normalizedToTime(timeToNormalized(ms)) == ms);
}

TEST_CASE("off-grid host values map as the SDK's RangeParameter displays them") {
    // RangeParameter with stepCount 99: plain = min + min(99, int(norm * 100)).
    for (int k = 0; k <= 1000; ++k) {
        const double norm = k / 1000.0;
        const int sdk = kTMin + std::min(99, (int)(norm * 100.0));
        CHECK(normalizedToTime(norm) == sdk);
    }
}

TEST_CASE("applyTo hands every value to the engine") {
    ParamBox box;
    box.store(Param::Power, 1.0);
    box.store(Param::T3, timeToNormalized(9));
    box.store(Param::Input, 0.5);
    box.store(Param::Output, 0.25);
    Engine e;
    REQUIRE(e.prepare(48000.0));
    applyTo(box.plain(), e);
    CHECK(e.time(2) == 9);
    CHECK(e.time(0) == 83);
}
