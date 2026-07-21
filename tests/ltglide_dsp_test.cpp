#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "ltglide_dsp.h"
#include <algorithm>
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

// ─────────────────────────────────────────────────────────────────────────
// Host-transport gating (design doc 2026-07-21): play = sounds, stop =
// silent. setHostPlaying() is the gate; trigger() remains an independent
// manual/test primitive that bypasses it entirely (it sets state_ directly).
// ─────────────────────────────────────────────────────────────────────────

TEST_CASE("host not playing: transport stays Idle regardless of LOOP") {
    for (bool loopOn : {false, true}) {
        GlideTransport t;
        t.prepare(48000.0);
        t.setSweepSeconds(1.0);
        t.setLoop(loopOn);
        // Never told the host is playing -- hostPlaying_ defaults to false.
        for (int n = 0; n < 1000; ++n)
            CHECK(t.process().kind == GlideTransport::Kind::Silence);
        CHECK_FALSE(t.running());
        CHECK(t.passCount() == 0u);
    }
}

TEST_CASE("host playing, LOOP off: exactly one pass per play edge, no retrigger while still playing") {
    using GT = GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(1.0);
    t.setLoop(false);

    t.setHostPlaying(true);
    // First tick after the play edge is the head Dirac of an AUTO-begun pass
    // -- no trigger() call anywhere in this test.
    CHECK(t.process().kind == GT::Kind::Dirac);
    CHECK(t.passCount() == 1u);

    const long remaining = (long)((GT::kLeadSec + 1.0 + GT::kTailSec) * 48000.0) + 16;
    for (long i = 0; i < remaining; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 1u);

    // Playing is STILL true (no stop/start cycle) -- must NOT retrigger. This
    // is exactly the playedThisRun_ latch: without it, Idle's auto-begin
    // condition would fire again on every subsequent tick.
    for (int i = 0; i < 5000; ++i) {
        CHECK(t.process().kind == GT::Kind::Silence);
    }
    CHECK(t.passCount() == 1u);

    // A stop -> play cycle plays exactly one more pass.
    t.setHostPlaying(false);
    CHECK_FALSE(t.running());
    t.setHostPlaying(true);
    CHECK(t.process().kind == GT::Kind::Dirac);
    CHECK(t.passCount() == 2u);
}

