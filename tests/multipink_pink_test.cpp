#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_pink.h"
#include "strx_bands.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Seam;

// ---------------------------------------------------------------------------
// SMPTE ST 2095-1 acceptance test for a pinking filter.
//
// The standard defines exactly this case -- a pink signal whose output is the
// reference a system is calibrated against -- and fixes the uniformity of the
// third-octave band levels from 20 Hz to 16 kHz at +/-0.25 dB (Table 1, ed.
// 2023). It does NOT prescribe the filter; it prescribes the result. So this
// judges any candidate the same way, and a future replacement passes or fails
// by the same function.
//
// ANALYTIC, not measured. The tolerance is +/-0.25 dB and a third-octave band
// level measured on noise carries about +/-0.6 dB of spread at BT = 50: a
// noise-driven test cannot resolve what it is asked to judge. Feeding the
// filter white noise means its output power spectrum IS |H(f)|^2, so the band
// energy is the integral of |H|^2 over the band -- computed here to a precision
// the question deserves.
// ---------------------------------------------------------------------------

namespace {

constexpr double kSmpteToleranceDb = 0.25;
// SMPTE names 20 Hz to 16 kHz: ISO band indices 0..29 of our 31.
constexpr int kFirstBand = 0;
constexpr int kLastBand  = 29;

// Energy in one ISO 266 band, in dB, for white noise through the filter.
double bandEnergyDb(int i, double fs) {
    const double fl = strx::bandFl(i);
    const double fu = strx::bandFuNominal(i);
    const int kSteps = 8192;
    double sum = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        const double f = fl + (fu - fl) * (k + 0.5) / kSteps;
        const double m = std::pow(10.0, multipink::magnitudeDb(f, fs) / 20.0);
        sum += m * m;
    }
    return 10.0 * std::log10(sum * (fu - fl) / kSteps);
}

// Deviation of each band from the mean of the judged range. Pink carries equal
// energy in every constant-relative-bandwidth band, so a perfect pinking filter
// is a flat line here and the absolute level is irrelevant.
std::vector<double> deviations(double fs) {
    std::vector<double> lvl;
    for (int i = kFirstBand; i <= kLastBand; ++i) lvl.push_back(bandEnergyDb(i, fs));
    double mean = 0.0;
    for (double v : lvl) mean += v;
    mean /= (double)lvl.size();
    for (double& v : lvl) v -= mean;
    return lvl;
}

double worstDeviation(double fs) {
    double worst = 0.0;
    for (double v : deviations(fs)) worst = std::max(worst, std::fabs(v));
    return worst;
}

} // namespace

TEST_CASE("the acceptance criterion is the standard's, not ours") {
    // A sanity check on the yardstick itself: an ideal pinking filter is one
    // whose band energies are equal, so the deviation of a flat line is zero.
    // If the band integration were wrong, every filter would look wrong the
    // same way and the test would say nothing.
    const double fs = 48000.0;
    double widthRatio = 0.0;
    for (int i = kFirstBand; i <= kLastBand; ++i) {
        const double r = strx::bandFuNominal(i) / strx::bandFl(i);
        if (i == kFirstBand) widthRatio = r;
        CHECK(r == doctest::Approx(widthRatio).epsilon(1e-9));   // constant relative bandwidth
    }
    CHECK(strx::bandFc(17) == doctest::Approx(1000.0));
    CHECK(fs > 0);
}

TEST_CASE("multipink's pinking filter against SMPTE ST 2095-1") {
    struct Rate { double fs; const char* name; };
    static const Rate kRates[] = {
        {44100.0, "44.1 kHz"}, {48000.0, "48 kHz"}, {88200.0, "88.2 kHz"},
        {96000.0, "96 kHz"},   {192000.0, "192 kHz"} };

    // Printed rather than asserted, because the number a reader wants is the
    // shape of the failure and not only its size.
    std::printf("\n  SMPTE ST 2095-1 acceptance, +/-%.2f dB per third-octave, 20 Hz - 16 kHz\n\n",
                kSmpteToleranceDb);
    std::printf("  %-10s %8s   %s\n", "rate", "worst", "deviation at 20 / 25 / 31.5 / 40 / 63 Hz ... 16 kHz");
    for (const Rate& r : kRates) {
        const std::vector<double> d = deviations(r.fs);
        std::printf("  %-10s %7.2f    %+5.2f %+5.2f %+5.2f %+5.2f %+5.2f  ...  %+5.2f   %s\n",
                    r.name, worstDeviation(r.fs), d[0], d[1], d[2], d[3], d[5], d[29],
                    worstDeviation(r.fs) <= kSmpteToleranceDb ? "PASS" : "FAIL");
    }
    std::printf("\n");

    // What is true today, recorded as assertions so that a change is caught.
    // The filter was hand-fitted at one rate: it is close there and degrades
    // as the rate moves away, which is the whole defect in one line.
    // Pinned, so that any change to the filter or to the method of judging it
    // has to be made deliberately rather than noticed later.
    CHECK(worstDeviation(44100.0)  == doctest::Approx(0.41).epsilon(0.03));
    CHECK(worstDeviation(48000.0)  == doctest::Approx(0.60).epsilon(0.03));
    CHECK(worstDeviation(88200.0)  == doctest::Approx(2.40).epsilon(0.03));
    CHECK(worstDeviation(96000.0)  == doctest::Approx(2.68).epsilon(0.03));
    CHECK(worstDeviation(192000.0) == doctest::Approx(4.99).epsilon(0.03));

    // The finding that reframes the whole question: it fails at the rate it was
    // fitted at, too. 0.41 dB against a 0.25 dB tolerance means no remapping of
    // THIS fit can ever pass -- remapping reproduces the design-rate response,
    // and the design-rate response is already out of tolerance.
    CHECK(worstDeviation(44100.0) > kSmpteToleranceDb);

    // And the verdict the standard gives, at every rate. This is the assertion
    // that must flip when a replacement lands.
    for (const Rate& r : kRates)
        CHECK(worstDeviation(r.fs) > kSmpteToleranceDb);   // FAILS the standard
}

TEST_CASE("the deviation grows monotonically with sample rate, as a z-plane fit must") {
    // Not a property of this filter but of its KIND: the fit's corners are
    // fractions of fs, so moving fs away from the design rate can only make it
    // worse. A candidate that passes this test while failing the previous one
    // has been fitted at a different rate; one that fails this test is not a
    // z-plane fit at all.
    double prev = worstDeviation(48000.0);
    for (double fs : {88200.0, 96000.0, 192000.0}) {
        const double w = worstDeviation(fs);
        CHECK(w > prev);
        prev = w;
    }
}
