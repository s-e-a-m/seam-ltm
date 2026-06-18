#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "hilbert_dsp.h"
#include <cmath>

using namespace Seam::hilbert;

TEST_CASE("HilbertTransformer defaults to RBJ and tracks topology changes") {
    HilbertTransformer h;
    h.prepare(48000.0);
    CHECK(h.topology() == Topology::RBJ);
    h.setTopology(Topology::Polyphase);
    CHECK(h.topology() == Topology::Polyphase);
    CHECK(h.sampleRate() == 48000.0);
}

TEST_CASE("HilbertTransformer holds converged designs for both topologies") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        HilbertTransformer h;
        h.prepare(fs);
        CHECK(h.rbjDesign().converged);
        CHECK(h.rbjDesign().maxErrorDeg < 2.5);
        CHECK(h.polyDesign().converged);
        CHECK(h.polyDesign().maxErrorDeg < 4.0);
    }
}

// Steady-state phase difference (deg) between the two outputs at frequency f.
static double phaseDiffDeg(HilbertTransformer& h, double f, double fs) {
    const double w = 2.0 * M_PI * f / fs;
    h.reset();
    for (int n = 0; n < 8192; ++n) { double a, b; h.process(std::sin(w * n), a, b); }
    double reI = 0, imI = 0, reQ = 0, imQ = 0;
    const int N = int(std::round(64 * fs / f));
    for (int n = 0; n < N; ++n) {
        double a, b; h.process(std::sin(w * (8192 + n)), a, b);
        reI += a * std::cos(w * n); imI -= a * std::sin(w * n);
        reQ += b * std::cos(w * n); imQ -= b * std::sin(w * n);
    }
    double d = (std::atan2(imQ, reQ) - std::atan2(imI, reI)) * 180.0 / M_PI;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

TEST_CASE("HilbertTransformer outputs hold -90 deg in both topologies") {
    const double fs = 48000.0;
    HilbertTransformer h;
    h.prepare(fs);
    for (auto topo : {Topology::RBJ, Topology::Polyphase}) {
        h.setTopology(topo);
        for (double f : {100.0, 1000.0, 10000.0})
            CHECK(std::fabs(phaseDiffDeg(h, f, fs) - (-90.0)) <= 3.0);
    }
}
