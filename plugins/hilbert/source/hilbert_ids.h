//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hilbert — wideband quadrature/Hilbert transformer
// Unique identifier + parameter IDs for the hilbert VST3 plugin.
//──────────────────────────────────────────────────────────────────────────

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Generated once, never change. 11th plugin in the SEAM-LTM suite.
// Each of the four words is a 32-bit value (exactly 8 hex digits).
static const Steinberg::FUID HilbertProcessorUID (0x5E4D000B, 0xA1B2C3D4, 0x484C4254, 0x0000000B);

// Parameters.
enum HilbertParams : Steinberg::Vst::ParamID {
    kTopologyId = 0, // 0 = RBJ biquad cascade, 1 = polyphase first-order
};

} // namespace Seam
