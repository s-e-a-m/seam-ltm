//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — S.T.OOGE mk2g decoder
// Unique identifier and parameter IDs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {
// Generated once, never change. Each word is exactly 8 hex digits
// (word 3: "BST\0").
static const Steinberg::FUID BASTOOGEProcessorUID (0x5E4D0016, 0xA1B2C3D4, 0x42535400, 0x00000016);

enum BASTOOGEParams : Steinberg::Vst::ParamID {
    kCut    = 0,   // dipole cut, 100-250 Hz, logarithmic
    kTest   = 1,   // Normal, W, X, Y, Z
    kBypass = 2    // filters out, matrix in
};
} // namespace Seam
