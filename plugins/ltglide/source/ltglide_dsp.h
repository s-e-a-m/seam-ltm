// SEAM-LTM · ltglide — SDK-free DSP core (header-only, unit-testable).
//
// Glissando of Linkwitz shaped tone-bursts: an external progress p in [0,1]
// maps (via SweepFreq) to a carrier frequency f0->f1; GlissBurst retriggers a
// grain at each onset, latches the swept frequency for the whole grain
// (sample-and-hold), and shapes N=5 carrier cycles with a Hann window.
//
// FAUST REFERENCE (seam.linkwitz.lib):
//   sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p));
//   glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u)*win with { ... };  // Approach A
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Seam { namespace ltglide {

// progress p in [0,1] -> frequency (smode 0 = linear, 1 = exponential).
inline double SweepFreq(double f0, double f1, int smode, double p) {
    return (smode == 0) ? f0 + (f1 - f0) * p
                        : f0 * std::pow(f1 / f0, p);
}

// Retriggered-grain glissando burst engine (port of Faust slw.glissburst, A).
class GlissBurst {
public:
    static constexpr int    kN      = 5;      // canonical Linkwitz cycle count
    static constexpr double kFloorHz = 20.0;  // guards N/fg while fg is still 0

    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; reset(); }
    void reset() { phase_ = 0.0; fg_ = 0.0; started_ = false; releasing_ = false; done_ = false; }

    void setDelta(double sec) { delta_ = (sec > 0.0) ? sec : 0.0; }
    void setDmode(int dmode)  { dmode_ = (dmode != 0) ? 1 : 0; }  // 0 step, 1 gap

    double heldFrequency() const { return fg_; }
    double grainPhase()    const { return phase_; }

    // Ask the grain to stop retriggering: the CURRENT grain (if any is in
    // flight) is allowed to ring out to a clean window close, but no further
    // onset may ever latch a new one. Idempotent -- calling it more than
    // once, or before the first process() call, is harmless.
    void release() { releasing_ = true; }
    // True once the released grain has finished ringing out (or immediately,
    // if release() was called with none in flight).
    bool done() const { return done_; }
    // TRUE only between release() and the ring-out finishing. This — not
    // !done() — is the pump guard for Silence ticks: a FRESH burst also has
    // done()==false, and pumping it would synthesize grains at the clamped
    // arrival frequency through the whole pre-glide Lead (GS in-host bug:
    // a continuous low tone before the sweep).
    bool ringing() const { return releasing_ && !done_; }

    // One sample. fsig is the continuous swept frequency (Hz) for this sample.
    inline double process(double fsig) {
        if (done_) return 0.0;

        // --- recursive grain engine: carries previous (phase_, fg_) ---
        const double pphase = phase_;
        const double pfg    = fg_;
        const double denP   = std::max(kFloorHz, pfg);
        const double TgP    = periodSec(denP);
        const double inc    = 1.0 / std::max(1.0, TgP * fs_);
        const double adv    = pphase + inc;
        const bool   start  = !started_;                 // 1 - 1' : true at sample 0
        started_ = true;
        const bool   onset  = (adv >= 1.0) || start;

        // Once released, an onset here would latch a NEW grain -- or, on the
        // very first call ever, the mandatory start-up onset would latch the
        // first one. Either way that is exactly the retrigger release()
        // exists to prevent, so finalize instead of latching. The CURRENT
        // in-flight grain (if any) has already rung out by construction: its
        // Hann window closes at u==kN well before phase wraps around to the
        // next onset, so there is nothing left to lose here.
        if (releasing_ && onset) { done_ = true; return 0.0; }

        phase_ = adv - std::floor(adv);
        fg_    = onset ? fsig : pfg;                      // hold; latch at onset

        // --- output stage: uses the CURRENT (phase_, fg_) ---
        const double den = std::max(kFloorHz, fg_);
        const double Tg  = periodSec(den);
        const double u   = fg_ * phase_ * Tg;             // cycles since onset
        const double win = (u < (double)kN)
            ? 0.5 - 0.5 * std::cos(2.0 * M_PI * u / (double)kN)
            : 0.0;
        return std::sin(2.0 * M_PI * u) * win;
    }

private:
    inline double periodSec(double den) const {
        // step: onset-fixed max(delta, N/f); gap: gap-fixed N/f + delta.
        return (dmode_ == 0) ? std::max(delta_, (double)kN / den)
                             : (double)kN / den + delta_;
    }

    double fs_        = 48000.0;
    double delta_     = 0.3;
    int    dmode_     = 1;       // gap by default
    double phase_     = 0.0;     // grain ramp [0,1)
    double fg_        = 0.0;     // latched grain frequency (Hz)
    bool   started_   = false;
    bool   releasing_ = false;   // release() was called; ring out, then finalize
    bool   done_      = false;   // released grain has finished; process() now silent
};

// Pass transport: owns the progress p and the Dirac-bracketed pass timeline.
class GlideTransport {
public:
    enum class Kind { Silence, Dirac, Glide };
    struct Tick { Kind kind; double p; };

    static constexpr double kLeadSec = 5.0;
    static constexpr double kTailSec = 5.0;
    static constexpr double kWaitSec = 2.0;

    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        leadN_ = (long)std::llround(kLeadSec * fs_);
        tailN_ = (long)std::llround(kTailSec * fs_);
        waitN_ = (long)std::llround(kWaitSec * fs_);
        recomputeGlide();
        state_ = State::Idle; counter_ = 0;
    }
    void setSweepSeconds(double t) { tSec_ = (t > 0.0) ? t : 1.0; recomputeGlide(); }
    void setLoop(bool on) { loop_ = on; }

    // Host-transport gate (design doc 2026-07-21: play = sounds, stop =
    // silent). false->true (the host pressed play) arms a fresh run by
    // clearing playedThisRun_, so a LOOP-off pass is eligible to auto-begin
    // again. true->false (the host pressed stop) aborts an in-flight pass to
    // Idle immediately -- the processor's Glide->Silence edge fires the grain
    // release() on the very next tick, so the stop rings out cleanly instead
    // of clicking. A redundant call (on == hostPlaying_ already) is a no-op.
    void setHostPlaying(bool on) {
        if (on && !hostPlaying_) {
            playedThisRun_ = false;
        } else if (!on && hostPlaying_ && running()) {
            state_ = State::Idle; counter_ = 0;
        }
        hostPlaying_ = on;
    }

    // Return to Idle without re-deriving the fs-dependent sample counts;
    // used on (re)activation so a pass never resumes mid-way. Does NOT clear
    // hostPlaying_: the host is still playing across a reactivation (the
    // transport, not the host transport state, is what reactivated). DOES
    // clear playedThisRun_: a reactivation is treated as a fresh run, so the
    // intuitive outcome is one new pass on resume rather than staying latched
    // silent because a pass "already played" in a run that no longer exists.
    void reset() { state_ = State::Idle; counter_ = 0; playedThisRun_ = false; }
    // Manual/test primitive: begin a pass if Idle, independent of the host-
    // transport gate above (it sets state_ directly, bypassing process()'s
    // Idle branch entirely). Unchanged by the host-transport gating work.
    void trigger() { if (state_ == State::Idle) beginPass(); }
    bool running() const { return state_ != State::Idle; }

    // Monotone pass counter, +1 at each pass start. The receiver samples this
    // at GUI rate and cannot otherwise distinguish a new pass from the
    // previous one when the parameters are identical (calbus, Spec 2).
    uint64_t passCount() const { return passCounter_; }

    Tick process() {
        switch (state_) {
            case State::Idle:
                // Auto-begin only while the host is playing: LOOP's meaning
                // is now "repeat while playing" (loop_ re-arms every time),
                // not "start sounding by itself". With LOOP off, a pass
                // begins once per play edge -- the playedThisRun_ latch
                // (cleared on the false->true edge by setHostPlaying(), and
                // on reset()) is what stops it from firing again while
                // hostPlaying_ merely stays true.
                if (hostPlaying_ && (loop_ || !playedThisRun_)) {
                    playedThisRun_ = true;
                    beginPass();
                } else {
                    return { Kind::Silence, 0.0 };
                }
                return process();                 // fall into the fresh pass
            case State::HeadDirac:
                state_ = State::Lead; counter_ = 0;
                return { Kind::Dirac, 0.0 };
            case State::Lead:
                if (++counter_ >= leadN_) { state_ = State::Glide; counter_ = 0; }
                return { Kind::Silence, 0.0 };
            case State::Glide: {
                double p = (glideN_ > 0) ? (double)counter_ / (double)glideN_ : 0.0;
                if (++counter_ >= glideN_) { state_ = State::Tail; counter_ = 0; }
                return { Kind::Glide, p };
            }
            case State::Tail:
                if (++counter_ >= tailN_) { state_ = State::TailDirac; counter_ = 0; }
                return { Kind::Silence, 0.0 };
            case State::TailDirac:
                state_ = loop_ ? State::Wait : State::Idle; counter_ = 0;
                return { Kind::Dirac, 0.0 };
            case State::Wait:
                if (++counter_ >= waitN_) { state_ = State::Idle; counter_ = 0; }
                return { Kind::Silence, 0.0 };
        }
        return { Kind::Silence, 0.0 };
    }

private:
    enum class State { Idle, HeadDirac, Lead, Glide, Tail, TailDirac, Wait };
    void recomputeGlide() { glideN_ = (long)std::llround(tSec_ * fs_); if (glideN_ < 1) glideN_ = 1; }
    void beginPass() { state_ = State::HeadDirac; counter_ = 0; ++passCounter_; }

    double fs_ = 48000.0, tSec_ = 20.0;
    long leadN_ = 0, tailN_ = 0, waitN_ = 0, glideN_ = 0, counter_ = 0;
    bool loop_ = false;
    State state_ = State::Idle;
    uint64_t passCounter_ = 0;
    bool hostPlaying_ = false;      // host-transport gate (setHostPlaying())
    bool playedThisRun_ = false;    // LOOP-off latch: one pass per play edge
};

