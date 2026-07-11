// SEAM-LTM · dslar — SDK-free DSP core (header-only, unit-testable).
//
// Hand port of Di Scipio's LAR.pd: a mono FEEDFORWARD homeostatic processor.
// The Larsen loop is acoustic and external (dac~ -> room -> mic -> adc~), so
// there is NO internal feedback; tab1 is a feedforward delay, not a recursion.
//
// FAUST REFERENCE (seam.discipio.lib / seam.pdclone.lib):
//   lar(gate,drive,ref,k,tsmooth,tab1,tab2,output,x) =
//       audio(fx) * analysisGain(fx) * output with {
//     fx = x * (gate : spd.line(2000.0));
//     audio(s)        = s : spd.hip(100.0) : *(drive) : delms(tab1);
//     analysisGain(s) = s : delms(tab2) : larsengain(2048,1024,ref,k) : spd.line(tsmooth); };
//   larsengain(np,pd,ref,k) = spd.env(np,pd) : spd.dbtorms : (\r.|r-ref|^k);
//   spd.hip(f)  = fi.pole(coef):fi.zero(1):*(normal), coef=clip(1-f*2*3.14159/SR,0,1);
//   spd.env     = Hann-weighted RMS-power, control-rate hold (period = np/2);
//   spd.line    = control-rate ramp, 20 ms grain staircase, restart-from-current.
//
// Re-implemented by hand in readable C++ (project convention, seam-ltm/CLAUDE.md);
// faust -lang cpp is never used for plugin DSP. SR-independence lives here: the
// env window is sized round(fs*2048/44100) at prepare(fs). Pd's literal pi
// 3.14159 is reproduced so coefficients are bit-identical to Pd.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace Seam { namespace dslar {

// --- Pd x_acoustics.c converters (100 dB-offset scale, clamped, silence->0) ---
namespace pd {
    inline double powtodb(double p) {
        if (p <= 0.0) return 0.0;
        const double db = 100.0 + 10.0 * std::log10(p);
        return db < 0.0 ? 0.0 : db;
    }
    inline double dbtorms(double d) {
        if (d <= 0.0) return 0.0;
        const double dd = d > 485.0 ? 485.0 : d;   // Pd clamp
        return std::pow(10.0, (dd - 100.0) / 20.0);
    }
}

// --- Pd d_filter.c sighip: one-pole/one-zero highpass -------------------------
// coef = clip(1 - f*2pi/SR, 0, 1); w[n] = x[n] + coef*w[n-1];
// y[n] = normal*(w[n] - w[n-1]); normal = (1+coef)/2. Pd literal pi 3.14159.
// DIFFERS from a Butterworth biquad; this is Pd's exact topology.
class OnePoleHip {
public:
    void prepare(double fs) { fs_ = (fs > 0.0) ? fs : 48000.0; setCutoff(cutoff_); reset(); }
    void reset() { w1_ = 0.0; }
    void setCutoff(double hz) {
        cutoff_ = hz;
        double c = 1.0 - hz * (2.0 * 3.14159) / fs_;
        c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c);
        coef_   = c;
        normal_ = (1.0 + c) / 2.0;
    }
    double process(double x) {
        const double w = x + coef_ * w1_;      // fi.pole(coef)
        const double y = normal_ * (w - w1_);  // fi.zero(1) * normal
        w1_ = w;
        return y;
    }
private:
    double fs_ = 48000.0, cutoff_ = 100.0, coef_ = 0.0, normal_ = 0.0, w1_ = 0.0;
};

}} // namespace Seam::dslar
