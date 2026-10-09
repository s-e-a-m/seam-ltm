#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "abmodulex_dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace Seam::abmodulex;

TEST_CASE("abmodulex: unit LFU spreads equally to all ACN channels") {
    double a0,a1,a2,a3;
    encode(1.0, 0.0, 0.0, 0.0, a0,a1,a2,a3);
    CHECK(a0 == doctest::Approx(0.5));
    CHECK(a1 == doctest::Approx(0.5));
    CHECK(a2 == doctest::Approx(0.5));
    CHECK(a3 == doctest::Approx(0.5));
}

TEST_CASE("abmodulex is involutory (round-trips with itself / bamodulex)") {
    const double lfu=0.3, rfd=-0.7, rbu=0.1, lbd=0.9;
    double a0,a1,a2,a3;
    encode(lfu,rfd,rbu,lbd, a0,a1,a2,a3);
    double b0,b1,b2,b3;
    encode(a0,a1,a2,a3, b0,b1,b2,b3);
    CHECK(b0 == doctest::Approx(lfu));
    CHECK(b1 == doctest::Approx(rfd));
    CHECK(b2 == doctest::Approx(rbu));
    CHECK(b3 == doctest::Approx(lbd));
}

// ── The dual tetrahedron: the capsules read as LFD RFU RBD LBU ────────────
// Each slot keeps its azimuth and takes the opposite elevation, so only the
// Z row of the matrix changes sign. The menu crossfades Z over 25 ms.
using Frame = std::array<double, 4>;

// n samples of the constant input x, one sample per call (the ramp's
// progress then shows sample by sample).
static std::vector<Frame> runBlock(Encoder& e, const double (&x)[4], int n) {
    std::vector<Frame> y((size_t)n);
    for (int k = 0; k < n; ++k) {
        const double* in[4] = { &x[0], &x[1], &x[2], &x[3] };
        double* out[4] = { &y[k][0], &y[k][1], &y[k][2], &y[k][3] };
        e.process(in, out, 1);
    }
    return y;
}

TEST_CASE("the Encoder on the upright tetrahedron gives encode() bit for bit") {
    Encoder e; e.prepare(96000.0); e.reset();
    double worst = 0.0;
    for (int k = 0; k < 4096; ++k) {
        const double x[4] = { std::sin(k * 0.01), 0.3 * std::cos(k * 0.07), -0.2 * std::sin(k * 0.003), 0.7 * std::cos(k * 0.11) };
        const auto y = runBlock(e, x, 1);
        double a0, a1, a2, a3;
        encode(x[0], x[1], x[2], x[3], a0, a1, a2, a3);
        worst = std::max({ worst, std::fabs(y[0][0] - a0), std::fabs(y[0][1] - a1), std::fabs(y[0][2] - a2), std::fabs(y[0][3] - a3) });
    }
    CHECK(worst == 0.0);
}

TEST_CASE("on the dual tetrahedron a capsule in slot 1 (LFD) reads left, front and DOWN") {
    Encoder e; e.prepare(96000.0); e.setDual(true); e.reset();
    const double lfd[4] = { 1.0, 0.0, 0.0, 0.0 };
    auto y = runBlock(e, lfd, 1);
    CHECK(y[0][0] == 0.5);      // W
    CHECK(y[0][1] == 0.5);      // Y, left
    CHECK(y[0][2] == -0.5);     // Z, down
    CHECK(y[0][3] == 0.5);      // X, front
    const double rbd[4] = { 0.0, 0.0, 1.0, 0.0 };   // slot 3: right, back, down
    y = runBlock(e, rbd, 1);
    CHECK(y[0][0] == 0.5);
    CHECK(y[0][1] == -0.5);
    CHECK(y[0][2] == -0.5);
    CHECK(y[0][3] == -0.5);
}

static void checkGlide(double fs, int n) {
    CAPTURE(fs);
    Encoder e; e.prepare(fs); e.reset();
    const double x[4] = { 0.8, -0.1, 0.4, 0.3 };     // Z = (0.8 + 0.1 + 0.4 - 0.3)/2 = 0.5
    const auto y0 = runBlock(e, x, 1);
    e.setDual(true);
    const auto y = runBlock(e, x, 2 * n);
    for (int k : { 0, n / 2 - 1, n - 1, 2 * n - 1 }) {
        CAPTURE(k);
        const double g = k < n ? 1.0 - 2.0 * (k + 1) / n : -1.0;
        CHECK(y[k][2] == doctest::Approx(0.5 * g).epsilon(1e-12).scale(1.0));
        CHECK(y[k][0] == y0[0][0]);                   // W, Y, X untouched while Z glides
        CHECK(y[k][1] == y0[0][1]);
        CHECK(y[k][3] == y0[0][3]);
    }
}

TEST_CASE("switching to the dual glides Z from +1 to -1 in 25 ms, at 96 and 48 kHz") {
    checkGlide(96000.0, 2400);
    checkGlide(48000.0, 1200);
}
