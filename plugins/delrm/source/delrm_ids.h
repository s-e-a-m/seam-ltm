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
    // The compressor of channel 2, then of channel 4; the score's set by
    // default (sdt.delrmdyn).
    kParamDrive2 = 103,     // dB, 0-60, linear; +20
    kParamThresh2,          // dB, -60-0, linear; -24
    kParamRatio2,           // 1-40, logarithmic; 11
    kParamAttack2,          // s, 0.001-1, logarithmic; 0.03
    kParamRelease2,         // s, 0.005-5, logarithmic; 0.04
    kParamDrive4,           // 108
    kParamThresh4,
    kParamRatio4,
    kParamAttack4,
    kParamRelease4,         // 112
    // Read-only meters.
    kParamIn1 = 200, kParamIn2 = 201, kParamIn3 = 202, kParamIn4 = 203,   // input peak, dB
    kParamGr2 = 204, kParamGr4 = 205                                      // reduction, dB below
};

} // namespace Seam
