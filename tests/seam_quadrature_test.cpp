#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_quadrature.h"
#include <cmath>

using namespace Seam::quadrature;

TEST_CASE("allpass section has unit magnitude (phase-only)") {
    const double fs = 48000.0;
    for (double fHz : {20.0, 200.0, 2000.0, 18000.0}) {
        double w = 2.0 * M_PI * fHz / fs;
        double mag = allpassSectionMag(1000.0, 0.7071, fs, w);
        CHECK(mag == doctest::Approx(1.0).epsilon(1e-9));
    }
}

TEST_CASE("polyphase first-order section has unit magnitude (phase-only)") {
    const double fs = 48000.0;
    for (double a : {0.4, 0.6923878, 0.9882295}) {
        for (double fHz : {20.0, 200.0, 2000.0, 18000.0}) {
            double w = 2.0 * M_PI * fHz / fs;
            CHECK(polyphaseSectionMag(a, w) == doctest::Approx(1.0).epsilon(1e-9));
        }
    }
}

TEST_CASE("cascade phase sums distinct section phases") {
    const double fs = 48000.0;
    const int M = 8;
    double freqs[M];
    for (int i = 0; i < M; ++i) freqs[i] = 20.0 * std::pow(1000.0, double(i)/(M-1));
    APSpec a[1]    = {{300.0,  0.5}};
    APSpec b[1]    = {{4000.0, 0.8}};
    APSpec both[2] = {{300.0,  0.5}, {4000.0, 0.8}};
    double pa[M], pb[M], pab[M];
    cascadePhase(a,    1, fs, freqs, M, pa);
    cascadePhase(b,    1, fs, freqs, M, pb);
    cascadePhase(both, 2, fs, freqs, M, pab);
    for (int i = 0; i < M; ++i) CHECK(pab[i] == doctest::Approx(pa[i] + pb[i]).epsilon(1e-9));
}

TEST_CASE("designQuadrature reproduces the per-rate table max error") {
    struct Ref { double fs, err; };
    const Ref refs[] = {
        {44100.0, 2.04}, {48000.0, 1.36}, {88200.0, 0.53},
        {96000.0, 0.51}, {176400.0, 0.43}, {192000.0, 0.43},
    };
    for (auto r : refs) {
        QuadratureDesign d = designQuadrature(r.fs, 20.0, 20000.0, 3);
        CHECK(d.converged);
        CHECK(d.nSections == 3);
        CHECK(std::fabs(d.maxErrorDeg - r.err) <= 0.15);
    }
}

TEST_CASE("designPolyphase reproduces the study's per-rate order-4 error") {
    // Reference: study-topologies perfs mode, order 4, band 20 Hz–20 kHz.
    struct Ref { double fs, err; };
    const Ref refs[] = {
        {44100.0, 1.572}, {48000.0, 1.668}, {88200.0, 2.448},
        {96000.0, 2.571}, {176400.0, 3.186}, {192000.0, 3.240},
    };
    for (auto r : refs) {
        PolyphaseDesign d = designPolyphase(r.fs, 4);
        CHECK(d.converged);
        CHECK(d.nA == 4);
        CHECK(d.nB == 4);
        CHECK(d.sampleRate == r.fs);
        CHECK(std::fabs(d.maxErrorDeg - r.err) <= 0.2);
    }
}

TEST_CASE("designPolyphase is deterministic") {
    PolyphaseDesign a = designPolyphase(48000.0, 4);
    PolyphaseDesign b = designPolyphase(48000.0, 4);
    for (int i = 0; i < a.nA + a.nB; ++i)
        CHECK(a.a[i] == doctest::Approx(b.a[i]));
}

// Steady-state phase (degrees) of one output at frequency f, measured by a
// single-bin DFT over an integer number of periods after IIR warm-up.
static double measurePhaseDeg(QuadraturePair& qp, double f, double fs, bool quadOut) {
    const double w = 2.0 * M_PI * f / fs;
    const int warm = 8192;
    const int periods = 64;
    const int N = int(std::round(periods * fs / f));
    qp.reset();
    for (int n = 0; n < warm; ++n) {
        double ip, q; qp.process(std::sin(w * n), ip, q);
    }
    double re = 0.0, im = 0.0;
    for (int n = 0; n < N; ++n) {
        double ip, q; qp.process(std::sin(w * (warm + n)), ip, q);
        double y = quadOut ? q : ip;
        re += y * std::cos(w * n);
        im -= y * std::sin(w * n);
    }
    return std::atan2(im, re) * 180.0 / M_PI;
}

static double wrap180(double d) {
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

TEST_CASE("QuadraturePair (RBJ) holds -90 deg phase difference across the band") {
    const double fs = 48000.0;
    QuadratureDesign d = designQuadrature(fs, 20.0, 20000.0, 3);
    QuadraturePair qp;
    qp.configureRBJ(d);
    for (double f : {50.0, 200.0, 1000.0, 5000.0, 15000.0}) {
        double pIn = measurePhaseDeg(qp, f, fs, false);
        double pQ  = measurePhaseDeg(qp, f, fs, true);
        double diff = wrap180(pQ - pIn);
        CHECK(std::fabs(diff - (-90.0)) <= d.maxErrorDeg + 0.5);
    }
}

TEST_CASE("QuadraturePair (polyphase) holds -90 deg phase difference across the band") {
    const double fs = 48000.0;
    PolyphaseDesign d = designPolyphase(fs, 4);
    QuadraturePair qp;
    qp.configurePolyphase(d);
    for (double f : {50.0, 200.0, 1000.0, 5000.0, 15000.0}) {
        double pIn = measurePhaseDeg(qp, f, fs, false);
        double pQ  = measurePhaseDeg(qp, f, fs, true);
        double diff = wrap180(pQ - pIn);
        CHECK(std::fabs(diff - (-90.0)) <= d.maxErrorDeg + 0.5);
    }
}

TEST_CASE("designQuadrature is deterministic") {
    QuadratureDesign a = designQuadrature(48000.0, 20.0, 20000.0, 3);
    QuadratureDesign b = designQuadrature(48000.0, 20.0, 20000.0, 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(a.hr[i].f == doctest::Approx(b.hr[i].f));
        CHECK(a.hi[i].Q == doctest::Approx(b.hi[i].Q));
    }
}
