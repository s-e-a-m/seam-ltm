//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — the processor's state (SDK)
//
// The seven normalized doubles in Param order (power, stoneId, distance,
// sections, scale, density, gain), little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. A recalled value out of range is stored as it is and
// clamped by ParamBox::plain().
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "alogos_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace alogos {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

// Returns how many fields the blob held; every parameter
// is stored, the missing ones as their defaults.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace alogos
