#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_dsp.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/delrm_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace delrm;

static void settle(Engine& e, double fs) {
    REQUIRE(e.prepare(fs));
    e.setOutput(1.0); e.setPower(true);
    e.reset();                                  // every ramp onto its target
}

// The signal through the engine in blocks of `block`, 2 s; hook(pos) runs
// before the block starting at pos; every output sample goes to `see`.
template <class Hook, class See>
static void run(Engine& e, double fs, int block, Hook hook, See see, bool inPlace = false) {
    std::vector<double> ib((size_t)4 * block), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = inPlace ? in[c] : ob.data() + (size_t)c * block; }
    DelrmSignal sig(fs);
    const long total = (long)fs * 2;
    for (long pos = 0; pos < total; pos += block) {
        const int m = (int)std::min<long>(block, total - pos);
        hook(pos);
        sig.fill(in, m);
        e.process(in, out, m);
        for (int c = 0; c < 4; ++c) for (int k = 0; k < m; ++k) see(c, pos + k, out[c][k]);
    }
}

static refwin::Compare against(double fs, const double* ref, double changeAt = -1, double mt = 0) {
    Engine e; settle(e, fs);
    refwin::Compare cmp(4, 16, (long)fs / 8);
    run(e, fs, 256, [&](long pos) { if (pos == (long)changeAt) e.setDistance(mt); },
        [&](int c, long g, double y) { cmp.see(c, g, y, ref); });
    return cmp;
}

// ── Test 5 of the spec: the four channels against the spec ────────────────
TEST_CASE("the engine equals sdt.delrmcomb and sdt.delrmrm : sdt.delrmdyn at 96 kHz") {
    const auto c = against(96000.0, &delrmref::kWin96[0][0][0]);
    MESSAGE("engine 96k rel. error " << c.relErr());
    CHECK(c.compared == 4 * 16 * 512);
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kWin96_energy[0][0]) < 1e-10);
}

TEST_CASE("the engine equals the spec at 48 kHz") {
    const auto c = against(48000.0, &delrmref::kWin48[0][0][0]);
    MESSAGE("engine 48k rel. error " << c.relErr());
    CHECK(c.compared == 4 * 16 * 512);
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kWin48_energy[0][0]) < 1e-10);
}

// ── Test 6: a change of distance at the same block in both ────────────────
TEST_CASE("a change of distance at sample 96000 follows the spec through the jump") {
    const auto c = against(96000.0, &delrmref::kChange96[0][0][0], 96000, 10.0);
    MESSAGE("engine change rel. error " << c.relErr());
    CHECK(c.compared == 4 * 16 * 512);
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kChange96_energy[0][0]) < 1e-10);
}

// ── Test 4 and 7: the delay and the memory ─────────────────────────────────
TEST_CASE("D at the starting distance is 2113 samples at 96 kHz, 1061 at 48 kHz") {
    Engine e; settle(e, 96000.0);
    CHECK(e.delaySamples() == 2113);
    REQUIRE(e.prepare(48000.0));
    CHECK(e.delaySamples() == 1061);
}

TEST_CASE("every line holds the longest D the slider can ask, at every rate up to 384 kHz") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0}) {
        const Seam::PrimeSieve s(sieveBound(fs));
        const std::size_t len = lineLength(fs, s);
        long bad = 0, none = 0;
        for (int mm = 0; mm <= 30000; mm += 7) {
            const uint32_t d = delayFor(mm / 1000.0, fs, s);
            if (mm > 1 && d == 0) ++none;            // the sieve ran out
            if ((std::size_t)d + 1 > len) ++bad;
        }
        CAPTURE(fs);
        CHECK(none == 0);
        CHECK(bad == 0);
        CHECK(len == (std::size_t)delayFor(kMaxMetres, fs, s) + 1);
    }
}

TEST_CASE("distances off the range clamp to 0 and 30 m") {
    Engine e; settle(e, 96000.0);
    e.setDistance(-1.0);
    CHECK(e.delaySamples() == 0);
    e.setDistance(31.0);
    const Seam::PrimeSieve s(sieveBound(96000.0));
    CHECK(e.delaySamples() == delayFor(30.0, 96000.0, s));
}

TEST_CASE("at 0 m the comb doubles the input and the triple product is x^2 times the integral") {
    Engine e; settle(e, 96000.0);
    e.setDistance(0.0);
    REQUIRE(e.delaySamples() == 0);
    Seam::LeakyIntegrator li; li.prepare(96000.0, 1.0);
    Seam::CompressorMono cm; cm.prepare(96000.0, 11, -24, 0.03, 0.04);
    double a[64], b[64], y0[64], y1[64], y2[64], y3[64];
    for (int i = 0; i < 64; ++i) { a[i] = 0.1 * std::sin(i * 0.2); b[i] = 0.3 * std::cos(i * 0.07); }
    const double* in[4] = { a, b, a, b };
    double* out[4] = { y0, y1, y2, y3 };
    e.process(in, out, 64);
    double worstComb = 0.0, worstRm = 0.0;
    for (int i = 0; i < 64; ++i) {
        worstComb = std::max(worstComb, std::fabs(y0[i] - 2.0 * a[i]));
        const double p = b[i] * b[i] * (96000.0 * li.tick(b[i]));
        worstRm = std::max(worstRm, std::fabs(y1[i] - cm.tick(10.0 * p)));
    }
    CHECK(worstComb == 0.0);
    CHECK(worstRm == 0.0);     // the same operations in the same order
}

