//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_primes.h — prime delays without division on the audio thread
//
// FAUST REFERENCE:
//   sff.np         = ffunction(int next_pr(int), "../h/nextprime.h", "");
//                    the smallest prime STRICTLY greater than n
//   sma.ms2npsamp  = select2(n < 2, n : sff.np, n)
//                    with { n = int(floor(ms*ma.SR/1000 + 0.5)); };
//
// nextprime.h tests each candidate by trial division, up to ~800 divisions
// for a number near 2.5 million. A plugin that recomputes 42 delays when a
// slider moves cannot afford that inside process(), so the primes come from
// a sieve of Eratosthenes built once, outside the audio thread, over the
// odd numbers (one bit each: 317 KB at stunedrev's 192 kHz bound). A lookup
// then scans at most one prime gap (154 below 5 million).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace Seam {

class PrimeSieve {
public:
    // Allocates; call outside the audio thread.
    explicit PrimeSieve(uint32_t bound) : bound_(bound), odd_(bound / 2 + 1, true) {
        odd_[0] = false;                                     // 1 is not prime
        for (uint64_t p = 3; p * p <= bound_; p += 2)
            if (odd_[p / 2])
                for (uint64_t m = p * p; m <= bound_; m += 2 * p) odd_[m / 2] = false;
    }

    uint32_t bound() const { return bound_; }

    // Defined for n <= bound().
    bool isPrime(uint32_t n) const {
        if (n < 2) return false;
        if (n == 2) return true;
        if ((n & 1) == 0) return false;
        return odd_[n / 2];
    }

    // sff.np: the smallest prime strictly greater than n; 0 when none lies
    // within the bound (the caller sized the bound so that it never happens).
    uint32_t nextPrimeAbove(uint32_t n) const {
        if (n < 2) return 2;
        for (uint64_t c = (n & 1) ? (uint64_t)n + 2 : (uint64_t)n + 1; c <= bound_; c += 2)
            if (odd_[c / 2]) return (uint32_t)c;
        return 0;
    }

private:
    uint32_t bound_;
    std::vector<bool> odd_;   // odd_[j] stands for 2j+1
};

// sma.ms2npsamp: milliseconds to a prime number of samples at fs.
inline uint32_t msToPrimeSamples(double ms, double fs, const PrimeSieve& s) {
    const long n = (long)std::floor(ms * fs / 1000.0 + 0.5);
    if (n < 2) return n < 0 ? 0u : (uint32_t)n;
    return s.nextPrimeAbove((uint32_t)n);
}

} // namespace Seam
