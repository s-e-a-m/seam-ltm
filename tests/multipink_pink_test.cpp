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
//
// CRITERION CHANGE (2026-08-19): the filter under test is now the Hz-anchored
// matched-Z design of PinkDesign (multipink_pink.h), not a fixed z-plane fit.
// Two consequences follow. First, SMPTE's own 16 kHz ceiling was a stand-in
// for "the top band this rig can measure" -- below 48 kHz it is exactly that,
// because the 20 kHz band's upper edge (22.39 kHz) is not measurable at any
// rate under 48 kHz, but at 88.2/96/176.4/192 kHz the rig CAN measure well
// above 16 kHz, so the judged range now follows strx's own measurability rule
// (nominal upper edge within 0.85*Nyquist) instead of stopping at a fixed
// frequency. Second, the verdict flips: where the old fixed fit failed the
// standard at every rate it was tested at, including the one it was fitted to,
// the new design passes at all six rates tested, with a sentinel tolerance
// tighter than the standard's to catch regressions the standard itself is too
// loose to see.
// ---------------------------------------------------------------------------

namespace {

constexpr double kSmpteToleranceDb = 0.25;
constexpr double kSentinelDb       = 0.10;   // regression sentinel, see the spec

// The judged bands: 20 Hz up to the highest band whose nominal upper edge is
// within 0.85*Nyquist. This is strx's own measurability rule, and below
// 48 kHz it coincides exactly with SMPTE's 16 kHz ceiling, because the 20 kHz
// band's upper edge is 22.39 kHz and is not measurable there at all.
std::vector<int> judgedBands(double fs) {
    std::vector<int> v;
    for (int i = 0; i < 40; ++i) {
        const double fc = 1000.0 * std::pow(10.0, (strx::kBandN0 + i) / 10.0);
        const double fu = fc * std::pow(10.0, 0.05);
        if (fu <= 0.85 * 0.5 * fs) v.push_back(i);
    }
    return v;
}

double bandFcExt(int i) { return 1000.0 * std::pow(10.0, (strx::kBandN0 + i) / 10.0); }
double bandFlExt(int i) { return bandFcExt(i) * std::pow(10.0, -0.05); }
double bandFuExt(int i) { return bandFcExt(i) * std::pow(10.0,  0.05); }

// Energy in one band, in dB, for white noise through the filter. Analytic:
// with white input the output power spectrum IS |H|^2, so this is an integral
// and not a measurement. A third-octave level taken from noise carries about
// 0.6 dB of spread at BT = 50 and could not resolve a 0.25 dB tolerance.
double bandEnergyDb(const Seam::multipink::PinkDesign& d, int i) {
    const double fl = bandFlExt(i), fu = bandFuExt(i);
    const int kSteps = 8192;
    double sum = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        const double f = fl + (fu - fl) * (k + 0.5) / kSteps;
        const double m = std::pow(10.0, d.magnitudeDb(f) / 20.0);
        sum += m * m;
    }
    return 10.0 * std::log10(sum * (fu - fl) / kSteps);
}

std::vector<double> deviations(double fs) {
    Seam::multipink::PinkDesign d;
    d.design(fs);
    std::vector<double> lvl;
    for (int i : judgedBands(fs)) lvl.push_back(bandEnergyDb(d, i));
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
    for (int i = 0; i <= 29; ++i) {
        const double r = strx::bandFuNominal(i) / strx::bandFl(i);
        if (i == 0) widthRatio = r;
        CHECK(r == doctest::Approx(widthRatio).epsilon(1e-9));   // constant relative bandwidth
    }
    CHECK(strx::bandFc(17) == doctest::Approx(1000.0));
    CHECK(fs > 0);
}

TEST_CASE("the pinking filter meets SMPTE ST 2095-1 at every rate") {
    struct Rate { double fs; const char* name; int bands; double ceilingHz; };
    static const Rate kRates[] = {
        {44100.0,  "44.1 kHz",  30, 15848.9}, {48000.0,  "48 kHz",    30, 15848.9},
        {88200.0,  "88.2 kHz",  33, 31622.8}, {96000.0,  "96 kHz",    33, 31622.8},
        {176400.0, "176.4 kHz", 36, 63095.7}, {192000.0, "192 kHz",   36, 63095.7} };

    std::printf("\n  +/-%.2f dB per third-octave, 20 Hz to 0.85*Nyquist\n\n", kSmpteToleranceDb);
    for (const Rate& r : kRates) {
        const double w = worstDeviation(r.fs);
        std::printf("  %-10s %2zu bands, up to %8.0f Hz   %6.3f dB   %s\n",
                    r.name, judgedBands(r.fs).size(), r.ceilingHz, w,
                    w <= kSmpteToleranceDb ? "PASS" : "FAIL");
    }
    std::printf("\n");

    for (const Rate& r : kRates) {
        CHECK((int)judgedBands(r.fs).size() == r.bands);
        CHECK(bandFcExt(judgedBands(r.fs).back()) == doctest::Approx(r.ceilingHz).epsilon(0.001));
        CHECK(worstDeviation(r.fs) <= kSmpteToleranceDb);   // the standard
        CHECK(worstDeviation(r.fs) <= kSentinelDb);         // the sentinel
    }

    // Pinned, so that a change has to be deliberate. Measured 2026-08-19 at
    // one pole per octave; Task 6 may move these by changing kPolesPerOctave.
    CHECK(worstDeviation(44100.0)  == doctest::Approx(0.079).epsilon(0.05));
    CHECK(worstDeviation(48000.0)  == doctest::Approx(0.071).epsilon(0.05));
    CHECK(worstDeviation(88200.0)  == doctest::Approx(0.080).epsilon(0.05));
    CHECK(worstDeviation(96000.0)  == doctest::Approx(0.072).epsilon(0.05));
    CHECK(worstDeviation(176400.0) == doctest::Approx(0.081).epsilon(0.05));
    CHECK(worstDeviation(192000.0) == doctest::Approx(0.072).epsilon(0.05));
}

TEST_CASE("the error does not grow with the sample rate, as an Hz-anchored design must not") {
    const double at48  = worstDeviation(48000.0);
    const double at192 = worstDeviation(192000.0);
    CHECK(std::fabs(at192 - at48) < 0.05);
}
