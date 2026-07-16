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
