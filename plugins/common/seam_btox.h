//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · B-format (FuMa) to AmbiX (ACN/SN3D) conversion
//
// B-format uses FuMa channel ordering and weighting: W, X, Y, Z
//   W is attenuated by 1/√2 (the "Furse-Malham" convention)
//
// AmbiX uses ACN ordering and SN3D normalization: W, Y, Z, X
//   W has unit gain for a unit-amplitude plane wave
//
// Conversion:
//   a0 (W_ambix) = W_bformat × √2    — restore full amplitude
//   a1 (Y_ambix) = Y_bformat          — just reorder
//   a2 (Z_ambix) = Z_bformat          — just reorder
//   a3 (X_ambix) = X_bformat          — just reorder
//
// FAUST REFERENCE (btox/b2x from test2/*.dsp):
//   btox(w,x,y,z) = w*sqrt(2), y, z, x;
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cmath>

namespace Seam {

static constexpr double kSqrt2 = 1.4142135623730951; // √2

// Convert one frame from B-format (W,X,Y,Z) to AmbiX (a0,a1,a2,a3).
template <typename T>
inline void bFormatToAmbiX (T w, T x, T y, T z,
                            T& a0, T& a1, T& a2, T& a3)
{
    a0 = w * static_cast<T> (kSqrt2);
    a1 = y;
    a2 = z;
    a3 = x;
}

} // namespace Seam
