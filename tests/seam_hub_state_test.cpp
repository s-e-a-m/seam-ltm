#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

#include <atomic>
#include <string>
#include <cstring>
#include <thread>
#include <vector>

extern "C" int32_t seam_hub_test_recent_lines(char* out, int32_t cap);

// Registers an anonymous test member that says and hears nothing.
static int32_t reg(SeamHub* bus, const char* name = "probe", uint32_t says = 0) {
    return seam_hub_v1_register(bus, name, says, 0);
}

static std::vector<std::string> recentLines() {
    static char buf[256 * 160];
    const int32_t n = seam_hub_test_recent_lines(buf, sizeof buf);
    std::vector<std::string> v;
    std::string all(buf);
    size_t p = 0;
    while (p < all.size()) { size_t e = all.find('\n', p); v.push_back(all.substr(p, e - p)); p = e + 1; }
    CHECK((int32_t)v.size() == n);
    return v;
}

// Records are grouped into registration epochs: n = epoch * kEpochStride + i.
// The epoch is recoverable from any single field, which is what lets the churn
// test below tell "this emitter's record" from "the previous emitter's record".
static constexpr uint64_t kEpochStride = 1000000ull;

// The tearing probe: every field is derived from one counter n, so a torn
// read (half of record n, half of record n+1) breaks the cross-field
// invariant even though each individual field looks plausible. The derived
// fields are spread across the whole struct — head, middle and union tail — so
// a tear at any offset shows up.
static SeamHubState makeProbe(uint64_t n) {
    SeamHubState r{};
    r.kind    = kSeamHubGlide;
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

static bool probeIsConsistent(const SeamHubState& r) {
    const uint64_t n = r.u.glide.passCounter;
    return r.kind    == (uint32_t)kSeamHubGlide
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

// isFresh() below memcmps the whole struct, which assumes it is padding-free
// (any padding byte is indeterminate and would make an otherwise-fresh
// record compare unequal to `zero{}` nondeterministically). True today at 88
// bytes; pin it so a future field reorder that introduces padding fails loud
// here instead of making this test flaky.
//
// This is a narrower, test-local echo of the ABI-layout static_assert in
// seam_hub.h (right after the struct) — that one is the real reason this
// number matters (the layout IS the ABI, and SEAM_HUB_VERSION must bump
// alongside it), and it fires for every consumer, not only when
// SEAM_BUILD_TESTS is on. Keep both in sync if you ever grow the struct.
static_assert(sizeof(SeamHubState) == 88,
              "isFresh()'s memcmp assumes SeamHubState has no padding; "
              "re-verify padding-freeness if this size ever changes");

// A slot that is registered but has not published yet legitimately reads back
// all-zero. That is the ONLY all-zero record a correct reader may return; a
// record that is part zero and part probe is a tear.
static bool isFresh(const SeamHubState& r) {
    SeamHubState zero{};
    return std::memcmp(&r, &zero, sizeof(SeamHubState)) == 0;
}

TEST_CASE("version reports 1") {
    CHECK(seam_hub_v1_version() == 1u);
}

TEST_CASE("register returns distinct handles and unregister frees them") {
    SeamHub* bus = seam_hub_v1_get();
    int32_t a = reg(bus);
    int32_t b = reg(bus);
    CHECK(a >= 0);
    CHECK(b >= 0);
    CHECK(a != b);
    seam_hub_v1_unregister(bus, a);
    seam_hub_v1_unregister(bus, b);
}

TEST_CASE("registry exhausts at 32 slots and recovers") {
    SeamHub* bus = seam_hub_v1_get();
    std::vector<int32_t> handles;
    for (int i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) {
        int32_t h = reg(bus);
        CHECK(h >= 0);
        handles.push_back(h);
    }
    CHECK(reg(bus) == SEAM_HUB_NO_HANDLE);   // full
    seam_hub_v1_unregister(bus, handles.back());
    int32_t reused = reg(bus);
    CHECK(reused >= 0);                          // freed slot is reusable
    handles.back() = reused;
    for (int32_t h : handles) seam_hub_v1_unregister(bus, h);
}

TEST_CASE("snapshot returns only registered slots and their last record") {
    SeamHub* bus = seam_hub_v1_get();
    int32_t h = reg(bus);
    SeamHubState in = makeProbe(7);
    seam_hub_v1_publish_state(bus, h, &in);

    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    int32_t n = seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS);
    CHECK(n == 1);
    CHECK(out[0].state.u.glide.passCounter == 7u);
    CHECK(probeIsConsistent(out[0].state));

    seam_hub_v1_unregister(bus, h);
    n = seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS);
    CHECK(n == 0);
}

