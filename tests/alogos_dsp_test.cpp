#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "alogos_dsp.h"
#include "ref/alogos_inphirev_table.h"
#include "ref/alogos_ref.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace alogos;

static const Scale kScales[] = { Scale::Phi, Scale::Sqrt2, Scale::E, Scale::Pi };
static const char* kScaleNames[] = { "phi", "sqrt2", "e", "pi" };
static const int kDens[] = { 1, 2, 4 };

static const uint32_t (*inphirevFor(double fs))[15][4][18] {
    if (fs == 44100.0) return alogosref::kInphirev44;
    if (fs == 48000.0) return alogosref::kInphirev48;
    if (fs == 96000.0) return alogosref::kInphirev96;
    return alogosref::kInphirev192;
}

static std::set<uint32_t> delaySet(Scale s, int D, int id, int j, int N, double fs,
                                   const Seam::PrimeSieve& sv) {
    std::set<uint32_t> out;
    const double d = gridMetres(s, D, id, j);
    for (int k = 0; k < kFaces; ++k)
        for (int i = 0; i < N; ++i) out.insert(sectionDelay(s, D, d, i, k, fs, sv));
    return out;
}

TEST_CASE("default setting is INPHIREV") {
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        Seam::PrimeSieve sv(sieveBound(fs));
        auto ref = inphirevFor(fs);
        for (int id = 0; id <= kMaxStoneId; ++id)
            for (int j = 0; j < 15; ++j)
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 18; ++i)
                        REQUIRE(sectionDelay(Scale::Phi, 1, gridMetres(Scale::Phi, 1, id, j), i, k, fs, sv)
                                == ref[id][j][k][i]);
    }
}

static uint64_t fnv(uint64_t h, uint32_t v) {
    for (int b = 0; b < 4; ++b) { h ^= (v >> (8 * b)) & 0xff; h *= 0x100000001b3ULL; }
    return h;
}

TEST_CASE("delay table equals sgs.aldel") {
    long hashes = 0, edges = 0;
    for (int rate = 0; rate < 2; ++rate) {
        const double fs = rate == 0 ? 48000.0 : 96000.0;
        Seam::PrimeSieve sv(sieveBound(fs));
        for (int s = 0; s < 4; ++s)
            for (int dn = 0; dn < 3; ++dn)
                for (int id = 0; id < 5; ++id) {
                    const int D = kDens[dn], J = alogosref::kJ[s][dn];
                    REQUIRE(gridMax(kScales[s], D) == J);
                    for (int j = 0; j <= J; ++j) {
                        const double d = gridMetres(kScales[s], D, id, j);
                        REQUIRE(d == alogosref::kAlgrid[s][dn][id][j]);
                        const int N = alogosref::kNmax[s][dn][id][j];
                        REQUIRE(nMax(kScales[s], D, d) == N);
                        uint64_t h = 0xcbf29ce484222325ULL;
                        for (int k = 0; k < 4; ++k)
                            for (int i = 0; i < N; ++i)
                                h = fnv(h, sectionDelay(kScales[s], D, d, i, k, fs, sv));
                        const uint64_t want = rate == 0 ? alogosref::kAldelHash48[s][dn][id][j]
                                                        : alogosref::kAldelHash96[s][dn][id][j];
                        CHECK_MESSAGE(h == want, kScaleNames[s], " D=", D, " id=", id, " j=", j, " fs=", fs);
                        ++hashes;
                    }
                    for (int side = 0; side < 2; ++side) {
                        const int j = side ? J : 0;
                        const double d = gridMetres(kScales[s], D, id, j);
                        const int N = rate == 0 ? alogosref::kAldelEdgeNmax48[s][dn][id][side]
                                                : alogosref::kAldelEdgeNmax96[s][dn][id][side];
                        const int off = rate == 0 ? alogosref::kAldelEdgeOff48[s][dn][id][side]
                                                  : alogosref::kAldelEdgeOff96[s][dn][id][side];
                        const uint32_t* list = (rate == 0 ? alogosref::kAldelEdge48 : alogosref::kAldelEdge96) + off;
                        for (int k = 0; k < 4; ++k)
                            for (int i = 0; i < N; ++i)
                                REQUIRE(sectionDelay(kScales[s], D, d, i, k, fs, sv) == list[k * N + i]);
                        ++edges;
                    }
                }
    }
    std::printf("[contact] hashes checked: %ld, edge lists checked: %ld\n", hashes, edges);
}

TEST_CASE("grid range") {
    static const int want[4][3] = {{14,28,58},{19,40,81},{6,13,27},{5,11,24}};
    for (int s = 0; s < 4; ++s)
        for (int dn = 0; dn < 3; ++dn) {
            const int D = kDens[dn], J = gridMax(kScales[s], D);
            CHECK(J == want[s][dn]);
            for (int id = 0; id <= kMaxStoneId; ++id) {
                CHECK(gridMetres(kScales[s], D, id, 0) >= 1.0);
                CHECK(gridMetres(kScales[s], D, id, J) <= 6.0);
            }
        }
    CHECK(density(0) == 1); CHECK(density(1) == 2); CHECK(density(2) == 4);
    CHECK(ratio(Scale::Phi) == doctest::Approx((1 + std::sqrt(5.0)) / 2));
    CHECK(gainValue(Gain::InvPhi) == doctest::Approx(0.6180339887498949));
    CHECK(gainValue(Gain::Diffusion) == doctest::Approx(std::sqrt(std::sqrt(2.0) - 1.0)));
}

