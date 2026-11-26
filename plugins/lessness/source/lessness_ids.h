//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · lessness — the hub's detector
// Unique identifier + custom-view name tags for the lessness VST3 plugin.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 23rd plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (0x0017 = 23), then the salt word, then
// the uppercase-ASCII short name: "LESS" in word 3 (0x4C455353), and the
// index again in word 4.
static const Steinberg::FUID LessnessProcessorUID(
    0x5E4D0017, 0xA1B2C3D4, 0x4C455353, 0x00000017);   // "LESS"

// Custom-view name tags (match resource/lessness.uidesc custom-view-name
// attrs). Three panes, the footer's sheet line, and the two GUI actions.
static const char* kViewMembers   = "LessnessMembers";
static const char* kViewProposals = "LessnessProposals";
static const char* kViewLines     = "LessnessLines";
static const char* kViewSheet     = "LessnessSheet";
static const char* kViewFolder    = "LessnessFolderButton";
static const char* kViewNewSheet  = "LessnessNewSheetButton";

} // namespace Seam