TEST_CASE("a reclaimed slot never exposes the previous owner's record") {
    SeamHub* bus = seam_hub_v1_get();
    int32_t first = reg(bus);
    SeamHubState in = makeProbe(99);
    seam_hub_v1_publish_state(bus, first, &in);
    seam_hub_v1_unregister(bus, first);

    int32_t second = reg(bus);
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    CHECK(seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 1);
    CHECK(isFresh(out[0].state));
    seam_hub_v1_unregister(bus, second);
}

TEST_CASE("a stale handle cannot publish into or evict the slot's new owner") {
    SeamHub* bus = seam_hub_v1_get();
    int32_t stale = reg(bus);
    CHECK(stale >= 0);
    seam_hub_v1_unregister(bus, stale);

    int32_t fresh = reg(bus);   // reclaims the same slot
    CHECK(fresh >= 0);
    CHECK(fresh != stale);                          // the epoch moved

    SeamHubState r = makeProbe(42);
    seam_hub_v1_publish_state(bus, stale, &r);         // must be dropped

    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    CHECK(seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 1);
    CHECK(isFresh(out[0].state));            // the new owner's record is untouched

    seam_hub_v1_unregister(bus, stale);          // must not evict `fresh`
    CHECK(seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 1);

    seam_hub_v1_unregister(bus, fresh);
    CHECK(seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 0);
}

