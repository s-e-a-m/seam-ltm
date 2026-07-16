#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

// Records are grouped into registration epochs: n = epoch * kEpochStride + i.
// The epoch is recoverable from any single field, which is what lets the churn
// test below tell "this emitter's record" from "the previous emitter's record".
static constexpr uint64_t kEpochStride = 1000000ull;

// The tearing probe: every field is derived from one counter n, so a torn
// read (half of record n, half of record n+1) breaks the cross-field
// invariant even though each individual field looks plausible. The derived
// fields are spread across the whole struct — head, middle and union tail — so
// a tear at any offset shows up.
static SeamCalbusRecord makeProbe(uint64_t n) {
    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusGlide;
    r.stoneId = (uint32_t)(n % 9);
    r.active  = 1;
    r.levelDb = -(double)(n % 100);
    r.sampleRate = 48000.0;
    r.u.glide.passCounter = n;
    r.u.glide.passStartSample = (int64_t)n;
    r.u.glide.f0 = (double)n;
    r.u.glide.f1 = (double)n;
    r.u.glide.durationSec = (double)(n % 37);
    r.u.glide.deltaSec    = (double)(n % 53);
    r.u.glide.sweepMode   = (uint32_t)(n & 1u);
    r.u.glide.diracMode   = (uint32_t)((n >> 1) & 1u);
    return r;
}

static bool probeIsConsistent(const SeamCalbusRecord& r) {
    const uint64_t n = r.u.glide.passCounter;
    return r.kind    == (uint32_t)kSeamCalbusGlide
        && r.active  == 1
        && r.sampleRate == 48000.0
        && r.u.glide.passStartSample == (int64_t)n
        && r.u.glide.f0 == (double)n
        && r.u.glide.f1 == (double)n
        && r.u.glide.durationSec == (double)(n % 37)
        && r.u.glide.deltaSec    == (double)(n % 53)
        && r.u.glide.sweepMode   == (uint32_t)(n & 1u)
        && r.u.glide.diracMode   == (uint32_t)((n >> 1) & 1u)
        && r.stoneId == (uint32_t)(n % 9)
        && r.levelDb == -(double)(n % 100);
}

// A slot that is registered but has not published yet legitimately reads back
// all-zero. That is the ONLY all-zero record a correct reader may return; a
// record that is part zero and part probe is a tear.
static bool isFresh(const SeamCalbusRecord& r) {
    SeamCalbusRecord zero{};
    return std::memcmp(&r, &zero, sizeof(SeamCalbusRecord)) == 0;
}

TEST_CASE("version reports 1") {
    CHECK(seam_calbus_v1_version() == 1u);
}

TEST_CASE("register returns distinct handles and unregister frees them") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t a = seam_calbus_v1_register(bus);
    int32_t b = seam_calbus_v1_register(bus);
    CHECK(a >= 0);
    CHECK(b >= 0);
    CHECK(a != b);
    seam_calbus_v1_unregister(bus, a);
    seam_calbus_v1_unregister(bus, b);
}

TEST_CASE("registry exhausts at 32 slots and recovers") {
    SeamCalbus* bus = seam_calbus_v1_get();
    std::vector<int32_t> handles;
    for (int i = 0; i < SEAM_CALBUS_MAX_SLOTS; ++i) {
        int32_t h = seam_calbus_v1_register(bus);
        CHECK(h >= 0);
        handles.push_back(h);
    }
    CHECK(seam_calbus_v1_register(bus) == -1);   // full
    seam_calbus_v1_unregister(bus, handles.back());
    int32_t reused = seam_calbus_v1_register(bus);
    CHECK(reused >= 0);                          // freed slot is reusable
    handles.back() = reused;
    for (int32_t h : handles) seam_calbus_v1_unregister(bus, h);
}

TEST_CASE("snapshot returns only registered slots and their last record") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t h = seam_calbus_v1_register(bus);
    SeamCalbusRecord in = makeProbe(7);
    seam_calbus_v1_publish(bus, h, &in);

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    int32_t n = seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS);
    CHECK(n == 1);
    CHECK(out[0].u.glide.passCounter == 7u);
    CHECK(probeIsConsistent(out[0]));

    seam_calbus_v1_unregister(bus, h);
    n = seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS);
    CHECK(n == 0);
}

TEST_CASE("a reclaimed slot never exposes the previous owner's record") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t first = seam_calbus_v1_register(bus);
    SeamCalbusRecord in = makeProbe(99);
    seam_calbus_v1_publish(bus, first, &in);
    seam_calbus_v1_unregister(bus, first);

    int32_t second = seam_calbus_v1_register(bus);
    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 1);
    CHECK(isFresh(out[0]));
    seam_calbus_v1_unregister(bus, second);
}

TEST_CASE("a stale handle cannot publish into or evict the slot's new owner") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t stale = seam_calbus_v1_register(bus);
    CHECK(stale >= 0);
    seam_calbus_v1_unregister(bus, stale);

    int32_t fresh = seam_calbus_v1_register(bus);   // reclaims the same slot
    CHECK(fresh >= 0);
    CHECK(fresh != stale);                          // the epoch moved

    SeamCalbusRecord r = makeProbe(42);
    seam_calbus_v1_publish(bus, stale, &r);         // must be dropped

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 1);
    CHECK(isFresh(out[0]));            // the new owner's record is untouched

    seam_calbus_v1_unregister(bus, stale);          // must not evict `fresh`
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 1);

    seam_calbus_v1_unregister(bus, fresh);
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 0);
}

