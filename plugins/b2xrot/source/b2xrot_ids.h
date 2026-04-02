//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · B2Xrot — B-format to AmbiX with rotation
//
// Unique identifiers for the B2Xrot VST3 plugin.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Processor + Controller combined (SingleComponentEffect)
static const Steinberg::FUID B2XrotProcessorUID (0x5E4D0003, 0xA1B2C3D4, 0x42325872, 0x6F740001);

// Parameter tags
enum B2XrotParams : Steinberg::Vst::ParamID
{
    kParamYaw   = 100,
    kParamPitch = 101,
    kParamRoll  = 102,
};

} // namespace Seam
