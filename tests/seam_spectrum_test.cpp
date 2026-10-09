#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_spectrum.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using Spec4 = Seam::MultiSpectrum<4>;

// One block of four channels; channel `ch` carries a sine of amplitude `a`
// at `hz`, the others silence. `t` is the running sample index.
template <class T = float>
static void feed(Spec4& s, double fs, double seconds, int ch, double hz, double a, long& t, int block = 512) {
    std::vector<std::vector<T>> buf(4, std::vector<T>((size_t)block, (T)0));
    const T* p[4] = { buf[0].data(), buf[1].data(), buf[2].data(), buf[3].data() };
    const long n = (long)(seconds * fs);
    for (long pos = 0; pos < n; pos += block) {
        const int m = (int)std::min<long>(block, n - pos);
        for (int i = 0; i < m; ++i)
            buf[(size_t)ch][(size_t)i] = (T)(a * std::sin(2 * M_PI * hz * (double)(t + i) / fs));
        t += m;
        s.process(p, m);
    }
}

static float peakNear(const Seam::SpectrumFrame<4>& f, int ch, double hz, double fs) {
    const int fft = 2 * (f.numBins - 1);
    const int k = (int)std::lround(hz * fft / fs);
    float m = -200.0f;
    for (int j = std::max(0, k - 2); j <= std::min(f.numBins - 1, k + 2); ++j) m = std::max(m, f.db[ch][j]);
    return m;
}

static float loudest(const Seam::SpectrumFrame<4>& f, int ch) {
    float m = -200.0f;
    for (int j = 0; j < f.numBins; ++j) m = std::max(m, f.db[ch][j]);
    return m;
}

TEST_CASE("the FFT grows with the rate: the resolution in Hz is the same at every rate") {
    CHECK(Seam::spectrumFftSize(44100.0)  == 4096);
    CHECK(Seam::spectrumFftSize(48000.0)  == 4096);
    CHECK(Seam::spectrumFftSize(88200.0)  == 8192);
    CHECK(Seam::spectrumFftSize(96000.0)  == 8192);
    CHECK(Seam::spectrumFftSize(192000.0) == 16384);
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        CHECK(fs / Seam::spectrumFftSize(fs) < 12.0);
}

TEST_CASE("a sine on one channel reads its level there and nothing on the others") {
    for (double fs : {48000.0, 96000.0}) {
        auto s = std::make_unique<Spec4>(); s->prepare(fs, 0.3);
        // On a bin's centre (85 bins, ~996 Hz): between two bins Hann loses
        // up to 1.42 dB (scalloping), as in strx.
        const double hz = 85.0 * fs / Seam::spectrumFftSize(fs);
        long t = 0;
        feed(*s, fs, 2.0, 2, hz, 0.5, t);
        auto f = std::make_unique<Seam::SpectrumFrame<4>>();
        REQUIRE(s->tryRead(*f));
        CHECK(f->numBins == Seam::spectrumFftSize(fs) / 2 + 1);
        CHECK(peakNear(*f, 2, hz, fs) == doctest::Approx(20 * std::log10(0.5)).epsilon(0.01));
        for (int c : {0, 1, 3}) CHECK(loudest(*f, c) <= -119.0f);
    }
}

TEST_CASE("a double bus reads as a float one") {
    auto a = std::make_unique<Spec4>(), b = std::make_unique<Spec4>();
    a->prepare(48000.0, 0.3); b->prepare(48000.0, 0.3);
    long ta = 0, tb = 0;
    feed<float>(*a, 48000.0, 1.0, 0, 440.0, 0.25, ta);
    feed<double>(*b, 48000.0, 1.0, 0, 440.0, 0.25, tb);
    auto fa = std::make_unique<Seam::SpectrumFrame<4>>(), fb = std::make_unique<Seam::SpectrumFrame<4>>();
    REQUIRE(a->tryRead(*fa)); REQUIRE(b->tryRead(*fb));
    CHECK(peakNear(*fa, 0, 440.0, 48000.0) == doctest::Approx(peakNear(*fb, 0, 440.0, 48000.0)).epsilon(1e-4));
}

TEST_CASE("a frame is read once; nothing new reads false") {
    auto s = std::make_unique<Spec4>(); s->prepare(48000.0, 0.3);
    auto f = std::make_unique<Seam::SpectrumFrame<4>>();
    CHECK_FALSE(s->tryRead(*f));
    long t = 0;
    feed(*s, 48000.0, 0.5, 0, 1000.0, 0.5, t);
    CHECK(s->tryRead(*f));
    CHECK_FALSE(s->tryRead(*f));
}

TEST_CASE("the four channels take their FFTs in turn, never in the same sample") {
    // Hops staggered by a quarter: at 96 kHz an 8192-point FFT per channel
    // would otherwise land on the same sample four times over.
    auto s = std::make_unique<Spec4>(); s->prepare(96000.0, 0.3);
    std::vector<float> z(1, 0.1f);
    const float* p[4] = { z.data(), z.data(), z.data(), z.data() };
    long before = s->fftCount();
    int most = 0;
    for (int i = 0; i < 3 * 8192; ++i) {
        s->process(p, 1);
        const long now = s->fftCount();
        most = std::max(most, (int)(now - before));
        before = now;
    }
    CHECK(most == 1);
    CHECK(s->fftCount() >= 4 * 5);     // every channel still runs once per hop
}

TEST_CASE("the display follows the music: a stopped tone falls 20 dB within 2 s") {
    auto s = std::make_unique<Spec4>(); s->prepare(48000.0, 0.3);
    auto f = std::make_unique<Seam::SpectrumFrame<4>>();
    long t = 0;
    feed(*s, 48000.0, 2.0, 1, 500.0, 0.5, t);
    REQUIRE(s->tryRead(*f));
    const float on = peakNear(*f, 1, 500.0, 48000.0);
    feed(*s, 48000.0, 2.0, 1, 500.0, 0.0, t);
    REQUIRE(s->tryRead(*f));
    CHECK(on - peakNear(*f, 1, 500.0, 48000.0) > 20.0f);
}

TEST_CASE("an unprepared analyser ignores the audio") {
    auto s = std::make_unique<Spec4>();
    std::vector<float> z(64, 0.5f);
    const float* p[4] = { z.data(), z.data(), z.data(), z.data() };
    s->process(p, 64);
    auto f = std::make_unique<Seam::SpectrumFrame<4>>();
    CHECK_FALSE(s->tryRead(*f));
    CHECK(s->sampleRate() == 0.0);
}
