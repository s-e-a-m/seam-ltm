//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_st.h — S.T.OOGE mk2g bell table (BASTOOGE)
//
// FAUST REFERENCE (seam.st.lib): stt.mk2g_bell
//
// Provenance: S.T.OOGE mk2g measurements of 2026-11-18 (dream-stone, private
// repository: data/stooge-mk2g-bastooge.dat,
// logs/2026-11-18-stooge-mk2g-primo-ruggito.md). The body's first-order
// components follow r = x^2/(1+x^2), x = f/f1, f1 = 359.3 Hz; the fit has
// RMS 1.16 dB. S.T.OOGE is developed with Francesco Palmieri.
//
// The table holds 16 rows at fc = 100 * 2.5^(k/15), k = 0..15. The
// interpolation reproduces the Faust arithmetic in the same order:
//   fc = clamp(fc, 100, 250)
//   u  = 15 * log(fc/100) / log(2.5), clamped to [0, 15]
//   i  = int(min(u, 14));  t = u - i
//   f0 = exp((1-t)*log(f0_i) + t*log(f0_(i+1)))
//   q  = (1-t)*q_i + t*q_(i+1);  g = (1-t)*g_i + t*g_(i+1)
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <algorithm>
#include <cmath>

namespace Seam::st {

struct Bell { double f0, q, gainDb; };

constexpr int kMk2gRows = 16;
constexpr double kMk2gCutMin = 100.0, kMk2gCutMax = 250.0, kMk2gCutDefault = 160.0;

inline Bell mk2gBell(double fcHz) {
    static constexpr double f0Tab[kMk2gRows] = {
        222.279, 231.823, 241.847, 252.38,
        263.457, 275.11, 287.379, 300.304,
        313.929, 328.302, 343.474, 359.499,
        376.439, 394.357, 413.322, 433.407};
    static constexpr double qTab[kMk2gRows] = {
        0.836531, 0.847386, 0.858033, 0.868433,
        0.878556, 0.888353, 0.897807, 0.906876,
        0.915531, 0.923743, 0.931485, 0.938721,
        0.945439, 0.951616, 0.957251, 0.962313};
    static constexpr double gTab[kMk2gRows] = {
        -4.98392, -4.72051, -4.46347, -4.21306,
        -3.96957, -3.73324, -3.50434, -3.28311,
        -3.06976, -2.86449, -2.66749, -2.47886,
        -2.29875, -2.12721, -1.9643, -1.81001};

    const double c = std::max(kMk2gCutMin, std::min(kMk2gCutMax, fcHz));
    const double u = std::max(0.0, std::min(15.0, 15.0 * std::log(c / 100.0) / std::log(2.5)));
    const int i = (int)std::min(u, 14.0);
    const double t = u - i;
    auto lerp = [t](double a, double b) { return (1.0 - t) * a + t * b; };
    Bell b;
    b.f0 = std::exp(lerp(std::log(f0Tab[i]), std::log(f0Tab[i + 1])));
    b.q = lerp(qTab[i], qTab[i + 1]);
    b.gainDb = lerp(gTab[i], gTab[i + 1]);
    return b;
}

} // namespace Seam::st
