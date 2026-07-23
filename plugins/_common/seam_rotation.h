//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · First-order AmbiX rotation
//
// 3D rotation of a first-order AmbiX soundfield (4 channels: W, Y, Z, X).
// ACN channel ordering, SN3D normalization.
//
// The rotation is applied in intrinsic order: Roll → Pitch → Yaw
//   Roll  = rotation around X-axis (tilts left-right vertical plane)
//   Pitch = rotation around Y-axis (tilts front-back vertical plane)
//   Yaw   = rotation around Z-axis (rotates horizontal plane)
//
// Channel 0 (W) is omnidirectional and unaffected by rotation.
//
// FAUST REFERENCE (rotateYPR from test2/*.dsp):
//   rotateYPR(yaw, pitch, roll, a0, a1, a2, a3) = a0, a1_ypr, a2_ypr, a3_ypr
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <cmath>

namespace Seam {

// Rotate a first-order AmbiX frame in-place.
// Angles in radians. Channels: a0=W, a1=Y, a2=Z, a3=X (ACN order).
template <typename T>
inline void rotateYPR (T yaw, T pitch, T roll,
                       T a0, T a1, T a2, T a3,
                       T& out0, T& out1, T& out2, T& out3)
{
    // W is omnidirectional — unchanged
    out0 = a0;

    // Roll (X-axis): affects Y and Z
    T a1_r = a1 * std::cos (roll) - a2 * std::sin (roll);
    T a2_r = a1 * std::sin (roll) + a2 * std::cos (roll);
    T a3_r = a3;

    // Pitch (Y-axis): affects Z and X
    T a1_rp = a1_r;
    T a2_rp = a2_r * std::cos (pitch) + a3_r * std::sin (pitch);
    T a3_rp = -a2_r * std::sin (pitch) + a3_r * std::cos (pitch);

    // Yaw (Z-axis): affects Y and X
    out1 = a1_rp * std::cos (yaw) - a3_rp * std::sin (yaw);
    out2 = a2_rp;
    out3 = a1_rp * std::sin (yaw) + a3_rp * std::cos (yaw);
}

// Apply per-harmonic gains to A1/A2/A3 (A0 fixed), THEN rotate. The order is
// deliberate and load-bearing: rotateYPR mixes A1/A2/A3 among themselves, so
// the gains must precede it to keep naming a fixed component at every angle.
// This composition is m2xhgr's post-Haar DSP and each of lr2xhgr's per-bank
// stages.
template <typename T>
inline void gainRotateYPR (T g1, T g2, T g3,
                           T yaw, T pitch, T roll,
                           T a0, T a1, T a2, T a3,
                           T& out0, T& out1, T& out2, T& out3)
{
    rotateYPR (yaw, pitch, roll,
               a0, a1 * g1, a2 * g2, a3 * g3,
               out0, out1, out2, out3);
}

} // namespace Seam
