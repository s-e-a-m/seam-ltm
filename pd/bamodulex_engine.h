//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · bamodulex~ engine — AmbiX → tetrahedral, Pd-independent
//
// Spec: docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md
// DSP:  plugins/bamodulex/source/bamodulex_dsp.h (shared with the VST3)
//
// No parameters and no state: one block of four AmbiX channels in, the
// four feeds of an upright tetrahedron out (LFU, RFD, RBU, LBD).
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "bamodulex_dsp.h"

namespace Seam { namespace pd {

// In-place safe: Pd may hand an outlet the memory of any inlet, so the four
// inputs of a frame are read before any output of that frame is written.
inline void bamodulexBlock (const float* const in[4], float* const out[4], int n)
{
    for (int i = 0; i < n; ++i) {
        const float w = in[0][i], y = in[1][i], z = in[2][i], x = in[3][i];
        float o[4];
        bamodulex::decode (w, y, z, x, o);
        for (int k = 0; k < 4; ++k)
            out[k][i] = o[k];
    }
}

}} // namespace Seam::pd