TEST_CASE("snap is the nearest grid point") {
    for (Scale sc : kScales)
        for (int D : kDens)
            for (int id = 0; id <= kMaxStoneId; ++id) {
                const int J = gridMax(sc, D);
                for (int j = 0; j <= J; ++j)
                    REQUIRE(snapGrid(sc, D, id, gridMetres(sc, D, id, j)) == j);
                for (int t = 0; t < 1000; ++t) {
                    const double m = 1.0 + 5.0 * t / 999.0;
                    const int j = snapGrid(sc, D, id, m);
                    const double mine = std::fabs(std::log(gridMetres(sc, D, id, j) / m));
                    for (int q = 0; q <= J; ++q)
                        REQUIRE(mine <= std::fabs(std::log(gridMetres(sc, D, id, q) / m)) + 1e-12);
                }
            }
    CHECK(snapGrid(Scale::Phi, 1, 1, kDefaultMetres) == 10);
    CHECK(gridMetres(Scale::Phi, 1, 1, 10) == doctest::Approx(3.3302).epsilon(1e-4));
}

TEST_CASE("N_max is a time") {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    for (Scale sc : kScales)
        for (int D : kDens)
            for (int t = 0; t < 50; ++t) {
                const double m = 1.0 + 5.0 * t / 49.0;
                const int N = nMax(sc, D, m);
                auto longest = [&](int n) { return m * std::pow(ratio(sc), (n - 1 + 0.75) / D) / c; };
                CHECK(longest(N) <= 90.0);
                if (N < kMaxSections) CHECK(longest(N + 1) > 90.0);
            }
    CHECK(nMax(Scale::Phi, 1, 3.33) == 19);
    CHECK(nMax(Scale::Phi, 2, 3.33) == 38);
    CHECK(nMax(Scale::Phi, 4, 3.33) == 76);
    CHECK(nMax(Scale::Pi, 1, 3.33) == 8);
}

TEST_CASE("faces increasing; collisions reported") {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    for (double fs : { 48000.0, 96000.0 }) {
        Seam::PrimeSieve sv(sieveBound(fs));
        for (int s = 0; s < 4; ++s) {
            for (int D : kDens) {
                long shared = 0, settings = 0;
                for (int id = 0; id <= 4; ++id)
                    for (int j = 0; j <= gridMax(kScales[s], D); ++j) {
                        const double d = gridMetres(kScales[s], D, id, j);
                        const int N = nMax(kScales[s], D, d);
                        std::set<uint32_t> seen;
                        for (int k = 0; k < kFaces; ++k) {
                            uint32_t prev = 0;
                            double xprev = 0;
                            for (int i = 0; i < N; ++i) {
                                const uint32_t n = sectionDelay(kScales[s], D, d, i, k, fs, sv);
                                const double x = fs * d * std::pow(ratio(kScales[s]), (i + k / 4.0) / D) / c;
                                if (i > 0 && x - xprev > 2.0 * std::log(std::max(x, 3.0)))
                                    REQUIRE(n > prev);
                                if (!seen.insert(n).second) ++shared;
                                prev = n; xprev = x;
                            }
                        }
                        ++settings;
                    }
                std::printf("[contact] fs=%g %s D=%d: %ld shared delays in %ld settings\n",
                            fs, kScaleNames[s], D, shared, settings);
            }
        }
    }
}

TEST_CASE("ids 1 and 2 at phi, D = 1 never share a delay") {
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        Seam::PrimeSieve sv(sieveBound(fs));
        for (int j = 0; j < 15; ++j) {
            auto a = delaySet(Scale::Phi, 1, 1, j, 18, fs, sv);
            auto b = delaySet(Scale::Phi, 1, 2, j, 18, fs, sv);
            for (uint32_t v : a) REQUIRE(b.count(v) == 0);
        }
    }
}

TEST_CASE("contacts between ids 1-4 are short") {
    const double fs = 96000.0;
    Seam::PrimeSieve sv(sieveBound(fs));
    for (int s = 0; s < 4; ++s)
        for (int D : kDens) {
            uint32_t longest = 0;
            for (int j = 0; j <= gridMax(kScales[s], D); ++j)
                for (int a = 1; a <= 4; ++a)
                    for (int b = a + 1; b <= 4; ++b) {
                        const int N = std::min(nMax(kScales[s], D, gridMetres(kScales[s], D, a, j)),
                                               nMax(kScales[s], D, gridMetres(kScales[s], D, b, j)));
                        auto sa = delaySet(kScales[s], D, a, j, N, fs, sv);
                        auto sb = delaySet(kScales[s], D, b, j, N, fs, sv);
                        for (uint32_t v : sa) if (sb.count(v)) longest = std::max(longest, v);
                    }
            const double ms = 1000.0 * longest / fs;
            std::printf("[contact] ids 1-4, %s D=%d at 96 kHz: longest shared delay %u samples = %.2f ms\n",
                        kScaleNames[s], D, longest, ms);
            if (s == 0) CHECK(ms <= 50.0);
        }
}
