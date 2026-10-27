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

// ══ The engine ═══════════════════════════════════════════════════════════
#include "stunedrev_burst.h"

static void settle(Engine& e, double fs, int id = 1, int j = 10, int n = kDefaultSections) {
    e.setSettings(id, j, n);                  // before prepare: taken at once
    REQUIRE(e.prepare(fs));
    e.setPower(true);
    e.reset();                                // every ramp onto its target
}

struct Capture {
    int seconds; long fs;
    std::vector<double> win, energy;          // [face][second][512], [face][second]
    Capture(int s, long rate) : seconds(s), fs(rate), win((size_t)4 * s * 512, 0.0), energy((size_t)4 * s, 0.0) {}
    double& w(int c, int s, int k) { return win[((size_t)c * seconds + s) * 512 + k]; }
    double& e(int c, int s) { return energy[(size_t)c * seconds + s]; }
};

// The burst through the engine in blocks; hook(pos) runs before the block
// that starts at pos.
template <class Hook>
static Capture run(Engine& e, long fs, int seconds, int block, Hook hook) {
    Capture cap(seconds, fs);
    Burst burst((double)fs);
    std::vector<double> ib((size_t)4 * block), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = ob.data() + (size_t)c * block; }
    const long total = fs * seconds;
    for (long pos = 0; pos < total; pos += block) {
        const int m = (int)std::min<long>(block, total - pos);
        hook(pos);
        burst.fill(in, m);
        e.process(in, out, m);
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < m; ++k) {
                const long g = pos + k; const int s = (int)(g / fs); const long off = g % fs;
                const double y = out[c][k];
                if (off < 512) cap.w(c, s, (int)off) = y;
                cap.e(c, s) += y * y;
            }
    }
    return cap;
}
static Capture run(Engine& e, long fs, int seconds, int block = 256) {
    return run(e, fs, seconds, block, [](long) {});
}

template <int S>
static double winErr(Capture& c, const double (*ref)[S][512]) {
    double err = 0.0, pk = 0.0;
    for (int l = 0; l < 4; ++l) for (int s = 0; s < S; ++s) for (int k = 0; k < 512; ++k) {
        err = std::max(err, std::fabs(c.w(l, s, k) - ref[l][s][k]));
        pk  = std::max(pk, std::fabs(ref[l][s][k]));
    }
    return err / pk;
}
template <int S>
static double energyErr(Capture& c, const double (*ref)[S]) {
    double err = 0.0, pk = 0.0;
    for (int l = 0; l < 4; ++l) for (int s = 0; s < S; ++s) {
        err = std::max(err, std::fabs(c.e(l, s) - ref[l][s]));
        pk  = std::max(pk, ref[l][s]);
    }
    return err / pk;
}

// Zeros through the engine in blocks of `block`, until the callback says stop.
template <class Stop>
static long feedZeros(Engine& e, int block, Stop stop, long guard = 100000000) {
    std::vector<double> ib((size_t)4 * block, 0.0), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = ob.data() + (size_t)c * block; }
    long n = 0;
    while (!stop() && n < guard) { e.process(in, out, block); n += block; }
    return n;
}

// Test 4 of the spec.
TEST_CASE("the engine equals sgs.inphirev(N, 1, 10), N = 1, 4, 17, at 96 and 48 kHz, over 30 s") {
    struct Row { int n; double fs;
                 const double (*w)[30][512]; const double (*e)[30]; };
    const Row rows[] = {
        { 1,  96000.0, inphirevref::kWin96_N1,  inphirevref::kWin96_N1_energy },
        { 4,  96000.0, inphirevref::kWin96_N4,  inphirevref::kWin96_N4_energy },
        { 17, 96000.0, inphirevref::kWin96_N17, inphirevref::kWin96_N17_energy },
        { 1,  48000.0, inphirevref::kWin48_N1,  inphirevref::kWin48_N1_energy },
        { 4,  48000.0, inphirevref::kWin48_N4,  inphirevref::kWin48_N4_energy },
        { 17, 48000.0, inphirevref::kWin48_N17, inphirevref::kWin48_N17_energy },
    };
    for (const Row& r : rows) {
        Engine e; settle(e, r.fs, 1, 10, r.n);
        Capture c = run(e, (long)r.fs, 30);
        const double we = winErr<30>(c, r.w), ee = energyErr<30>(c, r.e);
        MESSAGE("N = " << r.n << " at " << r.fs << " Hz: windows " << we << ", energy " << ee);
        CAPTURE(r.n); CAPTURE(r.fs);
        CHECK(we < 1e-12);
        CHECK(ee < 1e-10);
    }
}

