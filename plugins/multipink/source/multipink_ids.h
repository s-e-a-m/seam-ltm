#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 8th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (sdmx=0001, …, ddelay=0007, multipink=0008).
static const Steinberg::FUID MULTIPINKProcessorUID(
    0x5E4D0008, 0xA1B2C3D4, 0x4D554C54, 0x49504E4B);

enum MULTIPINKParams : Steinberg::Vst::ParamID {
    kParamReference   = 100,   // 0=-23, 1=-20, 2=-18 dBFS RMS  (stepped)
    kParamTrim        = 101,   // -6.0 … +6.0 dB                (continuous)
    kParamPower       = 102,   // 0 = silent / 1 = sounding      (bool)
    kParamStoneId     = 103,   // 0=undeclared, 1..8            (stepped)
    // Read-only display parameters, pushed from the audio thread:
    kParamSlotStart   = 200,   // -1..63 (sentinel -1 = unclaimed)
    kParamSlotCount   = 201,   // 0..64
    kParamPoolStatus  = 202,   // PoolStatus enum (0..3)
};

// STONE identity for the calibration bus (Spec 2). Declared by hand and never
// inferred from the pool slot: with four STONEs in the room, an instance that
// guesses is an instance that calibrates the wrong power amp. 0 = undeclared,
// which strx renders as "STONE ?".
static constexpr Steinberg::int32 kStoneIdStepCount = 9;   // "?" + 1..8

// Number of stepped values for kParamReference. Used by the GUI and by the
// processor to decode the normalized [0,1] parameter into an enum index.
static constexpr Steinberg::int32 kReferenceStepCount = 3;

// The reference levels in dBFS RMS, indexed by the stepped parameter value.
static constexpr double kReferenceLevelsDb[kReferenceStepCount] = {
    -23.0, -20.0, -18.0
};

} // namespace Seam
