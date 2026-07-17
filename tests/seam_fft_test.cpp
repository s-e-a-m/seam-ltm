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

// Calibration: a full-scale sine's peak bin must read ~0 dBFS (coherent-gain
// normalization), not the raw PSD/noise-power normalization (which reads
// ~+28 dB too high for N=4096-class windows). Half-scale must read ~-6 dB
// (20*log10(0.5)), confirming the calibration is linear in amplitude, not
// just a fixed offset.
TEST_CASE("Welch: full-scale sine peak bin reads ~0 dBFS") {
    const int N = 1024; const double fs = 48000.0;
    seam::fft::Welch w;
    w.prepare(N, 0.05, fs);
    const double f = fs * 64 / N;                 // exactly bin 64
    for (int i = 0; i < N*16; ++i)                 // let the EMA settle
        w.push((float)(1.0 * std::sin(2.0*M_PI*f*i/fs)));
    const float* mag = w.magnitudeDb();
    int peak = 0;
    for (int k = 1; k < w.numBins(); ++k) if (mag[k] > mag[peak]) peak = k;
    CHECK(peak == 64);
    CHECK(std::abs((double)mag[64] - 0.0) < 0.5);   // within 0.5 dB of 0 dBFS
}

TEST_CASE("Welch: half-scale sine peak bin reads ~-6 dBFS") {
    const int N = 1024; const double fs = 48000.0;
    seam::fft::Welch w;
    w.prepare(N, 0.05, fs);
    const double f = fs * 64 / N;                 // exactly bin 64
    for (int i = 0; i < N*16; ++i)                 // let the EMA settle
        w.push((float)(0.5 * std::sin(2.0*M_PI*f*i/fs)));
    const float* mag = w.magnitudeDb();
    int peak = 0;
    for (int k = 1; k < w.numBins(); ++k) if (mag[k] > mag[peak]) peak = k;
    CHECK(peak == 64);
    CHECK(std::abs((double)mag[64] - (-6.0)) < 0.5); // within 0.5 dB of -6 dBFS
}

// A full-scale sine at an exact bin centre, pushed long enough for the EMA to
// settle, then silence. The EMA decays toward the floor; the hold must not.
static void pushSine(seam::fft::Welch& w, double fs, double freq, int nSamples) {
    for (int i = 0; i < nSamples; ++i)
        w.push(float(std::sin(2.0 * M_PI * freq * double(i) / fs)));
}
static void pushSilence(seam::fft::Welch& w, int nSamples) {
    for (int i = 0; i < nSamples; ++i) w.push(0.0f);
}

TEST_CASE("Welch max-hold keeps the peak after the tone stops") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 0.05, fs);              // short tau so the EMA decays fast

    const int    bin  = 64;
    const double freq = double(bin) * fs / double(n);   // exact bin centre
    pushSine(w, fs, freq, n * 20);

    const float peakEma  = w.magnitudeDb()[bin];
    const float peakHold = w.holdDb()[bin];
    CHECK(peakEma  > -6.0f);             // full-scale sine reads ~0 dBFS
    CHECK(peakHold > -6.0f);

    pushSilence(w, n * 40);
    CHECK(w.magnitudeDb()[bin] < -60.0f);   // EMA decayed to the floor
    CHECK(w.holdDb()[bin] == peakHold);     // hold did NOT move
}

TEST_CASE("Welch resetHold clears the hold and leaves the EMA alone") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 2.0, fs);

    const int bin = 64;
    pushSine(w, fs, double(bin) * fs / double(n), n * 20);
    CHECK(w.holdDb()[bin] > -6.0f);

    const float emaBefore = w.magnitudeDb()[bin];
    w.resetHold();
    CHECK(w.holdDb()[bin] == -120.0f);          // hold cleared
    CHECK(w.magnitudeDb()[bin] == emaBefore);   // EMA untouched
}

TEST_CASE("Welch setEmaTau changes the decay rate and nothing else") {
    const double fs = 48000.0;
    const int    n  = 1024;
    const int    bin = 64;
    const double freq = double(bin) * fs / double(n);

    // Same excitation, same silence, two different taus -> the SHORT tau must
    // have decayed further. This is the property glide mode depends on.
    //
    // Excitation length: slow.prepare() keeps its tau = 2 s for this burst
    // (fast switches to 0.1 s below), so the burst must run several of ITS
    // time constants or slow never reaches steady state and starts the
    // silence already far below 0 dBFS, collapsing the margin this check
    // relies on. n*20 (~0.43 s, ~0.2 tau) settles fast (tau 0.1 s) but
    // leaves slow only ~19% converged; n*200 (~4.3 s, ~2 tau) settles both.
    seam::fft::Welch slow, fast;
    slow.prepare(n, 2.0, fs);
    fast.prepare(n, 2.0, fs);
    fast.setEmaTau(0.1);                        // switch at runtime

    pushSine(slow, fs, freq, n * 200);
    pushSine(fast, fs, freq, n * 200);
    pushSilence(slow, n * 10);
    pushSilence(fast, n * 10);

    CHECK(fast.magnitudeDb()[bin] < slow.magnitudeDb()[bin] - 6.0f);

    // The hold is independent of tau: both saw the same peak.
    CHECK(slow.holdDb()[bin] == doctest::Approx(fast.holdDb()[bin]).epsilon(0.01));
}

TEST_CASE("Welch reset clears the hold too") {
    const double fs = 48000.0;
    const int    n  = 1024;
    seam::fft::Welch w;
    w.prepare(n, 2.0, fs);
    pushSine(w, fs, double(64) * fs / double(n), n * 20);
    CHECK(w.holdDb()[64] > -6.0f);
    w.reset();
    CHECK(w.holdDb()[64] == -120.0f);
}
