#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_pink.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

// Run the shipped difference equation — Seam::multipink::pinkFilterBlock, the
// same code the processor calls — over a single stream. Templated on the state
// type so that float and double answer the same question.
template <typename T>
double measuredDb(const PinkDesign& d, double f, double fs) {
    const long n = 400000;
    const double w = 2.0 * M_PI * f / fs;
    std::vector<double> buf((size_t)n);
    for (long k = 0; k < n; ++k) buf[(size_t)k] = std::sin(w * (double)k);

    std::vector<T> state((size_t)d.numSections, (T)0);
    Seam::multipink::pinkFilterBlock<double, T>(
        d, buf.data(), 1, (int)n, state.data(), 1);

    double re = 0.0, im = 0.0;
    for (long k = n / 4 + 1; k < n; ++k) {
        re += buf[(size_t)k] * std::cos(w * (double)k);
        im += buf[(size_t)k] * std::sin(w * (double)k);
    }
    const double m = (double)(n - n / 4);
    return 20.0 * std::log10(2.0 * std::sqrt(re * re + im * im) / m);
}

// An INDEPENDENT reference for one stream: sample-outer, section-inner, its
// own state vector, written the other way round from the shared function so
// that a transcription slip in one does not hide in the other. Same recurrence
// and the same rounding, so the two must agree to the bit.
void referenceStream(const PinkDesign& d, float* row, int numSamples,
                     float* state) {
    for (int n = 0; n < numSamples; ++n) {
        float x = row[n];
        for (int i = 0; i < d.numSections; ++i) {
            const float y = (float)d.b0[i] * x + state[i];
            state[i] = (float)d.b1[i] * x - (float)d.a1[i] * y;
            x = y;
        }
        row[n] = x;
    }
}

