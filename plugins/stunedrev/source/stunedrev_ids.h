//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 18th plugin in the suite. word3 = ASCII "STR\0".
static const Steinberg::FUID StunedrevProcessorUID (0x5E4D0011, 0xA1B2C3D4, 0x53545200, 0x00000011);

enum StunedrevParams : Steinberg::Vst::ParamID {
    kParamPower  = 100,   // off / on   (100 + stunedrev::Param index)
    kParamT1     = 101,   // ms, line sqrt(2)
    kParamT2     = 102,   // ms, line phi
    kParamT3     = 103,   // ms, line e
    kParamT4     = 104,   // ms, line pi
    kParamInput  = 105,   // linear, CC83 in the original
    kParamOutput = 106    // linear, CC84 in the original
};

// Ranges and defaults: stunedrev_params.h (SDK-free, shared with the tests).

} // namespace Seam
