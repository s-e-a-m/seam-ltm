#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "inphirev_dsp.h"
#include "ref/inphirev_ref.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace inphirev;

static const double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

typedef int (*RefTable)[15][72];
static const int (*refFor(double fs))[15][72] {
    if (fs == 44100.0) return inphirevref::kDel44;
    if (fs == 48000.0) return inphirevref::kDel48;
    if (fs == 96000.0) return inphirevref::kDel96;
    return inphirevref::kDel192;
}

static std::set<uint32_t> delaySet(int id, int j, double fs, const Seam::PrimeSieve& s) {
    std::set<uint32_t> out;
    for (int k = 0; k < kFaces; ++k)
        for (int i = 0; i < kMaxSections; ++i) out.insert(sectionDelay(gridMetres(id, j), i, k, fs, s));
    return out;
}

static bool intersects(const std::set<uint32_t>& a, const std::set<uint32_t>& b) {
    for (uint32_t v : a) if (b.count(v)) return true;
    return false;
}

TEST_CASE("c matches the library") {
    CHECK(Seam::speedOfSound(Seam::kRoomCelsius) == inphirevref::kTsos20);
}

TEST_CASE("delay table equals sgs.ipdel") {
    for (double fs : kRates) {
        const Seam::PrimeSieve s(sieveBound(fs));
        const auto ref = refFor(fs);
        long bad = 0;
        for (int id = 0; id <= kMaxStoneId; ++id)
            for (int j = 0; j <= kGridMax; ++j)
                for (int k = 0; k < kFaces; ++k)
                    for (int i = 0; i < kMaxSections; ++i)
                        if ((int)sectionDelay(gridMetres(id, j), i, k, fs, s) != ref[id][j][k * 18 + i]) ++bad;
        INFO("fs = " << fs);
        CHECK(bad == 0);
    }
}

TEST_CASE("faces disjoint and increasing") {
    for (double fs : kRates) {
        const Seam::PrimeSieve s(sieveBound(fs));
        for (int id = 0; id <= kMaxStoneId; ++id)
            for (int j = 0; j <= kGridMax; ++j) {
                INFO("fs = " << fs << " id = " << id << " j = " << j);
                CHECK(delaySet(id, j, fs, s).size() == (size_t)(kFaces * kMaxSections));
                for (int k = 0; k < kFaces; ++k)
                    for (int i = 1; i < kMaxSections; ++i)
                        CHECK(sectionDelay(gridMetres(id, j), i, k, fs, s) >
                              sectionDelay(gridMetres(id, j), i - 1, k, fs, s));
            }
    }
}

TEST_CASE("ids 1 and 2 never share a delay") {
    for (double fs : kRates) {
        const Seam::PrimeSieve s(sieveBound(fs));
        for (int j1 = 0; j1 <= kGridMax; ++j1) {
            const auto a = delaySet(1, j1, fs, s);
            for (int j2 = 0; j2 <= kGridMax; ++j2) {
                INFO("fs = " << fs << " j1 = " << j1 << " j2 = " << j2);
                CHECK_FALSE(intersects(a, delaySet(2, j2, fs, s)));
            }
        }
    }
}

TEST_CASE("other id pairs: shared delays are counted") {
    for (double fs : kRates) {
        const Seam::PrimeSieve s(sieveBound(fs));
        std::vector<std::vector<std::set<uint32_t>>> sets(kMaxStoneId + 1);
        for (int id = 1; id <= kMaxStoneId; ++id)
            for (int j = 0; j <= kGridMax; ++j) sets[id].push_back(delaySet(id, j, fs, s));
        for (int a = 1; a <= kMaxStoneId; ++a)
            for (int b = a + 1; b <= kMaxStoneId; ++b) {
                if (a == 1 && b == 2) continue;
                int n = 0;
                for (int j1 = 0; j1 <= kGridMax; ++j1)
                    for (int j2 = 0; j2 <= kGridMax; ++j2)
                        if (intersects(sets[a][j1], sets[b][j2])) ++n;
                MESSAGE("fs " << fs << " ids " << a << "," << b << ": " << n << " of 225 (j1,j2) pairs share a delay");
            }
    }
}

TEST_CASE("grid range") {
    for (int id = 0; id <= kMaxStoneId; ++id) {
        CHECK(gridMetres(id, 0) >= 1.0);
        CHECK(gridMetres(id, kGridMax) < 6.0);
    }
    CHECK(gridMetres(1, 10) == doctest::Approx(3.330).epsilon(1e-3));
}
