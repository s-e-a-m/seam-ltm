// SEAM-LTM · abmodulex — SDK-free DSP core (header-only, unit-testable).
//
// A-format tetrahedral mic (LFU,RFD,RBU,LBD) -> First-Order AmbiX (ACN/SN3D:
// a0=W, a1=Y, a2=Z, a3=X). Involutory Hadamard/2 matrix — identical
// coefficients to bamodulex; applying the map twice is the identity.
#pragma once

namespace Seam { namespace abmodulex {

inline void encode(double lfu, double rfd, double rbu, double lbd,
                   double& a0, double& a1, double& a2, double& a3) {
    a0 = (lfu + rfd + rbu + lbd) * 0.5; // W
    a1 = (lfu - rfd - rbu + lbd) * 0.5; // Y
    a2 = (lfu - rfd + rbu - lbd) * 0.5; // Z
    a3 = (lfu + rfd - rbu - lbd) * 0.5; // X
}

}} // namespace
