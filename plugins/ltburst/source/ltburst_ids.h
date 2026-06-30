#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 12th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (multipink=0008, …, hilbert=000B, ltburst=000C).
static const Steinberg::FUID LTBURSTProcessorUID(
    0x5E4D000C, 0xB2C3D4E5, 0x4C544255, 0x52535400);

enum LTBURSTParams : Steinberg::Vst::ParamID {
    kParamReference = 100,   // 0=-23, 1=-20, 2=-18 dBFS RMS  (stepped)
    kParamTrim      = 101,   // -6.0 … +6.0 dB                (continuous)
    kParamFrequency = 102,   // 20 … 20000 Hz                 (continuous, log)
    kParamDwell     = 103,   // 0 … 1000 ms                   (continuous)
};

static constexpr Steinberg::int32 kReferenceStepCount = 3;
static constexpr double kReferenceLevelsDb[kReferenceStepCount] = {
    -23.0, -20.0, -18.0
};

// Frequency parameter range (logarithmic taper applied in the processor).
static constexpr double kFreqMinHz = 20.0;
static constexpr double kFreqMaxHz = 20000.0;
// Dwell parameter range (milliseconds, linear).
static constexpr double kDwellMinMs = 0.0;
static constexpr double kDwellMaxMs = 1000.0;

// Calibration constant — measured (see plugins/ltburst/doc/ltburst-validation.md).
// Makes "Reference=-23, Trim=0" land at -23.0 dBFS RMS over the active burst window.
static constexpr double kCalibrationOffsetDb = -15.730;

} // namespace Seam