TEST_CASE("seqlock: reader never observes a torn record under a hammering writer") {
    SeamHub* bus = seam_hub_v1_get();
    int32_t h = reg(bus);
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (uint64_t n = 0; !stop.load(std::memory_order_relaxed); ++n) {
            SeamHubState r = makeProbe(n);
            seam_hub_v1_publish_state(bus, h, &r);
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
        SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
        if (seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 1) {
            ++reads;
            // Registered but not published yet: the writer thread has not been
            // scheduled for its first publish. Legitimately all-zero.
            if (isFresh(out[0].state)) { ++fresh; continue; }
            if (!probeIsConsistent(out[0].state)) ++torn;
        }
    }
    stop.store(true);
    writer.join();
    seam_hub_v1_unregister(bus, h);

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
    SeamHub* bus = seam_hub_v1_get();
    std::atomic<bool> stop{false};

    // Announced BEFORE the register that opens the epoch, so at any instant the
    // bus's live epoch is <= liveEpoch. Hence a returned record whose epoch is
    // strictly below the value read before snapshot() started is proof the
    // reader emitted a record from an epoch that had already been unregistered
    // — a phantom.
    std::atomic<uint64_t> liveEpoch{0};

    // Counts completed register->publish->unregister CYCLES only — not loop
    // iterations. This matters because `epoch` above (and `liveEpoch`) advance
    // once per LOOP ITERATION regardless of whether register() actually
    // succeeded: `if (h < 0) continue;` skips straight to the next iteration
    // without touching the bus at all. Against a pre-filled registry (every
    // register() call fails), the loop still spins and `liveEpoch` still
    // climbs — in fact it climbs FASTER, since the failed-register path is
    // cheaper than a real cycle. So `liveEpoch`/`epoch` cannot be used to
    // prove churn happened; only a counter incremented after a cycle actually
    // completes can. See the assertion on `cycles` below.
    std::atomic<uint64_t> cycles{0};

    std::thread churn([&] {
        for (uint64_t epoch = 1; !stop.load(std::memory_order_relaxed); ++epoch) {
            liveEpoch.store(epoch, std::memory_order_release);
            int32_t h = reg(bus);
            if (h < 0) continue;
            // Few publishes per epoch, no pacing: what this test hunts is the
            // register()/unregister() race against snapshot(), so the epoch
            // turnover rate is the knob that matters, not the publish rate.
            for (uint64_t i = 0; i < 2; ++i) {
                SeamHubState r = makeProbe(epoch * kEpochStride + i);
                seam_hub_v1_publish_state(bus, h, &r);
            }
            seam_hub_v1_unregister(bus, h);
            cycles.fetch_add(1, std::memory_order_relaxed);
        }
    });

    constexpr int kIterations = 200000;
    int torn = 0, phantom = 0, fresh = 0, reads = 0;
    for (int i = 0; i < kIterations; ++i) {
        SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
        const uint64_t e0 = liveEpoch.load(std::memory_order_acquire);
        const int32_t n = seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS);
        const uint64_t e1 = liveEpoch.load(std::memory_order_acquire);
        if (n == 0) continue;
        ++reads;
        for (int32_t k = 0; k < n; ++k) {
            if (isFresh(out[k].state)) { ++fresh; continue; }  // registered, unpublished
            if (!probeIsConsistent(out[k].state)) { ++torn; continue; }
            const uint64_t e = out[k].state.u.glide.passCounter / kEpochStride;
            if (e < e0 || e > e1) ++phantom;
        }
    }
    stop.store(true);
    churn.join();

    const uint64_t finalEpoch = liveEpoch.load(std::memory_order_relaxed);
    const uint64_t finalCycles = cycles.load(std::memory_order_relaxed);
    MESSAGE("churn: reads=" << reads << " fresh=" << fresh
            << " torn=" << torn << " phantom=" << phantom
            << " finalEpoch=" << finalEpoch << " cycles=" << finalCycles);
    CHECK(torn == 0);
    CHECK(phantom == 0);
    // Liveness: the churn thread is registered for the overwhelming majority of
    // its cycle, so a reader that almost never returns a record is broken.
    CHECK(reads > kIterations / 10);
    // Validity of the test itself: `torn == 0` and `phantom == 0` pass
    // VACUOUSLY if epoch turnover collapses to near-zero — a reader that
    // reads a slot that never actually changes owner proves nothing about
    // ABA detection. A slower machine, or any future change that makes
    // register()/unregister() more expensive, could silently re-create the
    // exact configuration a previous fix deliberately avoided (a paced,
    // yield()-throttled churn thread that made this test pass against
    // provably buggy code). Pin the precondition the two CHECKs above rest
    // on: churn actually happened, many times, during this run.
    //
    // MUST be `cycles`, not `finalEpoch`/liveEpoch: `liveEpoch` is bumped at
    // the TOP of every loop iteration, before register() is even attempted,
    // and the `if (h < 0) continue;` failure path is cheaper than a full
    // cycle — so against a pre-filled registry (every register() fails,
    // real turnover == 0) `finalEpoch` climbs HIGHER than in a healthy run,
    // and `CHECK(finalEpoch > 1000)` would pass hardest exactly when the
    // test proves nothing. `cycles` only increments after unregister()
    // actually completes a real register/publish/unregister cycle, so it is
    // anti-correlated with the failure mode instead of correlated with it.
    // Healthy runs land in the tens of thousands (~53k-119k observed); 1000
    // is a floor with wide margin, not a target.
    CHECK(finalCycles > 1000);
}

TEST_CASE("publish with an invalid handle is a silent no-op") {
    SeamHub* bus = seam_hub_v1_get();
    SeamHubState r = makeProbe(1);
    seam_hub_v1_publish_state(bus, -1, &r);
    seam_hub_v1_publish_state(nullptr, 0, &r);
    seam_hub_v1_publish_state(bus, 0, nullptr);
    // Handles are opaque epoch+index tokens; a forged one belongs to no live
    // epoch and is dropped.
    seam_hub_v1_publish_state(bus, 0x7FFFFFFF, &r);
    seam_hub_v1_unregister(bus, 0x7FFFFFFF);
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    CHECK(seam_hub_v1_members(bus, out, SEAM_HUB_MAX_MEMBERS) == 0);
}

