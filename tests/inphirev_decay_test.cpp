#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "inphirev_decay.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace inphirev;

TEST_CASE("single section: the closed form") {
    const double fs = 48000.0, g2 = kG * kG;
    const uint32_t d = 48000;                       // t = 1 s
    // energy after m echoes: (1 - g^2) g^(2m)
    auto closed = [&](double level) {
        int m = 0;
        while ((1.0 - g2) * std::pow(g2, m) > level) ++m;
        return (double)m;                           // seconds
    };
    const Decay e = estimateDecay(&d, 1, fs);
    CHECK(std::fabs(e.t30 - closed(1e-3)) <= kBin + 1e-9);
    CHECK(std::fabs(e.t60 - closed(1e-6)) <= kBin + 1e-9);
}

TEST_CASE("design figures: (1, 10, 17) at 96 kHz") {
    const double fs = 96000.0;
    const Seam::PrimeSieve s(sieveBound(fs));
    const double want30[4] = { 201.3, 0, 0, 288.6 }, want60[4] = { 355.0, 0, 0, 509.3 };
    for (int k : { 0, 3 }) {
        uint32_t d[kMaxSections];
        for (int i = 0; i < 17; ++i) d[i] = sectionDelay(gridMetres(1, 10), i, k, fs, s);
        const Decay e = estimateDecay(d, 17, fs);
        INFO("face " << k << " t30 " << e.t30 << " t60 " << e.t60);
        CHECK(std::fabs(e.t30 - want30[k]) / want30[k] < 0.02);
        CHECK(std::fabs(e.t60 - want60[k]) / want60[k] < 0.02);
    }
}

TEST_CASE("beyond the horizon: +inf, not the clipped grid") {
    const double fs = 48000.0;
    const uint32_t d500 = (uint32_t)(500.0 * fs), d300 = (uint32_t)(300.0 * fs);
    const Decay a = estimateDecay(&d500, 1, fs);
    CHECK(std::isinf(a.t30));
    CHECK(std::isinf(a.t60));
    const Decay b = estimateDecay(&d300, 1, fs);   // t30 at 2100 s, t60 at 4200 s
    CHECK(std::isfinite(b.t30));
    CHECK(std::fabs(b.t30 - 2100.0) <= kBin + 1e-9);
    CHECK(std::isinf(b.t60));
}

TEST_CASE("estimate vs engine") {
    const double fs = 48000.0;
    Engine e;
    e.setSettings(1, 10, 12);
    REQUIRE(e.prepare(fs));
    e.setPower(true);
    e.reset();
    const Seam::PrimeSieve s(sieveBound(fs));
    Decay est[4];
    for (int k = 0; k < 4; ++k) {
        uint32_t d[kMaxSections];
        for (int i = 0; i < 12; ++i) d[i] = e.delay(k, i);
        est[k] = estimateDecay(d, 12, fs);
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
    // Zeros: block energies per face; the curve of what is left after t.
    for (int k = 0; k < 4; ++k) std::fill(inb[k].begin(), inb[k].end(), 0.0);
    std::vector<double> blk[4];
    std::vector<double> tail[4];
    const long maxBlocks = (long)(4000.0 * fs / B);
    bool done = false;
    // measured: after the input closes, find the time the remaining energy
    // (sum of block energies from then on) is 1e-3 of the total; the total
    // is only known once the tail has sunk, so run until the running level
    // is far below -30 dB of the energy so far, and until -60 dB for the pair.
    double total[4] = {0, 0, 0, 0};
    for (long b = 0; b < maxBlocks && !done; ++b) {
        e.process(inp, outp, B);
        for (int k = 0; k < 4; ++k) {
            double en = 0.0;
            for (int n = 0; n < B; ++n) en += outb[k][n] * outb[k][n];
            blk[k].push_back(en);
            total[k] += en;
        }
        // stop when every face's last 10 s hold < 1e-5 of its total and -30 dB is passed
        if (b % 4096 == 4095) {
            done = true;
            for (int k = 0; k < 4; ++k) {
                double last = 0.0;
                const long span = (long)(10.0 * fs / B);
                for (long q = (long)blk[k].size() - span; q < (long)blk[k].size(); ++q) last += blk[k][(size_t)q];
                if (!(last < 1e-6 * total[k])) done = false;
            }
        }
    }
    REQUIRE(done);
    for (int k = 0; k < 4; ++k) {
        double rest = total[k], t30 = -1;
        for (size_t q = 0; q < blk[k].size(); ++q) {
            rest -= blk[k][q];
            if (rest <= 1e-3 * total[k]) { t30 = (q + 1) * (double)B / fs; break; }
        }
        REQUIRE(t30 > 0);
        const double err = std::fabs(est[k].t30 - t30) / t30;
        std::printf("face %d: estimated t30 %.1f s (t60 %.1f), measured t30 %.1f s, error %.1f %%\n",
                    k, est[k].t30, est[k].t60, t30, 100.0 * err);
        INFO("face " << k << " estimate " << est[k].t30 << " measured " << t30);
        CHECK(err < 0.15);
    }
}

TEST_CASE("runtime, N = 18 at 6 m") {
    const double fs = 96000.0;
    const Seam::PrimeSieve s(sieveBound(fs));
    uint32_t d[kMaxSections];
    for (int i = 0; i < 18; ++i) d[i] = sectionDelay(6.0, i, 3, fs, s);
    const auto t0 = std::chrono::steady_clock::now();
    const Decay e = estimateDecay(d, 18, fs);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("estimateDecay N=18 6 m: %.1f ms (t30 %g)\n", ms, e.t30);
    CHECK(ms < 500.0);
}
