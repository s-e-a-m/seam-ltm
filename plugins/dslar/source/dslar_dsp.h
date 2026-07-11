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

// --- Pd x_time.c line: control-rate ramp, 20 ms grain staircase --------------
// v = setval + min(elapsed/R, 1)*(target-setval), restart-from-current on a new
// target (setval frozen to the current value at the change), sampled every 20 ms
// (DEFAULTLINEGRAIN) into a staircase. Same idiom as spd.line.
class ControlLine {
public:
    void prepare(double fs) {
        fs_    = (fs > 0.0) ? fs : 48000.0;
        grain_ = (int)(20.0 * fs_ / 1000.0);
        if (grain_ < 1) grain_ = 1;
        reset();
    }
    void reset() { v_ = 0.0; sv_ = 0.0; e_ = 0.0; prevTarget_ = 0.0; held_ = 0.0; n_ = 0; }
    void setRampMs(double ms) { ms_ = ms; }
    double process(double target) {
        const double R = ms_ * fs_ / 1000.0;
        const bool chg = (target != prevTarget_);
        if (chg) { sv_ = v_; e_ = 0.0; } else { e_ += 1.0; }
        const double frac = (R > 0.0) ? std::min(e_ / R, 1.0) : 1.0;
        v_ = sv_ + frac * (target - sv_);
        prevTarget_ = target;
        if (n_ % grain_ == 0) held_ = v_;    // pulse at n = 0, grain, 2*grain, ...
        ++n_;
        return held_;
    }
private:
    double fs_ = 48000.0, ms_ = 100.0, v_ = 0.0, sv_ = 0.0, e_ = 0.0,
           prevTarget_ = 0.0, held_ = 0.0;
    int grain_ = 882, n_ = 0;
};

// --- Pd d_ctl.c env~: Hann-weighted RMS-power envelope, control-rate hold -----
// result = sum_i hann[i] * x[n-i]^2, hann[i] = (1-cos(2*3.14159*i/np))/np
// (normalized, sum = 1). Emitted every `period` samples (Pd default np/2) and
// held between ticks; then powtodb -> dbtorms gives the Hann-weighted RMS.
// Ported as a ring buffer with the Hann sum computed at each capture and held:
// O(np/period) amortized per sample, float-identical to the overlap-add spec.
// SR-independence: np = round(fs*2048/44100), period = np/2, computed here.
class HannRms {
public:
    void prepare(double fs) {
        fs_      = (fs > 0.0) ? fs : 48000.0;
        npoints_ = (int)std::lround(fs_ * 2048.0 / 44100.0);
        if (npoints_ < 2) npoints_ = 2;
        period_  = npoints_ / 2;
        if (period_ < 1) period_ = 1;
        hann_.resize(npoints_);
        for (int i = 0; i < npoints_; ++i)
            hann_[i] = (1.0 - std::cos((2.0 * 3.14159 * i) / npoints_)) / npoints_;
        ring_.assign(npoints_, 0.0);
        reset();
    }
    void reset() {
        std::fill(ring_.begin(), ring_.end(), 0.0);
        pos_ = 0; n_ = 0; heldDb_ = 0.0;
    }
    double process(double x) {
        ring_[pos_] = x;
        pos_ = (pos_ + 1) % npoints_;
        if (n_ % period_ == 0) {                 // capture instant (control rate)
            double acc = 0.0;
            int idx = (pos_ - 1 + npoints_) % npoints_;   // most recent sample = x[n-0]
            for (int i = 0; i < npoints_; ++i) {
                const double s = ring_[idx];
                acc += hann_[i] * s * s;                  // hann[i] weights x[n-i]
                idx = (idx - 1 + npoints_) % npoints_;
            }
            heldDb_ = pd::powtodb(acc);
        }
        ++n_;
        return pd::dbtorms(heldDb_);
    }
    int window() const { return npoints_; }
private:
    double fs_ = 48000.0; int npoints_ = 2048, period_ = 1024, pos_ = 0, n_ = 0;
    double heldDb_ = 0.0;
    std::vector<double> ring_, hann_;
};

}} // namespace Seam::dslar
