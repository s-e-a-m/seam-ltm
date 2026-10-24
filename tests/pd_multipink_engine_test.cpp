#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_engine.h"

#include <cmath>
#include <cstdint>
#include <vector>

using Seam::pd::MultipinkEngine;
namespace mp = Seam::multipink;

namespace {

constexpr double kFs = 48000.0;

// Run the engine for total samples in blocks of block, one vector per channel.
std::vector<std::vector<float>> render (MultipinkEngine& e, int total, int block)
{
    std::vector<std::vector<float>> out (MultipinkEngine::kChannels, std::vector<float> (total));
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min (block, total - pos);
        float* o[MultipinkEngine::kChannels];
        for (int k = 0; k < MultipinkEngine::kChannels; ++k)
            o[k] = out[k].data () + pos;
        e.process (o, n);
    }
    return out;
}

} // namespace

// Golden values from the formula the VST3 shipped inline in seedLCGs before
// it moved to multipink_pink.h: a change here changes every stream.
TEST_CASE("poolSeed: the VST3's seeds for slots 1-4") {
    CHECK(mp::poolSeed (12345u, 0) == 4242673409u);
    CHECK(mp::poolSeed (12345u, 1) == 3470387887u);
    CHECK(mp::poolSeed (12345u, 2) == 2122387587u);
    CHECK(mp::poolSeed (12345u, 3) == 2021974986u);
}

// What the VST3 does for an instance on slots 1-4 at Reference -23, Trim 0:
// the plug-in's own source, filter and gain, float state, float samples.
TEST_CASE("equals a 4-channel multipink at -23 dBFS on slots 1-4") {
    constexpr int kBlock = 64, kBlocks = 200;
    mp::PinkDesign d;
    d.design (kFs);
    uint32_t lcg[4];
    for (int k = 0; k < 4; ++k)
        lcg[k] = mp::poolSeed (12345u, k);
    std::vector<float> state (mp::PinkDesign::kMaxSections * 4, 0.0f);
    std::vector<float> scratch (4 * kBlock);
    const float g = (float)std::pow (10.0, (-23.0 + d.calibrationOffsetDb ()) / 20.0);

    MultipinkEngine e;
    e.prepare (kFs, kBlock);
    const auto got = render (e, kBlock * kBlocks, kBlock);

    for (int b = 0; b < kBlocks; ++b) {
        for (int k = 0; k < 4; ++k)
            lcg[k] = mp::whiteNoiseRow<float> (lcg[k], scratch.data () + k * kBlock, kBlock);
        mp::pinkFilterBlock<float> (d, scratch.data (), 4, kBlock, state.data (), 4);
        for (int k = 0; k < 4; ++k)
            for (int n = 0; n < kBlock; ++n)
                REQUIRE(got[k][b * kBlock + n] == scratch[k * kBlock + n] * g);
    }
}

TEST_CASE("block size does not change the stream") {
    MultipinkEngine a, b;
    a.prepare (kFs, 64);
    b.prepare (kFs, 64);
    const auto x = render (a, 6400, 64);
    const auto y = render (b, 6400, 37);
    for (int k = 0; k < 4; ++k)
        REQUIRE(x[k] == y[k]);
}

TEST_CASE("prepare restarts the same streams") {
    MultipinkEngine e;
    e.prepare (kFs, 64);
    const auto first = render (e, 4096, 64);
    e.prepare (kFs, 64);
    const auto again = render (e, 4096, 64);
    for (int k = 0; k < 4; ++k)
        REQUIRE(first[k] == again[k]);
}

// The calibration: total RMS -23.000 dBFS per channel at 48 kHz
// (plugins/multipink/doc/calibration.md measured -22.997 over 31 s).
// Streams scatter a few hundredths of a dB, hence 0.1.
TEST_CASE("each channel at -23 dBFS RMS at 48 kHz, streams uncorrelated") {
    MultipinkEngine e;
    e.prepare (kFs, 64);
    const int total = (int)(30 * kFs);
    const auto x = render (e, total, 64);
    for (int k = 0; k < 4; ++k) {
        double s = 0;
        for (float v : x[k]) s += (double)v * v;
        const double db = 10.0 * std::log10 (s / total);
        INFO("channel " << k << ": " << db << " dBFS");
        CHECK(std::fabs (db + 23.0) < 0.1);
    }
    // Correlation of the first differences, not of the streams: pink keeps
    // most of its power in a few slow cycles, so over 30 s the raw estimate
    // of two independent streams scatters by +-0.03 (it measured -0.030,
    // -0.015, -0.004 at 30, 120, 480 s). Differencing tilts the weight to
    // the many fast cycles, where independent streams read |r| < 0.002 and
    // a shared or one-sample-shifted seed reads far above 0.005.
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) {
            double sij = 0, sii = 0, sjj = 0;
            for (int n = 1; n < total; ++n) {
                const double di = (double)x[i][n] - x[i][n - 1];
                const double dj = (double)x[j][n] - x[j][n - 1];
                sij += di * dj;
                sii += di * di;
                sjj += dj * dj;
            }
            const double r = sij / std::sqrt (sii * sjj);
            INFO("channels " << i << "," << j << ": r = " << r);
            CHECK(std::fabs (r) < 0.005);
        }
}
