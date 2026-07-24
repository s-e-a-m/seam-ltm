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

// True biquad-cascade magnitude (dB) of a design at frequency f.
static double designMagDb(const AirFilterDesign& d, double f, double fs) {
    const double w = 2.0 * M_PI * f / fs;
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    double db = 0.0;
    for (int k = 0; k < d.numSections; ++k) {
        const std::complex<double> num = d.b0[k] + d.b1[k] * z1 + d.b2[k] * z2;
        const std::complex<double> den = 1.0    + d.a1[k] * z1 + d.a2[k] * z2;
        db += 20.0 * std::log10(std::abs(num / den));
    }
    return db;
}

// Max |designed - target| dB over a fine log grid.
static double bandErrDb(const AirFilterDesign& des, double d, double T, double RH, double fs) {
    double err = 0.0;
    const double fHi = std::min(20000.0, 0.45 * fs);
    for (double f = 20.0; f <= fHi; f *= 1.1) {
        const double target = -alphaISO9613(f, T, RH) * d;
        err = std::max(err, std::abs(designMagDb(des, f, fs) - target));
    }
    return err;
}

TEST_CASE("cascade tracks alpha*d tightly across distance; shelf is looser") {
    const double fs = 48000.0, T = 20.0, RH = 50.0;
    for (double d : {5.0, 20.0, 30.0}) {
        auto cas = designAirFilter(d, T, RH, fs, Topology::Cascade);
        auto shl = designAirFilter(d, T, RH, fs, Topology::Shelf);
        CHECK(cas.converged);
        CHECK(shl.converged);
        const double casErr = bandErrDb(cas, d, T, RH, fs);
        const double shlErr = bandErrDb(shl, d, T, RH, fs);
        CHECK(casErr < 1.0);          // 2nd-order biquad cascade: tight everywhere
        CHECK(shlErr > casErr);       // shelf is the deliberately-loose topology
        CHECK(shlErr < 8.0);          // but still bounded (gross tilt)
        CHECK(cas.maxErrorDb < 1.0);  // the design's own metric agrees
    }
}

TEST_CASE("cascade holds across humidity and sample rate") {
    for (double RH : {10.0, 50.0, 90.0})
        CHECK(bandErrDb(designAirFilter(20.0, 20.0, RH, 48000.0, Topology::Cascade),
                        20.0, 20.0, RH, 48000.0) < 1.0);
    for (double fs : {44100.0, 96000.0, 192000.0})
        CHECK(bandErrDb(designAirFilter(20.0, 20.0, 50.0, fs, Topology::Cascade),
                        20.0, 20.0, 50.0, fs) < 1.0);
}

TEST_CASE("filter is unity at DC (no broadband loss without spreading)") {
    auto d = designAirFilter(20.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    CHECK(designMagDb(d, 1.0, 48000.0) == doctest::Approx(0.0).epsilon(0.02));
    auto s = designAirFilter(20.0, 20.0, 50.0, 48000.0, Topology::Shelf);
    CHECK(designMagDb(s, 1.0, 48000.0) == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("zero distance is a flat 0 dB pass") {
    auto d = designAirFilter(0.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    for (double f = 50.0; f <= 18000.0; f *= 2.0)
        CHECK(designMagDb(d, f, 48000.0) == doctest::Approx(0.0).epsilon(0.05));
}

TEST_CASE("AirFilter runtime settles to unity at DC") {
    auto des = designAirFilter(25.0, 20.0, 50.0, 48000.0, Topology::Cascade);
    AirFilter f; f.configure(des); f.reset();
    double y = 0.0;
    for (int n = 0; n < 8192; ++n) y = f.process(1.0);   // DC step
    CHECK(y == doctest::Approx(1.0).epsilon(0.01));       // high-shelf DC gain = 1
}

TEST_CASE("spreading gain: 1/max(d,1), attenuation only") {
    CHECK(spreadingGain(0.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(0.5)  == doctest::Approx(1.0));   // below 1 m: unity
    CHECK(spreadingGain(1.0)  == doctest::Approx(1.0));
    CHECK(spreadingGain(2.0)  == doctest::Approx(0.5));   // -6 dB
    CHECK(spreadingGain(4.0)  == doctest::Approx(0.25));  // -12 dB
    CHECK(spreadingGain(30.0) == doctest::Approx(1.0/30.0));
}
