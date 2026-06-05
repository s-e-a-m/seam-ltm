// SEAM-LTM · X2UHJ — SDK-free DSP core (header-only, unit-testable).
#pragma once
#include <cmath>
#include <initializer_list>
#include "x2uhj_coeffs.h"

namespace Seam { namespace x2uhj {

// Canonical RBJ 2nd-order all-pass. Same math as tools/rbj.py.
struct AllpassSection {
    double a1 = 0, a2 = 0;          // b0=a2, b1=a1, b2=1 (all-pass symmetry)
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set(double f, double Q, double fs) {
        const double w = 2.0 * M_PI * f / fs;
        const double alpha = std::sin(w) / (2.0 * Q);
        const double n = 1.0 + alpha;
        a1 = -2.0 * std::cos(w) / n;
        a2 = (1.0 - alpha) / n;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
    double process(double x) {
        const double y = a2 * x + a1 * x1 + 1.0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// A cascade of N all-pass sections (one network of the quadrature pair).
struct QuadratureNetwork {
    AllpassSection sec[8];
    int n = 0;
    void set(const APSpec* spec, int count, double fs) {
        n = count;
        for (int i = 0; i < n; ++i) sec[i].set(spec[i].f, spec[i].Q, fs);
    }
    void reset() { for (int i = 0; i < n; ++i) sec[i].reset(); }
    double process(double x) {
        for (int i = 0; i < n; ++i) x = sec[i].process(x);
        return x;
    }
};

// AmbiX (ACN order W,Y,Z,X ; SN3D) -> FuMa WXYZ with W scaled by 1/√2.
inline void ambixToFuMa(const double* acn, double* wxyz) {
    static const double kInvSqrt2 = 0.7071067811865476;
    wxyz[0] = acn[0] * kInvSqrt2; // W
    wxyz[1] = acn[3];             // X
    wxyz[2] = acn[1];             // Y
    wxyz[3] = acn[2];             // Z
}

// Full AmbiX -> UHJ C-format encoder (per-sample).
// Structure: filter bank (H_R on W,X,Y,Z ; H_I on W,X) then static matrix.
// Matrix constants: Gerzon, "Ambisonics in Multichannel Broadcasting and
// Video", JAES 1983 — standard UHJ values.
// FAUST REFERENCE (seam.ambisonics.lib): sam.x2uhj (canonical form added by
// this work; see plugins/x2uhj design spec).
class UHJEncoder {
public:
    void prepare(double fs) {
        for (auto* n : {&hrW,&hrX,&hrY,&hrZ}) n->set(kHR, kNumSections, fs);
        for (auto* n : {&hiW,&hiX})           n->set(kHI, kNumSections, fs);
    }
    void reset() {
        hrW.reset(); hrX.reset(); hrY.reset(); hrZ.reset();
        hiW.reset(); hiX.reset();
    }
    // acn: 4-sample AmbiX frame (ACN/SN3D). Outputs L,R,T,Q.
    void process(const double* acn, double& L, double& R, double& T, double& Q) {
        double f[4]; ambixToFuMa(acn, f);
        const double W = f[0], X = f[1], Y = f[2], Z = f[3];
        const double Wr = hrW.process(W), Xr = hrX.process(X);
        const double Yr = hrY.process(Y), Zr = hrZ.process(Z);
        const double Wi = hiW.process(W), Xi = hiX.process(X);
        const double Sigma = 0.9396926*Wr + 0.1855740*Xr;
        const double Delta = (-0.3420201*Wi + 0.5098604*Xi) + 0.6554516*Yr;
        L = 0.5 * (Sigma + Delta);
        R = 0.5 * (Sigma - Delta);
        T = (-0.1432*Wi + 0.6512*Xi) - 0.7071*Yr;
        Q = 0.9772 * Zr;
    }
private:
    QuadratureNetwork hrW, hrX, hrY, hrZ, hiW, hiX;
};

}} // namespace
