//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · DDELAY — Quad Speaker-Alignment Delay
//
// Unique identifiers for the DDELAY VST3 plugin.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace Seam {

static const Steinberg::FUID DDELAYProcessorUID (0x5E4D0007, 0xA1B2C3D4, 0x434D4445, 0x4C000001);

enum DDELAYParams : Steinberg::Vst::ParamID
{
    kParamDistance = 100
};

} // namespace Seam
