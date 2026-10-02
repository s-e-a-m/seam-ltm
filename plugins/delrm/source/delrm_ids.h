//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 19th plugin in the suite. word3 = ASCII "DRM\0".
static const Steinberg::FUID DelrmProcessorUID (0x5E4D0012, 0xA1B2C3D4, 0x44524D00, 0x00000012);

enum DelrmParams : Steinberg::Vst::ParamID {
    kParamPower    = 100,   // off / on        (100 + delrm::Param index)
    kParamDistance = 101,   // m, 0-30
    kParamOutput   = 102,   // linear, CC82 in the original
    // Read-only meters.
    kParamIn1 = 200, kParamIn2 = 201, kParamIn3 = 202, kParamIn4 = 203,   // input peak, dB
    kParamGr2 = 204, kParamGr4 = 205                                      // reduction, dB below
};

} // namespace Seam
