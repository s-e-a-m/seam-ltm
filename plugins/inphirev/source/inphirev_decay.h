// inphirev -- the printed decay (SDK-free; the GUI calls it, never the audio thread).
//
// After a stationary input closes, the energy left in a face is estimated from
// the energy distribution of its sections, the echoes taken as incoherent:
// a section with delay t and gain g sends g^2 of the energy at once and
// (1 - g^2)^2 g^(2(m-1)) at m t (m = 1, 2, ...). The face is the convolution of
// its sections' distributions, on a grid of kBin seconds, integrated
// backwards; the footer prints the first bin at -30 dB and at -60 dB.
// The estimate ignores the phase of the echoes; test 10 compares it with
// the decay measured on the engine.
#pragma once
#include "inphirev_dsp.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace inphirev {

constexpr double kBin = 0.1, kHorizon = 3000.0;

struct Decay { double t30, t60; };   // seconds; +inf when beyond kHorizon

inline Decay estimateDecay(const uint32_t* delays, int sections, double fs) {
    const int bins = (int)std::lround(kHorizon / kBin) + 1;
    const double g2 = kG * kG;
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
    // Backwards: the energy that arrives after bin b.
    double total = 0.0;
    for (double v : cur) total += v;
    const double inf = std::numeric_limits<double>::infinity();
    Decay d { inf, inf };
    double after = 0.0;
    std::vector<double> rest((size_t)bins);
    for (int b = bins - 1; b >= 0; --b) { rest[(size_t)b] = after; after += cur[(size_t)b]; }
    for (int b = 0; b < bins; ++b) {
        if (d.t30 == inf && rest[(size_t)b] <= 1e-3 * total) d.t30 = b * kBin;
        if (rest[(size_t)b] <= 1e-6 * total) { d.t60 = b * kBin; break; }
    }
    return d;
}

} // namespace inphirev