TEST_CASE("host playing, LOOP on: passes repeat while playing; stop mid-pass silences immediately; resume starts fresh") {
    using GT = GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(1.0);
    t.setLoop(true);

    t.setHostPlaying(true);
    CHECK(t.process().kind == GT::Kind::Dirac);       // pass 1 auto-begins
    CHECK(t.passCount() == 1u);

    // Drain pass 1 completely (lead + glide + tail + tail Dirac + wait) --
    // playing stays true throughout.
    const long body = (long)((GT::kLeadSec + 1.0 + GT::kTailSec) * 48000.0);
    for (long i = 0; i < body; ++i) t.process();
    CHECK(t.process().kind == GT::Kind::Dirac);       // tail Dirac
    const long waitN = (long)(GT::kWaitSec * 48000.0);
    for (long i = 0; i < waitN; ++i)
        CHECK(t.process().kind == GT::Kind::Silence);
    // A second pass begins after the Wait elapses, still with playing == true.
    CHECK(t.process().kind == GT::Kind::Dirac);
    CHECK(t.passCount() == 2u);

    // Stop mid-pass: running() must go false IMMEDIATELY (no further process()
    // call needed), and the very next tick must be Silence, not a resumed
    // Glide/Lead/Tail sample.
    for (int i = 0; i < 1000; ++i) t.process();       // get into the body
    CHECK(t.running());
    t.setHostPlaying(false);
    CHECK_FALSE(t.running());
    CHECK(t.process().kind == GT::Kind::Silence);

    // Resume: a fresh pass from HeadDirac, never resumed mid-pass. passCount
    // increments to 3 (a NEW pass, not pass 2 continuing).
    t.setHostPlaying(true);
    CHECK(t.process().kind == GT::Kind::Dirac);
    CHECK(t.passCount() == 3u);
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
    t.setHostPlaying(true);   // LOOP no longer sounds on its own (host-transport gating)
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

TEST_CASE("GlideTransport counts passes") {
    GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    CHECK(t.passCount() == 0u);

    t.trigger();
    CHECK(t.passCount() == 1u);          // beginPass happened at trigger

    // Run the whole pass out: head dirac + lead + glide + tail + tail dirac.
    using GT = GlideTransport;
    const long total = (long)((GT::kLeadSec + 2.0 + GT::kTailSec) * 48000.0) + 16;
    for (long i = 0; i < total; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 1u);          // one pass, counted once
}

TEST_CASE("GlideTransport increments the counter once per looped pass") {
    GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(true);
    t.setHostPlaying(true);   // LOOP no longer sounds on its own (host-transport gating)

    using GT = GlideTransport;
    const long onePass = (long)((GT::kLeadSec + 2.0 + GT::kTailSec + GT::kWaitSec) * 48000.0);
    uint64_t seen = 0;
    for (long i = 0; i < onePass * 3; ++i) {
        const uint64_t before = t.passCount();
        t.process();
        if (t.passCount() != before) ++seen;
    }
    CHECK(seen >= 2u);                   // at least two passes started
    CHECK(t.passCount() == seen);        // every increment is exactly +1
}

TEST_CASE("reset() returns to Idle without resuming a pass mid-way") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(false);
    t.trigger();
    // Advance into the pass (head Dirac + part of the lead).
    t.process();                                   // head Dirac
    for (int n = 0; n < 1000; ++n) t.process();    // into Lead
    CHECK(t.running() == true);

    t.reset();
    CHECK(t.running() == false);
    // Not looping and back to Idle -> the next tick is Silence, not a resumed
    // pass and not a fresh Dirac.
    CHECK(t.process().kind == GlideTransport::Kind::Silence);
}

TEST_CASE("GlideTransport::trigger fires exactly one pass with LOOP off") {
    using GT = Seam::ltglide::GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    // Without setHostPlaying(true) (and without trigger()), Idle's auto-begin
    // condition (hostPlaying_ && ...) never holds, so the transport is silent
    // forever. trigger() is the manual/test primitive that bypasses that gate
    // entirely -- see the host-transport-gating tests above for the gate itself.
    for (int i = 0; i < 1000; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 0u);

    t.trigger();
    CHECK(t.running());
    CHECK(t.passCount() == 1u);

    // Run the pass out; with LOOP off it must stop and stay stopped.
    const long total = (long)((GT::kLeadSec + 2.0 + GT::kTailSec) * 48000.0) + 16;
    for (long i = 0; i < total; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 1u);
    for (int i = 0; i < 1000; ++i) t.process();
    CHECK(t.passCount() == 1u);          // no second pass appeared
}

TEST_CASE("GlideTransport::trigger during a pass does not restart it") {
    using GT = Seam::ltglide::GlideTransport;
    GT t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    t.trigger();
    CHECK(t.passCount() == 1u);
    for (long i = 0; i < 48000; ++i) t.process();   // 1 s into the pass
    CHECK(t.running());

    t.trigger();                                    // must be ignored
    CHECK(t.passCount() == 1u);

    // A truncated-and-restarted pass would still publish a passCounter and an
    // anchor, and Spec 3 would average it as if it were whole. The guard is
    // what stops that.
    for (long i = 0; i < 48000; ++i) t.process();
    CHECK(t.running());
    CHECK(t.passCount() == 1u);
}

TEST_CASE("passCount survives reset() and prepare()") {
    // Load-bearing monotonicity invariant: a receiver must never see
    // passCounter go backwards. reset() is called from setActive(true)
    // (ltglide_processor.cpp), so a regression here would rewind the
    // counter on every deactivate/activate cycle.
    GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(true);
    t.setHostPlaying(true);   // LOOP no longer sounds on its own (host-transport gating)
    for (long i = 0; i < 48000L * 40; ++i) t.process();
    const uint64_t n = t.passCount();
    CHECK(n >= 1u);
    t.reset();          CHECK(t.passCount() == n);
    t.prepare(96000.0); CHECK(t.passCount() == n);
}

