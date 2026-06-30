#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ltburst_dsp.h"
#include <cmath>

using namespace Seam::ltburst;

// fs=48000, f0=1000, dwell=300 ms -> M=ceil(0.3*1000)=300, P=N+M=305.
// One period is P*fs/f0 = 305*48000/1000 = 14640 samples (integer).
static ShapedBurst makeDefault() {
    ShapedBurst b;
    b.prepare(48000.0);
    b.setFrequency(1000.0);
    b.setDwell(300.0);
    b.reset();
    return b;
}

TEST_CASE("period geometry: N=5, P=N+M, M from ceil(dwell*f0)") {
    ShapedBurst b = makeDefault();
    CHECK(ShapedBurst::kN == 5);
    CHECK(b.periodCycles() == 305);   // 5 + 300
    CHECK(b.frequency() == doctest::Approx(1000.0));
    CHECK(b.dwellMs()   == doctest::Approx(300.0));
}

TEST_CASE("burst starts on a zero crossing") {
    ShapedBurst b = makeDefault();
    // At u=0: win = 0.5-0.5*cos(0)=0 and sin(0)=0 -> output is exactly 0.
    CHECK(b.process() == 0.0);
}

TEST_CASE("output is silent during the dwell (u in [N, P))") {
    ShapedBurst b = makeDefault();
    // Advance to sample 10000: u = (f0/fs)*10000 mod P
    //   = (1000/48000)*10000 = 208.333..., well past N=5 -> window 0.
    double last = 0.0;
    for (int n = 0; n < 10001; ++n) last = b.process();
    // In the dwell region the window is exactly 0 -> output is exactly 0.
    CHECK(last == 0.0);
}

TEST_CASE("amplitude never exceeds unity and the burst is audible") {
    ShapedBurst b = makeDefault();
    double peak = 0.0;
    for (int n = 0; n < 14640; ++n) peak = std::max(peak, std::fabs(b.process()));
    CHECK(peak <= 1.0 + 1e-9);
    CHECK(peak > 0.5);   // the windowed carrier reaches a healthy level
}

TEST_CASE("output is periodic with period P*fs/f0 samples") {
    ShapedBurst a = makeDefault();
    ShapedBurst b = makeDefault();
    const int period = 14640;
    for (int n = 0; n < period; ++n) b.process();   // advance b by one period
    for (int n = 0; n < 512; ++n)
        CHECK(a.process() == doctest::Approx(b.process()).epsilon(1e-9));
}

TEST_CASE("no NaN/Inf across the parameter ranges") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (double f0 : {20.0, 1000.0, 20000.0}) {
            for (double dw : {0.0, 50.0, 1000.0}) {
                ShapedBurst b;
                b.prepare(fs); b.setFrequency(f0); b.setDwell(dw); b.reset();
                for (int n = 0; n < 20000; ++n) {
                    double y = b.process();
                    REQUIRE(std::isfinite(y));
                }
            }
        }
    }
}
