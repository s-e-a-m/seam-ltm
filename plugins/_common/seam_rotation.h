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

} // namespace Seam
