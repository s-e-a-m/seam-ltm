#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 8th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (sdmx=0001, …, ddelay=0007, multipink=0008).
static const Steinberg::FUID MULTIPINKProcessorUID(
    0x5E4D0008, 0xA1B2C3D4, 0x4D554C54, 0x49504E4B);

enum MULTIPINKParams : Steinberg::Vst::ParamID {
    kParamReference   = 100,   // 0=-23, 1=-20, 2=-18 dBFS         (stepped)
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

// The reference levels, in dBFS, indexed by the stepped parameter value.
//
// What the number MEANS: it is defined as the generator's total RMS at
// 48 kHz. What the calibration HOLDS INVARIANT across sample rates is the
// per-third-octave band level, not that RMS -- so at 96 kHz the total RMS of
// a -23 setting reads -22.718 dBFS while every band stays where it was. The
// two definitions coincide at 48 kHz and nowhere else; see
// plugins/multipink/doc/calibration.md.
static constexpr double kReferenceLevelsDb[kReferenceStepCount] = {
    -23.0, -20.0, -18.0
};

} // namespace Seam
