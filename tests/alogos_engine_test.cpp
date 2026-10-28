#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "alogos_dsp.h"
#include "alogos_text.h"
#include "ref/alogos_ref.h"
#include "stunedrev_burst.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

using namespace alogos;

// ══ The audio thread does not allocate ═══════════════════════════════════
// Every global operator new/delete is replaced by a counting version. The
// counters move only while tArmed is set, and tArmed is a thread_local set
// only around Engine::process() on the test thread, so the broker's worker
// (which does allocate and free) is not counted.
static thread_local bool tArmed = false;
static std::atomic<long> gAudioNews{0}, gAudioDeletes{0};

static void* countedAlloc(std::size_t n) {
    if (tArmed) gAudioNews.fetch_add(1);
    return std::malloc(n ? n : 1);
}
static void* countedAligned(std::size_t n, std::align_val_t a) {
    if (tArmed) gAudioNews.fetch_add(1);
    void* p = nullptr;
    const std::size_t al = std::max((std::size_t)a, sizeof(void*));
    if (posix_memalign(&p, al, n ? n : 1) != 0) return nullptr;
    return p;
}
static void countedFree(void* p) {
    if (!p) return;
    if (tArmed) gAudioDeletes.fetch_add(1);
    std::free(p);
}

void* operator new(std::size_t n) { if (void* p = countedAlloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = countedAlloc(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return countedAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return countedAlloc(n); }
void* operator new(std::size_t n, std::align_val_t a) { if (void* p = countedAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = countedAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return countedAligned(n, a); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete(void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { countedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { countedFree(p); }

template <class T>
static void proc(Engine& e, const T* const* in, T* const* out, int n) {
    tArmed = true;
    e.process(in, out, n);
    tArmed = false;
}
#define CHECK_AUDIO_NO_ALLOC()                 \
    do {                                       \
        CHECK(gAudioNews.load() == 0);         \
        CHECK(gAudioDeletes.load() == 0);      \
    } while (0)

// ══ Helpers ══════════════════════════════════════════════════════════════

struct Set {
    int id = 1; double m = kDefaultMetres; int n = kDefaultSections;
    int scale = 0, dn = 0, g = 0;
};
static void apply(Engine& e, const Set& s) { e.setSettings(s.id, s.m, s.n, s.scale, s.dn, s.g); }

static void settle(Engine& e, double fs, const Set& s = Set{}) {
    apply(e, s);                              // before prepare: taken at once
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
        proc<double>(e, in, out, m);
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

// Zeros through the engine in blocks of `block`, until the callback says
// stop, or `wall` seconds of real time pass (the worker allocates in real
// time while the test runs faster than it). Returns the samples fed.
template <class Stop>
static long feedZeros(Engine& e, int block, Stop stop, double wall = 60.0) {
    std::vector<double> ib((size_t)4 * block, 0.0), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = ob.data() + (size_t)c * block; }
    const auto t0 = std::chrono::steady_clock::now();
    long n = 0;
    while (!stop()) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > wall) break;
        proc<double>(e, in, out, block); n += block;
    }
    return n;
}
static void untilAllocating(Engine& e) {
    feedZeros(e, 256, [&] { return e.status() == Status::Allocating; }, 5.0);
    REQUIRE(e.status() == Status::Allocating);
}
template <class Pred>
static bool waitFor(Pred p, double wall = 10.0) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!p()) {
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > wall) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
static long blocks(Engine& e, int count, int block = 256) {
    return feedZeros(e, block, [n = 0, count]() mutable { return n++ >= count; });
}

// A settled change carried to the end: Ready with the target's readouts,
// then 50 ms of zeros so the fade-in is over.
static void finish(Engine& e, double fs, const Set& s, int block = 256) {
    const long n = feedZeros(e, block, [&] {
        return e.status() == Status::Ready && e.stoneId() == s.id && e.scale() == s.scale &&
               e.densityIdx() == s.dn && e.gain() == s.g;
    });
    (void)n;
    REQUIRE(e.status() == Status::Ready);
    const int tail = (int)(0.05 * fs);
    long done = 0;
    feedZeros(e, block, [&] { const bool stop = done >= tail; done += block; return stop; });
}

static void sameAsFresh(Engine& e, double fs, const Set& s, int seconds = 2) {
    Engine fresh; settle(fresh, fs, s);
    Capture a = run(e, (long)fs, seconds), b = run(fresh, (long)fs, seconds);
    CHECK(a.win == b.win);
    CHECK(a.energy == b.energy);
    long bad = 0;
    for (int k = 0; k < kFaces; ++k)
        for (int i = 0; i < kMaxSections; ++i) if (e.delay(k, i) != fresh.delay(k, i)) ++bad;
    CHECK(bad == 0);
    CHECK(e.sections() == fresh.sections());
}

static std::size_t neededDoubles(const Engine& e) {
    std::size_t t = 0;
    for (int k = 0; k < kFaces; ++k)
        for (int i = 0; i < e.sections(); ++i) t += (std::size_t)e.delay(k, i) + 1;
    return t;
}

// Settings used below.
static const Set kDefault {};                                    // phi, D 1, 3.33 m, 17
static const Set kSqrt2D4 { 1, kDefaultMetres, 128, 1, 2, 1 };   // sqrt 2 * 4, N_max
static const Set kPi1     { 1, kDefaultMetres, 128, 3, 0, 3 };   // pi * 1, N_max = 8
static const Set kE2      { 1, kDefaultMetres, 128, 2, 1, 2 };   // e * 2, N_max
static const Set kTiny    { 1, 1.0, 4, 0, 0, 0 };                // a few KiB: always a new, small arena

// ══ Self-test of the allocation counter ══════════════════════════════════

// The probes go through a volatile pointer: a new-expression whose result
// never escapes may be elided (C++14), and the Release build does elide it.
static int* volatile gProbe = nullptr;

TEST_CASE("the allocation counter counts on the armed thread only") {
    tArmed = true;
    gProbe = new int(3);
    delete gProbe;
    tArmed = false;
    CHECK(gAudioNews.load() == 1);
    CHECK(gAudioDeletes.load() == 1);
    std::atomic<int> step{0};
    std::thread t([&] {
        while (step.load() == 0) {}
        gProbe = new int(4); delete gProbe;      // another thread, while this one is armed
        step.store(2);
    });
    tArmed = true;
    step.store(1);
    while (step.load() != 2) {}
    tArmed = false;
    t.join();
    CHECK(gAudioNews.load() == 1);
    CHECK(gAudioDeletes.load() == 1);
    gAudioNews.store(0); gAudioDeletes.store(0);
}

// ══ The broker ═══════════════════════════════════════════════════════════

TEST_CASE("broker: allocateNow, request, take, retire, cancel, limit") {
    {
        ArenaBroker b(1024 * sizeof(double));
        double* p = b.allocateNow(1000);
        REQUIRE(p != nullptr);
        bool zero = true;
        for (int i = 0; i < 1000; ++i) zero &= p[i] == 0.0;
        CHECK(zero);
        CHECK(b.allocateNow(2000) == nullptr);            // above the limit
        CHECK(b.liveArenas() == 1);

        std::size_t n = 0;
        b.request(500);
        REQUIRE(waitFor([&] { return b.delivered(); }));
        double* q = b.take(n);
        REQUIRE(q != nullptr);
        CHECK(n == 500);
        CHECK(b.liveArenas() == 2);
        b.retire(p);
        CHECK(waitFor([&] { return b.liveArenas() == 1; }));

        b.request(4000);                                   // above the limit
        CHECK(waitFor([&] { return b.failed(); }));
        CHECK(b.take(n) == nullptr);

        b.request(300);                                    // superseded once delivered
        REQUIRE(waitFor([&] { return b.delivered(); }));
        b.request(200);
        double* r = nullptr;
        REQUIRE(waitFor([&] { return (r = b.take(n)) != nullptr; }));
        CHECK(n == 200);
        CHECK(b.liveArenas() == 2);                        // q and r: the 300 was freed

        b.request(100);                                    // cancelled once delivered
        REQUIRE(waitFor([&] { return b.delivered(); }));
        b.request(0);
        CHECK(waitFor([&] { return !b.delivered() && b.liveArenas() == 2; }));
        CHECK(b.take(n) == nullptr);
        b.retire(q); b.retire(r);
        CHECK(waitFor([&] { return b.liveArenas() == 0; }));
    }
    CHECK(ArenaBroker::liveArenasTotal() == 0);
    {
        ArenaBroker b;
        std::size_t n = 0;
        b.request((1024u << 20) / sizeof(double));          // 1 GiB, superseded while being zeroed
        REQUIRE(waitFor([&] { return b.liveArenas() == 1 && !b.delivered(); }, 10.0));
        b.request(50);
        double* z = nullptr;
        REQUIRE(waitFor([&] { return (z = b.take(n)) != nullptr; }));
        CHECK(n == 50);
        CHECK(b.liveArenas() == 1);                         // the 1 GiB was abandoned and freed
        b.retire(z);
        CHECK(waitFor([&] { return b.liveArenas() == 0; }));
    }
    CHECK(ArenaBroker::liveArenasTotal() == 0);
    {
        ArenaBroker b;
        b.retire(b.allocateNow(10));
        b.request(10);
        REQUIRE(waitFor([&] { return b.delivered(); }));
    }                                                      // destroyed holding two arenas
    CHECK(ArenaBroker::liveArenasTotal() == 0);
}

// ══ Test 4 of the spec ═══════════════════════════════════════════════════

TEST_CASE("the engine equals sgs.alogos") {
    struct Row { const char* name; int scale, dn, g, n; double fs; int j;
                 const double (*w)[16][512]; const double (*e)[16]; };
    using namespace alogosref;
#define ROW(SC, si, D, dni, gi, N)                                                                    \
    { #SC " D" #D " N" #N " 96k", si, dni, gi, N, 96000.0, kWinJ_##SC##_D##D,                         \
      kWin96_##SC##_D##D##_N##N, kWin96_##SC##_D##D##_N##N##_energy },                                \
    { #SC " D" #D " N" #N " 48k", si, dni, gi, N, 48000.0, kWinJ_##SC##_D##D,                         \
      kWin48_##SC##_D##D##_N##N, kWin48_##SC##_D##D##_N##N##_energy }
    const Row rows[] = {
        ROW(phi, 0, 1, 0, 0, 1), ROW(phi, 0, 1, 0, 0, 4), ROW(phi, 0, 1, 0, 0, 17),
        ROW(phi, 0, 4, 2, 0, 4), ROW(phi, 0, 4, 2, 0, 40),
        ROW(pi, 3, 1, 0, 3, 4), ROW(pi, 3, 1, 0, 3, 8),
        ROW(sqrt2, 1, 2, 1, 1, 4), ROW(sqrt2, 1, 2, 1, 1, 30),
    };
#undef ROW
    for (const Row& r : rows) {
        const std::string rowName = r.name; CAPTURE(rowName);
        REQUIRE(snapGrid((Scale)r.scale, density(r.dn), 1, kDefaultMetres) == r.j);
        Engine e; settle(e, r.fs, Set{ 1, kDefaultMetres, r.n, r.scale, r.dn, r.g });
        REQUIRE(e.sections() == r.n);
        Capture c = run(e, (long)r.fs, 16);
        const double we = winErr<16>(c, r.w), ee = energyErr<16>(c, r.e);
        MESSAGE(rowName << ": windows " << we << ", energy " << ee);
        CHECK(we < 1e-12);
        CHECK(ee < 1e-10);
    }
    CHECK_AUDIO_NO_ALLOC();
}

// ══ Test 5: a change of setting ══════════════════════════════════════════

TEST_CASE("a change applies after 250 ms, once, and ends as a fresh engine") {
    const double fs = 48000.0; const int B = 240;     // 0.1 s = 20 blocks, 0.35 s = 70 blocks
    SUBCASE("in the same arena") {
        // id 2 at 3.2 m and id 3 at 3.1 m: grid point 9, slightly shorter than
        // the default's 10, so the table fits the arena (more than half).
        const Set a { 2, 3.2, 17, 0, 0, 0 }, b { 3, 3.1, 17, 0, 0, 0 };
        Engine e; settle(e, fs);
        const uint32_t d00 = e.delay(0, 0);
        const std::size_t bytes = e.arenaBytes();
        long changedAt = -1;
        run(e, (long)fs, 3, B, [&](long pos) {
            if (pos == 0)    apply(e, a);
            if (pos == 4800) apply(e, b);             // restarts the pause
            if (changedAt < 0 && (e.delay(0, 0) != d00 || e.clears() > 0)) changedAt = pos;
        });
        MESSAGE("first effect at sample " << changedAt << " (0.35 s = 16800)");
        CHECK(changedAt >= 16800);
        CHECK(e.clears() == 1);
        CHECK(e.stoneId() == 3);
        CHECK(e.arenaBytes() == bytes);               // no reallocation
        CHECK(e.broker()->liveArenas() == 1);
        sameAsFresh(e, fs, b);
    }
    SUBCASE("growing and shrinking: phi 1 -> sqrt 2 * 4 -> pi 1") {
        Engine e; settle(e, fs);
        const std::size_t small = e.arenaBytes();
        long changedAt = -1;
        run(e, (long)fs, 1, B, [&](long pos) {
            if (pos == 0) apply(e, kSqrt2D4);
            if (changedAt < 0 && e.clears() > 0) changedAt = pos;
        });
        CHECK(changedAt >= 12000);
        finish(e, fs, kSqrt2D4);
        CHECK(e.clears() == 1);
        CHECK(e.arenaBytes() > 2 * small);            // reallocated, larger
        CHECK(e.arenaDoubles() == neededDoubles(e));
        MESSAGE("sqrt 2 * 4 at 3.33 m, 48 kHz: N = " << e.sections() << ", "
                << e.arenaBytes() / 1048576.0 << " MiB");
        CHECK(waitFor([&] { return e.broker()->liveArenas() == 1; }));   // the old one freed
        sameAsFresh(e, fs, kSqrt2D4);

        const std::size_t big = e.arenaBytes();
        apply(e, kPi1);
        finish(e, fs, kPi1);
        CHECK(e.clears() == 2);
        CHECK(2 * e.arenaBytes() < big);              // reallocated, smaller
        CHECK(e.arenaDoubles() == neededDoubles(e));
        CHECK(e.sections() == 8);
        CHECK(waitFor([&] { return e.broker()->liveArenas() == 1; }));
        sameAsFresh(e, fs, kPi1);
    }
    CHECK_AUDIO_NO_ALLOC();
}

TEST_CASE("a setting equal to the applied one starts nothing; values are clamped") {
    Engine e; settle(e, 48000.0);
    apply(e, kDefault);
    blocks(e, 400);
    CHECK(e.clears() == 0);
    e.setSettings(99, 40.0, 400, 9, 9, 9);              // (8, 6 m, 128, pi, D 4, diffusion)
    finish(e, 48000.0, Set{ 8, 6.0, 128, 3, 2, 4 });
    CHECK(e.stoneId() == 8); CHECK(e.scale() == 3); CHECK(e.densityIdx() == 2); CHECK(e.gain() == 4);
    CHECK(e.metres() <= 6.0);
    CHECK(e.metres() == gridMetres(Scale::Pi, 4, 8, gridMax(Scale::Pi, 4)));
    CHECK(e.sections() == nMax(Scale::Pi, 4, e.metres()));
    e.setSettings(-3, 0.1, 0, -1, -1, -1);              // (0, 1 m, 1, phi, D 1, 1/phi)
    finish(e, 48000.0, Set{ 0, 1.0, 1, 0, 0, 0 });
    CHECK(e.stoneId() == 0); CHECK(e.sections() == 1); CHECK(e.metres() == 1.0);
    CHECK_AUDIO_NO_ALLOC();
}

// "?" and 1 are both phase 0, so the table and the section count are
// identical; only the readouts move, with no fade and no clear.
TEST_CASE("identical table: readouts only") {
    const double fs = 48000.0;
    const Set s0 { 0, kDefaultMetres, 17, 0, 0, 0 }, s1 { 1, kDefaultMetres, 17, 0, 0, 0 };
    Engine e; settle(e, fs, s0);
    Engine ref; settle(ref, fs, s0);
    std::vector<double> ib((size_t)4 * 256), oa((size_t)4 * 256), ob((size_t)4 * 256);
    double* in[4]; double* oe[4]; double* orf[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * 256; oe[c] = oa.data() + (size_t)c * 256; orf[c] = ob.data() + (size_t)c * 256; }
    Burst burst(fs);
    double worst = 0.0;
    for (int blk = 0; blk < 400; ++blk) {          // 2.1 s: past the 250 ms settle
        if (blk == 4) apply(e, s1);
        burst.fill(in, 256);
        proc<double>(e, in, oe, 256);
        proc<double>(ref, in, orf, 256);
        for (int c = 0; c < 4; ++c) for (int k = 0; k < 256; ++k)
            worst = std::max(worst, std::fabs(oe[c][k] - orf[c][k]));
    }
    CHECK(e.clears() == 0);
    CHECK(e.stoneId() == 1);
    CHECK(worst == 0.0);                           // identical to the engine never touched
    CHECK_AUDIO_NO_ALLOC();
}

// The stored state belongs to the old g: a change of g alone clears.
TEST_CASE("g alone clears") {
    const double fs = 48000.0;
    const Set g2 { 1, kDefaultMetres, 17, 0, 0, 3 };
    Engine e; settle(e, fs);
    run(e, (long)fs, 1);                           // the memory holds the burst
    const std::size_t bytes = e.arenaBytes();
    const uint32_t dLast = e.delay(3, 16);
    apply(e, g2);
    bool sawClearing = false;
    feedZeros(e, 256, [&] {
        sawClearing |= e.clearing();
        return sawClearing && e.status() == Status::Ready;
    });
    CHECK(sawClearing);
    CHECK(e.clears() == 1);
    CHECK(e.gain() == 3);
    CHECK(e.arenaBytes() == bytes);                // the same arena, cleared
    CHECK(e.delay(3, 16) == dLast);
    CHECK(e.broker()->liveArenas() == 1);
    finish(e, fs, g2);
    sameAsFresh(e, fs, g2);
    CHECK_AUDIO_NO_ALLOC();
}

// Review focus 1.
TEST_CASE("a second change while allocating") {
    const double fs = 48000.0;
    SUBCASE("while the worker zeroes the first arena") {
        Engine e; settle(e, fs);
        apply(e, kSqrt2D4);
        untilAllocating(e);
        // The worker has the 1.4 GiB arena and is zeroing it (it takes ~0.5 s).
        REQUIRE(waitFor([&] { return e.broker()->liveArenas() == 2 && !e.broker()->delivered(); }));
        apply(e, kE2);
        finish(e, fs, kE2);
        CHECK(e.clears() == 2);
        CHECK(e.arenaDoubles() == neededDoubles(e));
        CHECK(waitFor([&] { return e.broker()->liveArenas() == 1; }));
        sameAsFresh(e, fs, kE2);
        CHECK(e.broker()->liveArenas() == 1);
    }
    SUBCASE("after the worker delivered the first arena, not yet taken") {
        Engine e; settle(e, fs);
        apply(e, kSqrt2D4);
        untilAllocating(e);
        REQUIRE(waitFor([&] { return e.broker()->delivered(); }, 30.0));
        CHECK(e.broker()->liveArenas() == 2);
        apply(e, kE2);
        // Inside the 250 ms the engine takes the delivered arena and sounds
        // sqrt 2 * 4; once settled, e * 2 replaces it (a smaller arena).
        finish(e, fs, kE2);
        CHECK(waitFor([&] { return e.broker()->liveArenas() == 1; }));
        sameAsFresh(e, fs, kE2);
    }
    SUBCASE("a second change that fits the old arena cancels the request") {
        const Set b { 2, 3.2, 17, 0, 0, 0 };
        Engine e; settle(e, fs);
        const std::size_t bytes = e.arenaBytes();
        apply(e, kSqrt2D4);
        untilAllocating(e);
        apply(e, b);
        finish(e, fs, b);
        CHECK(e.arenaBytes() == bytes);
        CHECK(waitFor([&] { return e.broker()->liveArenas() == 1 && !e.broker()->delivered(); }));
        sameAsFresh(e, fs, b);
    }
    CHECK_AUDIO_NO_ALLOC();
}

// Review focus 2.
TEST_CASE("release or destroy while allocating") {
    const double fs = 48000.0;
    REQUIRE(ArenaBroker::liveArenasTotal() == 0);
    {
        Engine e; settle(e, fs);
        apply(e, kSqrt2D4);
        untilAllocating(e);
        e.release();
        CHECK(ArenaBroker::liveArenasTotal() == 0);
        CHECK(e.status() == Status::Unprepared);
        // and it prepares again
        REQUIRE(e.prepare(fs));
        CHECK(e.broker()->liveArenas() == 1);
    }
    CHECK(ArenaBroker::liveArenasTotal() == 0);
    {
        Engine e; settle(e, fs);
        apply(e, kSqrt2D4);
        untilAllocating(e);
    }                                                  // destroyed while the worker zeroes
    CHECK(ArenaBroker::liveArenasTotal() == 0);
    {
        Engine e; settle(e, fs);
        apply(e, kTiny);
        untilAllocating(e);
        REQUIRE(waitFor([&] { return e.broker()->delivered(); }, 30.0));
        CHECK(ArenaBroker::liveArenasTotal() == 2);
    }                                                  // destroyed with an arena in the slot
    CHECK(ArenaBroker::liveArenasTotal() == 0);
    CHECK_AUDIO_NO_ALLOC();
}

// ══ Test 6: memory ═══════════════════════════════════════════════════════

TEST_CASE("arena holds every delay") {
    const char* names[] = { "phi", "sqrt2", "e", "pi" };
    for (double fs : { 48000.0, 96000.0 }) {
        const Seam::PrimeSieve sv(sieveBound(fs));
        for (int sc = 0; sc < 4; ++sc)
            for (int dn = 0; dn < 3; ++dn)
                for (double m : { kDefaultMetres, 1.0 }) {
                    const Set s { 1, m, 128, sc, dn, 0 };
                    Engine e; settle(e, fs, s);
                    const Scale S = (Scale)sc; const int D = density(dn);
                    const double d = gridMetres(S, D, 1, snapGrid(S, D, 1, m));
                    const std::string scaleName = names[sc]; CAPTURE(fs); CAPTURE(scaleName); CAPTURE(D); CAPTURE(m);
                    REQUIRE(e.sections() == nMax(S, D, d));
                    long bad = 0, overlap = 0;
                    std::size_t end = 0;
                    for (int k = 0; k < kFaces; ++k)
                        for (int i = 0; i < e.sections(); ++i) {
                            const uint32_t t = sectionDelay(S, D, d, i, k, fs, sv);
                            if (e.delay(k, i) != t) ++bad;
                            if (e.sectionLength(k, i) != (std::size_t)t + 1) ++bad;
                            if (e.sectionOffset(k, i) != end) ++overlap;
                            end = e.sectionOffset(k, i) + e.sectionLength(k, i);
                        }
                    CHECK(bad == 0);
                    CHECK(overlap == 0);
                    CHECK(end == e.arenaDoubles());
                    CHECK(e.arenaBytes() == end * sizeof(double));
                    MESSAGE(scaleName << " D" << D << " " << m << " m @ " << fs << ": N "
                            << e.sections() << ", " << e.arenaBytes() / 1048576.0 << " MiB");
                }
    }
}

// Review focus 3.
TEST_CASE("prepare at a new rate") {
    for (double from : { 48000.0, 96000.0 }) {
        const double to = from == 48000.0 ? 96000.0 : 48000.0;
        const Set s { 1, kDefaultMetres, 128, 0, 0, 0 };     // N_max = 19
        Engine e; settle(e, from, s);
        e.release();
        REQUIRE(e.prepare(to));
        const Seam::PrimeSieve sv(sieveBound(to));
        const double d = gridMetres(Scale::Phi, 1, 1, 10);
        long bad = 0;
        uint64_t h = 0xcbf29ce484222325ULL;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < e.sections(); ++i) {
                if (e.delay(k, i) != sectionDelay(Scale::Phi, 1, d, i, k, to, sv)) ++bad;
                for (int b = 0; b < 4; ++b) { h ^= (e.delay(k, i) >> (8 * b)) & 0xff; h *= 0x100000001b3ULL; }
            }
        CAPTURE(from); CAPTURE(to);
        CHECK(bad == 0);
        CHECK(e.sections() == alogosref::kNmax[0][0][1][10]);
        CHECK(h == (to == 48000.0 ? alogosref::kAldelHash48 : alogosref::kAldelHash96)[0][0][1][10]);
        CHECK(e.sampleRate() == to);
        CHECK(e.arenaDoubles() == neededDoubles(e));
        // prepare again, without release, while an allocation is pending
        apply(e, kTiny);
        e.setPower(true);
        untilAllocating(e);
        REQUIRE(e.prepare(from));
        CHECK(e.sampleRate() == from);
        CHECK(e.metres() == 1.0);                          // the pending setting, applied at once
        CHECK(e.sections() == 4);
        CHECK(e.arenaDoubles() == neededDoubles(e));
        CHECK(ArenaBroker::liveArenasTotal() == 1);
    }
    CHECK_AUDIO_NO_ALLOC();
}

// Review focus 3 (INPHIREV), now through the broker.
TEST_CASE("allocation failure via byteLimit, then a smaller setting recovers") {
    const double fs = 48000.0;
    const std::size_t limit = 50u * 1024 * 1024;           // the default needs about 100 MiB
    const Set small { 1, kDefaultMetres, 4, 0, 0, 0 };
    Engine e;
    apply(e, kDefault);
    CHECK_FALSE(e.prepare(fs, limit));
    e.setPower(true); e.reset();
    CHECK(e.failed());
    CHECK(e.arenaBytes() == 0);
    {
        double ib[4][64], ob[4][64];
        double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib[c]; out[c] = ob[c]; std::fill(ib[c], ib[c] + 64, 1.0); std::fill(ob[c], ob[c] + 64, 9.0); }
        CHECK_NOTHROW(proc<double>(e, in, out, 64));
        long nz = 0;
        for (int c = 0; c < 4; ++c) for (int k = 0; k < 64; ++k) nz += ob[c][k] != 0.0;
        CHECK(nz == 0);
    }
    apply(e, small);
    finish(e, fs, small);
    CHECK_FALSE(e.failed());
    sameAsFresh(e, fs, small);

    // A later change above the limit: silent, failed; a smaller one recovers.
    apply(e, kSqrt2D4);
    feedZeros(e, 256, [&] { return e.failed(); });
    REQUIRE(e.failed());
    {
        Capture c = run(e, (long)fs, 1);
        double peak = 0.0;
        for (double v : c.energy) peak = std::max(peak, v);
        CHECK(peak == 0.0);
    }
    const Set back { 2, 3.2, 4, 0, 0, 0 };
    apply(e, back);
    finish(e, fs, back);
    CHECK_FALSE(e.failed());
    sameAsFresh(e, fs, back);
    CHECK_AUDIO_NO_ALLOC();
}

// ══ Test 7: RESET ════════════════════════════════════════════════════════

static void finishReset(Engine& e, double fs, int block) {
    feedZeros(e, block, [&] { return !e.clearing(); });
    REQUIRE(e.status() == Status::Ready);
    const int tail = (int)(0.05 * fs);
    long done = 0;
    feedZeros(e, block, [&] { const bool stop = done >= tail; done += block; return stop; });
}

TEST_CASE("RESET: the engine sounds as a fresh one, exactly") {
    const Set s8 { 1, kDefaultMetres, 8, 0, 0, 0 };
    SUBCASE("at any block size") {
        for (int block : { 256, 1, 4093 }) {
            Engine e; settle(e, 48000.0, s8);
            run(e, 48000, 1);
            e.requestReset();
            feedZeros(e, block, [n = 0]() mutable { return ++n > 1; });
            finishReset(e, 48000.0, block);
            CAPTURE(block);
            CHECK(e.broker()->liveArenas() == 1);
            sameAsFresh(e, 48000.0, s8);
        }
    }
    SUBCASE("a click during the clearing restarts it") {
        // The default arena (about 100 MiB) takes 0.13 s to clear at 48 kHz.
        Engine e; settle(e, 48000.0);
        run(e, 48000, 1);
        e.requestReset();
        feedZeros(e, 256, [n = 0]() mutable { return ++n > 20; });
        REQUIRE(e.clearing());
        e.requestReset();
        finishReset(e, 48000.0, 256);
        sameAsFresh(e, 48000.0, kDefault);
    }
    SUBCASE("a setting arriving during the clearing") {
        const Set s4 { 4, 3.0, 11, 0, 0, 0 };
        Engine e; settle(e, 48000.0);
        run(e, 48000, 1);
        e.requestReset();
        feedZeros(e, 256, [n = 0]() mutable { return ++n > 20; });
        REQUIRE(e.clearing());
        apply(e, s4);
        finish(e, 48000.0, s4);
        sameAsFresh(e, 48000.0, s4);
    }
    SUBCASE("a click while allocating is absorbed: the new arena is fresh") {
        Engine e; settle(e, 48000.0);
        run(e, 48000, 1);
        apply(e, kTiny);
        untilAllocating(e);
        e.requestReset();
        feedZeros(e, 256, [&] { return e.status() == Status::Ready && e.metres() == 1.0; });
        finishReset(e, 48000.0, 256);
        CHECK(e.clears() == 1);
        sameAsFresh(e, 48000.0, kTiny);
    }
    CHECK_AUDIO_NO_ALLOC();
}

// ══ Readouts, decay, parameters ══════════════════════════════════════════

TEST_CASE("readouts equal the table: first and longest delay, metres") {
    for (double fs : { 48000.0, 96000.0 }) {
        for (int id : { 1, 5 }) {
            const Set s { id, 4.0, 9, 2, 1, 2 };
            Engine e; settle(e, fs, s);
            const Seam::PrimeSieve sv(sieveBound(fs));
            const double d = gridMetres(Scale::E, 2, id, snapGrid(Scale::E, 2, id, 4.0));
            CAPTURE(fs); CAPTURE(id);
            CHECK(e.metres() == d);
            CHECK(e.sections() == 9); CHECK(e.stoneId() == id);
            CHECK(e.sectionsMax() == nMax(Scale::E, 2, d));
            CHECK(e.delay(0, 0) == sectionDelay(Scale::E, 2, d, 0, 0, fs, sv));
            CHECK(e.delay(3, e.sections() - 1) == sectionDelay(Scale::E, 2, d, 8, 3, fs, sv));
            CHECK(e.delay(0, e.sections()) == 0u);         // not attached
        }
    }
}

// Spec test 9: the footer prints the engine's table. The strings come from
// alogos_text.h, the same functions the window's footer view calls.
TEST_CASE("footer: first and longest delay, effective sections and N_max, as printed") {
    struct Case { Set s; double fs; };
    const Case cases[] = {
        { kDefault, 96000.0 },                          // INPHIREV's default: N 17, max 19
        { { 1, kDefaultMetres, 30, 0, 0, 0 }, 48000.0 }, // asked 30, clamped to 19
        { kPi1, 96000.0 },                               // pi: N_max 8
        { { 5, 4.0, 9, 2, 1, 2 }, 48000.0 },             // e, D 2, id 5
    };
    for (const Case& c : cases) {
        Engine e; settle(e, c.fs, c.s);
        const Scale sc = (Scale)c.s.scale;
        const int D = density(c.s.dn);
        const double d = gridMetres(sc, D, c.s.id, snapGrid(sc, D, c.s.id, c.s.m));
        const int mx = nMax(sc, D, d), n = std::min(c.s.n, mx);
        const Seam::PrimeSieve sv(sieveBound(c.fs));
        const uint32_t first = sectionDelay(sc, D, d, 0, 0, c.fs, sv);
        const uint32_t last  = sectionDelay(sc, D, d, n - 1, kFaces - 1, c.fs, sv);
        CAPTURE(c.s.scale); CAPTURE(c.s.n); CAPTURE(c.fs);

        const Readout r = readout(e);
        CHECK(r.first == e.delay(0, 0));
        CHECK(r.last == e.delay(kFaces - 1, e.sections() - 1));
        CHECK(r.first == first);
        CHECK(r.last == last);
        CHECK(r.n == n);
        CHECK(r.nMax == mx);

        char line[128], want[128];
        tableLine(line, sizeof line, r);
        std::snprintf(want, sizeof want, "first A %s \xC2\xB7 last D %s \xC2\xB7 N %d \xC2\xB7 max %d",
                      delayText(first / c.fs).s, delayText(last / c.fs).s, n, mx);
        CHECK(std::string(line) == want);
    }
    // The default at 96 kHz, written out: 3.330 m is 931.4 samples, 931 moved
    // to the prime 937 = 9.76 ms (the spec's sketch "9.7" is d / c before
    // the prime), 17 of 19 sections, last D 30.7 s.
    Engine e; settle(e, 96000.0, kDefault);
    char line[128];
    tableLine(line, sizeof line, readout(e));
    CHECK(std::string(line).rfind("first A 9.8 ms \xC2\xB7 last D 30.7 s", 0) == 0);
    CHECK(std::string(line).find("N 17 \xC2\xB7 max 19") != std::string::npos);
}

TEST_CASE("footer: status and arena lines") {
    CHECK(std::string(statusText(Status::Ready, 1)) == "ready");
    CHECK(std::string(statusText(Status::Ready, 0)) == "STONE id undeclared");
    CHECK(std::string(statusText(Status::Allocating, 1)) == "allocating\xE2\x80\xA6");
    CHECK(std::string(statusText(Status::Clearing, 1)) == "clearing\xE2\x80\xA6");
    CHECK(std::string(statusText(Status::AllocFailed, 1)) == "allocation failed");
    CHECK(std::string(statusText(Status::Unprepared, 1)) == "inactive");
    char s[128];
    arenaLine(s, sizeof s, 579ull * 1024 * 1024, 96000.0, "ready");
    CHECK(std::string(s) == "arena 579 MiB @ 96 kHz \xC2\xB7 ready");
    arenaLine(s, sizeof s, 3 * 1024 * 1024 / 2, 44100.0, "allocating");
    CHECK(std::string(s) == "arena 1.5 MiB @ 44.1 kHz \xC2\xB7 allocating");
}

// The parameters' display (the controller's toString): the snapped metres
// and the effective count, equal to what the engine applies.
TEST_CASE("parameter display: distance snapped, sections effective with (max N)") {
    char s[32];
    distanceText(s, sizeof s, 0, 0, 1, kDefaultMetres);
    CHECK(std::string(s) == "3.330 m");
    for (int id : { 0, 2, 5, 8 })
        for (int scale : { 0, 1, 2, 3 })
            for (int dn : { 0, 1, 2 })
                for (double m : { 1.0, 2.2, kDefaultMetres, 5.9 }) {
                    // Before prepare the engine publishes its readouts at once, no arena.
                    Engine e; apply(e, Set{ id, m, 128, scale, dn, 0 });
                    char want[32];
                    std::snprintf(want, sizeof want, "%.3f m", e.metres());
                    distanceText(s, sizeof s, scale, dn, id, m);
                    CAPTURE(id); CAPTURE(scale); CAPTURE(dn); CAPTURE(m);
                    CHECK(std::string(s) == want);
                    CHECK(sectionsMaxFor(scale, dn, id, m) == e.sectionsMax());
                    sectionsText(s, sizeof s, 128, scale, dn, id, m);
                    std::snprintf(want, sizeof want, "%d (max %d)", e.sections(), e.sectionsMax());
                    CHECK(std::string(s) == want);
                }
    sectionsText(s, sizeof s, 17, 0, 0, 1, kDefaultMetres);
    CHECK(std::string(s) == "17");
    sectionsText(s, sizeof s, 19, 0, 0, 1, kDefaultMetres);
    CHECK(std::string(s) == "19");
    sectionsText(s, sizeof s, 30, 0, 0, 1, kDefaultMetres);
    CHECK(std::string(s) == "19 (max 19)");
    sectionsText(s, sizeof s, 17, 3, 0, 1, kDefaultMetres);
    CHECK(std::string(s) == "8 (max 8)");
}

// Review focus 5 (INPHIREV): the tails sink through the subnormals.
TEST_CASE("hours of silence: no subnormals") {
    Engine e; settle(e, 48000.0, Set{ 1, 1.0, 2, 0, 0, 0 });
    std::vector<double> ib(4 * 4096, 0.0), ob(4 * 4096);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 4096; out[c] = ob.data() + c * 4096; }
    ib[0] = 1.0;                                // the impulse on face A
    long subnormal = 0, nonzeroLate = 0;
    for (long done = 0; done < 2000000; done += 4096) {
        proc<double>(e, in, out, 4096);
        ib[0] = 0.0;
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4096; ++k) {
                if (std::fpclassify(out[c][k]) == FP_SUBNORMAL) ++subnormal;
                if (done > 1000000 && out[c][k] != 0.0) ++nonzeroLate;
            }
    }
    MESSAGE("late nonzero samples: " << nonzeroLate);
    CHECK(subnormal == 0);
    CHECK_AUDIO_NO_ALLOC();
}

// Review focus 5: the count asked is kept; only the table is limited.
TEST_CASE("N above N_max survives a scale round trip") {
    const double fs = 48000.0;
    Engine e; settle(e, fs, Set{ 1, kDefaultMetres, 30, 0, 0, 0 });
    CHECK(e.sections() == 19); CHECK(e.sectionsMax() == 19);
    const Set steps[] = {
        { 1, kDefaultMetres, 30, 0, 2, 0 },        // phi D 4: N_max 76, 30 back
        { 1, kDefaultMetres, 30, 3, 0, 0 },        // pi D 1: 8
        { 1, kDefaultMetres, 30, 0, 2, 0 },        // phi D 4 again: 30
        { 1, kDefaultMetres, 30, 0, 0, 0 },        // phi D 1: 19
    };
    const int want[] = { 30, 8, 30, 19 };
    for (int s = 0; s < 4; ++s) {
        apply(e, steps[s]);
        finish(e, fs, steps[s]);
        CAPTURE(s);
        CHECK(e.sections() == want[s]);
        CHECK(e.arenaDoubles() <= 2 * neededDoubles(e));
        CHECK(e.arenaDoubles() >= neededDoubles(e));
    }
    CHECK_AUDIO_NO_ALLOC();
}

// Spec test 6: growing, shrinking, failing, RESET, g alone, at 96 kHz too,
// in blocks of 1 and of 4093: process() never calls new or delete.
TEST_CASE("the audio thread does not allocate") {
    for (double fs : { 48000.0, 96000.0 })
        for (int block : { 1, 4093 }) {
            Engine e; apply(e, kDefault);
            REQUIRE(e.prepare(fs, 1500ull * 1024 * 1024));
            e.setPower(true);
            const Set seq[] = { kTiny, kDefault, { 1, kDefaultMetres, 17, 0, 0, 4 }, kSqrt2D4, kE2 };
            for (const Set& s : seq) {
                apply(e, s);
                feedZeros(e, block, [&] {
                    return e.scale() == s.scale && e.gain() == s.g &&
                           (e.status() == Status::Ready || e.failed());
                });
                e.requestReset();
                feedZeros(e, block, [n = 0]() mutable { return ++n > 2; });
            }
            CAPTURE(fs); CAPTURE(block);
            CHECK_AUDIO_NO_ALLOC();
        }
}
