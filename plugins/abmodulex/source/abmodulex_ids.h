//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — A-format → AmbiX
// Unique identifier. Stateless: no parameter IDs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {
// Generated once, never change. Each word is exactly 8 hex digits.
static const Steinberg::FUID ABMODULEXProcessorUID (0x5E4D000A, 0xA1B2C3D4, 0x5E4DAB7D, 0x0000000A);

enum ABMODULEXParams : Steinberg::Vst::ParamID {
    kParamTetrahedron = 100   // LFU RFD RBU LBD / LFD RFU RBD LBU (the dual)
};
} // namespace Seam