// ─────────────────────────────────────────────────────────────────────────
// BusAnchor -- the calbus publish-policy bookkeeping extracted from
// LTGLIDEProcessor (ltglide_processor.cpp). SDK-free, pure logic over
// uint64_t/int64_t/bool, so it is exercised here exactly as it runs in the
// processor: per-sample onSample() calls inside a block's loop, then one
// onRunningChanged() call after the loop -- mirroring
// LTGLIDEProcessor::processBlock's own structure.
// ─────────────────────────────────────────────────────────────────────────

static constexpr int64_t kNoPublish = -999999;

// Drives `numSamples` samples of `t` through `anchor`, exactly mirroring
// LTGLIDEProcessor::processBlock's bookkeeping: per-sample pass-start
// detection via onSample(), then one post-loop onRunningChanged() check --
// i.e. one simulated host audio block. `blockStartSample` is the host clock
// anchor for sample 0 of this block (-1 = invalid host clock this block).
// Returns the LAST anchor published during this block (by either onSample()
// or onRunningChanged()), or kNoPublish if nothing was published.
// `runningEdgeFired`, if non-null, reports whether onRunningChanged() itself
// fired this block (distinct from a pass-start firing).
static int64_t driveBlock(GlideTransport& t, BusAnchor& anchor,
                           int64_t blockStartSample, int numSamples,
                           bool* runningEdgeFired = nullptr) {
    int64_t last = kNoPublish;
    for (int s = 0; s < numSamples; ++s) {
        t.process();
        int64_t a;
        if (anchor.onSample(t.passCount(), blockStartSample, s, &a)) last = a;
    }
    int64_t a;
    const bool fired = anchor.onRunningChanged(t.running(), &a);
    if (fired) last = a;
    if (runningEdgeFired) *runningEdgeFired = fired;
    return last;
}

TEST_CASE("BusAnchor: a pass's anchor survives the transport going idle") {
    const double fs = 48000.0;
    using GT = GlideTransport;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(false);
    BusAnchor anchor;
    anchor.reset(t.passCount());
    t.trigger();

    const int64_t blockStart = 100000;

    // Block A: just the head Dirac sample -- registers the idle->running
    // edge and latches the pass's real anchor.
    const int64_t blockAAnchor = driveBlock(t, anchor, blockStart, 1);
    CHECK(t.running());
    CHECK(blockAAnchor == blockStart);

    // Block B: drain the rest of the pass (lead + glide + tail + tail Dirac)
    // to completion, under a DIFFERENT (and irrelevant) block-start clock
    // value, to prove the FINAL published anchor is the LATCHED pass
    // anchor from Block A, not this block's own clock.
    const long remaining = (long)((GT::kLeadSec + 1.0 + GT::kTailSec) * fs) + 16;
    const int64_t blockBAnchor = driveBlock(t, anchor, /*blockStartSample=*/999999, (int)remaining);

    CHECK_FALSE(t.running());                 // pass completed -> idle
    // The running->idle edge publish must carry the ORIGINAL pass anchor,
    // never this block's unrelated clock value and never -1.
    CHECK(blockBAnchor == blockStart);
    CHECK(blockBAnchor != -1);
    CHECK(anchor.lastPassStartSample() == blockStart);
}

