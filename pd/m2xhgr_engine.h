//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · m2xhgr~ engine — mono → AmbiX (Haar), Pd-independent
//
// Spec: docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md
// DSP:  plugins/m2xhgr/source/m2xhgr_dsp.h (shared with the VST3 plugin)
//
// [m2xhgr~ yaw pitch roll a1 a2 a3] — degrees and dB, the VST's units.
// The Haar runs in double inside the bank; the trims and the rotation run
// in double here too, and the result is rounded to Pd's float on output.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "m2xhgr_dsp.h"
#include "seam_meter.h"
#include "seam_pd_param.h"

#include <string>

namespace Seam { namespace pd {

class M2xhgrEngine
{
public:
    enum { kYaw, kPitch, kRoll, kA1, kA2, kA3, kNumParams };

    M2xhgrEngine (const double* args, int nargs)
    {
        for (int i = 0; i < kNumParams; ++i)
            p_[i].init (i < nargs ? args[i] : p_[i].deflt);
    }

    // DSP (re)start: new rate, fresh Haar memory, every ramp at its target.
    void prepare (double fs)
    {
        fs_ = fs;
        bank_.reset ();
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

    // In-place safe: the input sample is read before any output is written.
    void process (const float* in, float* const out[4], int n)
    {
        constexpr double deg = 3.14159265358979323846 / 180.0;
        for (int i = 0; i < n; ++i) {
            const double x     = in[i];
            const double yaw   = p_[kYaw].next ()   * deg;
            const double pitch = p_[kPitch].next () * deg;
            const double roll  = p_[kRoll].next ()  * deg;
            const double g1 = seam::meter::db2lin (p_[kA1].next ());
            const double g2 = seam::meter::db2lin (p_[kA2].next ());
            const double g3 = seam::meter::db2lin (p_[kA3].next ());

            double o[4];
            m2xhgr::tick (bank_, x, g1, g2, g3, yaw, pitch, roll, o);
            for (int k = 0; k < 4; ++k)
                out[k][i] = static_cast<float>(o[k]);
        }
    }

    std::string objectLine () const { return formatObjectLine ("m2xhgr~", p_, kNumParams); }

    double current (int index) const { return p_[index].current (); }

private:
    RampedParam p_[kNumParams] = {
        { "yaw",   -180.0, 180.0, 0.0 },
        { "pitch", -180.0, 180.0, 0.0 },
        { "roll",  -180.0, 180.0, 0.0 },
        { "a1",     -12.0,  12.0, 0.0 },
        { "a2",     -12.0,  12.0, 0.0 },
        { "a3",     -12.0,  12.0, 0.0 },
    };
    m2xhgr::Bank bank_;
    double fs_ = 0.0;
};

}} // namespace Seam::pd
