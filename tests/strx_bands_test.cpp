#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_bands.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace Seam::strx;

namespace {

// Pink noise, the signal this analyser exists to read: white LCG through the
// Paul Kellet 4-pole shaper, the same one multipink emits (noises.lib:402).
struct Pink {
    uint32_t st = 12345;
    double x1 = 0, x2 = 0, x3 = 0, y1 = 0, y2 = 0, y3 = 0;
    static constexpr double B[4] = { 0.049922035, -0.095993537, 0.050612699, -0.004408786 };
    static constexpr double A[3] = { -2.494956002, 2.017265875, -0.522189400 };
    double next() {
        st = st * 1103515245u + 12345u;
        const double x0 = (double)(int32_t)st / 2147483648.0;
        const double y0 = B[0]*x0 + B[1]*x1 + B[2]*x2 + B[3]*x3 - A[0]*y1 - A[1]*y2 - A[2]*y3;
        x3 = x2; x2 = x1; x1 = x0;
        y3 = y2; y2 = y1; y1 = y0;
        return y0;
    }
};

// RBJ peaking EQ, used only to injure the signal on purpose.
struct Peak {
    double b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    Peak(double gainDb, double fc, double Q, double fs) {
        const double A = std::pow(10.0, gainDb/40.0);
        const double w = 2.0*M_PI*fc/fs, al = std::sin(w)/(2.0*Q);
        const double a0 = 1 + al/A;
        b0 = (1 + al*A)/a0; b1 = (-2*std::cos(w))/a0; b2 = (1 - al*A)/a0;
        a1 = (-2*std::cos(w))/a0; a2 = (1 - al/A)/a0;
    }
    double operator()(double x) {
        const double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// Run the analyser on pink noise for `seconds`, optionally injured, and return
// the compensation table. gain scales the signal, to probe level independence.
std::vector<float> measure(double seconds, double gain = 1.0, Peak* defect = nullptr,
                           double fs = 48000.0, int delayR = 0) {
    BandLevels bands;
    bands.prepare(fs);
    Pink pink;
    const int block = 512;
    std::vector<float> L(block), R(block);
    std::vector<double> line(std::max(1, delayR + 1), 0.0);   // delay for R
    size_t w = 0;
    const long total = (long)(seconds * fs);
    for (long n = 0; n < total; n += block) {
        for (int s = 0; s < block; ++s) {
            double v = pink.next() * gain;
            if (defect) v = (*defect)(v);
            L[s] = (float)v;
            line[w] = v;
            w = (w + 1) % line.size();
            R[s] = (float)line[w];      // w now points at the oldest sample
        }
        bands.process(L.data(), R.data(), block);
    }
    std::vector<float> comp(kNumBands), settle(kNumBands);
    bands.compensation(comp.data(), settle.data());
    return comp;
}

int bandIndexFor(double fcNominal) {
    int best = 0;
    double bestd = 1e30;
    for (int i = 0; i < kNumBands; ++i) {
        const double d = std::fabs(std::log(bandFc(i) / fcNominal));
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

} // namespace

TEST_CASE("ISO 266 grid lands on the preferred numbers") {
    CHECK(bandFc(0)  == doctest::Approx(19.9526).epsilon(1e-4));   // nominal 20
    CHECK(bandFc(2)  == doctest::Approx(31.6228).epsilon(1e-4));   // nominal 31.5
    CHECK(bandFc(17) == doctest::Approx(1000.0).epsilon(1e-9));    // the anchor
    CHECK(bandFc(29) == doctest::Approx(15848.93).epsilon(1e-5));  // nominal 16k
    CHECK(bandFc(30) == doctest::Approx(19952.62).epsilon(1e-5));  // nominal 20k
    CHECK(kNumBands == 31);
    // Constant relative bandwidth: 0.2308 of the centre, everywhere.
    for (int i = 0; i < 25; ++i) {
        const double bw = bandFc(i)*std::pow(10.0,0.05) - bandFl(i);
        CHECK(bw / bandFc(i) == doctest::Approx(0.230768).epsilon(1e-4));
    }
}

TEST_CASE("per-band averaging time equalises the bandwidth-time product") {
    const double fs = 48000.0;
    CHECK(bandTau(17, fs) == doctest::Approx(0.2167).epsilon(1e-3));  // 1 kHz
    CHECK(bandTau(0,  fs) == doctest::Approx(10.859).epsilon(1e-3));  // 20 Hz
    // BT is the invariant wherever tau is free: every band buys the same
    // confidence. Above ~2.2 kHz the 0.1 s floor takes over, and there BT can
    // only grow -- the clamp buys MORE confidence than asked for, never less,
    // which is the only direction that is safe to clamp in.
    for (int i = 0; i < kNumBands; ++i) {
        const double bw = bandFu(i, fs) - bandFl(i);
        const double bt = bandTau(i, fs) * bw;
        INFO("band ", i, " (", bandFc(i), " Hz) BT = ", bt);
        if (bandTau(i, fs) > 0.1) CHECK(bt == doctest::Approx(kBandBT).epsilon(1e-9));
        else                      CHECK(bt > kBandBT);
    }
    CHECK(bandTau(20, fs) > 0.1);    // 2 kHz: still free
    CHECK(bandTau(21, fs) == doctest::Approx(0.1));   // 2.5 kHz: on the floor
}

TEST_CASE("band filter is a faithful port of fi.bandpass6e") {
    // Reference: faust -double, fi.bandpass6e(891.2509381337456,
    // 1122.0184543019633) at 48 kHz, impulse response captured 2026-08-18.
    static const double kRef[6] = {
        0.00029580935378616157, 0.00058742829587159380, 0.00058876471558640976,
        0.00060790929151671876, 0.00064105788779269949, 0.00068341055508245707,
    };
    Bandpass6e bp;
    bp.design(891.2509381337456, 1122.0184543019633, 48000.0);
    for (int i = 0; i < 6; ++i) {
        const double y = bp.process(i == 0 ? 1.0 : 0.0);
        CHECK(y == doctest::Approx(kRef[i]).epsilon(1e-9));
    }
}

TEST_CASE("pink noise reads flat — that is what makes it the reference") {
    const auto comp = measure(40.0);
    for (int i = 0; i < kNumBands; ++i) {
        INFO("band ", i, " (", bandFc(i), " Hz) = ", comp[i], " dB");
        CHECK(std::fabs(comp[i]) < 1.5);
    }
    // Away from the extremes, where the mean is taken, it is tighter still.
    for (int i = kNormI0; i < kNormI0 + kNormNi; ++i) CHECK(std::fabs(comp[i]) < 1.2);
}

TEST_CASE("the table ignores absolute level") {
    const auto a = measure(20.0, 1.0);
    const auto b = measure(20.0, std::pow(10.0, 12.0/20.0));   // +12 dB
    for (int i = 0; i < kNumBands; ++i)
        CHECK(a[i] == doctest::Approx(b[i]).epsilon(1e-4));
}

TEST_CASE("an injected dip comes back as a positive compensation, in its band") {
    const double fs = 48000.0;
    Peak dip(-8.0, 125.0, 3.0, fs);
    const auto comp = measure(40.0, 1.0, &dip, fs);
    const int i125 = bandIndexFor(125.0);
    INFO("125 Hz band index ", i125, " reads ", comp[i125]);
    CHECK(comp[i125] > 2.0);                       // says "raise here"
    CHECK(comp[i125] < 8.0);                       // band-integrated, so under
    // It is the largest correction anywhere, and the far field is untouched.
    for (int i = 0; i < kNumBands; ++i)
        if (std::abs(i - i125) > 3) CHECK(comp[i] < comp[i125]);
    CHECK(std::fabs(comp[bandIndexFor(4000.0)]) < 1.5);
}

TEST_CASE("a spaced pair stays flat — the reason the two capsules are averaged "
          "in power and never summed to Mid") {
    // 40 cm between the capsules is 1.17 ms of path difference, so a Mid sum
    // (L+R) combs with its first null at c/2d = 429 Hz and every odd multiple
    // above it. Averaging the two band POWERS is immune: the null lives in the
    // sum, not in either capsule. This test is the whole geometric argument,
    // and it only bites because the two channels are delayed, not merely
    // different -- with L == R a Mid sum would pass it.
    const double fs = 48000.0;
    const int delay = (int)std::lround(0.40 / 343.0 * fs);   // 56 samples
    const auto comp = measure(40.0, 1.0, nullptr, fs, delay);
    for (int i = kNormI0; i < kNormI0 + kNormNi; ++i) {
        INFO("band ", i, " (", bandFc(i), " Hz) = ", comp[i], " dB");
        CHECK(std::fabs(comp[i]) < 1.2);
    }
}

TEST_CASE("a band whose passband runs into Nyquist is not reported at all") {
    // 20 kHz spans 17783..22387 Hz. At 44.1 kHz that upper edge is ABOVE
    // Nyquist and at 48 kHz it is at 0.93 of it, close enough that the
    // bilinear transform warps the design: measured on pink, the band read
    // -3.01 dB at 44.1 kHz and -0.65 dB at 48 kHz with nothing wrong upstream.
    CHECK(measurableBands(96000.0) == kNumBands);   // 20 kHz is at 0.47, fine
    CHECK(measurableBands(48000.0) == 30);          // 20 kHz band withheld
    CHECK(measurableBands(44100.0) == 30);
    CHECK(bandFuNominal(30) > kNyquistMargin * 0.5 * 48000.0);
    CHECK(bandFuNominal(29) < kNyquistMargin * 0.5 * 44100.0);
    BandLevels b;
    b.prepare(44100.0);
    CHECK(b.count() == 30);
    b.prepare(96000.0);
    CHECK(b.count() == kNumBands);
}

TEST_CASE("what IS reported at 44.1 kHz is clean") {
    // The regression this guards: before the Nyquist gate, the top band read
    // -3 dB on a signal that was exactly pink, and the number looked usable.
    const auto comp = measure(40.0, 1.0, nullptr, 44100.0);
    const int nb = measurableBands(44100.0);
    for (int i = 0; i < nb; ++i) {
        INFO("band ", i, " (", bandFc(i), " Hz) = ", comp[i], " dB");
        CHECK(std::fabs(comp[i]) < 1.5);
    }
}
