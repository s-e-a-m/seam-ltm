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

// ── The engine against sdt.stunedrev ──────────────────────────────────────
static void settle(Engine& e, double fs) {
    REQUIRE(e.prepare(fs));
    e.setInput(1.0); e.setOutput(1.0); e.setPower(true);
    e.reset();                              // every ramp onto its target
}

struct Capture {
    int seconds; long fs;
    std::vector<double> win, energy;        // [line][second][512], [line][second]
    Capture(int s, long rate) : seconds(s), fs(rate), win((size_t)4 * s * 512, 0.0), energy((size_t)4 * s, 0.0) {}
    double& w(int c, int s, int k) { return win[((size_t)c * seconds + s) * 512 + k]; }
    double& e(int c, int s) { return energy[(size_t)c * seconds + s]; }
};

// The burst through the engine in blocks of `block`; hook(pos) runs before
// the block that starts at pos. inPlace feeds the same buffers in and out.
template <class Hook>
static Capture run(Engine& e, long fs, int seconds, int block, Hook hook, bool inPlace = false) {
    Capture cap(seconds, fs);
    Burst burst((double)fs);
    std::vector<double> ib((size_t)4 * block), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) {
        in[c]  = ib.data() + (size_t)c * block;
        out[c] = inPlace ? in[c] : ob.data() + (size_t)c * block;
    }
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

// Test 4 of the spec.
TEST_CASE("the engine equals sdt.stunedrev(83, 47, 7, 71) at 96 kHz over 30 s") {
    Engine e; settle(e, 96000.0);
    Capture c = run(e, 96000, 30);
    MESSAGE("96 kHz: windows " << winErr<30>(c, stunedrevref::kWin96) << ", energy " << energyErr<30>(c, stunedrevref::kWin96_energy));
    CHECK(winErr<30>(c, stunedrevref::kWin96) < 1e-12);
    CHECK(energyErr<30>(c, stunedrevref::kWin96_energy) < 1e-10);
}

TEST_CASE("the engine equals sdt.stunedrev at 48 kHz over 30 s") {
    Engine e; settle(e, 48000.0);
    Capture c = run(e, 48000, 30);
    MESSAGE("48 kHz: windows " << winErr<30>(c, stunedrevref::kWin48) << ", energy " << energyErr<30>(c, stunedrevref::kWin48_energy));
    CHECK(winErr<30>(c, stunedrevref::kWin48) < 1e-12);
    CHECK(energyErr<30>(c, stunedrevref::kWin48_energy) < 1e-10);
}

// Test 5: a time moved at a block boundary.
TEST_CASE("a change of time equals the spec's: t3 7 -> 9 ms at sample 48128") {
    Engine e; settle(e, 96000.0);
    Capture c = run(e, 96000, 3, 256, [&](long pos) { if (pos == 48128) e.setTime(2, 9); });
    MESSAGE("change of time: windows " << winErr<3>(c, stunedrevref::kChange96));
    CHECK(winErr<3>(c, stunedrevref::kChange96) < 1e-12);
    CHECK(energyErr<3>(c, stunedrevref::kChange96_energy) < 1e-10);
}

// Test 9: the centroid is the 96 kHz time scale, to within a prime gap per section.
TEST_CASE("centroid: never below the exact time, above it by at most one prime gap per section") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Engine e; settle(e, fs);
        for (int j = 0; j < kLines; ++j) {
            // sum over i of ms·(i+1)·k = ms·k·903
            const double exact = kDefaultTimes[j] * kRatio[j] * 903.0 / 1000.0;
            const double c = e.centroidSeconds(j);
            CAPTURE(fs); CAPTURE(j);
            CHECK(c >= exact);
            CHECK(c - exact <= kSections * 155.0 / fs);   // rounding + gap < 155 samples
        }
    }
    Engine e; settle(e, 96000.0);
    CHECK(std::fabs(e.centroidSeconds(0) - 106.0) < 0.1);   // the report's 106.0 s
}

// Review focus 1, 2, 3, 5.
TEST_CASE("in-place buffers give the out-of-place output exactly") {
    Engine a; settle(a, 48000.0);
    Engine b; settle(b, 48000.0);
    Capture ca = run(a, 48000, 2);
    Capture cb = run(b, 48000, 2, 256, [](long) {}, true);
    CHECK(ca.win == cb.win);
    CHECK(ca.energy == cb.energy);
}

