#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_dsp.h"
#include "ref/choir_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using choir::Engine;
using choir::Config;
using Buf = std::vector<std::vector<double>>;

// The inputs of choir.dsp: channel c, 16 sines at f_c*(k+1), 0.05 each.
struct Source {
    Config cfg; double fs; long n = 0;
    double sample(int c, long t) const {
        double x = 0.0;
        for (int k = 0; k < 16; ++k) x += 0.05 * std::sin(2 * M_PI * cfg.f[(size_t)c] * (k + 1) * (double)t / fs);
        return x;
    }
    void fill(Buf& in, int m) {
        for (int c = 0; c < 4; ++c) for (int i = 0; i < m; ++i) in[(size_t)c][(size_t)i] = sample(c, n + i);
        n += m;
    }
};

// Engine holds atomics: neither copyable nor movable, so it is built in
// place and settled by reference.
static void settle(Engine& e, double fs) {
    e.prepare(fs);
    e.setOutput(1.0); e.setPower(true);
    e.reset();
}

// Runs n samples of src through e in blocks; returns the last `keep` samples.
template <class T = double>
static Buf run(Engine& e, Source& src, long n, int keep, int block = 512) {
    Buf in(4, std::vector<double>((size_t)block));
    std::vector<std::vector<T>> ti(4, std::vector<T>((size_t)block)), to(4, std::vector<T>((size_t)block));
    Buf kept(4);
    for (long pos = 0; pos < n; pos += block) {
        const int m = (int)std::min<long>(block, n - pos);
        src.fill(in, m);
        const T* ip[4]; T* op[4];
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < m; ++i) ti[(size_t)c][(size_t)i] = (T)in[(size_t)c][(size_t)i];
            ip[c] = ti[(size_t)c].data(); op[c] = to[(size_t)c].data();
        }
        e.process(ip, op, m);
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i < m; ++i)
                if (pos + i >= n - keep) kept[(size_t)c].push_back((double)to[(size_t)c][(size_t)i]);
    }
    return kept;
}

// Zero input for n samples, in blocks of 512; returns the largest |output|.
static double runSilence(Engine& e, long n) {
    std::vector<double> z(512, 0.0);
    Buf out(4, std::vector<double>(512, 0.0));
    const double* ip[4] = { z.data(), z.data(), z.data(), z.data() };
    double* op[4];
    for (int c = 0; c < 4; ++c) op[c] = out[(size_t)c].data();
    double mx = 0.0;
    for (long pos = 0; pos < n; pos += 512) {
        e.process(ip, op, 512);
        for (auto& ch : out) for (double v : ch) mx = std::max(mx, std::fabs(v));
    }
    return mx;
}

template <int L>
static double relErr(const Buf& y, const double (*ref)[L]) {
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < L; ++k) {
            e  = std::max(e, std::fabs(y[(size_t)c][(size_t)k] - ref[c][k]));
            pk = std::max(pk, std::fabs(ref[c][k]));
        }
    return e / pk;
}

TEST_CASE("equals sdt.choir(350, 1.5) at 96 kHz") {
    Engine e; settle(e, 96000.0);
    Source s{Config(), 96000.0};
    auto y = run(e, s, 288000 + 2048, 2048);
    CHECK(relErr<2048>(y, choirref::kChoir96) < 1e-12);
}

TEST_CASE("equals sdt.choir(350, 1.5) at 48 kHz, choirdens included") {
    Engine e; settle(e, 48000.0);
    Source s{Config(), 48000.0};
    auto y = run(e, s, 144000 + 1024, 1024);
    CHECK(relErr<1024>(y, choirref::kChoir48) < 1e-12);
}

TEST_CASE("the block size does not change the output") {
    Engine a, b, c; settle(a, 96000.0); settle(b, 96000.0); settle(c, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0}, sc{Config(), 96000.0};
    auto ya = run(a, sa, 20000, 4096, 512);
    auto yb = run(b, sb, 20000, 4096, 1);
    auto yc = run(c, sc, 20000, 4096, 4096);
    for (int ch = 0; ch < 4; ++ch) {
        CHECK(ya[(size_t)ch] == yb[(size_t)ch]);
        CHECK(ya[(size_t)ch] == yc[(size_t)ch]);
    }
}

TEST_CASE("a float bus equals a double bus to float precision") {
    Engine a, b; settle(a, 96000.0); settle(b, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0};
    auto yd = run<double>(a, sa, 96000, 2048);
    auto yf = run<float>(b, sb, 96000, 2048);
    double e = 0.0, pk = 0.0;
    for (int c = 0; c < 4; ++c)
        for (size_t k = 0; k < yd[(size_t)c].size(); ++k) {
            e = std::max(e, std::fabs(yd[(size_t)c][k] - yf[(size_t)c][k]));
            pk = std::max(pk, std::fabs(yd[(size_t)c][k]));
        }
    CHECK(e / pk < 1e-5);
}

