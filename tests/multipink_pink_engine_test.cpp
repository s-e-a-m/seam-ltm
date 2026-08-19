#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "multipink_pink.h"
#include <algorithm>
#include <cmath>
#include <complex>
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
    // A property of the FILTER, and of the filter alone: it is anchored in Hz,
    // so its gain at 1 kHz is the same at every rate. Measured spread across
    // the six rates is under 0.01 dB.
    //
    // Reading that as a property of the OUTPUT is what cost this project
    // 3.01 dB per doubling of fs. A rate-invariant gain at 1 kHz delivers a
    // rate-invariant band level only if what it multiplies is also
    // rate-invariant, and the source's spectral DENSITY is not: the LCG's RMS
    // is fixed but spread over 0..fs/2, so it halves when fs doubles. The
    // filter stood still and the band level fell anyway. What corrects it is
    // the density term in calibrationOffsetDb(); what tests it is the
    // end-to-end case at the bottom of this file, which measures the band
    // level of the real source through the real filter rather than assuming
    // the source's contribution.
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
// One continuous run of the shipped source, so the rows differ by construction
// and this file holds no second copy of the recurrence.
std::vector<float> excitation(int numStreams, int numSamples) {
    std::vector<float> v((size_t)numStreams * (size_t)numSamples);
    Seam::multipink::whiteNoiseRow<float>(0x9E3779B9u, v.data(), (int)v.size());
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
// ripple at each rate. The load-bearing number is the MEASURED one: over
// every band measurable at all six rates, the worst spread is 0.0837 dB, at
// the 15.85 kHz band where the correction section is weakest.
//
// multipink_pink_test's per-rate acceptance ripple -- worst deviation from the
// mean, 0.071 to 0.081 dB -- is CONSISTENT WITH that, and no more. It is not a
// bound on it: the acceptance figure is a deviation from a per-rate mean over
// a per-rate set of judged bands, while this test compares absolute levels at
// one fixed band across rates. Doubling the one to get ~0.16 dB is a heuristic
// that happens to bracket the measurement, not a derivation of it.
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
    // rises about 0.27-0.28 dB per doubling of fs instead of standing still.
    // It is not one number: 0.2825 dB from 48 to 96 kHz, 0.2844 from 44.1 to
    // 88.2, 0.2652 from 96 to 192, because the ladder's top section moves.
    PinkDesign d48, d96, d192, d441;
    d441.design(44100.0);
    d48.design(48000.0);
    d96.design(96000.0);
    d192.design(192000.0);
    CHECK(predictedTotalRmsDb(d48,  -23.0) == doctest::Approx(-23.000).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d441, -23.0) == doctest::Approx(-23.030).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d96,  -23.0) == doctest::Approx(-22.718).epsilon(0.001));
    CHECK(predictedTotalRmsDb(d192, -23.0) == doctest::Approx(-22.453).epsilon(0.001));
    // Absolute, like the six pinned acceptance values and unlike what this
    // line used to be: Approx(0.28).epsilon(0.05) compares against
    // eps*(1 + max(|a|,|b|)), so on a value well under 1 it admitted +/-0.064,
    // which is +/-23% -- the same relative-tolerance trap this branch already
    // had to fix once. The step is 0.2825 dB; 0.005 dB is a real tolerance.
    CHECK(std::fabs((predictedTotalRmsDb(d96, -23.0) - predictedTotalRmsDb(d48, -23.0))
                    - 0.2825) < 0.005);
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

