#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "dslar_dsp.h"
#include <cmath>

using namespace Seam::dslar;

TEST_CASE("pd::powtodb — Pd 100 dB-offset power->dB, clamped") {
    CHECK(pd::powtodb(1.0)  == doctest::Approx(100.0));   // 100 + 10*log10(1)
    CHECK(pd::powtodb(0.01) == doctest::Approx(80.0));    // 100 + 10*log10(0.01)
    CHECK(pd::powtodb(0.0)  == doctest::Approx(0.0));      // silence -> 0
    CHECK(pd::powtodb(0.25) == doctest::Approx(93.9794000867));
}

TEST_CASE("pd::dbtorms — inverse converter, clamped") {
    CHECK(pd::dbtorms(100.0) == doctest::Approx(1.0));
    CHECK(pd::dbtorms(80.0)  == doctest::Approx(0.1));
    CHECK(pd::dbtorms(0.0)   == doctest::Approx(0.0));
    // dbtorms(powtodb(0.25)) == sqrt(0.25) == 0.5 (the dB round-trip cancels)
    CHECK(pd::dbtorms(pd::powtodb(0.25)) == doctest::Approx(0.5));
}

TEST_CASE("OnePoleHip: coef follows Pd 1-f*2*3.14159/SR; blocks DC") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    // Feed DC 1.0; a highpass settles its output toward 0.
    double y = 0.0;
    for (int n = 0; n < 44100; ++n) y = hp.process(1.0);
    CHECK(std::fabs(y) < 1e-3);
}

TEST_CASE("OnePoleHip: first sample of a unit step equals normal") {
    OnePoleHip hp;
    hp.prepare(44100.0);
    hp.setCutoff(100.0);
    const double coef   = std::min(1.0, std::max(0.0, 1.0 - 100.0*(2.0*3.14159)/44100.0));
    const double normal = (1.0 + coef) / 2.0;
    // w0 = 1 + coef*0 = 1 ; y0 = normal*(w0 - 0) = normal.
    CHECK(hp.process(1.0) == doctest::Approx(normal));
}

// Reference Pd line: continuous v = sv + min(e/R,1)*(target-sv), restart from
// current on retarget, then a 20 ms grain staircase (sample-and-hold).
static std::vector<double> pd_line_reference(const std::vector<double>& in,
                                             double ms, double fs) {
    const double R = ms * fs / 1000.0;
    const int    G = (int)(20.0 * fs / 1000.0);
    std::vector<double> cont(in.size());
    double sv = 0.0, v = 0.0, prev = 0.0; int e = 0;
    for (size_t n = 0; n < in.size(); ++n) {
        const bool chg = (in[n] != prev);
        if (chg) { sv = v; e = 0; } else { e += 1; }
        v = sv + std::min((double)e / R, 1.0) * (in[n] - sv);
        cont[n] = v; prev = in[n];
    }
    std::vector<double> out(in.size());
    double held = 0.0;
    for (size_t n = 0; n < in.size(); ++n) {
        if ((int)(n % (size_t)G) == 0) held = cont[n];
        out[n] = held;
    }
    return out;
}

TEST_CASE("ControlLine matches the Pd line reference (step + mid-ramp retarget)") {
    const double fs = 44100.0, ms = 100.0;
    std::vector<double> in;
    for (int n = 0; n < 2000; ++n) in.push_back(1.0);   // step 0->1
    for (int n = 0; n < 7000; ++n) in.push_back(0.3);   // retarget 1->0.3 mid-ramp
    ControlLine ln; ln.prepare(fs); ln.setRampMs(ms); ln.reset();
    std::vector<double> ref = pd_line_reference(in, ms, fs);
    double md = 0.0;
    for (size_t n = 0; n < in.size(); ++n)
        md = std::max(md, std::fabs(ln.process(in[n]) - ref[n]));
    CHECK(md < 1e-9);
}

