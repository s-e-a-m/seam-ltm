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

TEST_CASE("a negative output clamps to 0") {
    ParamBox b;
    b.store(Param::Output, -0.5);
    CHECK(b.plain().output == 0.0);
}

TEST_CASE("the POWER threshold is 0.5") {
    ParamBox b;
    b.store(Param::Power, 0.49);
    CHECK_FALSE(b.plain().power);
    b.store(Param::Power, 0.5);
    CHECK(b.plain().power);
}

TEST_CASE("NaN host values give finite, in-range plain values") {
    const double nan = std::nan("");
    ParamBox b;
    b.store(Param::Distance, nan);
    b.store(Param::Output, nan);
    b.store(Param::Power, nan);
    const Plain p = b.plain();
    CHECK(std::isfinite(p.metres));
    CHECK((p.metres >= 0.0 && p.metres <= 30.0));
    CHECK(std::isfinite(p.output));
    CHECK((p.output >= 0.0 && p.output <= 1.0));
    CHECK_FALSE(p.power);
}

// ── The compressor's ten sliders: five for channel 2, five for channel 4 ──
static bool same(const Dynamics& a, const Dynamics& b) { return a == b; }

TEST_CASE("untouched sliders give the score's set exactly, on both channels") {
    ParamBox b;
    const Plain p = b.plain();
    for (int r = 0; r < 2; ++r) {
        CAPTURE(r);
        CHECK(p.dyn[r].driveDb == 20.0);
        CHECK(p.dyn[r].ratio == 11.0);
        CHECK(p.dyn[r].threshDb == -24.0);
        CHECK(p.dyn[r].attack == 0.03);
        CHECK(p.dyn[r].release == 0.04);
    }
}

static double rel(double got, double want) { return std::fabs(got / want - 1.0); }

TEST_CASE("drive and threshold are linear in dB; ratio, attack and release are logarithmic") {
    struct Row { double n, drive, thresh, ratio, attack, release; };
    // At n = 0.5 the logarithmic ones sit at the geometric mean of their ends.
    const Row rows[] = {
        { 0.0,   0.0, -60.0,  1.0,                  0.001,                 0.005 },
        { 0.25, 10.0, -45.0,  2.5148668593658708,   0.0056234132519034910, 0.028117066259517455 },
        { 0.5,  20.0, -30.0,  6.3245553203367586,   0.031622776601683791,  0.15811388300841897 },
        { 1.0,  40.0,   0.0, 40.0,                  1.0,                   5.0 },
    };
    for (const Row& w : rows) {
        CAPTURE(w.n);
        ParamBox b;
        b.store(Param::Drive2, w.n);  b.store(Param::Thresh2, w.n);  b.store(Param::Ratio2, w.n);
        b.store(Param::Attack2, w.n); b.store(Param::Release2, w.n);
        const Dynamics d = b.plain().dyn[0];
        CHECK(std::fabs(d.driveDb - w.drive) < 1e-12);
        CHECK(std::fabs(d.threshDb - w.thresh) < 1e-12);
        CHECK(rel(d.ratio, w.ratio) < 1e-12);
        CHECK(rel(d.attack, w.attack) < 1e-12);
        CHECK(rel(d.release, w.release) < 1e-12);
    }
}

TEST_CASE("each slider reaches its own field of its own channel, and nothing else") {
    const Param ch2[5] = { Param::Drive2, Param::Thresh2, Param::Ratio2, Param::Attack2, Param::Release2 };
    const Param ch4[5] = { Param::Drive4, Param::Thresh4, Param::Ratio4, Param::Attack4, Param::Release4 };
    for (int r = 0; r < 2; ++r)
        for (int f = 0; f < 5; ++f) {
            CAPTURE(r); CAPTURE(f);
            ParamBox b;
            b.store(r == 0 ? ch2[f] : ch4[f], 1.0);
            const Plain p = b.plain();
            Dynamics want;                       // the score's set with one field at its top
            double* field[5] = { &want.driveDb, &want.threshDb, &want.ratio, &want.attack, &want.release };
            const double top[5] = { 40.0, 0.0, 40.0, 1.0, 5.0 };
            *field[f] = top[f];
            CHECK(same(p.dyn[r], want));
            CHECK(same(p.dyn[1 - r], Dynamics{}));
            CHECK(p.power == false);
            CHECK(std::fabs(p.metres - 7.291) < 1e-12);
        }
}

TEST_CASE("NaN on the compressor's sliders gives finite, in-range values") {
    const double nan = std::nan("");
    ParamBox b;
    for (int i = (int)Param::Drive2; i < kNumParams; ++i) b.store((Param)i, nan);
    const Plain p = b.plain();
    for (int r = 0; r < 2; ++r) {
        CHECK((p.dyn[r].driveDb >= 0.0 && p.dyn[r].driveDb <= 40.0));
        CHECK((p.dyn[r].threshDb >= -60.0 && p.dyn[r].threshDb <= 0.0));
        CHECK((p.dyn[r].ratio >= 1.0 && p.dyn[r].ratio <= 40.0));
        CHECK((p.dyn[r].attack >= 0.001 && p.dyn[r].attack <= 1.0));
        CHECK((p.dyn[r].release >= 0.005 && p.dyn[r].release <= 5.0));
    }
}

TEST_CASE("applyTo hands each channel's set to the engine") {
    Engine e;
    REQUIRE(e.prepare(96000.0));
    ParamBox b;
    b.store(Param::Thresh4, 0.25);
    b.store(Param::Ratio2, 1.0);
    applyTo(b.plain(), e);
    CHECK(e.dynamics(0).ratio == 40.0);
    CHECK(e.dynamics(0).threshDb == -24.0);
    CHECK(std::fabs(e.dynamics(1).threshDb - (-45.0)) < 1e-12);
    CHECK(e.dynamics(1).ratio == 11.0);
}

// The default plays the score's value exactly (above); here the host's
// slider must also SHOW it: the default position, read through the mapping.
TEST_CASE("each slider's default position reads as the score's value on its scale") {
    const double score[5] = { 20.0, -24.0, 11.0, 0.03, 0.04 };
    for (int i = (int)Param::Drive2; i < kNumParams; ++i) {
        CAPTURE(i);
        const int f = (i - (int)Param::Drive2) % 5;
        const double v = dynToPlain(f, defaultNormalized((Param)i));
        CHECK(std::fabs(v - score[f]) <= 1e-12 * std::fabs(score[f]));
    }
}
