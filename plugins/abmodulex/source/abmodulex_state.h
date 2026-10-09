//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — the Tetrahedron menu between the threads, and the
// processor's state
//
// One normalized double in an atomic, as in delRM: process() stores what the
// host's queue brings, setState stores a recalled preset from the UI thread.
// On disk it is one little-endian double under the suite's append-only
// contract (seam_state.h). abmodulex wrote an empty state until 2026-10-09,
// so an old session reads no field and opens on the upright tetrahedron.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_state.h"
#include "base/source/fstreamer.h"
#include <atomic>

namespace Seam { namespace abmodulex {

constexpr double kDefaultTetrahedron = 0.0;    // upright, LFU RFD RBU LBD

class ParamBox {
public:
    void   store(double norm)  { v_.store(norm); }
    double normalized() const  { return v_.load(); }
    // The menu's second entry, LFD RFU RBD LBU. A NaN compares false: upright.
    bool   dual() const        { return normalized() >= 0.5; }
private:
    std::atomic<double> v_{kDefaultTetrahedron};
};

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    s.writeDouble(box.normalized());
}

// Returns how many fields the blob held; a missing field is stored as its default.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v = kDefaultTetrahedron;
    const int n = Seam::readStateDoubles(state, &v, 1);
    box.store(v);
    return n;
}

}} // namespace