TEST_CASE("ControlLine reaches the target and holds it") {
    ControlLine ln; ln.prepare(44100.0); ln.setRampMs(100.0); ln.reset();
    double y = 0.0;
    for (int n = 0; n < 44100; ++n) y = ln.process(1.0);   // 1 s >> 100 ms ramp
    CHECK(y == doctest::Approx(1.0));
}

TEST_CASE("HannRms: window is SR-adaptive round(fs*2048/44100)") {
    HannRms e44; e44.prepare(44100.0);
    CHECK(e44.window() == 2048);                          // exact at the design SR
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        HannRms e; e.prepare(fs);
        CHECK(e.window() == (int)std::lround(fs*2048.0/44100.0));
    }
}

TEST_CASE("HannRms of DC 0.5 settles to 0.5 (Hann-normalized RMS)") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        HannRms e; e.prepare(fs);
        double r = 0.0;
        for (int n = 0; n < 3*e.window(); ++n) r = e.process(0.5);
        CHECK(r == doctest::Approx(0.5).epsilon(1e-4));   // sum(hann)=1 -> RMS = |DC|
    }
}

TEST_CASE("DelayLine: integer-sample delay of an impulse") {
    DelayLine dl; dl.prepare(44100.0);
    dl.setDelayMs(1.0);                          // round(1*44.1) = 44 samples
    const int D = (int)(1.0 * 44100.0 / 1000.0 + 0.5);
    std::vector<double> out;
    out.push_back(dl.process(1.0));              // impulse at n=0
    for (int n = 1; n < 128; ++n) out.push_back(dl.process(0.0));
    CHECK(out[0] == doctest::Approx(0.0));
    CHECK(out[D] == doctest::Approx(1.0));       // impulse reappears at n = D
}

TEST_CASE("DelayLine: zero delay is a pass-through") {
    DelayLine dl; dl.prepare(44100.0);
    dl.setDelayMs(0.0);
    CHECK(dl.process(0.7) == doctest::Approx(0.7));
}

static Larsen makeLar(double fs) {
    Larsen L; L.prepare(fs);
    L.setPower(true);
    L.setDrive(1.0);
    L.setTarget(1.0);
    L.setSteepness(40.0);
    L.setSmoothingMs(200.0);
    L.setLoopDelayMs(50.0);
    L.setDecorrelationMs(20.0);
    L.setOutput(1.0);
    return L;
}

TEST_CASE("Larsen: DC 0.5 settles the loop gain g to 0.5^40 (homeostat), any SR") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Larsen L = makeLar(fs);
        // Feed long enough for the 2000 ms input fade + window + 200 ms smoothing.
        const int N = (int)(fs * 3.0);           // 3 s
        for (int n = 0; n < N; ++n) L.process(0.5);
        CHECK(L.measuredRms()   == doctest::Approx(0.5).epsilon(1e-3));
        CHECK(L.analysisGain()  == doctest::Approx(std::pow(0.5, 40.0)).epsilon(1e-2));
    }
}

TEST_CASE("Larsen: power off mutes the input fade (fx -> 0)") {
    Larsen L = makeLar(44100.0);
    L.setPower(false);
    double y = 0.0;
    for (int n = 0; n < 44100*3; ++n) y = L.process(0.5);
    CHECK(std::fabs(y) < 1e-9);
    CHECK(L.measuredRms() == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("Larsen: no NaN/Inf across parameter extremes") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Larsen L; L.prepare(fs);
        L.setPower(true); L.setDrive(4.0); L.setTarget(0.0);
        L.setSteepness(80.0); L.setSmoothingMs(1.0);
        L.setLoopDelayMs(200.0); L.setDecorrelationMs(200.0); L.setOutput(1.0);
        for (int n = 0; n < 20000; ++n) {
            double y = L.process(std::sin(0.01 * n));
            REQUIRE(std::isfinite(y));
        }
    }
}
