#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_pink.h"
#include <cmath>
#include <vector>

using Seam::multipink::PinkDesign;

static const double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

TEST_CASE("the ladder is stable and fits its capacity at every rate") {
    for (double fs : kRates) {
        PinkDesign d;
        d.design(fs);
        CHECK(d.numSections > 0);
        CHECK(d.numSections <= PinkDesign::kMaxSections);
        // a1 is the z^-1 denominator coefficient, so the pole is -a1
        for (int i = 0; i < d.numSections; ++i)
            CHECK(std::fabs(d.a1[i]) < 1.0);          // stable
    }
}

TEST_CASE("the interior slope is pink, -3.01 dB per octave") {
    PinkDesign d;
    d.design(48000.0);
    // Judged between 100 Hz and 8 kHz, clear of both guard regions.
    for (double f = 100.0; f <= 4000.0; f *= 2.0) {
        const double slope = d.magnitudeDb(2.0 * f) - d.magnitudeDb(f);
        CHECK(slope == doctest::Approx(-3.0103).epsilon(0.005));
    }
}

TEST_CASE("the anchor does not move with the sample rate") {
    // This is what makes the calibration constant a constant: the filter is
    // anchored in Hz, so its gain at 1 kHz must be the same at every rate.
    // Measured spread across the six rates is under 0.01 dB.
    double first = 0.0;
    for (int k = 0; k < 6; ++k) {
        PinkDesign d;
        d.design(kRates[k]);
        const double a = d.anchorGainDb();
        if (k == 0) first = a;
        CHECK(std::fabs(a - first) < 0.01);
        CHECK(a == doctest::Approx(-27.72).epsilon(0.01));
    }
}

TEST_CASE("the RMS gain falls with the sample rate, and by how much") {
    // The other half of the same fact: a filter anchored in Hz cannot also
    // hold its total RMS. 5.8 dB from 44.1 to 192 kHz, which is why the
    // calibration anchors on the band level and not on the RMS.
    PinkDesign lo, hi;
    lo.design(44100.0);
    hi.design(192000.0);
    CHECK(lo.rmsGainDb() == doctest::Approx(-31.07).epsilon(0.01));
    CHECK(hi.rmsGainDb() == doctest::Approx(-36.88).epsilon(0.01));
    CHECK(lo.rmsGainDb() - hi.rmsGainDb() == doctest::Approx(5.81).epsilon(0.02));
}

namespace {

// Run the actual difference equation, transposed direct form II, one state per
// section — the form the processor uses. Templated on the state type so that
// float and double answer the same question.
template <typename T>
double measuredDb(const PinkDesign& d, double f, double fs) {
    std::vector<T> s((size_t)d.numSections, (T)0);
    const long n = 400000;
    double re = 0.0, im = 0.0;
    const double w = 2.0 * M_PI * f / fs;
    for (long k = 0; k < n; ++k) {
        T x = (T)std::sin(w * (double)k);
        for (int i = 0; i < d.numSections; ++i) {
            const T y = (T)d.b0[i] * x + s[(size_t)i];
            s[(size_t)i] = (T)d.b1[i] * x - (T)d.a1[i] * y;
            x = y;
        }
        if (k > n / 4) { re += (double)x * std::cos(w * (double)k);
                         im += (double)x * std::sin(w * (double)k); }
    }
    const double m = (double)(n - n / 4);
    return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / m);
}

} // namespace

TEST_CASE("the running filter matches its own analytic response") {
    PinkDesign d;
    d.design(48000.0);
    for (double f : {20.0, 1000.0, 16000.0})
        CHECK(measuredDb<double>(d, f, 48000.0) == doctest::Approx(d.magnitudeDb(f)).epsilon(0.002));
}

TEST_CASE("single precision costs under a thousandth of a dB") {
    // The opposite of the third-octave filterbank, where float put every band
    // below 160 Hz on the epsilon floor. Same frequencies, same f/fs, other
    // topology: first-order real poles instead of a sixth-order narrowband
    // section in direct form. Worst case here is 192 kHz at 20 Hz.
    for (double fs : {48000.0, 192000.0}) {
        PinkDesign d;
        d.design(fs);
        for (double f : {20.0, 31.5, 1000.0}) {
            const double diff = measuredDb<float>(d, f, fs) - measuredDb<double>(d, f, fs);
            CHECK(std::fabs(diff) < 0.001);
        }
    }
}

TEST_CASE("the calibration offset is the one the design implies") {
    // The LCG source's own update and cast (multipink_processor.cpp:381-390)
    // were run for 5*10^8 samples in a throwaway program outside this suite
    // and measured -4.7712 dB, within 0.00001 dB of 1/sqrt(3) -- so the
    // theoretical value below is a confirmed measurement, not an assumption.
    PinkDesign d;
    d.design(48000.0);
    const double kWhiteRmsDb = 20.0 * std::log10(1.0 / std::sqrt(3.0));
    CHECK(kWhiteRmsDb == doctest::Approx(-4.771).epsilon(0.001));
    CHECK(d.rmsGainDb() == doctest::Approx(-31.409).epsilon(0.001));
    CHECK(-(kWhiteRmsDb + d.rmsGainDb()) == doctest::Approx(36.180).epsilon(0.001));
}
