//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 20th plugin in the suite. word3 = ASCII "CHR\0".
static const Steinberg::FUID ChoirProcessorUID (0x5E4D0013, 0xA1B2C3D4, 0x43485200, 0x00000013);

enum ChoirParams : Steinberg::Vst::ParamID {
    kParamPower  = 100,   // off / on   (100 + choir::Param index)
    kParamOutput = 101    // linear, CC86 in the patch
};

} // namespace Seam
