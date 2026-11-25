//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · multipink — the state it announces on the hub.
//
// Kept out of the processor so the field logic is unit-testable without the
// VST3 SDK (tests/multipink_pink_test.cpp).
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub.h"

namespace Seam { namespace multipink {

inline SeamHubState makePinkState(uint32_t stoneId, bool claimed, bool power,
                                  double levelDb, double fs,
                                  int32_t slotStart, int32_t slotCount) {
    SeamHubState s{};   // zeroed: the hub compares field-wise, padding stays deterministic
    s.kind    = kSeamHubPink;
    s.stoneId = stoneId;
    // "active" is the conjunction that exists nowhere else: the pool tracks
    // ownership, so with four instances loaded it reports slots 0/4/8/12 all
    // claimed and cannot say which one is sounding. Level stays a separate
    // field — reference (-23/-20/-18) plus trim (±6) never reaches silence,
    // so an "audible level" test would need a threshold nobody can justify.
    s.active  = (claimed && power) ? 1u : 0u;
    s.levelDb = levelDb;
    s.sampleRate = fs;
    s.u.pink.slotStart = slotStart;
    s.u.pink.slotCount = slotCount;
    return s;
}

}} // namespace Seam::multipink