// Test 5: a change of setting applies once, after 250 ms of calm.
TEST_CASE("a change applies after 250 ms, once, and ends as a fresh engine") {
    const double fs = 48000.0; const int B = 240;     // 0.1 s = 20 blocks, 0.35 s = 70 blocks
    Engine e; settle(e, fs);
    const uint32_t d00 = e.delay(0, 0);
    long changedAt = -1;
    Capture c = run(e, (long)fs, 3, B, [&](long pos) {
        if (pos == 0)    e.setSettings(2, 5, 10);
        if (pos == 4800) e.setSettings(3, 7, 12);     // restarts the pause
        if (changedAt < 0 && (e.delay(0, 0) != d00 || e.clears() > 0)) changedAt = pos;
    });
    MESSAGE("first effect at sample " << changedAt << " (0.35 s = 16800)");
    CHECK(changedAt >= 16800);
    CHECK(e.clears() == 1);
    CHECK(e.stoneId() == 3); CHECK(e.grid() == 7); CHECK(e.sections() == 12);
    Engine fresh; settle(fresh, fs, 3, 7, 12);
    Capture a = run(e, (long)fs, 2), b = run(fresh, (long)fs, 2);
    CHECK(a.win == b.win);
    CHECK(a.energy == b.energy);
}

TEST_CASE("a setting equal to the applied one starts nothing") {
    Engine e; settle(e, 48000.0);
    e.setSettings(1, 10, kDefaultSections);
    feedZeros(e, 256, [&, n = 0]() mutable { return ++n > 400; });
    CHECK(e.clears() == 0);
    e.setSettings(99, -4, 40);                          // clamped to (8, 0, 18)
    feedZeros(e, 256, [&, n = 0]() mutable { return ++n > 400; });
    CHECK(e.stoneId() == 8); CHECK(e.grid() == 0); CHECK(e.sections() == 18);
}

// Review fix: "?" and 1 are both phase 0, so the table and the section count
// are identical; only the readouts move, with no fade and no clear.
TEST_CASE("a setting with the identical table changes the readouts only: no clear, output continuous") {
    const double fs = 48000.0;
    Engine e; settle(e, fs, 0, 10, 17);
    Engine ref; settle(ref, fs, 0, 10, 17);
    std::vector<double> ib((size_t)4 * 256), oa((size_t)4 * 256), ob((size_t)4 * 256);
    double* in[4]; double* oe[4]; double* orf[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * 256; oe[c] = oa.data() + (size_t)c * 256; orf[c] = ob.data() + (size_t)c * 256; }
    Burst burst(fs);
    double worst = 0.0;
    for (int blk = 0; blk < 400; ++blk) {          // 2.1 s: past the 250 ms settle
        if (blk == 4) e.setSettings(1, 10, 17);
        burst.fill(in, 256);
        e.process(in, oe, 256);
        ref.process(in, orf, 256);
        for (int c = 0; c < 4; ++c) for (int k = 0; k < 256; ++k)
            worst = std::max(worst, std::fabs(oe[c][k] - orf[c][k]));
    }
    CHECK(e.clears() == 0);
    CHECK(e.stoneId() == 1);
    CHECK(worst == 0.0);                           // identical to the engine never touched
}

// Test 6: the arena holds every delay.
TEST_CASE("arena holds every delay: every id, grid point, section and face, at every rate") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        const Seam::PrimeSieve s(sieveBound(fs));
        long bad = 0, zero = 0;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i) {
                const std::size_t len = sectionLength(k, i, fs, s);
                for (int id = 1; id <= kMaxStoneId; ++id)
                    for (int j = 0; j <= kGridMax; ++j) {
                        const uint32_t t = sectionDelay(gridMetres(id, j), i, k, fs, s);
                        if (t == 0) ++zero;
                        if ((std::size_t)t + 1 > len) ++bad;
                    }
            }
        const double mib = (double)arenaDoubles(fs, s) * sizeof(double) / (1024.0 * 1024.0);
        MESSAGE("arena at " << fs << " Hz: " << mib << " MiB");
        CAPTURE(fs);
        CHECK(zero == 0);
        CHECK(bad == 0);
    }
    Engine e; settle(e, 48000.0);
    CHECK(e.arenaBytes() > 280.0 * 1024 * 1024);
    CHECK(e.arenaBytes() < 300.0 * 1024 * 1024);
}

// Test 7: RESET, as stunedrev's.
static void finishReset(Engine& e, double fs, int block) {
    feedZeros(e, block, [&] { return !e.clearing(); });
    REQUIRE(e.status() == Status::Ready);
    const int tail = (int)(0.05 * fs);
    long done = 0;
    feedZeros(e, block, [&] { const bool stop = done >= tail; done += block; return stop; });
}

TEST_CASE("after RESET the engine sounds as a fresh one, exactly") {
    for (int block : {256, 1, 4093}) {
        Engine e; settle(e, 48000.0, 1, 10, 8);
        run(e, 48000, 1);                     // the memory holds the burst
        e.requestReset();
        // the click is served by the first block: feed one before testing clearing()
        feedZeros(e, block, [n = 0]() mutable { return ++n > 1; });
        finishReset(e, 48000.0, block);
        Engine fresh; settle(fresh, 48000.0, 1, 10, 8);
        Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
        CAPTURE(block);
        CHECK(a.win == b.win);
        CHECK(a.energy == b.energy);
    }
}

