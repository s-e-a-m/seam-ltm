//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LR2XHGR — Stereo to AmbiX via Haar wavelet
//
// Unique identifiers for the LR2XHGR VST3 plugin.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Processor + Controller combined (SingleComponentEffect)
static const Steinberg::FUID LR2XHGRProcessorUID (0x5E4D0004, 0xA1B2C3D4, 0x4C523258, 0x48475201);

// Parameter tags
enum LR2XHGRParams : Steinberg::Vst::ParamID
{
    kParamDivergence = 99,
    kParamYaw        = 100,
    kParamPitch      = 101,
    kParamRoll       = 102,
};

} // namespace Seam
