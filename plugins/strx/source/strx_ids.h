//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — STONE observation M/S analyzer
// Unique identifier + custom-view name tags for the strx VST3 plugin.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// Generated once (uuidgen: ac17e4c3-f367-4b1b-b361-5184b55b3603), never
// change. Each of the four words is a 32-bit value (exactly 8 hex digits).
static const Steinberg::FUID StrxProcessorUID(
    0xAC17E4C3, 0xF3674B1B, 0xB3615184, 0xB55B3603);

// Custom-view name tags (match resource/strx.uidesc custom-view "name"
// attrs). No custom views are built yet — the goniometer/spectrum/meters
// panes land in Tasks 7-9 — but the tags are reserved here so the uidesc
// and processor stay in sync as those views are added.
static const char* kViewGoniometer = "StrxGoniometer";
static const char* kViewSpectrum   = "StrxSpectrum";
static const char* kViewMeters     = "StrxMeters";

} // namespace Seam
