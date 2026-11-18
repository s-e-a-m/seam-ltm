#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "bastooge_dsp.h"
#include "bamodulex_dsp.h"
#include "seam_svf.h"
#include "seam_st.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using Seam::bastooge::Engine;
using Seam::bastooge::Test;

namespace {

struct Lcg {
    uint32_t s = 24680u;
    double next() {
        s = s * 1664525u + 1013904223u;
        return (double)(s >> 8) / (double)(1u << 24) * 2.0 - 1.0;
    }
};

using Frames = std::vector<std::vector<double>>; // [4][n]

Frames run(Engine& e, const Frames& in) {
    const int n = (int)in[0].size();
    Frames out(4, std::vector<double>(n, 0.0));
    const double* ip[4] = {in[0].data(), in[1].data(), in[2].data(), in[3].data()};
    double* op[4] = {out[0].data(), out[1].data(), out[2].data(), out[3].data()};
    e.process<double>(ip, op, n);
    return out;
}

Frames noise(int n, uint32_t seed = 24680u) {
    Lcg r;
    r.s = seed;
    Frames f(4, std::vector<double>(n));
    for (auto& c : f) for (auto& v : c) v = r.next();
    return f;
}

double rms(const std::vector<double>& v, int from = 0) {
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return std::sqrt(s / (double)(v.size() - from));
}

} // namespace

TEST_CASE("bypass + Normal equals bamodulex sample for sample") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Engine e;
        e.prepare(fs);
        e.setBypass(true);
        e.setTest(Test::Normal);
        const Frames in = noise(4096);
        const Frames out = run(e, in);
        for (int i = 0; i < 4096; ++i) {
            double ref[4];
            Seam::bamodulex::decode(in[0][i], in[1][i], in[2][i], in[3][i], ref);
            for (int c = 0; c < 4; ++c) REQUIRE(out[c][i] == ref[c]);
        }
    }
}

TEST_CASE("test modes: signs and per-face amplitude") {
    // expected (LFU, RFD, RBU, LBD) for W, X, Y, Z
    const double exp[4][4] = {{1, 1, 1, 1}, {1, 1, -1, -1}, {1, -1, -1, 1}, {1, -1, 1, -1}};
    const Test modes[4] = {Test::W, Test::X, Test::Y, Test::Z};
    for (int m = 0; m < 4; ++m) {
        Engine e;
        e.prepare(48000.0);
        e.setBypass(true);
        e.setTest(modes[m]);
        Frames in = noise(256);
        std::fill(in[0].begin(), in[0].end(), 1.0);
        const Frames out = run(e, in);
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 4; ++c) CHECK(out[c][i] == doctest::Approx(exp[m][c]).epsilon(1e-12));
    }
}

TEST_CASE("test modes go through the filters") {
    Engine e;
    e.prepare(48000.0);
    e.setBypass(false);
    e.setTest(Test::X);
    const int n = 48000;
    Frames in(4, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) in[0][i] = std::sin(2.0 * M_PI * 30.0 * i / 48000.0);
    const Frames out = run(e, in);
    const double rin = rms(in[0], 12000);
    for (int c = 0; c < 4; ++c) {
        // the matrix scales by 2 * 1/2: unity at the dipole, so compare with the input RMS
        CHECK(20.0 * std::log10(rms(out[c], 12000) / rin) < -40.0);
    }
}

TEST_CASE("wiring: each chain is the specified cascade") {
    const double fs = 48000.0, fc = 137.0;
    const int n = 4096;
    const Frames in = noise(n, 13579u);
    {
        Engine e;
        e.setCut(fc);
        e.prepare(fs);
        e.setBypass(false);
        e.setTest(Test::W);
        const Frames out = run(e, in);
        const auto b = Seam::st::mk2gBell(fc);
        Seam::Svf bell, ap;
        bell.design(fs, Seam::Svf::Type::Bell, b.f0, b.q, b.gainDb);
        ap.design(fs, Seam::Svf::Type::Allpass, fc, std::sqrt(0.5));
        for (int i = 0; i < n; ++i) {
            const double w = ap.tick(bell.tick(2.0 * in[0][i]));
            double ref[4];
            Seam::bamodulex::decode(w, 0.0, 0.0, 0.0, ref);
            for (int c = 0; c < 4; ++c) REQUIRE(std::fabs(out[c][i] - ref[c]) < 1e-12);
        }
    }
    {
        Engine e;
        e.setCut(fc);
        e.prepare(fs);
        e.setBypass(false);
        e.setTest(Test::X);
        const Frames out = run(e, in);
        Seam::Svf h1, h2;
        h1.design(fs, Seam::Svf::Type::Highpass, fc, std::sqrt(0.5));
        h2.design(fs, Seam::Svf::Type::Highpass, fc, std::sqrt(0.5));
        for (int i = 0; i < n; ++i) {
            const double x = h2.tick(h1.tick(2.0 * in[0][i]));
            double ref[4];
            Seam::bamodulex::decode(0.0, 0.0, 0.0, x, ref); // test X: ACN 3
            for (int c = 0; c < 4; ++c) REQUIRE(std::fabs(out[c][i] - ref[c]) < 1e-12);
        }
    }
}

TEST_CASE("cut jump: no click") {
    const double fs = 48000.0;
    const int half = 24000, tail = 4800, n = half + 2 * tail;
    Engine e;
    e.setCut(100.0);
    e.prepare(fs);
    e.setBypass(false);
    e.setTest(Test::Normal);
    Frames in(4, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) in[0][i] = in[3][i] = 0.5 * std::sin(2.0 * M_PI * 1000.0 * i / fs);
    Frames out(4, std::vector<double>(n));
    const double* ip[4] = {in[0].data(), in[1].data(), in[2].data(), in[3].data()};
    double* op[4] = {out[0].data(), out[1].data(), out[2].data(), out[3].data()};
    const int jump = half + 100; // mid-buffer: a non-multiple of any inner block
    e.process<double>(ip, op, jump);
    e.setCut(250.0);
    const double* ip2[4] = {ip[0] + jump, ip[1] + jump, ip[2] + jump, ip[3] + jump};
    double* op2[4] = {op[0] + jump, op[1] + jump, op[2] + jump, op[3] + jump};
    e.process<double>(ip2, op2, n - jump);
    for (int c = 0; c < 4; ++c) {
        double before = 0, after = 0;
        for (int i = half - tail + 100; i < half + 100; ++i) before = std::max(before, std::fabs(out[c][i] - out[c][i - 1]));
        for (int i = jump; i < jump + tail; ++i) {
            REQUIRE(std::isfinite(out[c][i]));
            after = std::max(after, std::fabs(out[c][i] - out[c][i - 1]));
        }
        CHECK(after <= 1.2 * before);
    }
}

TEST_CASE("silence after signal leaves no subnormal state") {
    Engine e;
    e.prepare(48000.0);
    e.setBypass(false);
    e.setTest(Test::Normal);
    const Frames burst = noise(2000);
    run(e, burst);
    const Frames silence(4, std::vector<double>(48000 * 3, 0.0));
    const Frames out = run(e, silence);
    CHECK(e.hasSubnormalState() == false);
    for (int c = 0; c < 4; ++c) CHECK(out[c].back() == 0.0);
}
