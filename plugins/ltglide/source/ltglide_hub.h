//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ltglide — the state it announces on the hub.
//
// Kept out of the processor so the field logic is unit-testable without the
// VST3 SDK (tests/ltglide_dsp_test.cpp).
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub.h"

namespace Seam { namespace ltglide {

// passStartSample is -1 when the host sample of the pass start is not known
// (activation, stop); see BusAnchor in ltglide_dsp.h for when it is latched.
inline SeamHubState makeGlideState(uint32_t stoneId, bool running, double levelDb,
                                   double fs, uint64_t passCounter,
                                   int64_t passStartSample, double f0, double f1,
                                   double T, double delta, uint32_t smode,
                                   uint32_t dmode) {
    SeamHubState s{};   // zeroed: the hub compares field-wise, padding stays deterministic
    s.kind    = kSeamHubGlide;
    s.stoneId = stoneId;
    s.active  = running ? 1u : 0u;
    s.levelDb = levelDb;
    s.sampleRate = fs;
    s.u.glide.passCounter     = passCounter;
    s.u.glide.passStartSample = passStartSample;
    s.u.glide.f0          = f0;
    s.u.glide.f1          = f1;
    s.u.glide.durationSec = T;
    s.u.glide.deltaSec    = delta;
    s.u.glide.sweepMode   = smode;
    s.u.glide.diracMode   = dmode;
    return s;
}

}} // namespace Seam::ltglide
