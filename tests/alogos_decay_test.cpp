#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "alogos_decay.h"
#include "alogos_dsp.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace alogos;

static const double kInvPhi = 2.0 / (1.0 + std::sqrt(5.0));

TEST_CASE("single section: the closed form, every g of the menu") {
    const double fs = 48000.0;
    const uint32_t d = 48000;                       // t = 1 s
    for (int gi = 0; gi < 5; ++gi) {
        const double g = gainValue((Gain)gi), g2 = g * g;
        // energy after m echoes: (1 - g^2) g^(2m)
        auto closed = [&](double level) {
            int m = 0;
            while ((1.0 - g2) * std::pow(g2, m) > level) ++m;
            return (double)m;                       // seconds
        };
        const Decay e = estimateDecay(&d, 1, fs, g);
        INFO("g menu index " << gi << " g " << g);
        CHECK(std::fabs(e.t30 - closed(1e-3)) <= kBin + 1e-9);
        CHECK(std::fabs(e.t60 - closed(1e-6)) <= kBin + 1e-9);
    }
}

TEST_CASE("design figures: (1, 10, 17) at 96 kHz") {
    const double fs = 96000.0;
    const Seam::PrimeSieve s(sieveBound(fs));
    const double want30[4] = { 201.3, 0, 0, 288.6 }, want60[4] = { 355.0, 0, 0, 509.3 };
    for (int k : { 0, 3 }) {
        uint32_t d[kMaxSections];
        for (int i = 0; i < 17; ++i) d[i] = sectionDelay(Scale::Phi, 1, gridMetres(Scale::Phi, 1, 1, 10), i, k, fs, s);
        const Decay e = estimateDecay(d, 17, fs, kInvPhi);
        INFO("face " << k << " t30 " << e.t30 << " t60 " << e.t60);
        CHECK(std::fabs(e.t30 - want30[k]) / want30[k] < 0.02);
        CHECK(std::fabs(e.t60 - want60[k]) / want60[k] < 0.02);
    }
}

TEST_CASE("beyond the horizon: +inf, not the clipped grid") {
    const double fs = 48000.0;
    const uint32_t d500 = (uint32_t)(500.0 * fs), d300 = (uint32_t)(300.0 * fs);
    const Decay a = estimateDecay(&d500, 1, fs, kInvPhi);
    CHECK(std::isinf(a.t30));
    CHECK(std::isinf(a.t60));
    const Decay b = estimateDecay(&d300, 1, fs, kInvPhi);   // t30 at 2100 s, t60 at 4200 s
    CHECK(std::isfinite(b.t30));
    CHECK(std::fabs(b.t30 - 2100.0) <= kBin + 1e-9);
    CHECK(std::isinf(b.t60));
}