TEST_CASE("block partition: 1, 7 and 4093-sample blocks equal 256-sample blocks exactly") {
    Engine ref; settle(ref, 48000.0);
    Capture cr = run(ref, 48000, 2);
    for (int block : {1, 7, 4093}) {
        Engine e; settle(e, 48000.0);
        Capture c = run(e, 48000, 2, block);
        CAPTURE(block);
        CHECK(c.win == cr.win);
        CHECK(c.energy == cr.energy);
    }
}

TEST_CASE("prepare at a new rate gives a fresh engine at that rate") {
    Engine e; settle(e, 96000.0);
    run(e, 96000, 1);                       // memory full of the burst
    settle(e, 48000.0);                     // the host's setActive(false/true) with a new rate
    Engine fresh; settle(fresh, 48000.0);
    for (int j = 0; j < kLines; ++j) {
        CHECK(e.centroidSeconds(j) == fresh.centroidSeconds(j));
        for (int i = 0; i < kSections; ++i) CHECK(e.delay(j, i) == fresh.delay(j, i));
    }
    Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
    CHECK(a.win == b.win);
}

TEST_CASE("without memory process() writes zeros") {
    Engine e;                                // never prepared
    double ib[4][64], ob[4][64];
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib[c]; out[c] = ob[c]; std::fill(ib[c], ib[c] + 64, 1.0); std::fill(ob[c], ob[c] + 64, 9.0); }
    e.process(in, out, 64);
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 64; ++k) CHECK(ob[c][k] == 0.0);
    CHECK(e.status() == Status::Unprepared);
    settle(e, 48000.0);
    e.release();
    std::fill(ob[0], ob[0] + 64, 9.0);
    e.process(in, out, 64);
    CHECK(ob[0][10] == 0.0);
}

TEST_CASE("POWER off: after its 25 ms ramp the output is exactly zero; the lines keep running") {
    Engine e;  settle(e, 48000.0);
    Engine on; settle(on, 48000.0);
    e.setPower(false);                        // off from the start, back on at sample 48128
    Burst be(48000.0), bo(48000.0);
    const int B = 256;
    std::vector<double> ie(4 * B), oe(4 * B), io(4 * B), oo(4 * B);
    double *pie[4], *poe[4], *pio[4], *poo[4];
    for (int c = 0; c < 4; ++c) {
        pie[c] = ie.data() + c * B; poe[c] = oe.data() + c * B;
        pio[c] = io.data() + c * B; poo[c] = oo.data() + c * B;
    }
    long nonzero = 0; double err = 0.0;
    for (long pos = 0; pos < 96000; pos += B) {
        if (pos == 48128) e.setPower(true);
        be.fill(pie, B); bo.fill(pio, B);
        e.process(pie, poe, B); on.process(pio, poo, B);
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < B; ++k) {
                const long g = pos + k;
                if (g >= 1200 && g < 48128 && poe[c][k] != 0.0) ++nonzero;      // ramp of 1200 samples
                if (g >= 48128 + 1200) err = std::max(err, std::fabs(poe[c][k] - poo[c][k]));
            }
    }
    CHECK(nonzero == 0);
    CHECK(err == 0.0);   // the memory kept turning while POWER was off
}

// ── Test 7: RESET ─────────────────────────────────────────────────────────
// Run zeros through e until the clearing has ended and the fade is back.
static void finishReset(Engine& e, double fs, int block) {
    std::vector<double> ib((size_t)4 * block, 0.0), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = ob.data() + (size_t)c * block; }
    long guard = 0;
    do { e.process(in, out, block); } while (e.status() == Status::Clearing && ++guard < 10000000);
    REQUIRE(e.status() == Status::Ready);
    const int tail = (int)(0.05 * fs);       // the 25 ms fade back, and more
    for (int done = 0; done < tail; done += block) e.process(in, out, block);
}

TEST_CASE("after RESET the engine sounds as a fresh one, exactly") {
    for (int block : {256, 1, 4093}) {
        Engine e; settle(e, 48000.0);
        run(e, 48000, 1);                     // the memory holds the burst
        e.requestReset();
        finishReset(e, 48000.0, block);
        Engine fresh; settle(fresh, 48000.0);
        Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
        CAPTURE(block);
        CHECK(a.win == b.win);
        CHECK(a.energy == b.energy);
    }
}

