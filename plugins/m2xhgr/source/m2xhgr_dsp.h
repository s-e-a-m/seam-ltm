//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · M2XHGR — Mono → AmbiX (Haar) DSP core
//
// FAUST REFERENCE (seam.ambisonics.lib): m2xhgr  (Haar via seam.dwt.lib)
//   m2xhgr(g1,g2,g3) = sdw.haarmn(1) : hgain(g1,g2,g3);
//   process = m2xhgr(g1,g2,g3) : rotateYPR(yaw, pitch, roll);
//
// One Haar bank per mono input. The bank is the building block of LR2XHGR
// (two banks + lr2xhgr::mix) and of the Pd externals in pd/.
//
// The Haar runs in double whatever the host's sample type; the trims and
// the rotation run in the sample type T, exactly as the VST3 processor did
// before this header existed — so float and double hosts stay bit-identical.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "seam_haar.h"
#include "seam_rotation.h"

namespace Seam { namespace m2xhgr {

struct Bank
{
    HaarState<double> haar;

    void reset () { haar.reset (); }

    // A0..A3 of haarmn(1), computed in double and cast to T.
    template <typename T>
    void decompose (T x, T a[4])
    {
        double d0, d1, d2, d3;
        haarDecompose (haar, static_cast<double>(x), d0, d1, d2, d3);
        a[0] = static_cast<T>(d0);
        a[1] = static_cast<T>(d1);
        a[2] = static_cast<T>(d2);
        a[3] = static_cast<T>(d3);
    }
};

// One sample: Haar → A1/A2/A3 trims (linear) → rotation (radians).
template <typename T>
inline void tick (Bank& b, T x, T g1, T g2, T g3,
                  T yaw, T pitch, T roll, T out[4])
{
    T a[4];
    b.decompose (x, a);
    gainRotateYPR (g1, g2, g3, yaw, pitch, roll,
                   a[0], a[1], a[2], a[3],
                   out[0], out[1], out[2], out[3]);
}

}} // namespace Seam::m2xhgr