// One engine render per setting; the estimate gets the g of the same setting.
static void estimateVsEngine(const char* name, Scale sc, Gain gain, int N, double metres) {
    const double fs = 48000.0, g = gainValue(gain);
    Engine e;
    e.setSettings(1, metres, N, (int)sc, 0, (int)gain);   // stone 1, density 1
    REQUIRE(e.prepare(fs));
    e.setPower(true);
    e.reset();
    REQUIRE(e.sections() == N);
    Decay est[4];
    for (int k = 0; k < 4; ++k) {
        uint32_t d[kMaxSections];
        for (int i = 0; i < N; ++i) d[i] = e.delay(k, i);
        est[k] = estimateDecay(d, N, fs, g);
        REQUIRE(std::isfinite(est[k].t30));
    }
    const double noiseSec = std::max(60.0, 1.5 * est[3].t60);
    const int B = 512;
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::vector<double> inb[4], outb[4];
    const double* inp[4]; double* outp[4];
    for (int k = 0; k < 4; ++k) { inb[k].assign(B, 0.0); outb[k].assign(B, 0.0); inp[k] = inb[k].data(); outp[k] = outb[k].data(); }
    const long noiseBlocks = (long)(noiseSec * fs / B);
    for (long b = 0; b < noiseBlocks; ++b) {
        for (int k = 0; k < 4; ++k) for (int n = 0; n < B; ++n) inb[k][n] = u(rng);
        e.process(inp, outp, B);
    }
    // Zeros. The estimate is a level: the power of a stationary input's
    // output t seconds after it closes, relative to the power before (the
    // energy of the echoes still to come). Measured as the block energy
    // averaged over a 0.5 s window against the steady power of the input,
    // the variance of u(-1,1), 1/3 (the cascade is all-pass, so the output
    // has the same power). Integrating the tail instead would measure the
    // second moment of the echoes and read about 15 % early.
    for (int k = 0; k < 4; ++k) std::fill(inb[k].begin(), inb[k].end(), 0.0);
    std::vector<double> blk[4];
    const long maxBlocks = (long)(4000.0 * fs / B);
    const long W = (long)std::lround(0.5 * fs / B);          // window, blocks
    const double steady = 1.0 / 3.0 * B;                      // per block
    bool done = false;
    for (long b = 0; b < maxBlocks && !done; ++b) {
        e.process(inp, outp, B);
        for (int k = 0; k < 4; ++k) {
            double en = 0.0;
            for (int n = 0; n < B; ++n) en += outb[k][n] * outb[k][n];
            blk[k].push_back(en);
        }
        // stop when every face's last 10 s sit below -40 dB
        if (b % 1024 == 1023) {
            done = true;
            for (int k = 0; k < 4; ++k) {
                double last = 0.0;
                const long span = (long)(10.0 * fs / B);
                for (long q = (long)blk[k].size() - span; q < (long)blk[k].size(); ++q) last += blk[k][(size_t)q];
                if (!(last / span < 1e-4 * steady)) done = false;
            }
        }
    }
    REQUIRE(done);
    for (int k = 0; k < 4; ++k) {
        double t30 = -1;
        double acc = 0.0;
        for (long q = 0; q + W <= (long)blk[k].size(); ++q) {
            if (q == 0) { for (long j = 0; j < W; ++j) acc += blk[k][(size_t)j]; }
            else acc += blk[k][(size_t)(q + W - 1)] - blk[k][(size_t)(q - 1)];
            if (acc / W <= 1e-3 * steady) { t30 = (q + 0.5 * W) * (double)B / fs; break; }
        }
        REQUIRE(t30 > 0);
        const double err = std::fabs(est[k].t30 - t30) / t30;
        std::printf("%s N=%d face %d: estimated t30 %.1f s (t60 %.1f), measured t30 %.1f s, error %.1f %%\n",
                    name, N, k, est[k].t30, est[k].t60, t30, 100.0 * err);
        INFO("face " << k << " estimate " << est[k].t30 << " measured " << t30);
        CHECK(err < 0.05);
    }
}

// phi, 3.330 m, N = 12 as INPHIREV did. For pi the delays grow by 3.14 per
// section: N = 8 at 1.0 m (t30 of 35 to 84 s over the four faces) keeps the
// decay to a few tens of seconds, and so the render short.
TEST_CASE("estimate vs engine, phi, g = 1/phi") { estimateVsEngine("phi", Scale::Phi, Gain::InvPhi, 12, 3.33); }
TEST_CASE("estimate vs engine, pi, g = 1/pi")   { estimateVsEngine("pi", Scale::Pi, Gain::InvPi, 8, 1.0); }

TEST_CASE("runtime, N = 18 at 6 m") {
    const double fs = 96000.0;
    const Seam::PrimeSieve s(sieveBound(fs));
    uint32_t d[kMaxSections];
    for (int i = 0; i < 18; ++i) d[i] = sectionDelay(Scale::Phi, 1, 6.0, i, 3, fs, s);
    const auto t0 = std::chrono::steady_clock::now();
    const Decay e = estimateDecay(d, 18, fs, kInvPhi);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("estimateDecay N=18 6 m: %.1f ms (t30 %g)\n", ms, e.t30);
    CHECK(ms < 500.0);
}
