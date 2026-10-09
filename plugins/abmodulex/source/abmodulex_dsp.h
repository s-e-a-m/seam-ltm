// SEAM-LTM · abmodulex — SDK-free DSP core (header-only, unit-testable).
//
// A-format tetrahedral mic (LFU,RFD,RBU,LBD) -> First-Order AmbiX (ACN/SN3D:
// a0=W, a1=Y, a2=Z, a3=X). Involutory Hadamard/2 matrix — identical
// coefficients to bamodulex; applying the map twice is the identity.
//
// The dual tetrahedron (2026-10-09): the same four slots read as LFD RFU
// RBD LBU. Each capsule keeps its azimuth and takes the opposite elevation,
// so only the Z row changes sign (sam.abmodulexw(1)). The two tetrahedra
// together are the eight corners of a cube; the dual is the upright one
// mirrored in the horizontal plane. Through bamodulex the chain becomes the
// 4x4 matrix of ±1/2 written in doc/README.md: W, X and Y pass, Z inverts.
#pragma once
#include "seam_ramp.h"

namespace Seam { namespace abmodulex {

constexpr double kSwitchRamp = 0.025;   // s, the suite's switch crossfade

inline void encode(double lfu, double rfd, double rbu, double lbd,
                   double& a0, double& a1, double& a2, double& a3) {
    a0 = (lfu + rfd + rbu + lbd) * 0.5; // W
    a1 = (lfu - rfd - rbu + lbd) * 0.5; // Y
    a2 = (lfu - rfd + rbu - lbd) * 0.5; // Z
    a3 = (lfu + rfd - rbu - lbd) * 0.5; // X
}

// encode() with the Z row under a gain that glides between +1 (upright)
// and -1 (dual) when the menu changes. At +1 it is encode() bit for bit.
class Encoder {
public:
    void prepare(double fs) { fs_ = fs; z_.setTarget(z_.target(), kSwitchRamp, fs_); }
    void reset()            { z_.snap(); }
    void setDual(bool dual) {
        const double t = dual ? -1.0 : 1.0;
        if (t != z_.target()) z_.setTarget(t, kSwitchRamp, fs_);
    }
    bool dual() const { return z_.target() < 0.0; }

    template <class S>
    void process(const S* const* in, S* const* out, int n) {
        for (int i = 0; i < n; ++i) {
            double a0, a1, a2, a3;
            encode((double)in[0][i], (double)in[1][i], (double)in[2][i], (double)in[3][i], a0, a1, a2, a3);
            const double zg = z_.next();
            out[0][i] = (S)a0; out[1][i] = (S)a1; out[2][i] = (S)(zg * a2); out[3][i] = (S)a3;
        }
    }

private:
    double fs_ = 48000.0;
    LinearRamp z_ = upright();
    static LinearRamp upright() { LinearRamp r; r.setTarget(1.0, 0.0, 1.0); r.snap(); return r; }
};

}} // namespace
