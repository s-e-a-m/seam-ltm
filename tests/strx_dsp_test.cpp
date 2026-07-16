#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_dsp.h"
#include <cmath>
#include <functional>
#include <vector>

using namespace Seam::strx;

// Drive the analyzer to steady state with a generated stereo signal.
static AnalysisFrame settle(std::function<void(int,float&,float&)> gen) {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(512), R(512);
    for (int blk = 0; blk < 400; ++blk) {              // ~4 s at 48k/512
        for (int i = 0; i < 512; ++i) gen(blk*512+i, L[i], R[i]);
        a.analyzeScalars(L.data(), R.data(), 512);
    }
    return a.frame();
}

TEST_CASE("mono (L=R): correlation ~1, width ~0, side at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = r = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); });
    CHECK(f.correlation == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(0.0).epsilon(0.02));
    CHECK(f.side        <= -55.0f);
    CHECK(f.panorama    == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("anti-phase (L=-R): correlation ~-1, width ~1, mid at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = -l; });
    CHECK(f.correlation == doctest::Approx(-1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.mid         <= -55.0f);
}

TEST_CASE("left only: panorama fully left (~-1)") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = 0.0f; });
    CHECK(f.panorama == doctest::Approx(-1.0).epsilon(0.05));
}

TEST_CASE("goniometer: mono maps to the vertical axis (x~0, y!=0)") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    a.analyze(L.data(), R.data(), 1024);
    const auto& f = a.frame();
    REQUIRE(f.numPoints > 0);
    float maxAbsX = 0, maxAbsY = 0;
    for (int i = 0; i < f.numPoints; ++i) {
        maxAbsX = std::max(maxAbsX, std::fabs(f.gx[i]));
        maxAbsY = std::max(maxAbsY, std::fabs(f.gy[i]));
    }
    CHECK(maxAbsX < 1e-3f);      // S = 0 on the horizontal axis
    CHECK(maxAbsY > 0.1f);       // M carries the energy (vertical)
}

TEST_CASE("spectrum: a 1 kHz mid tone peaks near the 1 kHz bin") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    for (int b = 0; b < 8; ++b) a.analyze(L.data(), R.data(), N);
    const auto& f = a.frame();
    const int expBin = int(std::lround(1000.0 * N / 48000.0));
    int peak = 1;
    for (int k = 2; k < f.numBins; ++k) if (f.specM[k] > f.specM[peak]) peak = k;
    CHECK(std::abs(peak - expBin) <= 2);
}

TEST_CASE("triple-buffer: read after process returns the latest frame") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i) { L[i] = 0.5f; R[i] = 0.0f; }  // left only
    a.process(L.data(), R.data(), 1024);
    AnalysisFrame out;
    REQUIRE(a.tryReadFrame(out));
    CHECK(out.panorama == doctest::Approx(-1.0).epsilon(0.1));
    CHECK_FALSE(a.tryReadFrame(out));    // nothing new since last read
}
