//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — the grid and the delay table (SDK-free)
//
// FAUST REFERENCE (seam.silvi.lib):
//   ipphase(id) = ba.take(max(id,1), (0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875));
//   ipgrid(id, j) = pow(sma.phi(1), (j + ipphase(id))/4);
//   ipdel(d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(sma.phi(1), i + k/4) / sma.rsos + 0.5));
//   };
//
// Four faces (k) of up to 18 all-pass sections (i). Section i of face k lasts
// d * phi^(i + k/4) / c seconds, rounded to the sample and moved to the next
// prime strictly above. The distance d lives on a grid per STONE id, in the
// van der Corput order of phases, so no two STONEs play the same spiral.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_basics.h"
#include "seam_primes.h"
#include <cmath>
#include <cstdint>

namespace inphirev {

constexpr int kFaces = 4, kMaxSections = 18, kDefaultSections = 17;
constexpr int kGridMax = 14, kDefaultGrid = 10, kMaxStoneId = 8;

// sma.phi(1) and g = 1/phi (sma.rphi(1)).
inline const double kPhi = (1.0 + std::sqrt(5.0)) / 2.0;
inline const double kG   = 1.0 / kPhi;

// ipphase: the van der Corput phase of a STONE id; 0 for id 0 (undeclared)
// and for an id out of range.
inline double gridPhase(int stoneId) {
    static const double kTable[kMaxStoneId + 1] =
        { 0.0, 0.0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875 };
    return (stoneId >= 0 && stoneId <= kMaxStoneId) ? kTable[stoneId] : 0.0;
}

// ipgrid: phi^((j + u)/4) metres.
inline double gridMetres(int stoneId, int j) {
    return std::pow(kPhi, (j + gridPhase(stoneId)) / 4.0);
}

// ipdel: n kept when below 2, otherwise the prime strictly above n.
inline uint32_t sectionDelay(double metres, int i, int k, double fs, const Seam::PrimeSieve& s) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const int n = (int)std::floor(fs * metres * std::pow(kPhi, i + k / 4.0) / c + 0.5);
    return n < 2 ? (uint32_t)n : s.nextPrimeAbove((uint32_t)n);
}

// The largest n of the plugin (id 8, j = 14, section 17, face 3) plus room
// for one prime gap: 1024 is above every gap below 2^32 (the largest is 336).
inline uint32_t sieveBound(double fs) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const double d = gridMetres(kMaxStoneId, kGridMax);
    return (uint32_t)std::floor(fs * d * std::pow(kPhi, (kMaxSections - 1) + 3 / 4.0) / c + 0.5) + 1024u;
}

} // namespace inphirev