TEST_CASE("BusAnchor: -1 is published only when the host clock is invalid, never merely because the transport is idle") {
    const double fs = 48000.0;
    using GT = GlideTransport;
    const long remaining = (long)((GT::kLeadSec + 1.0 + GT::kTailSec) * fs) + 16;

    // Case A: valid host clock at pass start -> the running->idle edge that
    // fires when the pass later completes must NOT be -1.
    {
        GlideTransport t;
        t.prepare(fs); t.setSweepSeconds(1.0); t.setLoop(false);
        BusAnchor anchor;
        anchor.reset(t.passCount());
        t.trigger();
        driveBlock(t, anchor, /*blockStart=*/42, 1);       // head Dirac, valid clock
        CHECK(t.running());
        const int64_t idleAnchor = driveBlock(t, anchor, /*blockStart=*/-1, (int)remaining);
        CHECK_FALSE(t.running());
        CHECK(idleAnchor != -1);      // idling alone must not manufacture -1
        CHECK(idleAnchor == 42);
    }

    // Case B: invalid host clock (-1) at pass start -> the pass genuinely
    // has no anchor, so BOTH the pass-start publish and the later idle-edge
    // publish are legitimately -1. This -1 means "no valid host clock", not
    // "the transport is idle" -- distinguishing the two is the whole point.
    {
        GlideTransport t;
        t.prepare(fs); t.setSweepSeconds(1.0); t.setLoop(false);
        BusAnchor anchor;
        anchor.reset(t.passCount());
        t.trigger();
        const int64_t startAnchor = driveBlock(t, anchor, /*blockStart=*/-1, 1);
        CHECK(startAnchor == -1);
        CHECK(t.running());
        const int64_t idleAnchor = driveBlock(t, anchor, /*blockStart=*/12345, (int)remaining);
        CHECK_FALSE(t.running());
        CHECK(idleAnchor == -1);      // still -1: genuinely no clock, not "idle => -1"
    }
}

TEST_CASE("BusAnchor: reactivation clears stale edge state (no spurious edge, no stale anchor)") {
    const double fs = 48000.0;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(false);
    BusAnchor anchor;
    anchor.reset(t.passCount());

    // First activation: trigger a pass and get partway into it -- still
    // running, so wasRunning_ ends up true. This mirrors a host that
    // deactivates the plugin mid-pass (setActive(false) can land anywhere).
    t.trigger();
    driveBlock(t, anchor, /*blockStart=*/7777, 1);       // head Dirac: idle->running edge
    CHECK(t.running());
    for (int i = 0; i < 100; ++i) t.process();           // still mid-pass, still running
    CHECK(t.running());
    CHECK(anchor.lastPassStartSample() == 7777);

    // Simulate deactivate/reactivate, mirroring setActive(true): transport
    // .reset() forces Idle without resuming mid-way, then anchor.reset()
    // runs with the CARRIED-OVER (monotone) pass counter.
    t.reset();
    CHECK_FALSE(t.running());
    anchor.reset(t.passCount());
    CHECK(anchor.lastPassStartSample() == -1);    // stale anchor cleared

    // The transport is Idle right after reactivation, exactly as it was
    // before this check. If reset() failed to also clear wasRunning_ (it was
    // true, from the mid-pass deactivation above), this call would spuriously
    // report a running->idle edge that never actually happened this
    // activation. It must NOT fire.
    int64_t a;
    CHECK_FALSE(anchor.onRunningChanged(t.running(), &a));

    // Trigger a SECOND pass: its anchor must be fresh, never the stale 7777
    // from the previous activation.
    t.trigger();
    const int64_t secondAnchor = driveBlock(t, anchor, /*blockStart=*/9999, 1);
    CHECK(secondAnchor == 9999);
    CHECK(anchor.lastPassStartSample() == 9999);
    CHECK(anchor.lastPassStartSample() != 7777);
}

TEST_CASE("BusAnchor: running/idle edge fires exactly once per transition, not once per block") {
    const double fs = 48000.0;
    using GT = GlideTransport;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(false);
    BusAnchor anchor;
    anchor.reset(t.passCount());
    t.trigger();

    const long total = (long)((GT::kLeadSec + 1.0 + GT::kTailSec) * fs) + 16;
    const int  blockSize = 64;        // many small blocks, like real audio callbacks
    int  edgeCount = 0;
    long done = 0;
    while (done < total) {
        const int n = (int)std::min<long>(blockSize, total - done);
        bool fired = false;
        driveBlock(t, anchor, 1000 + done, n, &fired);
        if (fired) ++edgeCount;
        done += n;
    }
    CHECK_FALSE(t.running());
    // Two transitions happened across ~thousands of blocks: idle->running at
    // the pass's start, running->idle when it completes. Each must fire its
    // edge EXACTLY once, not once per block.
    CHECK(edgeCount == 2);
}

