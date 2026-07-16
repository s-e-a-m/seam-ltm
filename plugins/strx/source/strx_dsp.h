// SEAM-LTM · strx_dsp — SDK-free analysis core for the STONE observation analyzer.
//
// FAUST REFERENCE:
//   seam.stereophony.lib : sst.sdmx   — Blumlein M/S sum-and-difference matrix
//   seam.analyzers.lib   : san.correlation, san.width, san.panorama, san.vectorangle
//   seam.analyzers.lib   : an.mth_octave_spectral_level (idiomatic spectrum equiv.)
// The spectral curve here uses a Welch FFT (seam_fft.h) for a finer display,
// citing the filterbank analyzer as the SR-independent Faust equivalent.
#pragma once
#include "seam_meter.h"
#include <algorithm>
#include <cmath>

namespace Seam { namespace strx {

struct AnalysisFrame {
    float inL = -60.f, inR = -60.f, mid = -60.f, side = -60.f;
    float correlation = 0.f;   // [-1,1]
    float width       = 0.f;   // [0,1], 0 = mono
    float panorama    = 0.f;   // [-1,1]
    float angleRad    = 0.f;
};

class Analyzer {
public:
    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        lvlL_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlR_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlM_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlS_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        // one-pole for the covariance means (0.3 s, matching san sfa_tau)
        coef_ = std::exp(-1.0 / (0.3 * fs_));
        reset();
    }
    void reset() {
        lvlL_.reset(); lvlR_.reset(); lvlM_.reset(); lvlS_.reset();
        mLR_ = mLL_ = mRR_ = 0.0;
        frame_ = AnalysisFrame{};
    }
    void analyzeScalars(const float* L, const float* R, int n) {
        double rmsL=0, rmsR=0, rmsM=0, rmsS=0;
        for (int i = 0; i < n; ++i) {
            const double l = L[i], r = R[i];
            const double m = (l + r) * 0.70710678, s = (l - r) * 0.70710678; // sst.sdmx
            rmsL = lvlL_.feed(float(l)); rmsR = lvlR_.feed(float(r));
            rmsM = lvlM_.feed(float(m)); rmsS = lvlS_.feed(float(s));
            mLR_ = l*r + coef_*(mLR_ - l*r);   // moving means (san.correlation)
            mLL_ = l*l + coef_*(mLL_ - l*l);
            mRR_ = r*r + coef_*(mRR_ - r*r);
        }
        const double eps = 1e-12;
        frame_.inL = float(seam::meter::lin2db(rmsL));
        frame_.inR = float(seam::meter::lin2db(rmsR));
        frame_.mid = float(seam::meter::lin2db(rmsM));
        frame_.side = float(seam::meter::lin2db(rmsS));
        frame_.correlation = float(mLR_ / std::sqrt(std::max(eps, mLL_*mRR_)));
        frame_.width = float(rmsS / std::max(eps, rmsM + rmsS));    // san.width
        frame_.panorama = float((mRR_ - mLL_) / std::max(eps, mRR_ + mLL_)); // san.panorama
        frame_.angleRad = float(0.5 * std::atan2(2.0*mLR_, mLL_ - mRR_));     // san.vectorangle
    }
    const AnalysisFrame& frame() const { return frame_; }
protected:
    double fs_ = 48000.0, coef_ = 0.0;
    double mLR_ = 0, mLL_ = 0, mRR_ = 0;
    seam::meter::LevelFollower lvlL_, lvlR_, lvlM_, lvlS_;
    AnalysisFrame frame_;
};

}} // namespace Seam::strx
