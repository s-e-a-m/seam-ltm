#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_primes.h"
#include <cstdint>
#include <vector>

using Seam::PrimeSieve;

// faust-libraries src/h/nextprime.h, is_prime: the reference of sff.np.
static bool trialPrime(uint32_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if ((n & 1) == 0) return false;
    for (uint32_t i = 3; (uint64_t)i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

// The largest n stunedrev asks at 192 kHz is round(100*42*pi*192) =
// 2 533 274; its sieve bound adds 1024.
static constexpr uint32_t kBound192 = 2533274u + 1024u;

TEST_CASE("isPrime equals trial division for every n up to the 192 kHz bound") {
    const PrimeSieve s(kBound192);
    long mismatches = 0;
    for (uint32_t n = 0; n <= kBound192; ++n)
        if (s.isPrime(n) != trialPrime(n)) ++mismatches;
    CHECK(mismatches == 0);
}

TEST_CASE("nextPrimeAbove equals nextprime.h's next_pr for every n, strictly greater") {
    const PrimeSieve s(kBound192);
    // Walk down: `above` is the smallest prime strictly greater than n.
    uint32_t above = 0;
    long mismatches = 0, notGreater = 0;
    for (uint32_t n = kBound192 + 1; n-- > 0; ) {
        const uint32_t got = s.nextPrimeAbove(n);
        if (got != above) ++mismatches;
        if (got != 0 && got <= n) ++notGreater;
        if (trialPrime(n)) above = n;
    }
    CHECK(mismatches == 0);
    CHECK(notGreater == 0);
    CHECK(s.nextPrimeAbove(0) == 2);
    CHECK(s.nextPrimeAbove(1) == 2);
    CHECK(s.nextPrimeAbove(2) == 3);
    CHECK(s.nextPrimeAbove(4) == 5);
}

TEST_CASE("msToPrimeSamples is sma.ms2npsamp: round, keep below 2, else the prime above") {
    const PrimeSieve s(100000);
    CHECK(Seam::msToPrimeSamples(0.01, 96000.0, s) == 1);        // 0.96 -> 1, kept
    CHECK(Seam::msToPrimeSamples(1.0, 96000.0, s) == 97);        // 96 -> 97
    CHECK(Seam::msToPrimeSamples(97.0 / 96.0, 96000.0, s) == 101); // 97 is prime: stepped over
    CHECK(Seam::msToPrimeSamples(1.0, 44100.0, s) == 47);        // 44.1 -> 44 -> 47
    CHECK(Seam::msToPrimeSamples(10.5 / 96.0, 96000.0, s) == 13);  // floor(10.5 + 0.5) = 11 -> 13
}
