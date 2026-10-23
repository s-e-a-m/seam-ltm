#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lr2xhgr_engine.h"

#include <cmath>
#include <cstdint>
#include <vector>

using Seam::pd::Lr2xhgrEngine;

namespace {

std::vector<float> lcgNoise (int n, uint32_t seed)
{
    std::vector<float> v (n);
    for (auto& x : v) {
        seed = seed * 1664525u + 1013904223u;
        x = (float)((double)(seed >> 8) / (double)(1u << 24) * 2.0 - 1.0);
    }
    return v;
}

struct Out4 {
    std::vector<float> ch[4];
    float* ptrs[4];
    explicit Out4 (int n) { for (auto& c : ch) c.assign (n, 0.0f); }
    float* const* p () { for (int k = 0; k < 4; ++k) ptrs[k] = ch[k].data (); return ptrs; }
};

constexpr double kDeg = 3.14159265358979323846 / 180.0;

} // namespace

TEST_CASE("defaults and creation args") {
    CHECK(Lr2xhgrEngine (nullptr, 0).objectLine () == "lr2xhgr~ 90 0 0 0 0 0 0");
    const double two[2] = { 35, 10 };
    CHECK(Lr2xhgrEngine (two, 2).objectLine () == "lr2xhgr~ 35 10 0 0 0 0 0");
    const double nine[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    CHECK(Lr2xhgrEngine (nine, 9).objectLine () == "lr2xhgr~ 1 2 3 4 5 6 7");
}

TEST_CASE("clipping to the VST ranges") {
    Lr2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    e.set ("divergence", 400);
    e.set ("roll", -270);
    e.set ("a2", 20);
    CHECK(e.objectLine () == "lr2xhgr~ 360 0 0 -180 0 12 0");
    e.set ("divergence", -5);
    CHECK(e.objectLine () == "lr2xhgr~ 0 0 0 -180 0 12 0");
}

TEST_CASE("unknown message is refused and changes nothing") {
    Lr2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    CHECK_FALSE(e.set ("foo", 1));
    CHECK(e.objectLine () == "lr2xhgr~ 90 0 0 0 0 0 0");
}

TEST_CASE("equivalence with the plugin DSP at steady state") {
    const double args[7] = { 35, 10, -20, 5, -2, 0, 3 };
    Lr2xhgrEngine e (args, 7);
    e.prepare (48000);

    const int n = 4096;
    auto inL = lcgNoise (n, 111u);
    auto inR = lcgNoise (n, 222u);
    Out4 out (n);
    e.process (inL.data (), inR.data (), out.p (), n);

    Seam::m2xhgr::Bank bankL, bankR;
    const double g1 = std::pow (10.0, -2.0 / 20.0);
    const double g2 = 1.0;
    const double g3 = std::pow (10.0, 3.0 / 20.0);
    for (int i = 0; i < n; ++i) {
        double la[4], ra[4], ref[4];
        bankL.decompose ((double)inL[i], la);
        bankR.decompose ((double)inR[i], ra);
        // The plugin hands mix HALF the divergence, in radians.
        Seam::lr2xhgr::mix<double> (35.0 / 2.0 * kDeg, 10 * kDeg, -20 * kDeg, 5 * kDeg,
                                    g1, g2, g3, la, ra, ref);
        for (int k = 0; k < 4; ++k)
            REQUIRE((double)out.ch[k][i] == doctest::Approx (ref[k]).epsilon (1e-6));
    }
}

TEST_CASE("prepare snaps a pending ramp") {
    Lr2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    e.set ("divergence", 180);
    std::vector<float> in (10, 0.0f);
    Out4 out (10);
    e.process (in.data (), in.data (), out.p (), 10);
    REQUIRE(e.current (0) < 180.0);
    e.prepare (48000);
    CHECK(e.current (0) == 180.0);
}

TEST_CASE("in-place buffers give the separate-buffer result") {
    const double args[7] = { 35, 10, -20, 5, -2, 0, 3 };
    const int n = 512;
    auto inL = lcgNoise (n, 333u);
    auto inR = lcgNoise (n, 444u);

    Lr2xhgrEngine a (args, 7), b (args, 7);
    a.prepare (48000);
    b.prepare (48000);

    Out4 sep (n);
    a.process (inL.data (), inR.data (), sep.p (), n);

    Out4 alias (n);
    alias.ch[0] = inL;                      // out[0] shares memory with L
    alias.ch[1] = inR;                      // out[1] shares memory with R
    float* p[4] = { alias.ch[0].data (), alias.ch[1].data (),
                    alias.ch[2].data (), alias.ch[3].data () };
    b.process (p[0], p[1], p, n);

    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < n; ++i)
            REQUIRE(alias.ch[k][i] == sep.ch[k][i]);
}

TEST_CASE("rate check") {
    Lr2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    CHECK(e.rateIsNominal ());
    e.prepare (96000);
    CHECK_FALSE(e.rateIsNominal ());
}
