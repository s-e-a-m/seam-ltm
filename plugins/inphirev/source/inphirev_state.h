//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — the processor's state (SDK)
//
// A version (1.0) and the four normalized doubles in Param order,
// little-endian, under the suite's append-only contract (seam_state.h): a
// short blob keeps the defaults for the fields it lacks. A recalled value
// out of range is stored as it is and clamped by ParamBox::plain().
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "inphirev_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace inphirev {

constexpr double kStateVersion = 1.0;
constexpr int    kStateFields  = 1 + kNumParams;   // version + params

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    s.writeDouble(kStateVersion);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

// Returns how many fields the blob held (version included); every parameter
// is stored, the missing ones as their defaults.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kStateFields];
    v[0] = kStateVersion;
    for (int i = 0; i < kNumParams; ++i) v[1 + i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kStateFields);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[1 + i]);
    return n;
}

} // namespace inphirev