// Bus-publish bookkeeping (calbus, Spec 2): decides WHEN to publish and WHAT
// anchor to publish, without touching the bus itself. SDK-free and pure
// logic over primitives (uint64_t/int64_t/bool), so the processor's publish
// policy is unit-testable even though CalbusClient/SeamCalbusRecord are not
// (see ltglide_processor.h/.cpp, which own the actual publish() calls).
//
// Two independent edges feed the calibration bus:
//   - a NEW PASS starting mid-block (per-sample, via onSample());
//   - the transport's running/idle state CHANGING across a block boundary
//     (per-block, via onRunningChanged()).
//
// The load-bearing rule (seam_calbus.h's passStartSample contract): -1 means
// exactly one thing, "no valid host clock for this pass's anchor" -- never
// "the transport happens to be idle right now". A completed pass's anchor
// must therefore survive the run->idle transition instead of being
// overwritten by an unconditional -1 heartbeat. lastPassStartSample() latches
// that anchor, and onRunningChanged() republishes it -- not -1.
class BusAnchor {
public:
    // Clears edge-detect state to what a fresh (re)activation looks like: no
    // anchor carried over from a previous activation, no stale running/idle
    // edge. lastPublishedPassCount seeds the pass-start detector so the
    // first pass the transport reports AFTER this reset (whatever its value)
    // is recognised as new, rather than compared against a stale baseline.
    void reset(uint64_t lastPublishedPassCount) {
        lastPublishedPass_ = lastPublishedPassCount;
        lastPassStartSample_ = -1;
        wasRunning_ = false;
    }

