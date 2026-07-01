#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 12th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (multipink=0008, …, hilbert=000B, ltburst=000C).
static const Steinberg::FUID LTBURSTProcessorUID(
    0x5E4D000C, 0xB2C3D4E5, 0x4C544255, 0x52535400);

enum LTBURSTParams : Steinberg::Vst::ParamID {
    kParamLevel     = 100,   // -60 … 0 dBFS   (continuous)
    kParamFrequency = 101,   // 20 … 20000 Hz  (log)
    kParamDwell     = 102,   // 0 … 1000 ms
};

// Level parameter range (dBFS, linear taper — the value is the carrier/peak amplitude).
static constexpr double kLevelMinDb     = -60.0;
static constexpr double kLevelMaxDb     =   0.0;
static constexpr double kLevelDefaultDb = -20.0;

// Frequency parameter range (logarithmic taper applied in the processor).
static constexpr double kFreqMinHz = 20.0;
static constexpr double kFreqMaxHz = 20000.0;
// Dwell parameter range (milliseconds, linear).
static constexpr double kDwellMinMs = 0.0;
static constexpr double kDwellMaxMs = 1000.0;

} // namespace Seam
