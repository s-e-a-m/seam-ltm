// SEAM-LTM · ltburst — SDK-free DSP core (header-only, unit-testable).
//
// Fixed-frequency Linkwitz shaped tone-burst: an N=5-cycle raised-cosine
// (Hann) windowed sinusoid, repeated every P = N + M carrier cycles, where
// M = ceil(dwell * f0) is the dwell (silence) quantised to whole cycles.
//
// FAUST REFERENCE (seam.linkwitz.lib):
//   shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win with {
//       M = max(1, int(ceil(dwell*f0))); P = N + M;
//       c = os.phasor(1, f0/P); u = P*c;
//       win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); };
//   shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);
//
// This core tracks the carrier phase u in [0, P) directly (equivalent to
// Faust's P*c): u advances by f0/fs cycles per sample and wraps at P, so the
// carrier frequency is exactly f0 regardless of P, and a runtime change to
// f0/dwell never discontinues the carrier phase.
#pragma once
#include <cmath>
#include <algorithm>

namespace Seam { namespace ltburst {

class ShapedBurst {
public:
    static constexpr int kN = 5;   // canonical Linkwitz cycle count

    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; recompute(); reset(); }
    void reset() { u_ = 0.0; }

    void setFrequency(double hz) { f0_ = hz; recompute(); }
    void setDwell(double ms)     { dwellSec_ = ms * 0.001; recompute(); }

    double frequency()    const { return f0_; }
    double dwellMs()      const { return dwellSec_ * 1000.0; }
    int    periodCycles() const { return P_; }       // N + M

    // One unit-amplitude sample of the shaped burst; advances the phase.
    inline double process() {
        const double win = (u_ < (double)kN)
            ? 0.5 - 0.5 * std::cos(2.0 * M_PI * u_ / (double)kN)
            : 0.0;
        const double y = std::sin(2.0 * M_PI * u_) * win;
        u_ += incCycles_;
        if (u_ >= (double)P_) u_ -= (double)P_;
        return y;
    }

private:
    void recompute() {
        int M = (int)std::ceil(dwellSec_ * f0_);
        if (M < 1) M = 1;                    // matches Faust max(1, ...)
        P_ = kN + M;
        incCycles_ = (fs_ > 0.0) ? f0_ / fs_ : 0.0;   // carrier advances at f0
        if (u_ >= (double)P_) u_ = std::fmod(u_, (double)P_);  // keep in range
    }

    double fs_       = 48000.0;
    double f0_       = 1000.0;
    double dwellSec_ = 0.3;
    int    P_        = kN + 300;
    double u_        = 0.0;        // carrier phase in cycles, [0, P)
    double incCycles_ = 1000.0 / 48000.0;
};

}} // namespace Seam::ltburst
