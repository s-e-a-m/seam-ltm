//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · seam_pd_param.h — parameters shared by the Pd externals
//
// Spec: docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md
//
// A Pd external takes its parameters as creation arguments (saved with the
// patch) and as messages (tweaked in rehearsal). Each parameter is clipped
// to the VST3 plugin's range and moves to a new value along a short linear
// ramp, so that rehearsal tweaks don't click. The VST3 plugins have no such
// ramp; at steady state the output is the same.
//
// Pd-independent on purpose: tested under ctest without a Pd binary.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "seam_ramp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Seam { namespace pd {

constexpr double kRampSeconds = 0.020;     // 20 ms
constexpr double kNominalRate = 48000.0;   // the rate the Haar mapping is tuned at

struct RampedParam
{
    const char* name;
    double lo, hi, deflt;
    LinearRamp ramp {};

    double clip (double v) const { return std::min (hi, std::max (lo, v)); }

    // Creation value: clipped, no ramp. NaN — the glue's mark for a
    // non-numeric argument — takes the default, so later values keep
    // their positions.
    void init (double v)
    {
        ramp.setTarget (std::isnan (v) ? deflt : clip (v), 0.0, 1.0);
        ramp.snap ();
    }

    // Message value: clipped, reached in kRampSeconds. NaN is ignored.
    void set (double v, double fs)
    {
        if (!std::isnan (v))
            ramp.setTarget (clip (v), kRampSeconds, fs);
    }

    void   snap ()          { ramp.snap (); }
    double next ()          { return ramp.next (); }
    double target ()  const { return ramp.target (); }
    double current () const { return ramp.value (); }
};

// "<cls> v1 v2 …" from the targets — the line to paste into a patch.
inline std::string formatObjectLine (const char* cls, const RampedParam* p, int n)
{
    std::string line = cls;
    char buf[32];
    for (int i = 0; i < n; ++i) {
        std::snprintf (buf, sizeof buf, " %g", p[i].target ());
        line += buf;
    }
    return line;
}

// Index of the parameter called `name`, or -1.
inline int findParam (const RampedParam* p, int n, const char* name)
{
    for (int i = 0; i < n; ++i)
        if (std::string (p[i].name) == name)
            return i;
    return -1;
}

}} // namespace Seam::pd