TEST_CASE("an unprepared engine writes silence") {
    Engine e;
    std::vector<double> z(64, 1.0), o(64, 1.0);
    const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
    double* out[4] = { o.data(), o.data(), o.data(), o.data() };
    e.process(in, out, 64);
    for (double v : o) CHECK(v == 0.0);
}

TEST_CASE("RESET silences the ringing bands; the request survives a stopped host") {
    Engine e; settle(e, 96000.0);
    Source s{Config(), 96000.0};
    run(e, s, 192000, 1);
    CHECK(runSilence(e, 512) > 0.0);      // with zero input the choir still sings
    e.requestReset();                      // the host is stopped: no process here
    CHECK(runSilence(e, 20 * 512) == 0.0);
}

TEST_CASE("RESET does not rewind the noise") {
    // After a reset the bands start from zero but the noise goes on: the
    // engine must differ from a fresh one fed the same signal, whose noise
    // starts at the generator's beginning.
    Engine a, b; settle(a, 96000.0); settle(b, 96000.0);
    Source s0{Config(), 96000.0};
    run(a, s0, 48000, 1);
    a.requestReset();
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0};
    auto ya = run(a, sa, 96000, 2048), yb = run(b, sb, 96000, 2048);
    double d = 0.0;
    for (int c = 0; c < 4; ++c)
        for (size_t k = 0; k < ya[(size_t)c].size(); ++k) d = std::max(d, std::fabs(ya[(size_t)c][k] - yb[(size_t)c][k]));
    CHECK(d > 0.0);
}

TEST_CASE("bands at or above 20 kHz are inactive, and the engine stays finite") {
    Config cfg; cfg.f = {{5000, 5000, 5000, 5000}}; cfg.a = {{1, 1, 1, 1}};
    Engine e(cfg); settle(e, 48000.0);
    for (int k = 0; k < 16; ++k) CHECK(e.active(0, k) == (5000.0 * (k + 1) < 20000.0));
    Source s{cfg, 48000.0};
    auto y = run(e, s, 20 * 48000, 4096);
    for (auto& ch : y) for (double v : ch) REQUIRE(std::isfinite(v));
    CHECK(e.display().load(0, 15) == -1.0f);
}

TEST_CASE("sixty seconds of silence leave no subnormal state") {
    Config fast; fast.q = 5.0; fast.release = 0.01;     // decays to the subnormal range within the run
    Engine e(fast); settle(e, 8000.0);
    Source s{fast, 8000.0};
    run(e, s, 8000, 1);
    runSilence(e, 60 * 8000);
    CHECK_FALSE(e.hasSubnormalState());
}

TEST_CASE("output and POWER reach their targets in 25 ms at every rate") {
    for (double fs : {48000.0, 96000.0}) {
        Engine e; e.prepare(fs); e.reset();               // gain 0
        e.setOutput(1.0); e.setPower(true);
        const int n = (int)std::lround(0.025 * fs);
        std::vector<double> z((size_t)n, 0.0), o((size_t)n, 0.0);
        const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
        double* out[4] = { o.data(), o.data(), o.data(), o.data() };
        e.process(in, out, n - 1);
        CHECK(e.gain() < 1.0);
        e.process(in, out, 1);
        CHECK(e.gain() == 1.0);
    }
}

TEST_CASE("the display reads a partial's amplitude in its band") {
    Engine e; settle(e, 96000.0);
    // channel 0 hears one sine of amplitude 0.1 at 3*48 Hz = band k = 2
    Buf in(4, std::vector<double>(512, 0.0)), out(4, std::vector<double>(512));
    const double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = in[(size_t)c].data(); op[c] = out[(size_t)c].data(); }
    long n = 0;
    for (int b = 0; b < 12 * 96000 / 512; ++b) {
        for (int i = 0; i < 512; ++i) in[0][(size_t)i] = 0.1 * std::sin(2 * M_PI * 144.0 * (double)(n + i) / 96000.0);
        n += 512;
        e.process(ip, op, 512);
    }
    CHECK(20 * std::log10(e.display().load(0, 2)) == doctest::Approx(-20.0).epsilon(0.005));
    for (int k = 0; k < 16; ++k)
        if (k != 2) CHECK(20 * std::log10(std::max(1e-12f, e.display().load(0, k))) < -40.0);
}

TEST_CASE("a new rate redesigns everything: equal to a fresh engine") {
    Engine a; settle(a, 48000.0);
    Source s48{Config(), 48000.0};
    run(a, s48, 48000, 1);
    a.prepare(96000.0); a.setOutput(1.0); a.setPower(true); a.reset();
    Engine b; settle(b, 96000.0);
    Source sa{Config(), 96000.0}, sb{Config(), 96000.0};
    auto ya = run(a, sa, 96000, 1024), yb = run(b, sb, 96000, 1024);
    for (int c = 0; c < 4; ++c) CHECK(ya[(size_t)c] == yb[(size_t)c]);
}
