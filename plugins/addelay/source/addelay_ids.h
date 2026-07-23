//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Distance Air-Absorption Delay
// Unique identifier + parameter IDs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 14th plugin in the suite. word3 = ASCII "ADLY".
static const Steinberg::FUID AddelayProcessorUID (0x5E4D000E, 0xA1B2C3D4, 0x41444C59, 0x0000000E);

enum AddelayParams : Steinberg::Vst::ParamID {
    kParamDistance    = 100,
    kParamTemperature = 101,
    kParamHumidity    = 102,
    kParamTopology    = 103,   // {Shelf, Cascade}
    kParamSpreading   = 104    // {Off, On}
};

// Parameter ranges (physical units).
static constexpr double kAddDistMax   = 30.0;    // m
static constexpr double kAddTempMin   = -20.0;   // C
static constexpr double kAddTempMax   = 50.0;    // C
static constexpr double kAddRhMin     = 0.0;     // %
static constexpr double kAddRhMax     = 100.0;   // %

} // namespace Seam
