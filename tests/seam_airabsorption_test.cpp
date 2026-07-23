#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_airabsorption.h"
#include <cmath>

using namespace Seam::air;

// Reference conditions: 20 C, 50% RH, 1 atm. These BRACKETS are order-of-
// magnitude anchors (dB/m). Before merge, pin exact ISO 9613-1 Table values
// (or the NPL calculator) and tighten — see Step 5's provenance note.
TEST_CASE("alpha is small at low f, grows with frequency (20C/50%RH/1atm)") {
    const double a125  = alphaISO9613(125.0,   20.0, 50.0);
    const double a1k   = alphaISO9613(1000.0,  20.0, 50.0);
    const double a4k   = alphaISO9613(4000.0,  20.0, 50.0);
    const double a10k  = alphaISO9613(10000.0, 20.0, 50.0);

    CHECK(a125 < a1k);
    CHECK(a1k  < a4k);
    CHECK(a4k  < a10k);

    // 1 kHz ~ a few dB/km = a few 1e-3 dB/m; 10 kHz ~ 1e-1 dB/m.
    CHECK(a1k  == doctest::Approx(0.005).epsilon(0.6));   // 0.002..0.008 dB/m
    CHECK(a10k > 0.03);
    CHECK(a10k < 0.30);
    CHECK(a125 > 0.0);
}

TEST_CASE("alpha stays finite and positive at humidity and temperature edges") {
    CHECK(alphaISO9613(4000.0, 20.0,  0.0) > 0.0);   // bone-dry
    CHECK(alphaISO9613(4000.0, 20.0, 100.0) > 0.0);  // saturated
    CHECK(std::isfinite(alphaISO9613(8000.0, -20.0, 10.0)));
    CHECK(std::isfinite(alphaISO9613(8000.0,  50.0, 90.0)));
}

// Dry air absorbs high frequencies MORE than very humid air in the mid band
// is a common misconception; the ISO curve peaks at intermediate humidity.
// Just assert humidity actually changes the result (not a stubbed constant).
TEST_CASE("humidity changes alpha") {
    CHECK(alphaISO9613(4000.0, 20.0, 20.0) != alphaISO9613(4000.0, 20.0, 80.0));
}