// ─────────────────────────────────────────────────────────────────────────
// GlissBurst::release()/done() -- declick at sweep end (GS-reported bug).
// The transport stops feeding GlissBurst::process() the instant it leaves
// State::Glide, which used to hard-truncate an in-flight grain mid-window
// (a step from a mid-window sine value to 0 -- audible as a click whenever
// the sweep end happened to land mid-grain, e.g. f1=160 Hz but not 120 Hz).
// release() lets the CURRENT grain ring out to a clean window close instead
// of being cut, and latches done() so no further grain can ever retrigger.
// ─────────────────────────────────────────────────────────────────────────

TEST_CASE("release() lets an in-flight grain ring out instead of hard-truncating it") {
    const double fs = 48000.0;
    const double f  = 160.0;
    const double delta = 0.02;
    GlissBurst g = makeGap(f, delta);

    // Gap-mode period: Tg = N/f + delta. The Hann window is active (u < N)
    // for the first N/f seconds of each period (1500 samples here); drive to
    // a sample PROVABLY inside that window, i.e. u strictly in (0.5, N-0.5),
    // by recomputing u independently from the public phase/frequency getters.
    // Target u == 2.25 rather than merely "> 0.5": sin(2*pi*u) is itself
    // ZERO at every half-integer u (0.5, 1.5, ... 4.5), so stopping right
    // after crossing 0.5 would land on a sample that is coincidentally near
    // silent regardless of truncation, defeating the continuity check below.
    // 2.25 sits a quarter-cycle away from the nearest zero (peak-ish sine),
    // guaranteeing a substantially non-zero sample to release() from.
    const double Tg = (double)GlissBurst::kN / f + delta;
    double y = g.process(f);                          // sample 0: forced onset
    double u = g.heldFrequency() * g.grainPhase() * Tg;
    int n = 0;
    while (u < 2.25 && n < 1500) {
        y = g.process(f);
        u = g.heldFrequency() * g.grainPhase() * Tg;
        ++n;
    }
    REQUIRE(u > 0.5);
    REQUIRE(u < (double)GlissBurst::kN - 0.5);
    REQUIRE(std::fabs(y) > 0.5);                       // genuinely mid-grain, not near a zero crossing
    CHECK_FALSE(g.done());

    g.release();
    CHECK_FALSE(g.done());                             // not done yet -- still ringing

    // (a) Sample-to-sample continuity through the rest of the ring-out: no
    // jump bigger than the max slope of a windowed 160 Hz sine at 48 kHz
    // (~2*pi*f/fs ~= 0.021), with margin.
    const double kMaxStep = 0.05;
    double prev = y;
    bool   sawExactZero = false;
    int    zeroAtSample = -1;
    for (int i = 0; i < 4000; ++i) {
        const double cur = g.process(f);
        CHECK(std::fabs(cur - prev) < kMaxStep);
        if (!sawExactZero && cur == 0.0) { sawExactZero = true; zeroAtSample = i; }
        prev = cur;
    }

    // (b) The grain completes to exactly 0.0 within 5/f seconds of margin,
    // and stays there for at least one further would-be period -- no
    // retrigger at the next onset.
    REQUIRE(sawExactZero);
    CHECK(zeroAtSample < (int)std::llround(5.0 / f * fs) + 200);
    const int oneMorePeriod = (int)std::llround(Tg * fs) + 10;
    for (int i = 0; i < oneMorePeriod; ++i)
        CHECK(g.process(f) == 0.0);

    // (c) done() reports true once the released grain has finished.
    CHECK(g.done());
}

