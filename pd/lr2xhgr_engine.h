//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · lr2xhgr~ engine — stereo → AmbiX (Haar), Pd-independent
//
// Spec: docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md
// DSP:  plugins/m2xhgr/source/m2xhgr_dsp.h (one bank per channel) and
//       plugins/lr2xhgr/source/lr2xhgr_dsp.h (mix), shared with the VST3
//
// [lr2xhgr~ divergence yaw pitch roll a1 a2 a3] — degrees and dB.
// Divergence is the total angle between L and R; as in the plugin, mix
// receives half of it (L turns by +div/2, R by -div/2).
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "lr2xhgr_dsp.h"
#include "m2xhgr_dsp.h"
#include "seam_meter.h"
#include "seam_pd_param.h"

#include <string>

namespace Seam { namespace pd {

class Lr2xhgrEngine
{
public:
    enum { kDivergence, kYaw, kPitch, kRoll, kA1, kA2, kA3, kNumParams };

    Lr2xhgrEngine (const double* args, int nargs)
    {
        for (int i = 0; i < kNumParams; ++i)
            p_[i].init (i < nargs ? args[i] : p_[i].deflt);
    }

    void prepare (double fs)
    {
        fs_ = fs;
        bankL_.reset ();
        bankR_.reset ();
        for (auto& q : p_)
            q.snap ();
    }

    bool rateIsNominal () const { return fs_ == kNominalRate; }

    bool set (const char* name, double value)
    {
        const int i = findParam (p_, kNumParams, name);
        if (i < 0)
            return false;
        p_[i].set (value, fs_);
        return true;
    }

    // In-place safe: both input samples are read before any output is written.
    void process (const float* inL, const float* inR, float* const out[4], int n)
    {
        constexpr double deg = 3.14159265358979323846 / 180.0;
        for (int i = 0; i < n; ++i) {
            const double l = inL[i];
            const double r = inR[i];
            const double half  = p_[kDivergence].next () * 0.5 * deg;
            const double yaw   = p_[kYaw].next ()   * deg;
            const double pitch = p_[kPitch].next () * deg;
            const double roll  = p_[kRoll].next ()  * deg;
            const double g1 = seam::meter::db2lin (p_[kA1].next ());
            const double g2 = seam::meter::db2lin (p_[kA2].next ());
            const double g3 = seam::meter::db2lin (p_[kA3].next ());

            double la[4], ra[4], o[4];
            bankL_.decompose (l, la);
            bankR_.decompose (r, ra);
            lr2xhgr::mix (half, yaw, pitch, roll, g1, g2, g3, la, ra, o);
            for (int k = 0; k < 4; ++k)
                out[k][i] = static_cast<float>(o[k]);
        }
    }

    std::string objectLine () const { return formatObjectLine ("lr2xhgr~", p_, kNumParams); }

    double current (int index) const { return p_[index].current (); }

private:
    RampedParam p_[kNumParams] = {
        { "divergence",   0.0, 360.0, 90.0 },
        { "yaw",       -180.0, 180.0,  0.0 },
        { "pitch",     -180.0, 180.0,  0.0 },
        { "roll",      -180.0, 180.0,  0.0 },
        { "a1",         -12.0,  12.0,  0.0 },
        { "a2",         -12.0,  12.0,  0.0 },
        { "a3",         -12.0,  12.0,  0.0 },
    };
    m2xhgr::Bank bankL_, bankR_;
    double fs_ = 0.0;
};

}} // namespace Seam::pd
