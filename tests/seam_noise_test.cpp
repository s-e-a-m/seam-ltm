#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_noise.h"
#include "ref/lmo_ref.h"

using Seam::FaustMultinoise;

TEST_CASE("FaustMultinoise(8) equals no.multinoise(8) bit for bit") {
    FaustMultinoise nz(8);
    double v[8];
    int mismatches = 0;
    for (int k = 0; k < 512; ++k) {
        nz.tick(v);
        for (int c = 0; c < 8; ++c)
            if (v[c] != lmoref::kNoise8[c][k]) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("reset returns to the start of the sequence") {
    FaustMultinoise nz(8);
    double a[8], b[8];
    nz.tick(a);
    for (int k = 0; k < 100; ++k) nz.tick(b);
    nz.reset();
    nz.tick(b);
    for (int c = 0; c < 8; ++c) CHECK(a[c] == b[c]);
}

TEST_CASE("a different seed gives a different sequence") {
    FaustMultinoise a(4), b(4, 54321);
    double va[4], vb[4];
    a.tick(va); b.tick(vb);
    CHECK(va[0] != vb[0]);
}

TEST_CASE("values lie in [-1, 1]") {
    FaustMultinoise nz(8);
    double v[8];
    for (int k = 0; k < 100000; ++k) {
        nz.tick(v);
        for (double x : v) { REQUIRE(x >= -1.0); REQUIRE(x <= 1.0); }
    }
}