// A reproducible, per-stream-distinct excitation. Distinct matters: with all
// 64 rows identical, mixing one stream's state into another's is invisible.
std::vector<float> excitation(int numStreams, int numSamples) {
    std::vector<float> v((size_t)numStreams * (size_t)numSamples);
    uint32_t st = 0x9E3779B9u;
    for (size_t i = 0; i < v.size(); ++i) {
        st = st * 1103515245u + 12345u;
        v[i] = (float)((int32_t)st / 2147483648.0);
    }
    return v;
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

// ---------------------------------------------------------------------------
// Calibration.
//
// The design claims one thing and one thing only: the per-third-octave BAND
// level does not move with the sample rate. Everything below tests that claim
// directly, because the claim is what an amplifier setting depends on.
//
// A probe that only counted the FILTER's band integral -- which is genuinely
// rate-invariant -- shipped a fixed 36.180 dB offset and cost 3.01 dB per
// doubling of fs. Measured in Reaper through strx over 76 s and 61 s of
// integration: mean band level -38.4 dB at 48 kHz, -41.3 dB at 96 kHz. The
// missing term is the SOURCE's spectral density: the LCG's RMS is the same at
// every rate, but it is spread over 0..fs/2.
// ---------------------------------------------------------------------------

namespace {

constexpr double kWhiteRmsDb = -4.7712125471966244;   // 20*log10(1/sqrt(3))

// 10*log10 of the integral of |H|^2 over one third-octave band, in Hz. Same
// analytic route as the acceptance test: with white input the output power
// spectrum IS |H|^2, so the band energy is an integral, not a measurement.
double bandIntegralDb(const PinkDesign& d, double fc) {
    const double fl = fc * std::pow(10.0, -0.05);
    const double fu = fc * std::pow(10.0,  0.05);
    const int kSteps = 8192;
    double sum = 0.0;
    for (int k = 0; k < kSteps; ++k) {
        const double f = fl + (fu - fl) * (k + 0.5) / kSteps;
        const double m = std::pow(10.0, d.magnitudeDb(f) / 20.0);
        sum += m * m;
    }
    return 10.0 * std::log10(sum * (fu - fl) / kSteps);
}

// The level the plugin actually puts into one third-octave band, in dBFS, as
// the sum of the four terms the calibration is made of:
//   gain + source RMS + filter band integral - source spectral density.
double predictedBandLevelDb(const PinkDesign& d, double referenceDb, double fc) {
    const double gainDb = referenceDb + d.calibrationOffsetDb();
    return gainDb + kWhiteRmsDb + bandIntegralDb(d, fc)
         - 10.0 * std::log10(d.sampleRate() * 0.5);
}

// Total broadband RMS, in dBFS: gain + source RMS + the filter's RMS gain.
double predictedTotalRmsDb(const PinkDesign& d, double referenceDb) {
    return referenceDb + d.calibrationOffsetDb() + kWhiteRmsDb + d.rmsGainDb();
}

} // namespace

TEST_CASE("the offset's base value is the one the design implies at 48 kHz") {
    // At the reference rate the density term is zero and the offset reduces to
    // the old derivation, so this is the number the base constant must carry.
    // The LCG source's own update and cast (multipink_processor.cpp) were run
    // for 5*10^8 samples in a throwaway program outside this suite and
    // measured -4.7712 dB, within 0.00001 dB of 1/sqrt(3) -- so the value
    // above is a confirmed measurement, not an assumption.
    PinkDesign d;
    d.design(48000.0);
    CHECK(kWhiteRmsDb == doctest::Approx(-4.771).epsilon(0.001));
    CHECK(d.rmsGainDb() == doctest::Approx(-31.409).epsilon(0.001));
    CHECK(-(kWhiteRmsDb + d.rmsGainDb()) == doctest::Approx(36.180).epsilon(0.001));
    CHECK(PinkDesign::kCalibrationOffsetBaseDb == doctest::Approx(36.180).epsilon(1e-9));
    CHECK(d.calibrationOffsetDb() == doctest::Approx(36.180).epsilon(1e-9));
}

TEST_CASE("the offset carries the source's spectral density, 3.01 dB per doubling") {
    for (double fs : kRates) {
        PinkDesign d;
        d.design(fs);
        CHECK(d.calibrationOffsetDb() ==
              doctest::Approx(36.180 + 10.0 * std::log10(fs / 48000.0)).epsilon(1e-9));
    }
    PinkDesign lo, hi;
    lo.design(48000.0);
    hi.design(96000.0);
    CHECK(hi.calibrationOffsetDb() - lo.calibrationOffsetDb()
          == doctest::Approx(3.0103).epsilon(0.001));
}

// The threshold for band-level invariance across sample rates, derived from
// the two bounds that actually constrain it rather than chosen round.
//
// FLOOR -- invariance can never be tighter than the filter's own band ripple.
// Each rate designs its own ladder, with its own section count and its own
// geometric ratio, so a given band sits at a slightly different point of the
// ripple at each rate. multipink_pink_test measures that ripple as a worst
// deviation from the mean of 0.071 to 0.081 dB per rate, which bounds the
// difference between two rates at the same band by twice that, ~0.16 dB.
// Measured over every band measurable at all six rates, the worst spread is
// 0.084 dB, at the 15.85 kHz band where the correction section is weakest.
//
// CEILING -- the smallest error this test exists to catch is one doubling of
// the sample rate with the source's density term missing: 3.01 dB.
//
// 0.20 dB sits between them with an argument on each side: 2.4x above the
// measured floor, 15x below the smallest error it must detect, and below
// SMPTE ST 2095-1's own +/-0.25 dB uniformity tolerance -- so any drift this
// test tolerates cannot by itself push a band out of the standard. It is a
// property of the design, not of whichever band a test happens to pick.
constexpr double kBandInvarianceDb = 0.20;

TEST_CASE("the band level does not move with the sample rate, which is the whole claim") {
    // The test that would have caught the shipped defect. Without the density
    // term it goes red by 3.01 dB per doubling: 3.01 dB at 96 kHz and 6.02 dB
    // at 192 kHz against 48 kHz.
    //
    // Run over EVERY third-octave band measurable at all six rates, not one
    // chosen band: the design claims the property for the whole spectrum, so a
    // test that held only at 1 kHz would be reporting a coincidence. The band
    // is a parameter throughout (predictedBandLevelDb takes fc), and the ceiling
    // is the highest band whose upper edge is inside 0.85*Nyquist at 44.1 kHz,
    // which is the tightest of the six.
    PinkDesign d[6];
    for (int r = 0; r < 6; ++r) d[r].design(kRates[r]);

    std::printf("\n  predicted third-octave band level, Reference = -23 dBFS\n");
    std::printf("  spread across 44.1 ... 192 kHz, threshold %.2f dB\n\n",
                kBandInvarianceDb);
    for (int r = 0; r < 6; ++r)
        std::printf("  %8.1f Hz   offset %7.3f dB   1 kHz band %9.4f dBFS\n",
                    kRates[r], d[r].calibrationOffsetDb(),
                    predictedBandLevelDb(d[r], -23.0, 1000.0));
    std::printf("\n");

    double worst = 0.0, worstFc = 0.0;
    int bands = 0;
    for (int n = -17; n <= 40; ++n) {
        const double fc = 1000.0 * std::pow(10.0, n / 10.0);
        if (fc * std::pow(10.0, 0.05) > 0.85 * 0.5 * 44100.0) break;
        double lo = 1e9, hi = -1e9;
        for (int r = 0; r < 6; ++r) {
            const double lvl = predictedBandLevelDb(d[r], -23.0, fc);
            lo = std::min(lo, lvl);
            hi = std::max(hi, lvl);
        }
        ++bands;
        if (hi - lo > worst) { worst = hi - lo; worstFc = fc; }
        CHECK(hi - lo < kBandInvarianceDb);
    }
    std::printf("  %d bands judged, worst spread %.4f dB at %.1f Hz\n\n",
                bands, worst, worstFc);
    CHECK(bands == 30);
    CHECK(worst < kBandInvarianceDb);
    // The anchor band's absolute level, pinned so a uniform shift is deliberate.
    CHECK(predictedBandLevelDb(d[1], -23.0, 1000.0)
          == doctest::Approx(-39.489).epsilon(0.0002));
}

TEST_CASE("the total RMS is what holding the band level still implies") {
    // The other side of the same coin, and the reason the total RMS is NOT
    // the calibrated quantity: with every band held at the same level, each
    // extra octave of pink adds a little total energy, so the broadband RMS
    // rises 0.28 dB per doubling of fs instead of standing still.
    PinkDesign d48, d96, d192, d441;
    d441.design(44100.0);
    d48.design(48000.0);
    d96.design(96000.0);
    d192.design(192000.0);
    CHECK(predictedTotalRmsDb(d48,  -23.0) == doctest::Approx(-23.000).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d441, -23.0) == doctest::Approx(-23.030).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d96,  -23.0) == doctest::Approx(-22.718).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d192, -23.0) == doctest::Approx(-22.453).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d96, -23.0) - predictedTotalRmsDb(d48, -23.0)
          == doctest::Approx(0.28).epsilon(0.05));
    // The offset is filter-intrinsic, not reference-dependent: changing the
    // reference moves the level by exactly as much.
    CHECK(predictedTotalRmsDb(d48, -18.0) - predictedTotalRmsDb(d48, -23.0)
          == doctest::Approx(5.0).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// The shared block function: the code the processor actually runs.
//
// These exist because a whole-branch review found that swapping the
// state indices, flipping the sign of a1, or dropping the state write-back in
// the shipped loop left the whole suite green. Each of those three mutations
// must now turn one of the checks below red.
// ---------------------------------------------------------------------------

TEST_CASE("64 streams through the shared function match a per-stream reference") {
    // Catches a swapped state index (stream k would carry section k's state,
    // and every row would come out wrong) and a flipped a1 sign.
    PinkDesign d;
    d.design(48000.0);
    constexpr int kStreams = 64, kSamples = 512;

    std::vector<float> block = excitation(kStreams, kSamples);
    std::vector<float> ref   = block;

    std::vector<float> state((size_t)PinkDesign::kMaxSections * kStreams, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, block.data(), kStreams, kSamples, state.data(), kStreams);

    for (int ch = 0; ch < kStreams; ++ch) {
        std::vector<float> refState((size_t)d.numSections, 0.0f);
        referenceStream(d, ref.data() + (size_t)ch * kSamples, kSamples,
                        refState.data());
    }
    // Counted rather than asserted sample by sample: one CHECK reports the
    // first disagreement and how many there were, instead of 32768 of them.
    size_t bad = 0, firstBad = 0;
    for (size_t i = 0; i < block.size(); ++i)
        if (block[i] != ref[i]) { if (!bad) firstBad = i; ++bad; }
    INFO("first mismatch at index ", firstBad);
    CHECK(bad == 0);
}

TEST_CASE("state carries across blocks: one block of N equals two of N/2") {
    // Catches a dropped state write-back, which is invisible inside a single
    // block because the running state lives in a local until the row ends.
    PinkDesign d;
    d.design(48000.0);
    constexpr int kStreams = 8, kSamples = 256, kHalf = kSamples / 2;

    const std::vector<float> src = excitation(kStreams, kSamples);

    std::vector<float> whole = src;
    std::vector<float> stateWhole((size_t)PinkDesign::kMaxSections * kStreams, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, whole.data(), kStreams, kSamples, stateWhole.data(), kStreams);

    // Two halves. The rows are numSamples-strided, so the halves have to be
    // packed as their own kStreams x kHalf buffers rather than sliced.
    std::vector<float> firstHalf((size_t)kStreams * kHalf);
    std::vector<float> secondHalf((size_t)kStreams * kHalf);
    for (int ch = 0; ch < kStreams; ++ch)
        for (int n = 0; n < kHalf; ++n) {
            firstHalf[(size_t)ch * kHalf + n]  = src[(size_t)ch * kSamples + n];
            secondHalf[(size_t)ch * kHalf + n] = src[(size_t)ch * kSamples + kHalf + n];
        }
    std::vector<float> stateSplit((size_t)PinkDesign::kMaxSections * kStreams, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, firstHalf.data(), kStreams, kHalf, stateSplit.data(), kStreams);
    Seam::multipink::pinkFilterBlock<float>(
        d, secondHalf.data(), kStreams, kHalf, stateSplit.data(), kStreams);

    size_t bad = 0;
    int firstCh = -1, firstN = -1;
    for (int ch = 0; ch < kStreams; ++ch)
        for (int n = 0; n < kSamples; ++n) {
            const float split = (n < kHalf)
                ? firstHalf[(size_t)ch * kHalf + n]
                : secondHalf[(size_t)ch * kHalf + (n - kHalf)];
            if (whole[(size_t)ch * kSamples + n] != split) {
                if (!bad) { firstCh = ch; firstN = n; }
                ++bad;
            }
        }
    INFO("first mismatch at stream ", firstCh, " sample ", firstN);
    CHECK(bad == 0);
}

TEST_CASE("a stream's output does not depend on its neighbours") {
    // The other face of a state-index mix-up: filtering stream 3 alone must
    // give what filtering it inside a pool of 64 gives.
    PinkDesign d;
    d.design(96000.0);
    constexpr int kStreams = 64, kSamples = 300, kProbe = 3;

    std::vector<float> pool = excitation(kStreams, kSamples);
    std::vector<float> solo(pool.begin() + (size_t)kProbe * kSamples,
                            pool.begin() + (size_t)(kProbe + 1) * kSamples);

    std::vector<float> statePool((size_t)PinkDesign::kMaxSections * kStreams, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, pool.data(), kStreams, kSamples, statePool.data(), kStreams);

    std::vector<float> stateSolo((size_t)PinkDesign::kMaxSections, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, solo.data(), 1, kSamples, stateSolo.data(), 1);

    size_t bad = 0;
    int firstN = -1;
    for (int n = 0; n < kSamples; ++n)
        if (pool[(size_t)kProbe * kSamples + n] != solo[(size_t)n]) {
            if (!bad) firstN = n;
            ++bad;
        }
    INFO("first mismatch at sample ", firstN);
    CHECK(bad == 0);
}

TEST_CASE("the shared function is the impulse response the analysis describes") {
    // Ties the running code back to PinkDesign::magnitudeDb through a route
    // that does not go past measuredDb: the first sample of the impulse
    // response is the product of the b0 coefficients, and a sign flip on a1
    // moves the tail without touching that, so both are checked.
    PinkDesign d;
    d.design(48000.0);
    // Long enough that truncation does not corrupt the 1 kHz bin: the 2 Hz
    // pole has a ~3800-sample time constant at 48 kHz, and 4096 samples left
    // 0.08 dB of it on the table.
    constexpr int kSamples = 262144;

    std::vector<double> ir((size_t)kSamples, 0.0);
    ir[0] = 1.0;
    std::vector<double> state((size_t)d.numSections, 0.0);
    Seam::multipink::pinkFilterBlock<double, double>(
        d, ir.data(), 1, kSamples, state.data(), 1);

    double b0prod = 1.0;
    for (int i = 0; i < d.numSections; ++i) b0prod *= d.b0[i];
    CHECK(ir[0] == doctest::Approx(b0prod).epsilon(1e-12));

    // DFT of the truncated IR at 1 kHz against the analytic magnitude.
    const double w = 2.0 * M_PI * 1000.0 / 48000.0;
    double re = 0.0, im = 0.0;
    for (int n = 0; n < kSamples; ++n) {
        re += ir[(size_t)n] * std::cos(w * (double)n);
        im -= ir[(size_t)n] * std::sin(w * (double)n);
    }
    const double db = 20.0 * std::log10(std::sqrt(re * re + im * im));
    CHECK(db == doctest::Approx(d.magnitudeDb(1000.0)).epsilon(0.001));
}

TEST_CASE("the state lands where the contract says it does") {
    // The layout is part of the contract -- state[sec*stateStride + stream] --
    // and a transposed index is only invisible while the transpose happens to
    // be injective. Checked here directly, and with stateStride deliberately
    // wider than numStreams so that a transposed write stays inside the buffer
    // and fails as a wrong number rather than as a crash.
    PinkDesign d;
    d.design(48000.0);
    constexpr int kStreams = 16, kStride = 64, kSamples = 128;

    std::vector<float> block = excitation(kStreams, kSamples);
    std::vector<float> ref   = block;
    std::vector<float> state((size_t)PinkDesign::kMaxSections * kStride, 0.0f);
    Seam::multipink::pinkFilterBlock<float>(
        d, block.data(), kStreams, kSamples, state.data(), kStride);

    size_t bad = 0;
    for (int ch = 0; ch < kStreams; ++ch) {
        std::vector<float> refState((size_t)d.numSections, 0.0f);
        referenceStream(d, ref.data() + (size_t)ch * kSamples, kSamples,
                        refState.data());
        for (int sec = 0; sec < d.numSections; ++sec)
            if (state[(size_t)sec * kStride + ch] != refState[(size_t)sec]) ++bad;
    }
    CHECK(bad == 0);
}