TEST_CASE("RESET: clearing lasts the same time at any block size, about 0.39 s at 96 kHz") {
    Engine e; settle(e, 96000.0);
    e.requestReset();
    std::vector<double> ib(4 * 64, 0.0), ob(4 * 64);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 64; out[c] = ob.data() + c * 64; }
    long samples = 0;
    do { e.process(in, out, 64); samples += 64; } while (e.status() == Status::Clearing);
    const double seconds = samples / 96000.0;
    MESSAGE("RESET at 96 kHz: " << seconds << " s");
    CHECK(seconds > 0.3);
    CHECK(seconds < 0.5);
}

TEST_CASE("RESET silences the output during the clearing and ignores the input") {
    Engine e; settle(e, 48000.0);
    run(e, 48000, 1);
    e.requestReset();
    // 25 ms fade, then the clearing: from the first block after the fade
    // the output is exactly zero while a full-scale input arrives.
    std::vector<double> ib(4 * 256, 1.0), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    for (int b = 0; b < 6; ++b) e.process(in, out, 256);       // 1536 samples > 1200 of fade
    long nonzero = 0;
    while (e.status() == Status::Clearing) {
        e.process(in, out, 256);
        if (e.status() == Status::Clearing)
            for (int c = 0; c < 4; ++c) for (int k = 0; k < 256; ++k) if (out[c][k] != 0.0) ++nonzero;
    }
    CHECK(nonzero == 0);
}

TEST_CASE("RESET twice: a click during the clearing restarts it") {
    Engine e; settle(e, 96000.0);
    std::vector<double> ib(4 * 256, 0.0), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    e.requestReset();
    long first = 0;
    for (int b = 0; b < 40; ++b) { e.process(in, out, 256); first += 256; }
    REQUIRE(e.status() == Status::Clearing);
    e.requestReset();                         // the second click
    long second = 0;
    do { e.process(in, out, 256); second += 256; } while (e.status() == Status::Clearing);
    // One whole RESET, measured on a fresh engine: the second click restarts
    // it from the beginning; it neither finishes the first nor adds to it.
    Engine f; settle(f, 96000.0);
    f.requestReset();
    long whole = 0;
    do { f.process(in, out, 256); whole += 256; } while (f.status() == Status::Clearing);
    CHECK(second >= whole - 256);
    CHECK(second <= whole + 256);
}

TEST_CASE("RESET with POWER off clears and stays silent; a click before prepare is dropped") {
    Engine e;
    e.requestReset();                         // before prepare
    settle(e, 48000.0);
    CHECK(e.status() == Status::Ready);
    {
        double z[4][256] = {}; double* zp[4] = { z[0], z[1], z[2], z[3] };
        e.process(zp, zp, 256);
        CHECK(e.status() == Status::Ready);    // the old click was not replayed
    }
    run(e, 48000, 1);
    e.setPower(false);
    e.requestReset();
    finishReset(e, 48000.0, 256);
    Capture c = run(e, 48000, 1);
    double sum = 0.0;
    for (int l = 0; l < 4; ++l) sum += c.e(l, 0);
    CHECK(sum == 0.0);
}

// ── Subnormals (final review): the lines lose no energy, so their tails sink
// through the subnormal range and stay in the arena for hours; on x86 every
// subnormal operand costs ~100 cycles (CPU 4.7 % -> 15 % after 30 min of
// silence at 48 kHz). The engine flushes them for the duration of process()
// and restores the caller's floating-point state.
TEST_CASE("process() flushes subnormals to zero and restores the caller's FP state") {
    Engine e; settle(e, 48000.0);
    volatile double tiny = 1e-310;              // subnormal
    double ib[4][64], ob[4][64];
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib[c]; out[c] = ob[c]; std::fill(ib[c], ib[c] + 64, (double)tiny); }
    for (int b = 0; b < 100; ++b) e.process(in, out, 64);
    long nonzero = 0;
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 64; ++k) if (ob[c][k] != 0.0) ++nonzero;
    CHECK(nonzero == 0);                        // read as zero, never written
    volatile double one = 1.0;
    CHECK(tiny * one != 0.0);                   // outside process(), subnormals are back
}
