//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — S.T.OOGE mk2g decoder, DSP engine
//
// FAUST REFERENCE (seam.st.lib): stt.bastooge(fc, test, bypass)
//
//   bastooge(fc, test, bypass, a0, a1, a2, a3) = sam.bamodulex(w, a1f, a2f, a3f)
//   with {
//       s = 2 * a0;
//       r0 = select2(test == 0, select2(test == 1, 0, s), a0);
//       r1 = select2(test == 0, select2(test == 3, 0, s), a1);
//       r2 = select2(test == 0, select2(test == 4, 0, s), a2);
//       r3 = select2(test == 0, select2(test == 2, 0, s), a3);
//       w = select2(bypass != 0, r0 : bastooge_w(fc), r0);
//       d(v) = select2(bypass != 0, v : bastooge_d(fc), v);
//       a1f = d(r1);  a2f = d(r2);  a3f = d(r3);      // ACN: Y, Z, X
//   };
//   bastooge_w(fc) = fi.svf.bell(f0, q, g) : fi.svf.ap(fc, sqrt(0.5));
//                    // (f0, q, g) = mk2g_bell(fc)
//   bastooge_d(fc) = fi.svf.hp(fc, sqrt(0.5)) : fi.svf.hp(fc, sqrt(0.5));
//
// Input: AmbiX ACN a0..a3 = W, Y, Z, X. Output: LFU, RFD, RBU, LBD.
// Test k = 1..4 (W, X, Y, Z) puts 2*a0 on ACN channel 0, 3, 1, 2 and zero
// elsewhere. Bypass removes the bell, the all-pass and the high-passes; the
// selector and the matrix remain.
//
// Smoothing of the cut (a choice, not in the Faust): the target cut is
// followed by a one-pole of 20 ms time constant, stepped once per 16-sample
// sub-block; at each step the Svf coefficients are redesigned (mk2gBell plus
// four tan/pow evaluations, so ~1/16 of the per-sample cost). The TPT
// structure stays stable under coefficient changes. When the smoothed cut
// is within 1e-4 Hz of the target it snaps to it and stops redesigning, so a
// static cut costs nothing. prepare() snaps at once, with no ramp.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "bamodulex_dsp.h"
#include "seam_st.h"
#include "seam_svf.h"

#include <algorithm>
#include <cmath>

namespace Seam::bastooge {

enum class Test : int { Normal = 0, W = 1, X = 2, Y = 3, Z = 4 };

class Engine {
public:
    void prepare(double fs) {
        fs_ = fs;
        cutSmooth_ = cutTarget_;
        alpha_ = 1.0 - std::exp(-(double)kBlock / (kTauSeconds * fs_));
        design();
        reset();
    }

    void reset() {
        wBell_.reset();
        wAp_.reset();
        for (auto& d : dip_) for (auto& s : d) s.reset();
    }

    void setCut(double fcHz) {
        cutTarget_ = std::max(st::kMk2gCutMin, std::min(st::kMk2gCutMax, fcHz));
    }
    void setBypass(bool on) { bypass_ = on; }
    void setTest(Test t) { test_ = t; }

    double cut() const { return cutSmooth_; }
    st::Bell bell() const { return st::mk2gBell(cutSmooth_); }

    bool hasSubnormalState() const {
        if (wBell_.isSubnormal() || wAp_.isSubnormal()) return true;
        for (const auto& d : dip_) for (const auto& s : d) if (s.isSubnormal()) return true;
        return false;
    }

    // in: ACN a0..a3; out: LFU, RFD, RBU, LBD.
    template <typename T>
    void process(const T* const in[4], T* const out[4], int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            if (counter_ == 0) {
                stepCut();
                counter_ = kBlock;
            }
            --counter_;

            const double a[4] = {(double)in[0][i], (double)in[1][i], (double)in[2][i], (double)in[3][i]};
            double r[4];
            const int t = (int)test_;
            if (t == 0) {
                for (int c = 0; c < 4; ++c) r[c] = a[c];
            } else {
                // test 1..4 = W, X, Y, Z -> ACN 0, 3, 1, 2
                static constexpr int kAcn[5] = {0, 0, 3, 1, 2};
                for (int c = 0; c < 4; ++c) r[c] = 0.0;
                r[kAcn[t]] = 2.0 * a[0];
            }

            double w = r[0];
            double d[3] = {r[1], r[2], r[3]};
            if (!bypass_) {
                w = wAp_.tick(wBell_.tick(w));
                for (int c = 0; c < 3; ++c) d[c] = dip_[c][1].tick(dip_[c][0].tick(d[c]));
            }
            double o[4];
            bamodulex::decode(w, d[0], d[1], d[2], o);
            for (int c = 0; c < 4; ++c) out[c][i] = (T)o[c];
        }
    }

private:
    static constexpr int kBlock = 16;
    static constexpr double kTauSeconds = 0.020;

    void stepCut() {
        if (cutSmooth_ == cutTarget_) return;
        cutSmooth_ += alpha_ * (cutTarget_ - cutSmooth_);
        if (std::fabs(cutTarget_ - cutSmooth_) < 1e-4) cutSmooth_ = cutTarget_;
        design();
    }

    void design() {
        const auto b = st::mk2gBell(cutSmooth_);
        const double q = std::sqrt(0.5);
        wBell_.design(fs_, Svf::Type::Bell, b.f0, b.q, b.gainDb);
        wAp_.design(fs_, Svf::Type::Allpass, cutSmooth_, q);
        for (auto& dc : dip_) for (auto& s : dc) s.design(fs_, Svf::Type::Highpass, cutSmooth_, q);
    }

    double fs_ = 48000.0;
    double cutTarget_ = st::kMk2gCutDefault, cutSmooth_ = st::kMk2gCutDefault;
    double alpha_ = 0.0;
    int counter_ = 0;
    bool bypass_ = false;
    Test test_ = Test::Normal;
    Svf wBell_, wAp_;
    Svf dip_[3][2]; // ACN 1, 2, 3: two high-passes in series
};

} // namespace Seam::bastooge
