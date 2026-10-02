#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_primes.h"
#include <cmath>
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

// ── sma.imt2npsamp: metres to a prime number of samples (delRM, DDELAY) ──
// The Faust, transcribed: mm rounded to the millimetre, 331.4 m/s, rounded
// to the sample, the prime strictly above (nextprime.h by trial division).
static uint32_t imt2npsampRef(double mt, double fs) {
    const double mm = std::floor(mt * 1000.0 + 0.5) / 1000.0;
    const long n = (long)std::floor(mm * fs / 331.4 + 0.5);
    if (n < 2) return n < 0 ? 0u : (uint32_t)n;
    uint32_t c = (uint32_t)n + 1;
    while (!trialPrime(c)) ++c;
    return c;
}

TEST_CASE("metresToPrimeSamples equals sma.imt2npsamp over 0-30 m in 1 mm steps") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        const Seam::PrimeSieve s((uint32_t)std::floor(30.0 * fs / 331.4 + 0.5) + 1024u);
        long bad = 0;
        for (int mm = 0; mm <= 30000; ++mm) {
            const double mt = mm / 1000.0;
            if (Seam::metresToPrimeSamples(mt, fs, s) != imt2npsampRef(mt, fs)) ++bad;
        }
        CAPTURE(fs);
        CHECK(bad == 0);
    }
}

TEST_CASE("metresToPrimeSamples: the values the library comments quote") {
    const Seam::PrimeSieve s(40000);
    CHECK(Seam::metresToPrimeSamples(7.291, 96000.0, s) == 2113);   // Davide's 22 ms at 96 kHz
    CHECK(Seam::metresToPrimeSamples(7.291, 48000.0, s) == 1061);
    CHECK(Seam::metresToPrimeSamples(30.0, 192000.0, s) == 17383);
    CHECK(Seam::metresToPrimeSamples(30.0, 384000.0, s) == 34763);  // past the spec's 1 << 15
    CHECK(Seam::metresToPrimeSamples(0.0, 96000.0, s) == 0);         // below 2: kept as it is
    CHECK(Seam::metresToPrimeSamples(0.003, 96000.0, s) == 1);       // 0.869 -> 1, kept
}

TEST_CASE("metresToPrimeSamples rounds to the millimetre before converting") {
    const Seam::PrimeSieve s(40000);
    // 7.2914 m and 7.2906 m are both 7.291 m to the millimetre.
    CHECK(Seam::metresToPrimeSamples(7.2914, 96000.0, s) == 2113);
    CHECK(Seam::metresToPrimeSamples(7.2906, 96000.0, s) == 2113);
    // 7.0372 m is 7.037 m to the millimetre. Rounded: n = 2038, prime above 2039.
    // Unrounded: n = 2039 (itself prime), prime above 2053. Only the rounding
    // gives 2039.
    CHECK(Seam::metresToPrimeSamples(7.0372, 96000.0, s) == 2039);
}