// Review focus 4.
TEST_CASE("a RESET click during the clearing restarts it: the end is a fresh engine") {
    Engine e; settle(e, 48000.0, 1, 10, 8);
    run(e, 48000, 1);
    e.requestReset();
    feedZeros(e, 256, [n = 0]() mutable { return ++n > 20; });   // past the fade, inside the clear
    REQUIRE(e.clearing());
    e.requestReset();
    finishReset(e, 48000.0, 256);
    Engine fresh; settle(fresh, 48000.0, 1, 10, 8);
    Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
    CHECK(a.win == b.win);
    CHECK(a.energy == b.energy);
}

TEST_CASE("a setting arriving during the clearing: the end is a fresh engine with the last settings") {
    Engine e; settle(e, 48000.0, 1, 10, 8);
    run(e, 48000, 1);
    e.requestReset();
    feedZeros(e, 256, [n = 0]() mutable { return ++n > 20; });
    REQUIRE(e.clearing());
    e.setSettings(4, 3, 11);
    const long n = feedZeros(e, 256, [&] { return !e.clearing() && e.stoneId() == 4 && e.sections() == 11; }, 5000000);
    REQUIRE(n < 5000000);
    finishReset(e, 48000.0, 256);
    CHECK(e.delay(0, 0) == [&] { Seam::PrimeSieve s(sieveBound(48000.0));
                                 return sectionDelay(gridMetres(4, 3), 0, 0, 48000.0, s); }());
    Engine fresh; settle(fresh, 48000.0, 4, 3, 11);
    Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
    CHECK(a.win == b.win);
    CHECK(a.energy == b.energy);
}

// Test 9: the footer's readouts.
TEST_CASE("readouts equal the table: first and longest delay, metres") {
    for (double fs : {48000.0, 96000.0}) {
        for (int id : {1, 5}) {
            Engine e; settle(e, fs, id, 12, 9);
            const Seam::PrimeSieve s(sieveBound(fs));
            const double d = gridMetres(id, 12);
            CAPTURE(fs); CAPTURE(id);
            CHECK(e.metres() == d);
            CHECK(e.sections() == 9); CHECK(e.stoneId() == id); CHECK(e.grid() == 12);
            CHECK(e.delay(0, 0) == sectionDelay(d, 0, 0, fs, s));
            CHECK(e.delay(3, e.sections() - 1) == sectionDelay(d, 8, 3, fs, s));
        }
    }
}

// Review focus 1.
TEST_CASE("prepare at a new rate: delays equal the reference at that rate, both ways") {
    for (double from : {48000.0, 96000.0}) {
        const double to = from == 48000.0 ? 96000.0 : 48000.0;
        Engine e; settle(e, from);
        e.release();
        REQUIRE(e.prepare(to));
        const auto ref = refFor(to);
        long bad = 0;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i)
                if ((int)e.delay(k, i) != ref[1][10][k * 18 + i]) ++bad;
        CAPTURE(from); CAPTURE(to);
        CHECK(bad == 0);
        CHECK(e.sampleRate() == to);
    }
}

// Review focus 3.
TEST_CASE("allocation failure: prepare returns false, failed() is set, the outputs are zeros") {
    Engine e;
    CHECK_FALSE(e.prepare(48000.0, 100u * 1024 * 1024));    // arena is ~290 MiB
    CHECK(e.failed());
    CHECK(e.arenaBytes() == 0);
    double ib[4][64], ob[4][64];
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib[c]; out[c] = ob[c]; std::fill(ib[c], ib[c] + 64, 1.0); std::fill(ob[c], ob[c] + 64, 9.0); }
    CHECK_NOTHROW(e.process(in, out, 64));
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 64; ++k) CHECK(ob[c][k] == 0.0);
    // The same limit as the arena itself is enough.
    CHECK(e.prepare(48000.0, SIZE_MAX));
    CHECK_FALSE(e.failed());
}

// Review focus 5.
TEST_CASE("hours of silence after a tail: every output sample is zero or normal") {
    Engine e; settle(e, 48000.0, 1, 0, 2);     // short delays: the tail sinks through the subnormals
    std::vector<double> ib(4 * 4096, 0.0), ob(4 * 4096);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 4096; out[c] = ob.data() + c * 4096; }
    ib[0] = 1.0;                                // the impulse on face A
    long subnormal = 0, nonzeroLate = 0;
    for (long done = 0; done < 2000000; done += 4096) {
        e.process(in, out, 4096);
        ib[0] = 0.0;
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4096; ++k) {
                if (std::fpclassify(out[c][k]) == FP_SUBNORMAL) ++subnormal;
                if (done > 1000000 && out[c][k] != 0.0) ++nonzeroLate;
            }
    }
    MESSAGE("late nonzero samples: " << nonzeroLate);
    CHECK(subnormal == 0);
}