    // Per-sample pass-start detection. `passCount` is the transport's
    // monotone counter as of the CURRENT sample; `blockStartSample` is the
    // host's continuousTimeSamples anchor for sample 0 of this block (-1 if
    // the host clock is invalid this block); `sampleOffset` is this sample's
    // index within the block -- the reviewed-and-unchanged head-Dirac
    // arithmetic (blockStartSample + sampleOffset), just relocated here.
    // Returns true and sets *anchor exactly on the sample a new pass begins.
    bool onSample(uint64_t passCount, int64_t blockStartSample, int sampleOffset, int64_t* anchor) {
        if (passCount == lastPublishedPass_) return false;
        lastPublishedPass_ = passCount;
        lastPassStartSample_ = (blockStartSample >= 0) ? blockStartSample + sampleOffset : -1;
        *anchor = lastPassStartSample_;
        return true;
    }

    // Per-block running/idle edge detection. Returns true and sets *anchor to
    // the LATCHED pass anchor (never -1 merely because running == false)
    // exactly on the block where `running` differs from the previous call
    // (or from the post-reset baseline of false).
    bool onRunningChanged(bool running, int64_t* anchor) {
        if (running == wasRunning_) return false;
        wasRunning_ = running;
        *anchor = lastPassStartSample_;
        return true;
    }

    int64_t lastPassStartSample() const { return lastPassStartSample_; }

private:
    uint64_t lastPublishedPass_ = 0;
    int64_t  lastPassStartSample_ = -1;
    bool     wasRunning_ = false;
};

}} // namespace Seam::ltglide
