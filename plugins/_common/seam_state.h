//─────────────────────────────────────────────────────────────────────────────
// seam_state.h — the SEAM-LTM suite's shared VST3 state decode rule
//
// Suite plugin state is a bare array of little-endian doubles with an
// append-only contract: new fields are only ever appended, never reordered,
// so a blob's "version" is implicit in its length. Restoring therefore reads
// field by field and stops cleanly at the first short read — the caller
// pre-loads `values` with each field's default, so the missing tail of a
// legacy blob restores as defaults instead of uninitialised stack memory.
//
// Spec: docs/superpowers/specs/2026-07-26-vst3-state-shortread-design.md
//─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

namespace Seam {

// Reads up to `count` little-endian doubles from `state` into `values`.
// `values` must arrive pre-loaded with each field's default: fields past the
// first short read keep those defaults. Returns how many fields were read.
inline int readStateDoubles (Steinberg::IBStream* state,
                             double* values, int count)
{
    Steinberg::IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < count; ++i) {
        double v = 0.0;
        if (!s.readDouble (v))   // false ⇔ fewer than 8 bytes available
            return i;
        values[i] = v;
    }
    return count;
}

} // namespace Seam
