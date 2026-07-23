//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LR2XHGR — Stereo → AmbiX (Haar) post-decomposition mix
//
// FAUST REFERENCE (seam.ambisonics.lib):
//   lr2xhgr(divergence, yaw, pitch, roll, g1, g2, g3) =
//       par(i, 2, m2xhgr(g1, g2, g3)) :
//       rotateYPR(divergence, pitch, roll),
//       rotateYPR(0-divergence, pitch, roll) :> si.bus(4) :
//       rotateYPR(yaw, 0, 0);
//
// The two Haar banks receive the SAME three gains — the shared-trim rule.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "seam_rotation.h"

namespace Seam { namespace lr2xhgr {

// la/ra are the two banks' Haar outputs (A0..A3). Angles in radians.
template <typename T>
inline void mix (T divergence, T yaw, T pitch, T roll,
                 T g1, T g2, T g3,
                 const T la[4], const T ra[4], T out[4])
{
    T l0, l1, l2, l3, r0, r1, r2, r3;
    // Same g1,g2,g3 to both banks — gains before the divergence rotation.
    gainRotateYPR (g1, g2, g3,  divergence, pitch, roll,
                   la[0], la[1], la[2], la[3], l0, l1, l2, l3);
    gainRotateYPR (g1, g2, g3, T(0) - divergence, pitch, roll,
                   ra[0], ra[1], ra[2], ra[3], r0, r1, r2, r3);

    const T s0 = l0 + r0, s1 = l1 + r1, s2 = l2 + r2, s3 = l3 + r3;
    rotateYPR (yaw, T(0), T(0), s0, s1, s2, s3,
               out[0], out[1], out[2], out[3]);
}

}} // namespace Seam::lr2xhgr
