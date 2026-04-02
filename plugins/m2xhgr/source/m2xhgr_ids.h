//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · M2XHGR — Mono to AmbiX via Haar wavelet
//
// Unique identifiers for the M2XHGR VST3 plugin.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Processor + Controller combined (SingleComponentEffect)
static const Steinberg::FUID M2XHGRProcessorUID (0x5E4D0005, 0xA1B2C3D4, 0x4D325848, 0x47520001);

// Parameter tags
enum M2XHGRParams : Steinberg::Vst::ParamID
{
    kParamYaw   = 100,
    kParamPitch = 101,
    kParamRoll  = 102,
};

} // namespace Seam