TEST_CASE("seqlock: reader never observes a torn record under a hammering writer") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t h = seam_calbus_v1_register(bus);
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (uint64_t n = 0; !stop.load(std::memory_order_relaxed); ++n) {
            SeamCalbusRecord r = makeProbe(n);
            seam_calbus_v1_publish(bus, h, &r);
            // A real emitter publishes at most once per audio block. An
            // unbroken tight loop is not a harder test, it is a different one:
            // it just starves the reader into its retry limit, which proves
            // nothing about tearing and makes any liveness assertion vacuous.
            std::this_thread::yield();
        }
    });

    constexpr int kIterations = 200000;
    int torn = 0, fresh = 0, reads = 0;
    for (int i = 0; i < kIterations; ++i) {
        SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
        if (seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 1) {
            ++reads;
            // Registered but not published yet: the writer thread has not been
            // scheduled for its first publish. Legitimately all-zero.
            if (isFresh(out[0])) { ++fresh; continue; }
            if (!probeIsConsistent(out[0])) ++torn;
        }
    }
    stop.store(true);
    writer.join();
    seam_calbus_v1_unregister(bus, h);

    MESSAGE("hammer: reads=" << reads << " fresh=" << fresh << " torn=" << torn);
    CHECK(torn == 0);
    // The slot stays registered for the whole run and the writer never blocks,
    // so the reader should succeed essentially always. "reads > 0" would let a
    // reader that gives up 199999 times out of 200000 pass.
    CHECK(reads > kIterations * 9 / 10);
}

// The test the two Critical bugs lived under. The hammer above exercises ONE
// writer on ONE permanently-registered slot — the configuration that already
// worked. Registration churn concurrent with snapshot() is the NORMAL case for
// this bus (plugins are activated and deactivated constantly), and it is where
// both bugs were: register()'s memset outside the seqlock, and inUse's
// inability to see release-then-reclaim.
TEST_CASE("churn: reader never observes a torn or phantom record while slots churn") {
    SeamCalbus* bus = seam_calbus_v1_get();
    std::atomic<bool> stop{false};

    // Announced BEFORE the register that opens the epoch, so at any instant the
    // bus's live epoch is <= liveEpoch. Hence a returned record whose epoch is
    // strictly below the value read before snapshot() started is proof the
    // reader emitted a record from an epoch that had already been unregistered
    // — a phantom.
    std::atomic<uint64_t> liveEpoch{0};

    std::thread churn([&] {
        for (uint64_t epoch = 1; !stop.load(std::memory_order_relaxed); ++epoch) {
            liveEpoch.store(epoch, std::memory_order_release);
            int32_t h = seam_calbus_v1_register(bus);
            if (h < 0) continue;
            // Few publishes per epoch, no pacing: what this test hunts is the
            // register()/unregister() race against snapshot(), so the epoch
            // turnover rate is the knob that matters, not the publish rate.
            for (uint64_t i = 0; i < 2; ++i) {
                SeamCalbusRecord r = makeProbe(epoch * kEpochStride + i);
                seam_calbus_v1_publish(bus, h, &r);
            }
            seam_calbus_v1_unregister(bus, h);
        }
    });

    constexpr int kIterations = 200000;
    int torn = 0, phantom = 0, fresh = 0, reads = 0;
    for (int i = 0; i < kIterations; ++i) {
        SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
        const uint64_t e0 = liveEpoch.load(std::memory_order_acquire);
        const int32_t n = seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS);
        const uint64_t e1 = liveEpoch.load(std::memory_order_acquire);
        if (n == 0) continue;
        ++reads;
        for (int32_t k = 0; k < n; ++k) {
            if (isFresh(out[k])) { ++fresh; continue; }  // registered, unpublished
            if (!probeIsConsistent(out[k])) { ++torn; continue; }
            const uint64_t e = out[k].u.glide.passCounter / kEpochStride;
            if (e < e0 || e > e1) ++phantom;
        }
    }
    stop.store(true);
    churn.join();

    MESSAGE("churn: reads=" << reads << " fresh=" << fresh
            << " torn=" << torn << " phantom=" << phantom);
    CHECK(torn == 0);
    CHECK(phantom == 0);
    // Liveness: the churn thread is registered for the overwhelming majority of
    // its cycle, so a reader that almost never returns a record is broken.
    CHECK(reads > kIterations / 10);
}

TEST_CASE("publish with an invalid handle is a silent no-op") {
    SeamCalbus* bus = seam_calbus_v1_get();
    SeamCalbusRecord r = makeProbe(1);
    seam_calbus_v1_publish(bus, -1, &r);
    seam_calbus_v1_publish(nullptr, 0, &r);
    seam_calbus_v1_publish(bus, 0, nullptr);
    // Handles are opaque epoch+index tokens; a forged one belongs to no live
    // epoch and is dropped.
    seam_calbus_v1_publish(bus, 0x7FFFFFFF, &r);
    seam_calbus_v1_unregister(bus, 0x7FFFFFFF);
    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
