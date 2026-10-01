//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the processor's state (SDK)
//
// Seven normalized doubles in Param order, little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. RESET is not here: it is not a plugin state.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "stunedrev_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace stunedrev {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

// Returns how many fields the blob held; every field is stored, the missing
// ones as their defaults.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace stunedrev
