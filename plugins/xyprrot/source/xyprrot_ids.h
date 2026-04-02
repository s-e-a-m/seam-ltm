//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · XYPRrot — First-order AmbiX rotation
//
// Unique identifiers for the XYPRrot VST3 plugin.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Processor + Controller combined (SingleComponentEffect)
static const Steinberg::FUID XYPRrotProcessorUID (0x5E4D0002, 0xA1B2C3D4, 0x58595052, 0x726F7401);

// Parameter tags — unique within this plugin
enum XYPRrotParams : Steinberg::Vst::ParamID
{
    kParamYaw   = 100,
    kParamPitch = 101,
    kParamRoll  = 102,
};

} // namespace Seam
