//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 21st plugin in the suite. word3 = ASCII "IPR\0".
static const Steinberg::FUID InphirevProcessorUID (0x5E4D0014, 0xA1B2C3D4, 0x49505200, 0x00000014);

enum InphirevParams : Steinberg::Vst::ParamID {
    kParamPower    = 100,   // off / on   (100 + inphirev::Param index)
    kParamStoneId  = 101,   // "?" (0), 1..8: which STONE instrument's grid
    kParamGrid     = 102,   // j, 0..14: the grid step
    kParamSections = 103    // N, 1..18: the sections of the cascade
};

// Ranges and defaults: inphirev_params.h (SDK-free, shared with the tests).

} // namespace Seam