TEST_CASE("instances are numbered per name and reused") {
    SeamHub* hub = seam_hub_v1_get();
    int32_t a = reg(hub, "strx"), b = reg(hub, "strx"), c = reg(hub, "bastone");
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == 3);
    CHECK(out[0].instance == 1u);
    CHECK(out[1].instance == 2u);
    CHECK(out[2].instance == 1u);
    seam_hub_v1_unregister(hub, a);
    int32_t d = reg(hub, "strx");
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == 3);
    bool sawFirstStrxAgain = false;
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(out[i].name, "strx") == 0 && out[i].instance == 1u) sawFirstStrxAgain = true;
    CHECK(sawFirstStrxAgain);
    for (int32_t h : {b, c, d}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("arrivals and departures are enqueued") {
    SeamHub* hub = seam_hub_v1_get();
    int32_t h = reg(hub, "queuetest", SEAM_HUB_BIT(kSeamHubPink));
    seam_hub_v1_unregister(hub, h);
    auto v = recentLines();
    REQUIRE(v.size() >= 2);
    const std::string& arrives = v[v.size() - 2];
    const std::string& leaves = v[v.size() - 1];
    CHECK(arrives.find("queuetest#1  arrives") != std::string::npos);
    CHECK(arrives.find("says pink") != std::string::npos);
    CHECK(leaves.find("queuetest#1  leaves") != std::string::npos);
}

TEST_CASE("the 33rd member is refused") {
    SeamHub* hub = seam_hub_v1_get();
    std::vector<int32_t> hs;
    for (int i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) {
        hs.push_back(reg(hub, "full"));
        CHECK(hs.back() != SEAM_HUB_NO_HANDLE);
    }
    const int32_t refused = reg(hub, "full");
    CHECK(refused == SEAM_HUB_NO_HANDLE);
    SeamHubState st = makeProbe(5);
    seam_hub_v1_publish_state(hub, refused, &st);   // no-op, must not crash or write
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == SEAM_HUB_MAX_MEMBERS);
    for (int i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) CHECK(out[i].hasState == 0u);
    for (int32_t h : hs) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("members carry name, masks and the last state") {
    SeamHub* hub = seam_hub_v1_get();
    const uint32_t hears = SEAM_HUB_BIT(kSeamHubGlide) | SEAM_HUB_BIT(kSeamHubBandTable);
    int32_t h = seam_hub_v1_register(hub, "multipink", SEAM_HUB_BIT(kSeamHubPink), hears);
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == 1);
    CHECK(out[0].hasState == 0u);
    CHECK(std::string(out[0].name) == "multipink");
    CHECK(out[0].says == SEAM_HUB_BIT(kSeamHubPink));
    CHECK(out[0].hears == hears);
    CHECK(out[0].instance == 1u);
    SeamHubState st{};
    st.kind = kSeamHubPink;
    st.levelDb = -17.5;
    seam_hub_v1_publish_state(hub, h, &st);
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == 1);
    CHECK(out[0].hasState == 1u);
    CHECK(out[0].state.levelDb == -17.5);
    seam_hub_v1_unregister(hub, h);
}

TEST_CASE("recent lines under a short cap keep the newest") {
    SeamHub* hub = seam_hub_v1_get();
    int32_t h = reg(hub, "capq");
    seam_hub_v1_unregister(hub, h);          // two lines: arrives, leaves
    char buf[128];
    // Big enough for the last line alone, too small for both.
    char full[4096];
    REQUIRE(seam_hub_test_recent_lines(full, sizeof full) >= 2);
    std::string all(full);
    const size_t lastStart = all.rfind('\n', all.size() - 2) + 1;
    const size_t lastLen = all.size() - lastStart;       // includes '\n'
    const int32_t cap = (int32_t)lastLen + 1;
    REQUIRE(cap < (int32_t)sizeof buf);
    CHECK(seam_hub_test_recent_lines(buf, cap) == 1);
    CHECK(std::string(buf) == all.substr(lastStart));
    CHECK(std::string(buf).find("leaves") != std::string::npos);
}
