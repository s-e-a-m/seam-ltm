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

}} // namespace Seam::strx