// ---------------------------------------------------------------------------
// END TO END: the SOURCE, measured rather than assumed.
//
// Everything above this line is analytic. predictedBandLevelDb() computes the
// filter term from magnitudeDb() and takes the source's contribution as a
// GIVEN: -4.7712 dB of RMS, flat, spread over 0..fs/2. That assumption is
// correct, and it is also exactly the term the original derivation forgot --
// so the suite that was meant to protect the calibration could not have
// caught its own defect. Change the LCG multiplier, change the cast divisor,
// add a DC offset, and all 41 analytic assertions stay green while every
// plug-in in the room is miscalibrated.
//
// This case closes that hole by running the real chain, in the order the
// audio thread runs it:
//
//   whiteNoiseRow  ->  pinkFilterBlock  ->  x gain
//
// and reading the third-octave band level back out of the samples. Nothing
// about the source is assumed; if the source changes, this goes red.
//
// HOW THE BAND LEVEL IS MEASURED. Welch: Hann-windowed segments of 16384 with
// 50% overlap, averaged, normalised so that the sum over all bins of one
// segment recovers the signal's mean square (2*|X[k]|^2 / (L * sum w^2)).
// The band is the SAME ideal rectangle the analytic prediction integrates
// over -- [fc*10^-0.05, fc*10^0.05] -- and bins straddling an edge are
// weighted by the fraction of their width that falls inside it, so the
// comparison is not limited by bin quantisation (0.4% of the band width at
// 1 kHz, which alone would be 0.02 dB). A real third-octave filter would have
// been the wrong instrument here: its skirts make its effective bandwidth
// something other than the rectangle the prediction assumes.
//
// HOW MUCH SIGNAL, AND WHY. 120 seconds at each rate: 5,760,000 samples at
// 48 kHz and 11,520,000 at 96 kHz. This is a measurement on noise, so its
// precision is set by the band-time product. The 1 kHz third-octave band is
// 230.8 Hz wide, so BT = 230.8 * 120 = 27,700, and the standard deviation of
// a chi-square-distributed band-power estimate is 10*log10(e)/sqrt(BT) =
// 4.34/166 = 0.026 dB. The 0.10 dB tolerance is therefore about 3.8 sigma.
// That figure is not just arithmetic: run over ten independent seeds at half
// this length the measured spread was 0.0388 dB at 1 kHz and 0.0124 dB at
// 4 kHz at 48 kHz, and 0.0374 / 0.0247 dB at 96 kHz -- consistent with the
// 0.037 and 0.013 that BT predicts at that length, and halving to 0.026 at
// 1 kHz here. The mean deviation from the analytic prediction over those ten
// seeds was +0.000 dB at 1 kHz: the estimator is unbiased, so the tolerance
// is spent on spread and not on a systematic offset.
//
// WHICH BANDS. 1 kHz and 4 kHz. BT falls with the band's width, so the same
// 120 s buys only BT = 6,900 at 250 Hz (sigma 0.052 dB, measured 0.075 dB at
// half length) and the tolerance would have to be loosened to about 0.2 dB to
// stay honest -- which is the analytic tests' job and not this one's. What
// this case exists to catch is a wrong SOURCE, and a wrong source is wrong at
// every band at once: the mutation it is written against, a cast divisor of
// 2^30 instead of 2^31, is 6.02 dB.
// ---------------------------------------------------------------------------

namespace {

// Iterative radix-2 FFT, in double, with the twiddles precomputed once.
void fftInPlace(std::complex<double>* a, int n, const std::complex<double>* tw) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + half] * tw[k * step];
                a[i + k]        = u + v;
                a[i + k + half] = u - v;
            }
    }
}

constexpr int    kWelchLen     = 16384;
constexpr double kMeasureSecs  = 120.0;
constexpr double kEndToEndDb   = 0.10;

// Run the shipped chain for kMeasureSecs and return the averaged one-sided
// power spectrum, bin k centred on k*fs/kWelchLen.
std::vector<double> measureSpectrum(const PinkDesign& d, double referenceDb,
                                    uint32_t seed) {
    const double fs  = d.sampleRate();
    // The gain stage, written exactly as MULTIPINKProcessor::computeGainLin
    // writes it: Reference + Trim + offset(fs), with Trim at 0.
    const double gain = std::pow(10.0, (referenceDb + d.calibrationOffsetDb()) / 20.0);
    const long   n    = (long)(kMeasureSecs * fs);
    const int    hop  = kWelchLen / 2;

    std::vector<double> w((size_t)kWelchLen);
    double sumW2 = 0.0;
    for (int i = 0; i < kWelchLen; ++i) {
        w[(size_t)i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / kWelchLen);
        sumW2 += w[(size_t)i] * w[(size_t)i];
    }
    std::vector<std::complex<double>> tw((size_t)kWelchLen / 2);
    for (int k = 0; k < kWelchLen / 2; ++k)
        tw[(size_t)k] = std::complex<double>(std::cos(-2.0 * M_PI * k / kWelchLen),
                                             std::sin(-2.0 * M_PI * k / kWelchLen));

    // One stream, float state and float samples: what the plug-in runs at both
    // sample sizes. The filter state carries across calls, so producing the
    // signal a block at a time is the same signal the processor would emit.
    std::vector<float> state((size_t)d.numSections, 0.0f);
    uint32_t lcg = seed;
    std::vector<float> buf((size_t)kWelchLen, 0.0f);
    auto produce = [&](float* p, int count) {
        lcg = Seam::multipink::whiteNoiseRow<float>(lcg, p, count);
        Seam::multipink::pinkFilterBlock<float>(d, p, 1, count, state.data(), 1);
        for (int i = 0; i < count; ++i) p[i] = (float)(p[i] * gain);
    };

    std::vector<std::complex<double>> seg((size_t)kWelchLen);
    std::vector<double> acc((size_t)kWelchLen / 2, 0.0);
    int segments = 0;
    produce(buf.data(), kWelchLen);
    for (long produced = kWelchLen; ; produced += hop) {
        for (int i = 0; i < kWelchLen; ++i)
            seg[(size_t)i] = std::complex<double>(buf[(size_t)i] * w[(size_t)i], 0.0);
        fftInPlace(seg.data(), kWelchLen, tw.data());
        for (int k = 0; k < kWelchLen / 2; ++k)
            acc[(size_t)k] += std::norm(seg[(size_t)k]);
        ++segments;
        if (produced + hop > n) break;
        for (int i = 0; i < kWelchLen - hop; ++i) buf[(size_t)i] = buf[(size_t)(i + hop)];
        produce(buf.data() + (kWelchLen - hop), hop);
    }
    for (double& v : acc) v /= (double)segments;
    return acc;
}

