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
    kParamPower     = 100,   // off / on
    kParamFrequency = 101,   // Hz
    kParamGlide     = 102,   // s, time of the next frequency move
    kParamDelta     = 103,   // Hz, distance between the two bands
    kParamVolume    = 104,   // linear, CC81 in the original
    kParamFNow      = 200    // read-only: the band centre now
};

static constexpr double kLmoFMin     = 20.0;
static constexpr double kLmoFMax     = 1500.0;   // the committed .dsp range
static constexpr double kLmoFDefault = 48.0;     // cue 0
static constexpr double kLmoGlideMax = 300.0;
static constexpr double kLmoDeltaMax = 50.0;

} // namespace Seam
