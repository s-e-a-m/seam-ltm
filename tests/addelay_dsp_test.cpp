#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "addelay_dsp.h"
#include <vector>
#include <cmath>

using namespace Seam::addelay;
using Seam::air::Topology;

TEST_CASE("delay length = nextPrime(round(d*SR/331.4))") {
    AirDelay dsp; dsp.prepare(48000.0);
    dsp.setParams(10.0, 20.0, 50.0, Topology::Shelf, false);
    const int raw = (int)std::lround(10.0 * 48000.0 / 331.4);   // ~1449
    CHECK(dsp.delaySamples() == nextPrime(raw));
    dsp.setParams(0.0, 20.0, 50.0, Topology::Shelf, false);
    CHECK(dsp.delaySamples() == 0);   // zero distance = no delay
}

TEST_CASE("all four channels receive the identical delay and filter") {
    AirDelay dsp; dsp.prepare(48000.0);
    dsp.setParams(5.0, 20.0, 50.0, Topology::Cascade, false);
    const int N = 2048, D = dsp.delaySamples();
    std::vector<float> in[4], out[4];
    for (int c = 0; c < 4; ++c) { in[c].assign(N, 0.0f); out[c].assign(N, 0.0f); }
    for (int c = 0; c < 4; ++c) in[c][0] = 1.0f;               // identical impulse
    const float* ip[4] = {in[0].data(),in[1].data(),in[2].data(),in[3].data()};
    float* op[4] = {out[0].data(),out[1].data(),out[2].data(),out[3].data()};
    dsp.process(ip, op, N);
    for (int c = 1; c < 4; ++c)
        for (int i = 0; i < N; ++i)
            CHECK(out[c][i] == doctest::Approx(out[0][i]));      // bit-for-bit equal path
    // impulse arrives no earlier than the delay (filter adds no pre-echo).
    for (int i = 0; i < D; ++i) CHECK(out[0][i] == doctest::Approx(0.0f));
}

TEST_CASE("spreading toggles a broadband level drop") {
    AirDelay dsp; dsp.prepare(48000.0);
    const int N = 4096;
    auto energy = [&](bool spread) {
        dsp.setParams(8.0, 20.0, 50.0, Topology::Cascade, spread);
        dsp.reset();
        std::vector<float> in(N, 0.0f);
        for (int i = 0; i < N; ++i) in[i] = std::sin(2.0 * M_PI * 100.0 * i / 48000.0);
        const float* ip[4] = {in.data(),in.data(),in.data(),in.data()};
        // distinct output buffers per channel (read/write aliasing avoided)
        std::vector<float> o0(N,0),o1(N,0),o2(N,0),o3(N,0);
        float* op2[4] = {o0.data(),o1.data(),o2.data(),o3.data()};
        dsp.process(ip, op2, N);
        double e = 0; for (int i = 0; i < N; ++i) e += o0[i]*o0[i];
        return e;
    };
    const double dry = energy(false), wet = energy(true);
    CHECK(wet < dry);                                  // 8 m => 1/8 gain at 100 Hz (below absorption)
    CHECK(wet == doctest::Approx(dry / 64.0).epsilon(0.15)); // (1/8)^2 in energy
}
