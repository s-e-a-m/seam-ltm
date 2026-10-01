#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_dsp.h"
#include "stunedrev_burst.h"
#include "ref/stunedrev_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace stunedrev;

// ── Test 2 of the spec: the 16 800 delays equal sdt.stdel, exactly ─────────
static long delayMismatches(double fs, const int (*ref)[kLines * kSections]) {
    const Seam::PrimeSieve s(sieveBound(fs));
    long bad = 0;
    for (int ms = kTMin; ms <= kTMax; ++ms)
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i)
                if ((int)sectionDelay(j, i, ms, fs, s) != ref[ms - 1][j * kSections + i]) ++bad;
    return bad;
}

TEST_CASE("delays equal sdt.stdel at 96 kHz, every ms, section and line") {
    CHECK(delayMismatches(96000.0, stunedrevref::kStdel96) == 0);
}

TEST_CASE("delays equal sdt.stdel at 48 kHz") {
    CHECK(delayMismatches(48000.0, stunedrevref::kStdel48) == 0);
}

// ── Test 6: the arena holds every delay the slider can ask ────────────────
TEST_CASE("every section's length holds its delay at every ms, at every rate up to 384 kHz") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0}) {
        const Seam::PrimeSieve s(sieveBound(fs));
        long bad = 0, zero = 0;
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i) {
                const std::size_t len = sectionLength(j, i, fs, s);
                for (int ms = kTMin; ms <= kTMax; ++ms) {
                    const uint32_t t = sectionDelay(j, i, ms, fs, s);
                    if (t == 0) ++zero;                 // the sieve ran out
                    if ((std::size_t)t + 1 > len) ++bad;
                }
            }
        CAPTURE(fs);
        CHECK(zero == 0);
        CHECK(bad == 0);
    }
}

TEST_CASE("the arena at 96 kHz is about 588 MiB, reported") {
    const Seam::PrimeSieve s(sieveBound(96000.0));
    std::size_t total = 0;
    for (int j = 0; j < kLines; ++j)
        for (int i = 0; i < kSections; ++i) total += sectionLength(j, i, 96000.0, s);
    const double mib = (double)total * sizeof(double) / (1024.0 * 1024.0);
    MESSAGE("arena at 96 kHz: " << total << " doubles, " << mib << " MiB");
    CHECK(mib > 580.0);
    CHECK(mib < 596.0);
}
