#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_noise.h"
#include <set>
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

TEST_CASE("MultinoiseBlock(72, 8, 64) equals sdt.choirnoise(4) bit for bit") {
    Seam::MultinoiseBlock nz(72, 8, 64);
    double v[64];
    int mismatches = 0;
    for (int k = 0; k < 128; ++k) {
        nz.tick(v);
        for (int c = 0; c < 64; ++c)
            if (v[c] != lmoref::kChoirNoise[c][k]) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("blocks of one generator never share a value") {
    Seam::MultinoiseBlock lmo(72, 0, 8), choir(72, 8, 64);
    double a[8], b[64];
    std::set<double> seen;
    for (int k = 0; k < 4096; ++k) { lmo.tick(a); for (double x : a) seen.insert(x); }
    int shared = 0;
    for (int k = 0; k < 4096; ++k) { choir.tick(b); for (double x : b) shared += (int)seen.count(x); }
    CHECK(shared == 0);
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
