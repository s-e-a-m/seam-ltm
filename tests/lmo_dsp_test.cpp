#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lmo_dsp.h"
#include "ref/lmo_ref.h"
#include <vector>
#include <cmath>
#include <algorithm>

using lmo::Engine;

// A settled engine: targets set, then reset() snaps every ramp.
static Engine settled(double fs, double f, double d) {
    Engine e;
    e.prepare(fs);
    e.setFrequency(f); e.setDelta(d); e.setVolume(1.0); e.setPower(true);
    e.reset();
    return e;
}

static std::vector<std::vector<double>> render(Engine& e, int n, int block = 512) {
    std::vector<std::vector<double>> y(4, std::vector<double>((size_t)n));
    for (int pos = 0; pos < n; pos += block) {
        const int m = std::min(block, n - pos);
        double* out[4] = { y[0].data() + pos, y[1].data() + pos, y[2].data() + pos, y[3].data() + pos };
        e.process(out, m);
    }
    return y;
}

template <int L>
static double relErr(const std::vector<std::vector<double>>& y, const double (*ref)[L]) {
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < L; ++k) {
            e  = std::max(e, std::fabs(y[(size_t)c][(size_t)k] - ref[c][k]));
            pk = std::max(pk, std::fabs(ref[c][k]));
        }
    return e / pk;
}

// The references skip 1 s (gen-ref.sh): a band at 97 Hz takes that long to
// form, and over its onset alone the comparison would weigh rounding
// against a signal that is not there yet (peak 3e-7 in the first 2048).
static void preroll(Engine& e, double fs) { render(e, (int)fs); }

TEST_CASE("equals sdt.lmo(4, 97.44, 20) at 96 kHz") {
    Engine e = settled(96000.0, 97.44, 20.0);
    preroll(e, 96000.0);
    auto y = render(e, 2048);
    CHECK(relErr<2048>(y, lmoref::kLmo96) < 1e-12);
}

TEST_CASE("equals sdt.lmo(4, 97.44, 20) at 48 kHz, density included") {
    Engine e = settled(48000.0, 97.44, 20.0);
    preroll(e, 48000.0);
    auto y = render(e, 1024);
    CHECK(relErr<1024>(y, lmoref::kLmo48) < 1e-12);
}

TEST_CASE("density holds the band's noise energy at its 96 kHz value") {
    // Deterministic: output variance of white noise through H is
    // sigma^2 * sum(h^2). sum(h^2) of a fixed analog band scales as 1/fs;
    // density^2 = fs/96000 cancels it. What remains is the bilinear
    // transform: it is prewarped at fc only, so the band's skirts are
    // compressed a little more as fc/fs grows (measured: 0.0001 dB at
    // 97.44 Hz, 0.0117 dB at 1 kHz and 44.1 kHz). The spec asks 0.1 dB.
    auto bandEnergy = [](double fc, double fs) {
        Seam::ButterworthSVF<24> hp, lp;
        hp.setType(Seam::ButterworthType::Highpass); lp.setType(Seam::ButterworthType::Lowpass);
        hp.setFrequency(fc, fs); lp.setFrequency(fc - lmo::kLpOffset, fs);
        double e = 0.0;
        for (int k = 0; k < (int)(4 * fs); ++k) {
            const double h = lp.tick(hp.tick(k == 0 ? 1.0 : 0.0));
            e += h * h;
        }
        const double g = lmo::densityGain(fs);
        return 10.0 * std::log10(e * g * g);
    };
    for (double fs : {44100.0, 48000.0, 192000.0}) {
        CHECK(std::fabs(bandEnergy(97.44, fs) - bandEnergy(97.44, 96000.0)) < 0.001);
        CHECK(std::fabs(bandEnergy(1000.0, fs) - bandEnergy(1000.0, 96000.0)) < 0.05);
    }
}

TEST_CASE("output level at 48 kHz matches 96 kHz (statistical)") {
    auto levelDb = [](double fs) {
        Engine e = settled(fs, 1000.0, 0.0);
        const int n = (int)(fs * 20.0);
        double s = 0.0;
        double buf[4][512];
        double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
        for (int pos = 0; pos < n; pos += 512) {
            e.process(out, 512);
            for (int c = 0; c < 4; ++c) for (int k = 0; k < 512; ++k) s += buf[c][k] * buf[c][k];
        }
        return 10.0 * std::log10(s / (4.0 * n));
    };
    // 4 channels x 20 s x ~73 Hz bandwidth: ~1.3 % power deviation per run (1 sigma)
    CHECK(std::fabs(levelDb(48000.0) - levelDb(96000.0)) < 0.25);
}

TEST_CASE("glide: 97.44 -> 112.67 Hz in 120 s, monotonic, no jumps") {
    const double fs = 48000.0;
    Engine e = settled(fs, 97.44, 0.0);
    e.setGlide(120.0);
    e.setFrequency(112.67);
    const int block = 480;
    double buf[4][480]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    double prev = e.currentFrequency(), maxStep = 0.0;
    bool monotonic = true;
    const long total = (long)(121.0 * fs);
    long reachedAt = -1;
    for (long pos = 0; pos < total; pos += block) {
        e.process(out, block);
        const double f = e.currentFrequency();
        if (f < prev) monotonic = false;
        maxStep = std::max(maxStep, f - prev);
        prev = f;
        if (reachedAt < 0 && f == 112.67) reachedAt = pos + block;
    }
    CHECK(monotonic);
    CHECK(reachedAt >= (long)(120.0 * fs));
    CHECK(reachedAt <= (long)(120.0 * fs) + block);
    // per block of 480 samples the ramp moves (112.67-97.44)/(120*fs)*480
    CHECK(maxStep < 1.01 * (112.67 - 97.44) / (120.0 * fs) * block);
}

