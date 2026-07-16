//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — STONE observation M/S analyzer
// Unique identifier + custom-view name tags for the strx VST3 plugin.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 15th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (ltburst=000C, ltglide=000D,
// dslar=000E, strx=000F), then the recent-plugin salt word, then the
// uppercase-ASCII short name in the tail words. "STRX" fits entirely in
// word 3, so word 4 is zero-padded (cf. dslar "DSLA","R\0\0\0").
static const Steinberg::FUID StrxProcessorUID(
    0x5E4D000F, 0xB2C3D4E5, 0x53545258, 0x00000000);   // "STRX"

// Custom-view name tags (match resource/strx.uidesc custom-view-name
// attrs). kViewMeters is wired (Task 7, strx_meters.h); the goniometer/
// spectrum panes land in Tasks 8-9. Tags reserved together so the uidesc
// and processor stay in sync as those views are added.
static const char* kViewGoniometer = "StrxGoniometer";
static const char* kViewSpectrum   = "StrxSpectrum";
static const char* kViewMeters     = "StrxMeters";

} // namespace Seam
