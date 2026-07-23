#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_airabsorption.h"
#include <cmath>
#include <complex>

using namespace Seam::air;

// Reference conditions: 20 C, 50% RH, 1 atm. These BRACKETS are order-of-
// magnitude anchors (dB/m). Before merge, pin exact ISO 9613-1 Table values
// (or the NPL calculator) and tighten — see Step 5's provenance note.
TEST_CASE("alpha is small at low f, grows with frequency (20C/50%RH/1atm)") {
    const double a125  = alphaISO9613(125.0,   20.0, 50.0);
    const double a1k   = alphaISO9613(1000.0,  20.0, 50.0);
    const double a4k   = alphaISO9613(4000.0,  20.0, 50.0);
    const double a10k  = alphaISO9613(10000.0, 20.0, 50.0);

    CHECK(a125 < a1k);
    CHECK(a1k  < a4k);
    CHECK(a4k  < a10k);

    // 1 kHz ~ a few dB/km = a few 1e-3 dB/m; 10 kHz ~ 1e-1 dB/m.
    CHECK(a1k  == doctest::Approx(0.005).epsilon(0.6));   // 0.002..0.008 dB/m
    CHECK(a10k > 0.03);
    CHECK(a10k < 0.30);
    CHECK(a125 > 0.0);
}

TEST_CASE("alpha stays finite and positive at humidity and temperature edges") {
    CHECK(alphaISO9613(4000.0, 20.0,  0.0) > 0.0);   // bone-dry
    CHECK(alphaISO9613(4000.0, 20.0, 100.0) > 0.0);  // saturated
    CHECK(std::isfinite(alphaISO9613(8000.0, -20.0, 10.0)));
    CHECK(std::isfinite(alphaISO9613(8000.0,  50.0, 90.0)));
}

// Dry air absorbs high frequencies MORE than very humid air in the mid band
// is a common misconception; the ISO curve peaks at intermediate humidity.
// Just assert humidity actually changes the result (not a stubbed constant).
TEST_CASE("humidity changes alpha") {
    CHECK(alphaISO9613(4000.0, 20.0, 20.0) != alphaISO9613(4000.0, 20.0, 80.0));
}

// Magnitude (dB) of a designed filter at frequency f.
static double designMagDb(const AirFilterDesign& d, double f, double fs) {
    // Product of first-order mix sections: y = v*x + (1-v)*LP(x).
    // Complex one-pole LP response H(w) = c / (1 - (1-c) e^{-jw}).
    const double w = 2.0 * M_PI * f / fs;
    std::complex<double> H(1.0, 0.0);
    for (int s = 0; s < d.numSections; ++s) {
        const double c = 1.0 - std::exp(-2.0 * M_PI * d.corner[s] / fs);
        const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
        const std::complex<double> lp = c / (1.0 - (1.0 - c) * z1);
        const double v = d.hfGain[s];
        H *= (v + (1.0 - v) * lp);
    }
    return 20.0 * std::log10(std::abs(H));
}

TEST_CASE("cascade tracks alpha*d across the band; shelf is looser") {
    const double fs = 48000.0, d = 20.0, T = 20.0, RH = 50.0;
    auto cas = designAirFilter(d, T, RH, fs, Topology::Cascade);
    auto shl = designAirFilter(d, T, RH, fs, Topology::Shelf);
    CHECK(cas.converged);
    CHECK(shl.converged);

    double casErr = 0.0, shlErr = 0.0;
    for (double f = 20.0; f <= 20000.0; f *= 1.3) {
        const double target = -alphaISO9613(f, T, RH) * d;   // dB (negative)
        casErr = std::max(casErr, std::abs(designMagDb(cas, f, fs) - target));
        shlErr = std::max(shlErr, std::abs(designMagDb(shl, f, fs) - target));
    }
    CHECK(casErr < 1.5);          // cascade tracks tightly
    // Shelf multiplier widened from the brief's 3.0x to 4.0x: a single
    // mix-form section's best achievable fit to this exact target floors
    // around 4.6 dB (numerically searched, see task-2-report.md), and
    // 3x the achieved cascade error (~1.39 dB) undercuts that floor for
    // any corner choice -- 3.0x and casErr<1.5 are mutually unsatisfiable
    // for this scenario. 4.0x keeps "same sign of tilt, looser" intact.
    CHECK(shlErr < casErr * 4.0);
    CHECK(cas.maxErrorDb == doctest::Approx(casErr).epsilon(0.25));
}

TEST_CASE("filter is unity at DC (no broadband loss without spreading)") {
    auto d = designAirFilter(20.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    CHECK(designMagDb(d, 1.0, 48000.0) == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("zero distance is a flat 0 dB pass") {
    auto d = designAirFilter(0.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    for (double f = 50.0; f <= 18000.0; f *= 2.0)
        CHECK(designMagDb(d, f, 48000.0) == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("AirFilter runtime matches its designed DC and settles") {
    auto des = designAirFilter(25.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    AirFilter f; f.configure(des); f.reset();
    double y = 0.0;
    for (int n = 0; n < 4096; ++n) y = f.process(1.0);   // DC step
    CHECK(y == doctest::Approx(1.0).epsilon(0.01));       // DC gain ~ unity
}

TEST_CASE("spreading gain: 1/max(d,1), attenuation only") {
    CHECK(spreadingGain(0.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(0.5)  == doctest::Approx(1.0));   // below 1 m: unity
    CHECK(spreadingGain(1.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(2.0)  == doctest::Approx(0.5));   // -6 dB
    CHECK(spreadingGain(4.0)  == doctest::Approx(0.25));  // -12 dB
    CHECK(spreadingGain(30.0) == doctest::Approx(1.0/30.0));
}
