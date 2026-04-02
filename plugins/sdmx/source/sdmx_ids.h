//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · SDMX — Sum and Difference Matrix
//
// Unique identifiers for the SDMX VST3 plugin.
// Each VST3 plugin needs a globally unique ID (FUID) so that hosts can
// distinguish it from every other plugin in the world.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"

namespace Seam {

// Processor + Controller combined (SingleComponentEffect)
// Generated once, never change these.
static const Steinberg::FUID SDMXProcessorUID (0x5E4D0001, 0xA1B2C3D4, 0x5E4D4D58, 0x00000001);

} // namespace Seam
