#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "m2xhgr_engine.h"

#include <cmath>
#include <cstdint>
#include <vector>

using Seam::pd::M2xhgrEngine;

namespace {

std::vector<float> lcgNoise (int n, uint32_t seed = 4242u)
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
    explicit Out4 (int n) { for (auto& c : ch) c.assign (n, 0.0f); }
    float* ptrs[4];
    float* const* p () { for (int k = 0; k < 4; ++k) ptrs[k] = ch[k].data (); return ptrs; }
};

constexpr double kDeg = 3.14159265358979323846 / 180.0;

} // namespace

TEST_CASE("defaults and creation args") {
    CHECK(M2xhgrEngine (nullptr, 0).objectLine () == "m2xhgr~ 0 0 0 0 0 0");

    const double two[2] = { 10, 20 };
    CHECK(M2xhgrEngine (two, 2).objectLine () == "m2xhgr~ 10 20 0 0 0 0");

    const double eight[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(M2xhgrEngine (eight, 8).objectLine () == "m2xhgr~ 1 2 3 4 5 6");
}

TEST_CASE("clipping to the VST ranges") {
    M2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    e.set ("yaw", 270);
    e.set ("a1", 40);
    e.set ("a3", -99);
    CHECK(e.objectLine () == "m2xhgr~ 180 0 0 12 0 -12");

    const double wild[1] = { -500 };
    CHECK(M2xhgrEngine (wild, 1).objectLine () == "m2xhgr~ -180 0 0 0 0 0");
}

TEST_CASE("unknown message is refused and changes nothing") {
    M2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    CHECK_FALSE(e.set ("divergence", 10));
    CHECK(e.objectLine () == "m2xhgr~ 0 0 0 0 0 0");
}

TEST_CASE("equivalence with the plugin DSP at steady state") {
    const double args[6] = { 30, -15, 45, -3, 2, 6 };
    M2xhgrEngine e (args, 6);
    e.prepare (48000);

    const int n = 4096;
    auto in = lcgNoise (n);
    Out4 out (n);
    e.process (in.data (), out.p (), n);

    Seam::m2xhgr::Bank bank;
    const double g1 = std::pow (10.0, -3.0 / 20.0);
    const double g2 = std::pow (10.0,  2.0 / 20.0);
    const double g3 = std::pow (10.0,  6.0 / 20.0);
    for (int i = 0; i < n; ++i) {
        double ref[4];
        Seam::m2xhgr::tick<double> (bank, (double)in[i], g1, g2, g3,
                                    30 * kDeg, -15 * kDeg, 45 * kDeg, ref);
        for (int k = 0; k < 4; ++k)
            REQUIRE((double)out.ch[k][i] == doctest::Approx (ref[k]).epsilon (1e-6));
    }
}

TEST_CASE("ramp: reaches the target in 20 ms, not before") {
    M2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    e.set ("yaw", 90);

    std::vector<float> in (960, 0.0f);
    Out4 out (960);
    e.process (in.data (), out.p (), 959);
    CHECK(e.current (0) < 90.0);
    e.process (in.data (), out.p (), 1);
    CHECK(e.current (0) == 90.0);
}

TEST_CASE("prepare snaps a pending ramp") {
    M2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    e.set ("yaw", 90);
    std::vector<float> in (10, 0.0f);
    Out4 out (10);
    e.process (in.data (), out.p (), 10);
    REQUIRE(e.current (0) < 90.0);
    e.prepare (48000);
    CHECK(e.current (0) == 90.0);
}

TEST_CASE("in-place buffers give the separate-buffer result") {
    const double args[6] = { 30, -15, 45, -3, 2, 6 };
    const int n = 512;
    auto in = lcgNoise (n);

    M2xhgrEngine a (args, 6), b (args, 6);
    a.prepare (48000);
    b.prepare (48000);

    Out4 sep (n);
    a.process (in.data (), sep.p (), n);

    Out4 alias (n);
    alias.ch[0] = in;                       // out[0] shares memory with the input
    float* p[4] = { alias.ch[0].data (), alias.ch[1].data (),
                    alias.ch[2].data (), alias.ch[3].data () };
    b.process (p[0], p, n);

    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < n; ++i)
            REQUIRE(alias.ch[k][i] == sep.ch[k][i]);
}

TEST_CASE("rate check") {
    M2xhgrEngine e (nullptr, 0);
    e.prepare (48000);
    CHECK(e.rateIsNominal ());
    e.prepare (44100);
    CHECK_FALSE(e.rateIsNominal ());
}
