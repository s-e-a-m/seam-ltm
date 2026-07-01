// SEAM-LTM · ltglide — SDK-free DSP core (header-only, unit-testable).
//
// Glissando of Linkwitz shaped tone-bursts: an external progress p in [0,1]
// maps (via SweepFreq) to a carrier frequency f0->f1; GlissBurst retriggers a
// grain at each onset, latches the swept frequency for the whole grain
// (sample-and-hold), and shapes N=5 carrier cycles with a Hann window.
//
// FAUST REFERENCE (seam.linkwitz.lib):
//   sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p));
//   glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u)*win with { ... };  // Approach A
#pragma once
#include <cmath>
#include <algorithm>

namespace Seam { namespace ltglide {

// progress p in [0,1] -> frequency (smode 0 = linear, 1 = exponential).
inline double SweepFreq(double f0, double f1, int smode, double p) {
    return (smode == 0) ? f0 + (f1 - f0) * p
                        : f0 * std::pow(f1 / f0, p);
}

// Retriggered-grain glissando burst engine (port of Faust slw.glissburst, A).
class GlissBurst {
public:
    static constexpr int    kN      = 5;      // canonical Linkwitz cycle count
    static constexpr double kFloorHz = 20.0;  // guards N/fg while fg is still 0

    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; reset(); }
    void reset() { phase_ = 0.0; fg_ = 0.0; started_ = false; }

    void setDelta(double sec) { delta_ = (sec > 0.0) ? sec : 0.0; }
    void setDmode(int dmode)  { dmode_ = (dmode != 0) ? 1 : 0; }  // 0 passo, 1 gap

    double heldFrequency() const { return fg_; }
    double grainPhase()    const { return phase_; }

    // One sample. fsig is the continuous swept frequency (Hz) for this sample.
    inline double process(double fsig) {
        // --- recursive grain engine: carries previous (phase_, fg_) ---
        const double pphase = phase_;
        const double pfg    = fg_;
        const double denP   = std::max(kFloorHz, pfg);
        const double TgP    = periodSec(denP);
        const double inc    = 1.0 / std::max(1.0, TgP * fs_);
        const double adv    = pphase + inc;
        const bool   start  = !started_;                 // 1 - 1' : true at sample 0
        started_ = true;
        const bool   onset  = (adv >= 1.0) || start;
        phase_ = adv - std::floor(adv);
        fg_    = onset ? fsig : pfg;                      // hold; latch at onset

        // --- output stage: uses the CURRENT (phase_, fg_) ---
        const double den = std::max(kFloorHz, fg_);
        const double Tg  = periodSec(den);
        const double u   = fg_ * phase_ * Tg;             // cycles since onset
        const double win = (u < (double)kN)
            ? 0.5 - 0.5 * std::cos(2.0 * M_PI * u / (double)kN)
            : 0.0;
        return std::sin(2.0 * M_PI * u) * win;
    }

private:
    inline double periodSec(double den) const {
        // passo: onset-fixed max(delta, N/f); gap: gap-fixed N/f + delta.
        return (dmode_ == 0) ? std::max(delta_, (double)kN / den)
                             : (double)kN / den + delta_;
    }

    double fs_      = 48000.0;
    double delta_   = 0.3;
    int    dmode_   = 1;       // gap by default
    double phase_   = 0.0;     // grain ramp [0,1)
    double fg_      = 0.0;     // latched grain frequency (Hz)
    bool   started_ = false;
};

}} // namespace Seam::ltglide