TEST_CASE("integration: fixed Glide->Silence pipeline lets the last grain ring out at sweep end") {
    // Mirrors LTGLIDEProcessor::processBlock's per-sample switch (transport_
    // + glide_): Glide ticks set a lastTickWasGlide flag and drive glide_;
    // Silence/Dirac ticks release() on the Glide->X edge and, for Silence,
    // keep pumping glide_.process() at the clamped arrival frequency (p=1.0)
    // while ringing() -- released and not yet done -- instead of dropping
    // straight to 0. (Guarding on !done() alone pumped FRESH bursts too:
    // the pre-glide Lead regression pinned two tests below.)
    const double fs = 48000.0;
    const double f0 = 1000.0, f1 = 160.0;
    const double delta = 0.02;
    const int    sm = 1;                    // exponential; irrelevant to the boundary

    GlideTransport transport;
    transport.prepare(fs);
    transport.setSweepSeconds(2.0);
    transport.setLoop(false);

    GlissBurst glide;
    glide.prepare(fs);
    glide.setDelta(delta);
    glide.setDmode(1);                      // gap
    glide.reset();

    bool lastTickWasGlide = false;
    double prev = 0.0;
    double maxJumpAtOrAfterBoundary = 0.0;
    bool   boundaryHit = false;
    bool   sawNonZeroAfterBoundary = false;
    int    samplesSinceBoundary = 0;

    transport.trigger();
    const long guard = (long)((GlideTransport::kLeadSec + 2.0 + GlideTransport::kTailSec) * fs) + 100;
    for (long i = 0; i < guard; ++i) {
        auto tick = transport.process();
        double y = 0.0;
        switch (tick.kind) {
            case GlideTransport::Kind::Dirac:
                if (lastTickWasGlide) { glide.release(); lastTickWasGlide = false; }
                y = 1.0;
                break;
            case GlideTransport::Kind::Glide: {
                if (tick.p == 0.0) glide.reset();
                lastTickWasGlide = true;
                y = glide.process(SweepFreq(f0, f1, sm, tick.p));
                break;
            }
            case GlideTransport::Kind::Silence:
            default:
                if (lastTickWasGlide) {
                    glide.release();
                    lastTickWasGlide = false;
                    boundaryHit = true;             // this sample IS the Glide->Tail edge
                }
                y = glide.ringing() ? glide.process(SweepFreq(f0, f1, sm, 1.0)) : 0.0;
                break;
        }

        if (boundaryHit) {
            maxJumpAtOrAfterBoundary = std::max(maxJumpAtOrAfterBoundary, std::fabs(y - prev));
            if (y != 0.0) sawNonZeroAfterBoundary = true;
            if (++samplesSinceBoundary > 4000) break;   // ring-out is <= 5/160 s ~= 1500 samples
        }
        prev = y;
        if (!transport.running() && boundaryHit && samplesSinceBoundary > 4000) break;
    }

    REQUIRE(boundaryHit);
    CHECK(sawNonZeroAfterBoundary);          // some ring-out actually happened
    CHECK(maxJumpAtOrAfterBoundary < 0.05);  // same bound as the burst-level test
}

TEST_CASE("release() edge cases: before any sample, called twice, and reset() afterwards") {
    // release() before any process() call at all: there is no grain in
    // flight to ring out, so output must be all-zero from the start (the
    // mandatory start-up onset is itself an onset, and release() blocks any
    // onset from latching a grain).
    GlissBurst none;
    none.prepare(48000.0);
    none.setDelta(0.02);
    none.setDmode(1);
    none.reset();
    none.release();
    for (int i = 0; i < 200; ++i) CHECK(none.process(160.0) == 0.0);
    CHECK(none.done());

    // Double release() mid-grain is harmless -- same outcome as one call.
    GlissBurst g = makeGap(160.0, 0.02);
    for (int i = 0; i < 100; ++i) g.process(160.0);
    g.release();
    g.release();
    for (int i = 0; i < 4000; ++i) g.process(160.0);
    CHECK(g.done());

    // reset() after done() restores normal operation: a fresh grain starts,
    // i.e. the very next sample is again close to a zero crossing (see
    // "burst starts on a zero crossing" above).
    g.reset();
    CHECK_FALSE(g.done());
    CHECK(std::fabs(g.process(160.0)) < 1e-4);
}

