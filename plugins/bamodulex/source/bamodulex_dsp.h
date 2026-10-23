//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BAMODULEX — AmbiX → tetrahedral DSP core (Gerzon BA-module)
//
//   LFU = (a0 + a1 + a2 + a3) / 2
//   RFD = (a0 - a1 - a2 + a3) / 2
//   RBU = (a0 - a1 + a2 - a3) / 2
//   LBD = (a0 + a1 - a2 - a3) / 2
//
// AmbiX (ACN): a0=W, a1=Y, a2=Z, a3=X.
// The 1/2 normalisation makes the matrix orthogonal, so that
// abmodulex ∘ bamodulex = identity.
//
// Shared by the VST3 processor and the Pd external in pd/.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

namespace Seam { namespace bamodulex {

// out = LFU, RFD, RBU, LBD.
template <typename T>
inline void decode (T w, T y, T z, T x, T out[4])
{
    const T half = static_cast<T>(0.5);
    out[0] = (w + y + z + x) * half;
    out[1] = (w - y - z + x) * half;
    out[2] = (w - y + z - x) * half;
    out[3] = (w + y - z - x) * half;
}

}} // namespace Seam::bamodulex