TEST_CASE("same target does not restart the glide") {
    const double fs = 48000.0;
    Engine e = settled(fs, 97.44, 0.0);
    e.setGlide(120.0);
    e.setFrequency(112.67);
    double buf[4][480]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    for (int b = 0; b < 1000; ++b) { e.setFrequency(112.67); e.process(out, 480); }
    // 480000 samples = 10 s of the 120 s ramp
    const double expected = 97.44 + (112.67 - 97.44) * 10.0 / 120.0;
    CHECK(std::fabs(e.currentFrequency() - expected) < 1e-6);
}

TEST_CASE("glide 0 uses the 25 ms ramp") {
    const double fs = 96000.0;
    Engine e = settled(fs, 100.0, 0.0);
    e.setFrequency(200.0);
    double buf[4][2400]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 2399);
    CHECK(e.currentFrequency() < 200.0);
    e.process(out, 1);
    CHECK(e.currentFrequency() == 200.0);
}

TEST_CASE("volume ramp: 25 ms, measured against the full-volume output") {
    for (double fs : {48000.0, 96000.0}) {
        Engine full = settled(fs, 97.44, 0.0);
        Engine ramp; ramp.prepare(fs);
        ramp.setFrequency(97.44); ramp.setPower(true); ramp.setVolume(0.0);
        ramp.reset();
        ramp.setVolume(1.0);
        const int n = (int)(0.05 * fs);
        auto a = render(full, n), b = render(ramp, n);
        const long rampLen = std::lround(0.025 * fs);
        // gain at sample k (the (k+1)-th next() call) is (k+1)/rampLen
        for (long k : {rampLen / 2, rampLen - 2, rampLen - 1, rampLen + 10}) {
            const double g = (k + 1 >= rampLen) ? 1.0 : (double)(k + 1) / rampLen;
            CHECK(std::fabs(b[0][(size_t)k] - g * a[0][(size_t)k]) < 1e-12);
        }
    }
}

TEST_CASE("power reverses from the current gain") {
    const double fs = 48000.0;
    Engine e; e.prepare(fs);
    e.setFrequency(97.44); e.setVolume(1.0); e.setPower(false);
    e.reset();
    e.setPower(true);
    double buf[4][600]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 600);       // half of the 1200-sample ramp
    e.setPower(false);
    Engine ref = settled(fs, 97.44, 0.0);
    double rbuf[4][600]; double* rout[4] = { rbuf[0], rbuf[1], rbuf[2], rbuf[3] };
    ref.process(rout, 600);
    e.process(out, 1);
    ref.process(rout, 1);
    // gain was 0.5 at sample 599; first sample after the reversal: 0.5 - 0.5/1200
    const double g = 0.5 - 0.5 / 1200.0;
    CHECK(std::fabs(buf[0][0] - g * rbuf[0][0]) < 1e-12);
}

TEST_CASE("output independent of block partition") {
    Engine a = settled(96000.0, 97.44, 20.0), b = settled(96000.0, 97.44, 20.0);
    a.setGlide(0.5); a.setFrequency(150.0);
    b.setGlide(0.5); b.setFrequency(150.0);
    auto ya = render(a, 20000, 512);
    std::vector<std::vector<double>> yb(4, std::vector<double>(20000));
    const int sizes[] = { 1, 7, 13, 16, 17, 100, 255 };
    int pos = 0, i = 0;
    while (pos < 20000) {
        const int m = std::min(sizes[i++ % 7], 20000 - pos);
        double* out[4] = { yb[0].data() + pos, yb[1].data() + pos, yb[2].data() + pos, yb[3].data() + pos };
        b.process(out, m);
        pos += m;
    }
    for (int c = 0; c < 4; ++c) CHECK(ya[(size_t)c] == yb[(size_t)c]);
}

TEST_CASE("extreme f and delta stay finite") {
    Engine e = settled(44100.0, 20.0, 50.0);
    auto y = render(e, 44100);
    bool finite = true; double pk = 0.0;
    for (auto& ch : y) for (double v : ch) { finite = finite && std::isfinite(v); pk = std::max(pk, std::fabs(v)); }
    CHECK(finite);
    CHECK(pk < 1.0);
}

TEST_CASE("re-prepare at a new rate") {
    Engine e = settled(48000.0, 97.44, 0.0);
    e.prepare(96000.0);
    e.setFrequency(200.0);   // glide 0: 25 ms at 96 kHz = 2400 samples
    double buf[4][2400]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
    e.process(out, 2399);
    CHECK(e.currentFrequency() < 200.0);
    e.process(out, 1);
    CHECK(e.currentFrequency() == 200.0);
    CHECK(lmo::updatePeriod(96000.0) == 16);
    CHECK(lmo::updatePeriod(48000.0) == 8);
}

TEST_CASE("float buffers carry the same signal") {
    Engine a = settled(96000.0, 97.44, 20.0), b = settled(96000.0, 97.44, 20.0);
    auto yd = render(a, 1024);
    std::vector<std::vector<float>> yf(4, std::vector<float>(1024));
    float* out[4] = { yf[0].data(), yf[1].data(), yf[2].data(), yf[3].data() };
    b.process(out, 1024);
    for (int k = 0; k < 1024; ++k) CHECK(yf[0][(size_t)k] == (float)yd[0][(size_t)k]);
}
