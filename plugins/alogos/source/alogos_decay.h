// alogos -- the printed decay (SDK-free; the GUI calls it, never the audio thread).
//
// After a stationary input closes, the energy left in a face is estimated from
// the energy distribution of its sections, the echoes taken as incoherent:
// a section with delay t and gain g sends g^2 of the energy at once and
// (1 - g^2)^2 g^(2(m-1)) at m t (m = 1, 2, ...). The face is the convolution of
// its sections' distributions, on a grid of kBin seconds, integrated
// backwards; the footer prints the first bin at -30 dB and at -60 dB.
// g is the all-pass gain of the setting (the g menu), not a constant.
// The estimate ignores the phase of the echoes; alogos_decay_test compares it
// with the decay measured on the engine.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace alogos {

constexpr double kBin = 0.1, kHorizon = 3000.0;

struct Decay { double t30, t60; };   // seconds; +inf when beyond kHorizon

inline Decay estimateDecay(const uint32_t* delays, int sections, double fs, double g) {
    const int bins = (int)std::lround(kHorizon / kBin) + 1;
    const double g2 = g * g;
    std::vector<double> cur((size_t)bins, 0.0), nxt((size_t)bins);
    cur[0] = 1.0;
    for (int i = 0; i < sections; ++i) {
        const double t = (double)delays[i] / fs;
        // The taps of this section: (bin offset, energy).
        std::vector<std::pair<int, double>> taps;
        taps.emplace_back(0, g2);
        double e = (1.0 - g2) * (1.0 - g2);
        for (int m = 1; e >= 1e-9; ++m, e *= g2) {
            const long b = std::lround(m * t / kBin);
            if (b >= bins) break;
            taps.emplace_back((int)b, e);
        }
        std::fill(nxt.begin(), nxt.end(), 0.0);
        for (const auto& tp : taps)
            for (int b = 0; b + tp.first < bins; ++b)
                nxt[(size_t)(b + tp.first)] += cur[(size_t)b] * tp.second;
        cur.swap(nxt);
    }
    // Each section is all-pass: a unit input carries energy 1 in all, so the
    // energy still to come after bin b is 1 minus what has arrived by b.
    // What the grid clips (taps past the horizon) therefore counts as still
    // to come, and a threshold not crossed within the horizon stays +inf.
    const double inf = std::numeric_limits<double>::infinity();
    Decay d { inf, inf };
    double arrived = 0.0;
    for (int b = 0; b < bins; ++b) {
        arrived += cur[(size_t)b];
        const double rest = 1.0 - arrived;
        if (d.t30 == inf && rest <= 1e-3) d.t30 = b * kBin;
        if (rest <= 1e-6) { d.t60 = b * kBin; break; }
    }
    return d;
}

} // namespace alogos
