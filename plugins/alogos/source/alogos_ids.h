//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 21st plugin in the suite (replaces INPHIREV). word3 = ASCII "ALG\0".
static const Steinberg::FUID AlogosProcessorUID (0x5E4D0015, 0xA1B2C3D4, 0x414C4700, 0x00000015);

enum AlogosParams : Steinberg::Vst::ParamID {
    kParamPower    = 100,   // off / on   (100 + alogos::Param index)
    kParamStoneId  = 101,   // "?" (0), 1..8: which STONE instrument's grid
    kParamDistance = 102,   // metres 1..6, continuous, snapped to the grid in the engine
    kParamSections = 103,   // N, 1..128
    kParamScale    = 104,   // phi, sqrt2, e, pi
    kParamDensity  = 105,   // D = 1, 2, 4
    kParamGain     = 106    // g: 1/phi, 1/sqrt2, 1/e, 1/pi, sqrt(sqrt2 - 1)
};

// Ranges and defaults: alogos_params.h (SDK-free, shared with the tests).

} // namespace Seam
