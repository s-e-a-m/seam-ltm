//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the engine (SDK-free)
//
// Four independent lines of 42 Moorer all-pass sections in series, each
// line tuned by an irrational ratio k (sdt.stunedrev, seam.tedesco.lib).
// The delay of section i is ms·(i+1)·k, rounded to the sample and moved to
// the next prime at the session's rate (sdt.stdel); each section's buffer is
// sized exactly for the longest delay the slider can ask, 100 ms, in one
// arena allocated outside the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_moorer.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace stunedrev {

constexpr int    kLines    = 4;
constexpr int    kSections = 42;
constexpr int    kTMin = 1, kTMax = 100;                     // ms, the score's slider
constexpr int    kDefaultTimes[kLines] = { 83, 47, 7, 71 };  // the Pd patch's start
constexpr double kShortRamp = 0.025;                          // s
// RESET zeroes the arena at this many bytes per sample of the block: the
// clearing lasts the same time at any block size and rate (588 MiB at
// 96 kHz in 0.39 s), and asks memset for 1.5 GB/s, a fraction of its speed.
constexpr std::size_t kClearBytesPerSample = 16384;

// g = 1/sqrt(2) as in the original; the ratios in the original's line order.
// ma.E and ma.PI are these doubles.
inline const double kG = 1.0 / std::sqrt(2.0);
inline const double kRatio[kLines] = {
    std::sqrt(2.0), (1.0 + std::sqrt(5.0)) / 2.0, 2.718281828459045, 3.141592653589793 };

// sdt.stdel(k, i, ms) = sma.ms2npsamp(ms*(i+1)*k): the product before the prime.
inline uint32_t sectionDelay(int line, int i, double ms, double fs, const Seam::PrimeSieve& s) {
    return Seam::msToPrimeSamples(ms * (i + 1) * kRatio[line], fs, s);
}

// Rounding and the prime above are both non-decreasing, so the longest
// delay of a section is the one at 100 ms; +1 holds w[n-(t-1)] with w[n].
// sdt.stmd sizes with a fixed +150 instead, because Faust sizes buffers at
// compile time and cannot evaluate sff.np there; exact sizing is right at
// any rate (at 384 kHz a prime gap of 154 would exceed the +150).
inline std::size_t sectionLength(int line, int i, double fs, const Seam::PrimeSieve& s) {
    return (std::size_t)sectionDelay(line, i, kTMax, fs, s) + 1;
}

// The largest n of the plugin (100 ms, section 42, pi) plus room for one
// prime gap: 1024 is above every gap below 2^32 (the largest is 336).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(kTMax * kSections * kRatio[3] * fs / 1000.0 + 0.5) + 1024u;
}

// Each section is a Seam::MoorerAllpass (seam_moorer.h), g = kG, attached
// to its slice of the arena.
using Section = Seam::MoorerAllpass;

} // namespace stunedrev