TEST_CASE("BusAnchor: no false unanchored record when a running/idle transition lands on a block boundary (mid-loop flicker)") {
    const double fs = 48000.0;
    using GT = GlideTransport;
    GlideTransport t;
    t.prepare(fs);
    t.setSweepSeconds(1.0);
    t.setLoop(true);
    t.setHostPlaying(true);   // LOOP no longer sounds on its own (host-transport gating)
    BusAnchor anchor;
    anchor.reset(t.passCount());

    // Exact call count for ONE full looped pass cycle: head Dirac (1) + lead
    // + glide + tail (body) + tail Dirac (1) + wait, ending on the exact
    // call where Wait -> Idle happens -- the boundary the old unconditional
    // "publish -1 while idle" code mishandled (Wait->Idle landing on a
    // block's last sample used to publish active=0, passStartSample=-1 for
    // a still-running pass sequence).
    const long leadN  = (long)(GT::kLeadSec * fs);
    const long tailN  = (long)(GT::kTailSec * fs);
    const long waitN  = (long)(GT::kWaitSec * fs);
    const long glideN = (long)(1.0 * fs);
    const long cycleCalls = 1 + leadN + glideN + tailN + 1 + waitN;

    // Block 1 spans exactly one cycle, so its LAST sample is the Wait->Idle
    // transition.
    const int64_t block1Start = 55555;
    const int64_t block1Anchor = driveBlock(t, anchor, block1Start, (int)cycleCalls);
    CHECK_FALSE(t.running());
    // The running->idle edge at the block boundary must carry pass 1's REAL
    // anchor -- not a false -1. This is the crux of this test.
    CHECK(block1Anchor == block1Start);
    CHECK(block1Anchor != -1);

    // Block 2 begins on its very FIRST sample with pass 2 (Idle+loop
    // triggers immediately inside GlideTransport::process()), and again
    // spans one full cycle.
    const int64_t block2Start = 66666;
    const int64_t block2Anchor = driveBlock(t, anchor, block2Start, (int)cycleCalls);
    CHECK_FALSE(t.running());
    CHECK(block2Anchor == block2Start);       // fresh anchor, not leaked from pass 1
    CHECK(block2Anchor != block1Start);
}

TEST_CASE("integration: the pre-glide Lead is SILENT (a fresh burst must not be pumped)") {
    // GS in-host regression: a continuous low-frequency tone sounded during
    // the 5 s Lead before the sweep. Root cause: the processor's Silence-tick
    // branch pumped glide_.process() guarded only by !done() -- but a FRESH
    // burst has done()==false without release() ever having been called, so
    // the "ring-out" path synthesized grains at f1 from the very first
    // Silence tick. The contract this test pins: Silence ticks pump the burst
    // only while it is RINGING (released and not yet done).
    const double fs = 48000.0;
    const double f0 = 1000.0, f1 = 160.0;

    GlideTransport transport;
    transport.prepare(fs);
    transport.setSweepSeconds(2.0);
    transport.setLoop(false);

    GlissBurst glide;
    glide.prepare(fs);
    glide.setDelta(0.02);
    glide.setDmode(1);

    CHECK_FALSE(glide.ringing());           // fresh burst: not ringing

    bool lastTickWasGlide = false;
    transport.trigger();
    bool sawGlideTick = false;
    double maxPreGlide = 0.0;
    const long guard = (long)((GlideTransport::kLeadSec + 0.5) * fs);
    for (long i = 0; i < guard && !sawGlideTick; ++i) {
        auto tk = transport.process();
        double y = 0.0;
        switch (tk.kind) {
            case GlideTransport::Kind::Glide:
                sawGlideTick = true;        // stop before the sweep itself
                break;
            case GlideTransport::Kind::Dirac:
                if (lastTickWasGlide) { glide.release(); lastTickWasGlide = false; }
                break;
            case GlideTransport::Kind::Silence:
            default:
                if (lastTickWasGlide) { glide.release(); lastTickWasGlide = false; }
                y = glide.ringing() ? glide.process(SweepFreq(f0, f1, 1, 1.0)) : 0.0;
                break;
        }
        if (!sawGlideTick && tk.kind == GlideTransport::Kind::Silence)
            maxPreGlide = std::max(maxPreGlide, std::fabs(y));
    }
    REQUIRE(sawGlideTick);                  // the pass did reach the sweep
    CHECK(maxPreGlide == 0.0);              // the whole Lead was dead silent
}
