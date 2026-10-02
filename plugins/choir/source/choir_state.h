//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the processor's state (SDK)
//
// Two normalized doubles in Param order, little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. RESET and the grid are not state.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace choir {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace choir
