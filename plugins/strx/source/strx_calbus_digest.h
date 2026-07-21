//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — one reading of a calibration-bus snapshot.
//
// Both the status line and the spectrum need to know which emitter is
// sounding. Two independent walks of the snapshot would be duplicated logic
// that can drift, and — worse — could name different emitters at the same
// instant when more than one is active, so the line would say one thing while
// the spectrum measured another. This is the single reading both use.
//
// Pure and SDK-free, so it can be unit-tested without a host or the dylib.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus.h"

#include <cstdint>

namespace Seam { namespace strx {

// Everything that defines WHAT the sweep emits. Two passes are comparable —
// foldable into one measurement — only if all of these match. stoneId is
// included as defence in depth (a different STONE normally forces a session
// via the no-glide gap already).
struct GlideParams {
    uint32_t stoneId = 0;
    double   levelDb = 0.0, sampleRate = 0.0;
    double   f0 = 0.0, f1 = 0.0, durationSec = 0.0, deltaSec = 0.0;
    uint32_t sweepMode = 0, diracMode = 0;
};
inline bool operator==(const GlideParams& a, const GlideParams& b) {
    return a.stoneId == b.stoneId && a.levelDb == b.levelDb && a.sampleRate == b.sampleRate &&
           a.f0 == b.f0 && a.f1 == b.f1 && a.durationSec == b.durationSec &&
           a.deltaSec == b.deltaSec && a.sweepMode == b.sweepMode && a.diracMode == b.diracMode;
}
inline bool operator!=(const GlideParams& a, const GlideParams& b) { return !(a == b); }

struct CalbusDigest {
    bool        available   = false;  // the dylib loaded and the version matched
    int32_t     count       = 0;      // records in the snapshot
    int32_t     firstActive = -1;     // index of the sounding emitter, or -1
    int32_t     activeCount = 0;      // how many are sounding (>1 = method violated)
    int32_t     idleCount   = 0;      // registered but silent
    bool        glide       = false;  // the FIRST ACTIVE record is a sweep
    uint64_t    passCounter = 0;      // that sweep's pass number (0 when not glide)
    GlideParams params;                 // that sweep's generation params (meaningful only when glide)
};

// `recs` may be null when count is 0. `available` comes from the client.
inline CalbusDigest digest(const SeamCalbusRecord* recs, int32_t n, bool available) {
    CalbusDigest d;
    d.available = available;
    if (!available || !recs || n <= 0) return d;
    d.count = n;
    for (int32_t i = 0; i < n; ++i) {
        if (!recs[i].active) { ++d.idleCount; continue; }
        if (d.firstActive < 0) d.firstActive = i;
        ++d.activeCount;
    }
    if (d.firstActive >= 0 && recs[d.firstActive].kind == (uint32_t)kSeamCalbusGlide) {
        d.glide = true;
        const SeamCalbusRecord& r = recs[d.firstActive];
        d.passCounter = r.u.glide.passCounter;
        d.params.stoneId     = r.stoneId;
        d.params.levelDb     = r.levelDb;
        d.params.sampleRate  = r.sampleRate;
        d.params.f0          = r.u.glide.f0;
        d.params.f1          = r.u.glide.f1;
        d.params.durationSec = r.u.glide.durationSec;
        d.params.deltaSec    = r.u.glide.deltaSec;
        d.params.sweepMode   = r.u.glide.sweepMode;
        d.params.diracMode   = r.u.glide.diracMode;
    }
    return d;
}

// Classifies what a NEW bus reading means for the measurement:
//   SessionStart — first pass after a no-glide state, OR a generation
//                  parameter changed mid-run: a new measurement session
//                  begins, the previous accumulation must go.
//   PassBoundary — passCounter advanced within a session with unchanged
//                  parameters: the previous pass completed and can fold
//                  into the accumulation.
//   None         — nothing changed (same pass re-polled, idle, or pink).
// `lastPass` and `lastParams` are in/out state owned by the caller (one
// instance per watch).
//
// Keying the decision on `passCounter` ALONE is the bug the sentinel fixes:
// `passCounter` is per-emitter-instance and starts at 1 on that instance's
// first pass (GlideTransport::beginPass() increments before a pass sounds —
// see ltglide_dsp.h), so STONE 1's pass 1 and STONE 2's pass 1 are the same
// number. Resetting `lastPass` to 0 whenever no glide is sounding closes that
// gap: 0 can never collide with a real pass number (an ACTIVE glide always
// has passCounter >= 1), and there is always a moment with no glide active
// between two different STONEs being measured one at a time. That same
// sentinel is what distinguishes SessionStart (lastPass == 0: we came from a
// no-glide state) from PassBoundary (a loop advancing within one session,
// where `active` never drops and the sentinel never clears).
//
// The `lastParams` sentinel closes a second gap, reported by GS from in-host
// use: MIN over passes is only meaningful over identical stimuli. Changing
// ltglide's Level (or any other generation parameter) mid-LOOP does not drop
// `active` and does not touch `passCounter`'s continuity, so the pass-only
// check would fold a pass measured at the new level into an accumulation
// built at the old one, poisoning it forever (MIN keeps whatever it sees
// once, in either direction). A run is "one continuous LOOP with unchanged
// generation parameters", so any parameter change is itself a session
// boundary, checked before the passCounter comparison.
enum class HoldAction { None, SessionStart, PassBoundary };

inline HoldAction holdAction(const CalbusDigest& d, uint64_t& lastPass, GlideParams& lastParams) {
    if (!d.glide) {
        lastPass = 0;
        return HoldAction::None;
    }
    if (lastPass == 0) {
        lastPass = d.passCounter;
        lastParams = d.params;
        return HoldAction::SessionStart;
    }
    if (d.params != lastParams) {
        lastPass = d.passCounter;
        lastParams = d.params;
        return HoldAction::SessionStart;
    }
    if (d.passCounter != lastPass) {
        lastPass = d.passCounter;
        return HoldAction::PassBoundary;
    }
    return HoldAction::None;
}

// Boolean view of holdAction: does the hold need clearing at all? Kept so the
// pre-accumulation tests keep pinning the shared sentinel semantics. This
// wrapper's signature has no room for caller-owned GlideParams state, so it
// keeps its own in a function-local static; that is safe here (and only
// here — never do this in a hot-path helper) because every call sequence
// starts from lastPass == 0, and holdAction's fresh branch unconditionally
// overwrites lastParams before ever comparing it, so a stale value left by a
// previous, unrelated sequence can never leak into a decision. A real param
// change also returning true is correct — it IS a hold-reset case.
inline bool shouldResetHold(const CalbusDigest& d, uint64_t& lastPass) {
    static thread_local GlideParams lastParams;
    return holdAction(d, lastPass, lastParams) != HoldAction::None;
}

}} // namespace Seam::strx
