#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_fft.h"
#include <cmath>
#include <vector>

using seam::fft::transform;

// helper: fill interleaved complex from a real signal
static std::vector<float> cplx(const std::vector<float>& re) {
    std::vector<float> d(re.size() * 2, 0.0f);
    for (size_t i = 0; i < re.size(); ++i) d[2*i] = re[i];
    return d;
}

TEST_CASE("FFT of a DC signal puts all energy in bin 0") {
    std::vector<float> re(8, 1.0f);
    auto d = cplx(re);
    transform(d.data(), 8, true);
    CHECK(d[0] == doctest::Approx(8.0f));       // bin 0 real = sum
    CHECK(d[1] == doctest::Approx(0.0f));
    for (int k = 1; k < 8; ++k)
        CHECK(std::hypot(d[2*k], d[2*k+1]) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("FFT of a full-scale cosine peaks at its bin (k=1)") {
    const int N = 16;
    std::vector<float> re(N);
    for (int n = 0; n < N; ++n) re[n] = std::cos(2.0 * M_PI * 1 * n / N);
    auto d = cplx(re);
    transform(d.data(), N, true);
    CHECK(std::hypot(d[2*1], d[2*1+1]) == doctest::Approx(N/2.0).epsilon(1e-4));
    CHECK(std::hypot(d[2*(N-1)], d[2*(N-1)+1]) == doctest::Approx(N/2.0).epsilon(1e-4));
}

TEST_CASE("forward then inverse (with 1/N) recovers the signal") {
    const int N = 8;
    std::vector<float> re = {1,2,3,4,4,3,2,1};
    auto d = cplx(re);
    transform(d.data(), N, true);
    transform(d.data(), N, false);
    for (int n = 0; n < N; ++n)
        CHECK(d[2*n]/N == doctest::Approx(re[n]).epsilon(1e-4));
}

#include <cstdio>
TEST_CASE("Welch: a steady sine peaks at the expected bin") {
    const int N = 1024; const double fs = 48000.0;
    seam::fft::Welch w;
    w.prepare(N, 0.05, fs);
    const double f = fs * 64 / N;                 // exactly bin 64
    for (int i = 0; i < N*8; ++i)
        w.push((float)std::sin(2.0*M_PI*f*i/fs));
    const float* mag = w.magnitudeDb();
    int peak = 0;
    for (int k = 1; k < w.numBins(); ++k) if (mag[k] > mag[peak]) peak = k;
    CHECK(peak == 64);
}

TEST_CASE("Welch: silence sits at/below the dB floor") {
    seam::fft::Welch w;
    w.prepare(256, 0.05, 48000.0);
    for (int i = 0; i < 256*8; ++i) w.push(0.0f);
    const float* mag = w.magnitudeDb();
    for (int k = 0; k < w.numBins(); ++k) CHECK(mag[k] <= -90.0f);
}
