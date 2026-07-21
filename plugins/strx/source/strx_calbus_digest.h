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

struct CalbusDigest {
    bool     available   = false;  // the dylib loaded and the version matched
    int32_t  count       = 0;      // records in the snapshot
    int32_t  firstActive = -1;     // index of the sounding emitter, or -1
    int32_t  activeCount = 0;      // how many are sounding (>1 = method violated)
    int32_t  idleCount   = 0;      // registered but silent
    bool     glide       = false;  // the FIRST ACTIVE record is a sweep
    uint64_t passCounter = 0;      // that sweep's pass number (0 when not glide)
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
        d.passCounter = recs[d.firstActive].u.glide.passCounter;
    }
    return d;
}

// Classifies what a NEW bus reading means for the measurement:
//   SessionStart — first pass after a no-glide state: a new measurement
//                  session begins, the previous accumulation must go.
//   PassBoundary — passCounter advanced within a session: the previous pass
//                  completed and can fold into the accumulation.
//   None         — nothing changed (same pass re-polled, idle, or pink).
// `lastPass` is in/out state owned by the caller (one instance per watch).
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
enum class HoldAction { None, SessionStart, PassBoundary };

inline HoldAction holdAction(const CalbusDigest& d, uint64_t& lastPass) {
    if (!d.glide) {
        lastPass = 0;
        return HoldAction::None;
    }
    if (d.passCounter != lastPass) {
        const bool fresh = (lastPass == 0);
        lastPass = d.passCounter;
        return fresh ? HoldAction::SessionStart : HoldAction::PassBoundary;
    }
    return HoldAction::None;
}

// Boolean view of holdAction: does the hold need clearing at all? Kept so the
// pre-accumulation tests keep pinning the shared sentinel semantics.
inline bool shouldResetHold(const CalbusDigest& d, uint64_t& lastPass) {
    return holdAction(d, lastPass) != HoldAction::None;
}

}} // namespace Seam::strx