// ── Review Focus: in-place, block sizes, rate change, no memory, silence ───
static std::vector<double> render(double fs, int block, bool inPlace) {
    Engine e; settle(e, fs);
    std::vector<double> all((size_t)4 * (size_t)fs * 2);
    run(e, fs, block, [](long) {}, [&](int c, long g, double y) { all[(size_t)c * (size_t)fs * 2 + (size_t)g] = y; }, inPlace);
    return all;
}

TEST_CASE("in-place buffers give exactly the out-of-place output") {
    CHECK(render(96000.0, 256, true) == render(96000.0, 256, false));
}

TEST_CASE("1, 7 and 4093-sample blocks give exactly the 256-sample output") {
    const auto ref = render(48000.0, 256, false);
    for (int b : {1, 7, 4093}) { CAPTURE(b); CHECK(render(48000.0, b, false) == ref); }
}

TEST_CASE("prepare(48000) after a run at 96 kHz equals a fresh 48 kHz engine") {
    Engine e; settle(e, 96000.0);
    run(e, 96000.0, 256, [](long) {}, [](int, long, double) {});
    REQUIRE(e.prepare(48000.0));
    e.setOutput(1.0); e.setPower(true); e.reset();
    std::vector<double> got((size_t)4 * 96000);
    run(e, 48000.0, 256, [](long) {}, [&](int c, long g, double y) { got[(size_t)c * 96000 + (size_t)g] = y; });
    CHECK(got == render(48000.0, 256, false));
    const Seam::PrimeSieve s(sieveBound(48000.0));
    CHECK(e.lineLength() == lineLength(48000.0, s));
}

TEST_CASE("before prepare and after release, process writes zeros") {
    Engine e;
    double a[32], y[4][32];
    for (double& v : a) v = 0.5;
    const double* in[4] = { a, a, a, a }; double* out[4] = { y[0], y[1], y[2], y[3] };
    for (auto& ch : y) for (double& v : ch) v = 9.0;
    e.process(in, out, 32);
    for (auto& ch : y) for (double v : ch) CHECK(v == 0.0);
    settle(e, 96000.0);
    e.release();
    for (auto& ch : y) for (double& v : ch) v = 9.0;
    e.process(in, out, 32);
    for (auto& ch : y) for (double v : ch) CHECK(v == 0.0);
}

TEST_CASE("silence after the loud part: exact zeros, never NaN, GR back toward 0") {
    Engine e; settle(e, 96000.0);
    run(e, 96000.0, 256, [](long) {}, [](int, long, double) {});
    const double grLoud = e.reductionDb(0);
    std::vector<double> z(256, 0.0), y(4 * 256);
    const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
    double* out[4] = { y.data(), y.data() + 256, y.data() + 512, y.data() + 768 };
    bool finite = true;
    for (int b = 0; b < 96000 * 4 / 256; ++b) {
        e.process(in, out, 256);
        for (double v : y) finite &= std::isfinite(v);
    }
    CHECK(finite);
    for (double v : y) CHECK(v == 0.0);
    CHECK(grLoud > 10.0);
    CHECK(e.reductionDb(0) < 0.01 * grLoud);
}

// ── Test 8: the meters ──────────────────────────────────────────────────────
TEST_CASE("the block meters equal what the engine computed in that block") {
    Engine e; settle(e, 96000.0);
    Seam::LeakyIntegrator li; li.prepare(96000.0, 1.0);
    Seam::CompressorMono cm; cm.prepare(96000.0, 11, -24, 0.03, 0.04);
    Seam::IntegerDelay dl; std::vector<double> buf(e.lineLength(), 0.0); dl.attach(buf.data(), buf.size());
    dl.setDelay(e.delaySamples());
    DelrmSignal sig(96000.0);
    std::vector<double> ib(4 * 256), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    for (int b = 0; b < 600; ++b) {                    // 1.6 s: through three level steps
        sig.fill(in, 256);
        double peak[4] = {}, depth = 0.0;
        for (int k = 0; k < 256; ++k) {
            for (int c = 0; c < 4; ++c) peak[c] = std::max(peak[c], std::fabs(in[c][k]));
            const double x = in[1][k];
            const double p = dl.tick(x) * x * (96000.0 * li.tick(x));
            cm.tick(10.0 * p);
            depth = std::max(depth, -cm.gainDb());
        }
        e.process(in, out, 256);
        for (int c = 0; c < 4; ++c) REQUIRE(e.blockPeak(c) == peak[c]);
        REQUIRE(e.blockReductionDb(0) == depth);
        REQUIRE(e.inputPeak(0) >= e.blockPeak(0));    // held with release, never below the block
        REQUIRE(e.reductionDb(0) >= e.blockReductionDb(0));
    }
}

TEST_CASE("the held meters release with a 300 ms time constant") {
    Engine e; settle(e, 96000.0);
    double one[256], zero[256], y[4][256];
    std::fill(one, one + 256, 0.5); std::fill(zero, zero + 256, 0.0);
    const double* hi[4] = { one, one, one, one }; const double* lo[4] = { zero, zero, zero, zero };
    double* out[4] = { y[0], y[1], y[2], y[3] };
    e.process(hi, out, 256);
    CHECK(e.inputPeak(0) == 0.5);
    const int blocks = (int)std::lround(0.3 * 96000.0 / 256.0);   // 113 blocks = 0.3013 s
    for (int b = 0; b < blocks; ++b) e.process(lo, out, 256);
    const double want = 0.5 * std::exp(-blocks * 256.0 / (0.3 * 96000.0));
    CHECK(std::fabs(e.inputPeak(0) / want - 1.0) < 1e-9);
}
