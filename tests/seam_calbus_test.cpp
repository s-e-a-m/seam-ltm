#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus.h"

#include <atomic>
#include <thread>
#include <vector>

// The tearing probe: every field is derived from one counter n, so a torn
// read (half of record n, half of record n+1) breaks the cross-field
// invariant even though each individual field looks plausible.
static SeamCalbusRecord makeProbe(uint64_t n) {
    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusGlide;
    r.stoneId = (uint32_t)(n % 9);
    r.active  = 1;
    r.levelDb = -(double)(n % 100);
    r.sampleRate = 48000.0;
    r.u.glide.passCounter = n;
    r.u.glide.f0 = (double)n;
    r.u.glide.f1 = (double)n;
    return r;
}

static bool probeIsConsistent(const SeamCalbusRecord& r) {
    const uint64_t n = r.u.glide.passCounter;
    return r.u.glide.f0 == (double)n
        && r.u.glide.f1 == (double)n
        && r.stoneId == (uint32_t)(n % 9)
        && r.levelDb == -(double)(n % 100);
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

TEST_CASE("seqlock: reader never observes a torn record under a hammering writer") {
    SeamCalbus* bus = seam_calbus_v1_get();
    int32_t h = seam_calbus_v1_register(bus);
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (uint64_t n = 0; !stop.load(std::memory_order_relaxed); ++n) {
            SeamCalbusRecord r = makeProbe(n);
            seam_calbus_v1_publish(bus, h, &r);
        }
    });

    int torn = 0, reads = 0;
    for (int i = 0; i < 200000; ++i) {
        SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
        if (seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 1) {
            ++reads;
            if (!probeIsConsistent(out[0])) ++torn;
        }
    }
    stop.store(true);
    writer.join();
    seam_calbus_v1_unregister(bus, h);

    CHECK(reads > 0);     // the probe actually exercised the lock
    CHECK(torn == 0);
}

TEST_CASE("publish with an invalid handle is a silent no-op") {
    SeamCalbus* bus = seam_calbus_v1_get();
    SeamCalbusRecord r = makeProbe(1);
    seam_calbus_v1_publish(bus, -1, &r);
    seam_calbus_v1_publish(bus, SEAM_CALBUS_MAX_SLOTS, &r);
    seam_calbus_v1_publish(nullptr, 0, &r);
    seam_calbus_v1_publish(bus, 0, nullptr);
    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(seam_calbus_v1_snapshot(bus, out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
