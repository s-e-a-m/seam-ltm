//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — scales, grid and delay table (SDK-free)
//
// FAUST REFERENCE (seam.silvi.lib):
//   algrid(r, D, id, j) = pow(r, (j + alphase(id))/(4*D));
//   aldel(r, D, d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(r, (i + k/4)/D) / sma.rsos + 0.5));
//   };
//
// Four faces (k) of up to 128 all-pass sections (i). Section i of face k
// lasts d * r^((i + k/4)/D) / c seconds, rounded to the sample and moved to
// the next prime strictly above. The scale r is phi, sqrt 2, e or pi; the
// density D is 1, 2 or 4 sections per factor r^(1/D). The distance d lives on
// a grid per STONE id, in the van der Corput order of phases, so no two
// STONEs play the same spiral. With phi and D = 1 the table is INPHIREV's.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_basics.h"
#include "seam_primes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace alogos {

enum class Scale : int { Phi = 0, Sqrt2, E, Pi };
enum class Gain : int { InvPhi = 0, InvSqrt2, InvE, InvPi, Diffusion };

constexpr int kFaces = 4, kMaxSections = 128, kDefaultSections = 17, kMaxStoneId = 8;
constexpr double kDefaultMetres = 3.33, kMinMetres = 1.0, kMaxMetres = 6.0, kCapSeconds = 90.0;

// The scale r of the spiral.
inline double ratio(Scale s) {
    switch (s) {
        case Scale::Phi:   return (1.0 + std::sqrt(5.0)) / 2.0;
        case Scale::Sqrt2: return std::sqrt(2.0);
        case Scale::E:     return std::exp(1.0);
        case Scale::Pi:    return 3.14159265358979323846;
    }
    return 1.0;
}

// The density menu index 0, 1, 2 as sections per factor: 1, 2, 4.
inline int density(int idx) { return idx <= 0 ? 1 : (idx == 1 ? 2 : 4); }

// The all-pass gain g of the menu.
inline double gainValue(Gain g) {
    switch (g) {
        case Gain::InvPhi:    return 2.0 / (1.0 + std::sqrt(5.0));
        case Gain::InvSqrt2:  return 1.0 / std::sqrt(2.0);
        case Gain::InvE:      return 1.0 / std::exp(1.0);
        case Gain::InvPi:     return 1.0 / 3.14159265358979323846;
        case Gain::Diffusion: return std::sqrt(std::sqrt(2.0) - 1.0);
    }
    return 1.0;
}

// alphase: the van der Corput phase of a STONE id; 0 for id 0 (undeclared)
// and for an id out of range.
inline double gridPhase(int stoneId) {
    static const double kTable[kMaxStoneId + 1] =
        { 0.0, 0.0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875 };
    return (stoneId >= 0 && stoneId <= kMaxStoneId) ? kTable[stoneId] : 0.0;
}

// J: the largest j for which the last STONE (phase 7/8) stays within 6 m.
inline int gridMax(Scale s, int D) {
    const double r = ratio(s);
    int j = 0;
    while (std::pow(r, (j + 1 + 0.875) / (4.0 * D)) <= kMaxMetres) ++j;
    return j;
}

// algrid: r^((j + u)/(4 D)) metres.
inline double gridMetres(Scale s, int D, int id, int j) {
    return std::pow(ratio(s), (j + gridPhase(id)) / (4.0 * D));
}

// The grid point nearest in ratio to a distance: j = clamp(round(4 D log_r(m) - u), 0, J).
inline int snapGrid(Scale s, int D, int id, double metres) {
    const double x = 4.0 * D * std::log(metres) / std::log(ratio(s)) - gridPhase(id);
    return std::clamp((int)std::floor(x + 0.5), 0, gridMax(s, D));
}

// N_max: the largest N >= 1 (at most kMaxSections) whose last section of the
// longest face, d * r^((N - 1 + 3/4)/D) / c, lasts no more than kCapSeconds.
inline int nMax(Scale s, int D, double metres) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const double r = ratio(s);
    int n = 1;
    while (n < kMaxSections &&
           metres * std::pow(r, (n + 3.0 / 4.0) / D) / c <= kCapSeconds) ++n;
    return n;
}

// aldel: n kept when below 2, otherwise the prime strictly above n.
inline uint32_t sectionDelay(Scale s, int D, double metres, int i, int k, double fs,
                             const Seam::PrimeSieve& sieve) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const int n = (int)std::floor(fs * metres * std::pow(ratio(s), (i + k / 4.0) / D) / c + 0.5);
    return n < 2 ? (uint32_t)n : sieve.nextPrimeAbove((uint32_t)n);
}

// The sieve must reach the prime above any delay: the 90 s cap, rounded, plus
// room for one prime gap (1024 is above every gap below 2^32).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(fs * kCapSeconds + 0.5) + 1024u;
}

} // namespace alogos
