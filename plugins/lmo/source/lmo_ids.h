//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 17th plugin in the suite. word3 = ASCII "LMO\0".
static const Steinberg::FUID LmoProcessorUID (0x5E4D0010, 0xA1B2C3D4, 0x4C4D4F00, 0x00000010);

enum LmoParams : Steinberg::Vst::ParamID {
    kParamPower     = 100,   // off / on   (100 + lmo::Param index)
    kParamFrequency = 101,   // Hz
    kParamGlide     = 102,   // s, time of the next frequency move
    kParamDelta     = 103,   // Hz, distance between the two bands
    kParamVolume    = 104,   // linear, CC81 in the original
    kParamFNow      = 200    // read-only: the band centre now
};

// Ranges and defaults: lmo_params.h (SDK-free, shared with the tests).

} // namespace Seam
