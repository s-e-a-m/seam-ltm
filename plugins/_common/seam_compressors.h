//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_compressors.h — the C++ side of compressors.lib (co)
//
// FAUST REFERENCE (compressors.lib, standard, J. O. Smith III, STK-4.3):
//   compressor_mono = compressor_lad_mono(0);
//   compressor_lad_mono(lad,ratio,thresh,att,rel,x)
//     = x@max(0,floor(0.5+ma.SR*lad)) * compression_gain_mono(ratio,thresh,att,rel,x);
//   compression_gain_mono(ratio,thresh,att,rel) =
//     an.amp_follower_ar(att,rel) : ba.linear2db : outminusindb(ratio,thresh) :
//     kneesmooth(att) : ba.db2linear
//   with { kneesmooth(att) = si.smooth(ba.tau2pole(att/2.0));
//          outminusindb(ratio,thresh,level) =
//            max(level-thresh,0.0) * (1.0/max(ma.EPSILON,float(ratio))-1.0); };
//   an.amp_follower_ar(att,rel) = abs : si.onePoleSwitching(att,rel);
//   si.onePoleSwitching(att,rel,x) = loop ~ _ with { loop(y) = (1-c)*x + c*y
//     with { c = ba.if(x > y, ba.tau2pole(att), ba.tau2pole(rel)); }; };
//   ba.tau2pole(tau) = 0 when |tau| < ma.EPSILON, else exp(-1/(tau*ma.SR));
//   ba.linear2db(g) = 20*log10(max(ma.MIN, g));  ba.db2linear(l) = pow(10, l/20);
//
// Two states: the envelope e, and the "knee", a second one-pole on the gain
// in dB with half the attack time. The attack/release switch compares |x|
// with the envelope's PREVIOUS value. gainDb() is the knee's output, the
// gain the sample was just multiplied by: a gain-reduction meter reads it.
// Callers wrap process in seam_denormals.h ScopedNoDenormals: the envelope
// and the knee stall at subnormals in silence.
// Written 2026-10-02 for SSCDO#2's delRM (ratio 11, -24 dB, 30 ms, 40 ms).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Seam {

inline double tau2pole(double tau, double fs) {
    return std::fabs(tau) < DBL_EPSILON ? 0.0 : std::exp(-1.0 / (tau * fs));
}

class CompressorMono {
public:
    void prepare(double fs, double ratio, double threshDb, double attack, double release) {
        thresh_ = threshDb;
        slope_ = 1.0 / std::max(DBL_EPSILON, ratio) - 1.0;
        cAtt_ = tau2pole(attack, fs);
        cRel_ = tau2pole(release, fs);
        cKnee_ = tau2pole(attack / 2.0, fs);
        reset();
    }
    void reset() { env_ = 0.0; knee_ = 0.0; }

    double tick(double x) {
        const double a = std::fabs(x);
        const double c = a > env_ ? cAtt_ : cRel_;           // the previous envelope
        env_ = (1.0 - c) * a + c * env_;
        const double level = 20.0 * std::log10(std::max(DBL_MIN, env_));
        const double g = std::max(level - thresh_, 0.0) * slope_;
        knee_ = (1.0 - cKnee_) * g + cKnee_ * knee_;
        return x * std::pow(10.0, knee_ / 20.0);
    }

    double gainDb() const { return knee_; }

private:
    double thresh_ = 0.0, slope_ = 0.0, cAtt_ = 0.0, cRel_ = 0.0, cKnee_ = 0.0;
    double env_ = 0.0, knee_ = 0.0;
};

} // namespace Seam
