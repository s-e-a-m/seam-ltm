#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_svf.h"
#include "ref/seam_svf_ref.h"
#include <algorithm>
#include <cmath>

using Seam::SvfBandpass;

template <int N>
static double impulseRelErr(double fs, double f, double q, const double (*ref)[N]) {
    SvfBandpass b; b.design(fs, f, q);
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < N; ++k) {
        const double y = b.tick(k == 0 ? 1.0 : 0.0);
        e = std::max(e, std::fabs(y - ref[0][k]));
        pk = std::max(pk, std::fabs(ref[0][k]));
    }
    return e / pk;
}

TEST_CASE("equals fi.svf.bp(48, 350) at 96 kHz, impulse response") {
    CHECK(impulseRelErr<2048>(96000.0, 48.0, 350.0, svfref::kSvf48) < 1e-13);
}

TEST_CASE("equals fi.svf.bp(1000, 0.7) at 48 kHz, impulse response") {
    CHECK(impulseRelErr<512>(48000.0, 1000.0, 0.7, svfref::kSvf1k) < 1e-13);
}

TEST_CASE("peak gain is q at the centre") {
    const double fs = 48000.0, f = 1000.0, q = 0.7;
    SvfBandpass b; b.design(fs, f, q);
    double pk = 0.0;
    for (int n = 0; n < (int)fs; ++n) {
        const double y = b.tick(std::sin(2.0 * M_PI * f * n / fs));
        if (n > fs / 2) pk = std::max(pk, std::fabs(y));
    }
    CHECK(pk == doctest::Approx(q).epsilon(1e-4));
}

TEST_CASE("reset empties the state") {
    SvfBandpass b; b.design(96000.0, 48.0, 350.0);
    for (int n = 0; n < 1000; ++n) b.tick(1.0);
    b.reset();
    CHECK(b.tick(0.0) == 0.0);
}

// ---- Svf: the hp / ap / bell modes of fi.svf --------------------------------
#include <complex>
#include <random>
#include <vector>

using Seam::Svf;

// H(f) of an impulse response by direct DFT at one frequency.
static std::complex<double> dftAt(const std::vector<double>& h, double f, double fs) {
    std::complex<double> s = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        s += h[n] * std::polar(1.0, -2.0 * M_PI * f * (double)n / fs);
    return s;
}

TEST_CASE("Svf bell: gain at f0") {
    const double fs = 96000.0;
    Svf b; b.design(fs, Svf::Type::Bell, 310.0, 0.91, -3.1);
    std::vector<double> h(65536);
    for (size_t n = 0; n < h.size(); ++n) h[n] = b.tick(n == 0 ? 1.0 : 0.0);
    const double db = 20.0 * std::log10(std::abs(dftAt(h, 310.0, fs)));
    CHECK(std::fabs(db + 3.1) < 0.01);
}

TEST_CASE("Svf: LR4 high-pass phase equals the all-pass") {
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    const double cuts[] = {100.0, 160.0, 250.0};
    const double q = std::sqrt(0.5);
    for (double fs : rates) for (double fc : cuts) {
        Svf h1, h2, ap;
        h1.design(fs, Svf::Type::Highpass, fc, q);
        h2.design(fs, Svf::Type::Highpass, fc, q);
        ap.design(fs, Svf::Type::Allpass, fc, q);
        const int N = 1 << 17;
        std::vector<double> hp(N), a(N);
        for (int n = 0; n < N; ++n) {
            const double x = n == 0 ? 1.0 : 0.0;
            hp[n] = h2.tick(h1.tick(x));
            a[n] = ap.tick(x);
        }
        double worst = 0.0;
        const double lo = 20.0, hi = 0.45 * fs;
        for (int k = 0; k < 30; ++k) {
            const double f = lo * std::pow(hi / lo, k / 29.0);
            const double d = std::arg(dftAt(hp, f, fs) / dftAt(a, f, fs));
            worst = std::max(worst, std::fabs(d));
        }
        INFO("fs=" << fs << " fc=" << fc);
        CHECK(worst < 1e-6);
    }
}

TEST_CASE("Svf: silence leaves no subnormal state") {
    const Svf::Type types[] = {Svf::Type::Highpass, Svf::Type::Allpass, Svf::Type::Bell};
    for (Svf::Type t : types) {
        Svf f; f.design(48000.0, t, 160.0, 0.9, -3.0);
        std::mt19937 rng(1);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        for (int n = 0; n < 48000; ++n) f.tick(u(rng));
        for (int n = 0; n < 480000; ++n) f.tick(0.0);
        CHECK_FALSE(f.isSubnormal());
    }
}
