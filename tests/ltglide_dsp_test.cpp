#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ltglide_dsp.h"
#include <cmath>

using namespace Seam::ltglide;

TEST_CASE("SweepFreq endpoints and midpoints") {
    // exponential (smode 1): geometric — midpoint is the geometric mean.
    CHECK(SweepFreq(20000, 20, 1, 0.0) == doctest::Approx(20000.0));
    CHECK(SweepFreq(20000, 20, 1, 1.0) == doctest::Approx(20.0));
    CHECK(SweepFreq(20000, 20, 1, 0.5) == doctest::Approx(std::sqrt(20000.0 * 20.0)));
    // linear (smode 0): arithmetic — midpoint is the arithmetic mean.
    CHECK(SweepFreq(100, 200, 0, 0.0) == doctest::Approx(100.0));
    CHECK(SweepFreq(100, 200, 0, 1.0) == doctest::Approx(200.0));
    CHECK(SweepFreq(100, 200, 0, 0.5) == doctest::Approx(150.0));
}

static GlissBurst makeGap(double f, double delta) {
    GlissBurst g;
    g.prepare(48000.0);
    g.setDelta(delta);
    g.setDmode(1);   // gap
    g.reset();
    (void)f;
    return g;
}

TEST_CASE("constant-frequency gap mode: onset spacing equals N/f + delta") {
    // f=1000, N=5, delta=0.3 -> Tg = 5/1000 + 0.3 = 0.305 s -> 14640 samples @48k.
    // (This equals the ltburst fixed-burst period for f0=1000, dwell=300 ms.)
    GlissBurst g = makeGap(1000.0, 0.3);
    const double f = 1000.0;
    // Sample 0 forces the first onset -> held freq becomes f immediately.
    g.process(f);
    CHECK(g.heldFrequency() == doctest::Approx(1000.0));
    // Find the next onset by watching heldFrequency latch again after a change.
    int firstOnset = 0;                 // sample 0 was an onset
    int nextOnset = -1;
    for (int n = 1; n < 40000; ++n) {
        double before = g.grainPhase();
        g.process(f);
        double after = g.grainPhase();
        if (after < before) { nextOnset = n; break; }  // ramp wrapped -> onset
    }
    REQUIRE(nextOnset > 0);
    CHECK((nextOnset - firstOnset) == 14640);
}

TEST_CASE("burst starts on a zero crossing") {
    GlissBurst g = makeGap(1000.0, 0.3);
    // Approach A advances phase_ by one sample of increment *before* using it
    // in the same call's output stage, so sample 0 is very close to (but not
    // exactly) u=0 -- unlike ltburst's exact-counter ("c") design. doctest::
    // Approx(0.0) uses a *relative* tolerance, which degenerates to ~zero
    // tolerance against an expected value of exactly 0.0, so an absolute
    // bound against the actual first-sample magnitude (~3.8e-6 here) is the
    // correct check, not doctest::Approx(0.0).epsilon(...).
    CHECK(std::fabs(g.process(1000.0)) < 1e-4);
}

TEST_CASE("sample-and-hold: frequency is latched for the whole grain") {
    GlissBurst g = makeGap(1000.0, 0.3);
    g.process(2000.0);                          // sample 0 onset latches 2000
    CHECK(g.heldFrequency() == doctest::Approx(2000.0));
    // Feed a different frequency; it must NOT take effect until the next onset.
    for (int n = 0; n < 100; ++n) g.process(500.0);
    CHECK(g.heldFrequency() == doctest::Approx(2000.0));
}

TEST_CASE("amplitude never exceeds unity") {
    GlissBurst g = makeGap(1000.0, 0.3);
    double peak = 0.0;
    for (int n = 0; n < 14640; ++n) peak = std::max(peak, std::fabs(g.process(1000.0)));
    CHECK(peak <= 1.0 + 1e-9);
    CHECK(peak > 0.5);
}

TEST_CASE("no NaN/Inf across a swept parameter range") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (int dmode : {0, 1}) {
            for (double delta : {0.05, 0.3, 1.0}) {
                GlissBurst g;
                g.prepare(fs); g.setDelta(delta); g.setDmode(dmode); g.reset();
                for (int n = 0; n < 20000; ++n) {
                    double p = (double)n / 20000.0;
                    double f = SweepFreq(20000.0, 20.0, 1, p);
                    double y = g.process(f);
                    REQUIRE(std::isfinite(y));
                }
            }
        }
    }
}

TEST_CASE("transport is silent when idle and not looping") {
    GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(1.0);
    for (int n = 0; n < 1000; ++n) {
        auto tick = t.process();
        CHECK(tick.kind == GlideTransport::Kind::Silence);
    }
    CHECK(t.running() == false);
}

TEST_CASE("triggered pass has the exact Dirac/silence/glide timeline") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);                 // glideN = 48000
    t.trigger();

    // Sample 0: head Dirac.
    auto s0 = t.process();
    CHECK(s0.kind == GlideTransport::Kind::Dirac);

    // Next 5 s: silence (lead).
    const long leadN = 5 * 48000;
    for (long n = 0; n < leadN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Next 1 s: glide, p rising from 0 toward 1.
    auto g0 = t.process();
    CHECK(g0.kind == GlideTransport::Kind::Glide);
    CHECK(g0.p == doctest::Approx(0.0));
    const long glideN = 48000;
    GlideTransport::Tick gLast = g0;
    for (long n = 1; n < glideN; ++n) gLast = t.process();
    CHECK(gLast.kind == GlideTransport::Kind::Glide);
    CHECK(gLast.p < 1.0);
    CHECK(gLast.p > 0.99);

    // Next 5 s: silence (tail).
    const long tailN = 5 * 48000;
    for (long n = 0; n < tailN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Tail Dirac, then idle (no loop).
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    CHECK(t.process().kind == GlideTransport::Kind::Silence);
    CHECK(t.running() == false);
}

TEST_CASE("trigger() is ignored while a pass is already running") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);                 // glideN = 48000
    t.trigger();

    // Sample 0: head Dirac.
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);

    // Advance a handful of samples into the Lead (silence) phase.
    const int intoLead = 100;
    for (int n = 0; n < intoLead; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Retrigger mid-pass: spec says trigger() is ignored while a pass is
    // already running, so this must NOT reset state_/counter_.
    t.trigger();

    // The rest of the lead window must be undisturbed: no second Dirac, no
    // restart -- just silence for the remainder of the original leadN.
    const long leadN = 5 * 48000;
    for (long n = intoLead; n < leadN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);

    // Glide begins right on schedule, with p == 0.0 at its first sample.
    auto g0 = t.process();
    CHECK(g0.kind == GlideTransport::Kind::Glide);
    CHECK(g0.p == doctest::Approx(0.0));
}

TEST_CASE("loop restarts automatically with a wait gap") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(true);
    // First tick from Idle+loop must be the head Dirac of pass 1.
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    // Drain the pass: lead + glide + tail.
    long body = 5 * 48000 + 48000 + 5 * 48000;
    for (long n = 0; n < body; ++n) t.process();
    // Tail Dirac.
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);
    // Wait (2 s) then a new head Dirac.
    long waitN = 2 * 48000;
    for (long n = 0; n < waitN; ++n)
        CHECK(t.process().kind == GlideTransport::Kind::Silence);
    CHECK(t.process().kind == GlideTransport::Kind::Dirac);   // pass 2 begins
}
