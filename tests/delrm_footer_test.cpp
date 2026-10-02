#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_footer.h"
#include "delrm_dsp.h"
#include <cstring>

using namespace delrm;

TEST_CASE("the footer line reads D in ms, samples and the rate") {
    char s[64];
    formatDelayLine(s, sizeof s, 2903, 96000.0);
    CHECK(std::string(s) == "D 30.24 ms 2903 samples @ 96 kHz");
    formatDelayLine(s, sizeof s, 1061, 44100.0);
    CHECK(std::string(s) == "D 24.06 ms 1061 samples @ 44.1 kHz");
    formatDelayLine(s, sizeof s, 0, 0.0);
    CHECK(std::string(s) == "D \xE2\x80\x94 inactive");
}

// The view is 260 px wide and InfoFont 12 is monospaced at about 7.2 px a
// character: 36 characters fit. "D 30.24 ms · 2903 samples @ 96.0 kHz" did
// not, and was cut in Reaper (2026-10-02).
TEST_CASE("at 30 m the line fits the footer at every rate up to 384 kHz") {
    char s[64];
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0}) {
        const Seam::PrimeSieve sieve(sieveBound(fs));
        formatDelayLine(s, sizeof s, delayFor(kMaxMetres, fs, sieve), fs);
        CAPTURE(s);
        CHECK(std::strlen(s) <= kFooterChars);
    }
}