// Integrate the measured spectrum over one third-octave band, edge bins
// weighted by the fraction of their width inside the band.
double measuredBandLevelDb(const std::vector<double>& spectrum, double fs, double fc) {
    const double fl = fc * std::pow(10.0, -0.05);
    const double fu = fc * std::pow(10.0,  0.05);
    const double df = fs / kWelchLen;
    double sumW2 = 0.0;
    for (int i = 0; i < kWelchLen; ++i) {
        const double v = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / kWelchLen);
        sumW2 += v * v;
    }
    double power = 0.0;
    for (int k = 1; k < kWelchLen / 2; ++k) {
        const double lo = (k - 0.5) * df, hi = (k + 0.5) * df;
        const double overlap = std::min(hi, fu) - std::max(lo, fl);
        if (overlap <= 0.0) continue;
        power += (overlap / df) * 2.0 * spectrum[(size_t)k]
               / ((double)kWelchLen * sumW2);
    }
    return 10.0 * std::log10(power);
}

} // namespace

TEST_CASE("the band level the plugin really emits is the one the design predicts") {
    // The seed only sets where in the LCG's 2^32-long period the stream
    // starts; the calibration claim is a property of the source's statistics
    // and holds for any seed, which was checked over ten of them.
    constexpr uint32_t kSeed = 0x9E3779B9u;
    const double kBands[] = {1000.0, 4000.0};

    PinkDesign d48, d96;
    d48.design(48000.0);
    d96.design(96000.0);

    const std::vector<double> s48 = measureSpectrum(d48, -23.0, kSeed);
    const std::vector<double> s96 = measureSpectrum(d96, -23.0, kSeed);

    std::printf("\n  end to end: whiteNoiseRow -> pinkFilterBlock -> gain,\n");
    std::printf("  %.0f s per rate, Reference = -23 dBFS, tolerance %.2f dB\n\n",
                kMeasureSecs, kEndToEndDb);

    for (double fc : kBands) {
        const double m48 = measuredBandLevelDb(s48, 48000.0, fc);
        const double m96 = measuredBandLevelDb(s96, 96000.0, fc);
        const double p48 = predictedBandLevelDb(d48, -23.0, fc);
        const double p96 = predictedBandLevelDb(d96, -23.0, fc);
        std::printf("  %7.1f Hz   48 kHz measured %9.4f  predicted %9.4f  diff %+7.4f\n",
                    fc, m48, p48, m48 - p48);
        std::printf("  %7.1f Hz   96 kHz measured %9.4f  predicted %9.4f  diff %+7.4f\n",
                    fc, m96, p96, m96 - p96);
        std::printf("  %7.1f Hz   96 kHz - 48 kHz measured %+7.4f dB\n\n", fc, m96 - m48);

        // The measurement agrees with the analytic prediction. This is what a
        // wrong source breaks: the prediction reads the source's level off a
        // constant, the measurement reads it off the samples.
        CHECK(std::fabs(m48 - p48) < kEndToEndDb);
        CHECK(std::fabs(m96 - p96) < kEndToEndDb);
        // And the two rates agree with each other, which is the claim itself,
        // now made on measured signal rather than on integrals. Without the
        // density term in calibrationOffsetDb() this difference is -3.01 dB.
        CHECK(std::fabs(m96 - m48) < kEndToEndDb);
    }
}
