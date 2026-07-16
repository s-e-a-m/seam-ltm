#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 13th plugin in the SEAM-LTM suite (ltburst = 0x5E4D000C).
static const Steinberg::FUID LTGLIDEProcessorUID(
    0x5E4D000D, 0xB2C3D4E5, 0x4C54474C, 0x49444500);   // "LTGL","IDE\0"

enum LTGLIDEParams : Steinberg::Vst::ParamID {
    kParamLevel   = 100,   // -60 … 0 dBFS       (linear taper)
    kParamF0      = 101,   // 20 … 20000 Hz      (log) sweep start
    kParamF1      = 102,   // 20 … 20000 Hz      (log) sweep end
    kParamSmode   = 103,   // 0 linear / 1 exponential
    kParamDmode   = 104,   // 0 passo / 1 gap
    kParamDelta   = 105,   // grain spacing, seconds
    kParamT       = 106,   // sweep duration, seconds
    kParamLoop    = 108,   // toggle: continuous passes (the sole transport control)
    kParamStoneId = 109,   // 0=undeclared, 1..8 (stepped) — calibration bus
};

// STONE identity for the calibration bus (Spec 2). ltglide has no slot to be
// inferred from and its chain identity lives in the host routing, which the
// receiver cannot read — so it is declared by hand. 0 = undeclared ("STONE ?").
static constexpr Steinberg::int32 kStoneIdStepCount = 9;   // "?" + 1..8

// Level (dBFS, linear taper — carrier/peak amplitude; also the Dirac ceiling).
static constexpr double kLevelMinDb     = -60.0;
static constexpr double kLevelMaxDb     =   0.0;
static constexpr double kLevelDefaultDb = -20.0;

// Sweep endpoints (Hz, log taper).
static constexpr double kFreqMinHz = 20.0;
static constexpr double kFreqMaxHz = 20000.0;
static constexpr double kF0DefaultHz = 20000.0;   // start: acuto
static constexpr double kF1DefaultHz = 20.0;      // end:   grave

// Grain spacing (seconds, linear).
static constexpr double kDeltaMinSec     = 0.02;
static constexpr double kDeltaMaxSec     = 2.0;
static constexpr double kDeltaDefaultSec = 0.3;

// Sweep duration (seconds, linear).
static constexpr double kTMinSec     = 2.0;
static constexpr double kTMaxSec     = 120.0;
static constexpr double kTDefaultSec = 20.0;

} // namespace Seam
