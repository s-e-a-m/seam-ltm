# Calibration Bus (Spec 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emitter plugins (multipink, ltglide) publish what they are playing on a shared calibration bus, and strx displays which emitter it hears.

**Architecture:** Static state does not cross `.vst3` module boundaries, so the shared registry lives in a separate `libseamcalbus.dylib` loaded at runtime via `dlopen` and reached through a pure C ABI. The registry is 32 fixed POD slots, each guarded by a seqlock so audio-thread writers never block on the GUI-thread reader, plus a per-slot registration epoch (`gen`) that makes registration churn — the normal case, since plugins are activated and deactivated constantly — visible to the reader. The seqlock alone cannot do this: it serialises record writes against record reads, but it cannot tell a reader that the slot changed owner mid-copy. Every failure path degrades to a silent no-op: the bus is an observer and may never stop a plugin from making sound.

**Tech Stack:** C++17, VST3 SDK (`SingleComponentEffect`, `VST3EditorDelegate`), VSTGUI (`CView`, `CVSTGUITimer`), CMake, doctest.

Design doc: `docs/superpowers/specs/2026-07-16-calbus-peer-aware-bus-design.md`

## Global Constraints

- Hand-written C++ only. Faust is the spec, never a code generator (`CLAUDE.md`).
- VST3 SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`; pass `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- Build generator must be Xcode: `-G Xcode`.
- Code, comments and commit messages in English.
- The C ABI boundary carries POD only: no `std::` types, no classes, no exceptions.
- Exported symbols are versioned `seam_calbus_v1_*`. `SEAM_CALBUS_VERSION` is `1`.
- Registry capacity `SEAM_CALBUS_MAX_SLOTS` is `32`.
- `stoneId` range is 0–8, where 0 means undeclared and renders as `STONE ?`.
- `active` means `claimed && !muted`. Level is a separate field and never folds into `active`.
- No error path may prevent a plugin from processing audio.

---

### Task 1: Bus core — C ABI, registry, seqlock, dylib target

**Files:**
- Create: `plugins/_common/calbus/seam_calbus.h`
- Create: `plugins/_common/calbus/seam_calbus.cpp`
- Create: `plugins/_common/calbus/CMakeLists.txt`
- Modify: `CMakeLists.txt` (add `add_subdirectory(plugins/_common/calbus)` before the plugin block at line ~81)
- Test: `tests/seam_calbus_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a new test target)

**Interfaces:**
- Consumes: nothing.
- Produces: the C ABI used by every later task —
  `uint32_t seam_calbus_v1_version(void)`;
  `SeamCalbus* seam_calbus_v1_get(void)`;
  `int32_t seam_calbus_v1_register(SeamCalbus*)` → an opaque non-negative handle
  (registration epoch packed with the slot index — NOT a slot index, do not
  index with it), or `-1` when full;
  `void seam_calbus_v1_unregister(SeamCalbus*, int32_t handle)`;
  `void seam_calbus_v1_publish(SeamCalbus*, int32_t handle, const SeamCalbusRecord*)` (RT-safe);
  `int32_t seam_calbus_v1_snapshot(SeamCalbus*, SeamCalbusRecord* out, int32_t maxCount)` → count written.
  Plus the `SeamCalbusRecord` struct and `kSeamCalbusPink` / `kSeamCalbusGlide`.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_calbus_test.cpp`:

```cpp
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

// isFresh() below memcmps the whole struct, which assumes it is padding-free
// (any padding byte is indeterminate and would make an otherwise-fresh
// record compare unequal to `zero{}` nondeterministically). True today at 88
// bytes; pin it so a future field reorder that introduces padding fails loud
// here instead of making this test flaky.
static_assert(sizeof(SeamCalbusRecord) == 88,
              "isFresh()'s memcmp assumes SeamCalbusRecord has no padding; "
              "re-verify padding-freeness if this size ever changes");

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

    const uint64_t finalEpoch = liveEpoch.load(std::memory_order_relaxed);
    MESSAGE("churn: reads=" << reads << " fresh=" << fresh
            << " torn=" << torn << " phantom=" << phantom
            << " finalEpoch=" << finalEpoch);
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
    CHECK(finalEpoch > 1000);
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

```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-test -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DSEAM_BUILD_TESTS=ON
```

Expected: CMake configure FAILS — `add_subdirectory` given source `plugins/_common/calbus` which is not an existing directory (or `seam_calbus_test.cpp` cannot find `seam_calbus.h`).

- [ ] **Step 3: Write the C ABI header**

Create `plugins/_common/calbus/seam_calbus.h`:

```c
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · calbus — peer-aware calibration bus (Spec 2)
//
// Shared state for the STONE auto-calibration system. Emitters (multipink,
// ltglide) publish what they are playing; the receiver (strx) subscribes.
//
// WHY A SEPARATE DYLIB: static state does not cross .vst3 module boundaries.
// multipink_pool works because its bitmap lives inside multipink.vst3 and all
// multipink instances load from that one dylib. multipink, ltglide and strx
// are three separate bundles, so a header-only bus would give each plugin its
// own private copy — silently, with no compile error. The VST3 SDK offers no
// cross-plugin channel by design (IConnectionPoint connects the two halves of
// a single instance, through the host), so the shared ground is declared here
// explicitly instead.
//
// This is a pure C ABI: C++ has no stable ABI, and the plugins that load this
// library may one day be built with different compilers or flags.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define SEAM_CALBUS_API __declspec(dllexport)
#else
  #define SEAM_CALBUS_API __attribute__((visibility("default")))
#endif

#define SEAM_CALBUS_VERSION   1u
#define SEAM_CALBUS_MAX_SLOTS 32

// The explicit "no slot" sentinel. Use this, never a bare 0, to initialise a
// handle that has not registered yet: 0 is index 0 / gen 0, i.e. slot 0
// before it has ever been claimed — a value publish()/unregister() will
// happily accept. A handle that was actually returned by register() has
// gen >= 1 (register() bumps gen before handing the handle out), so a
// zero-initialised handle can never collide with a real one. See publish()'s
// gen == 0 guard below.
#define SEAM_CALBUS_NO_HANDLE (-1)

// Emitter kind. The kind IS the calibration stage: Pink measures power amp +
// STONE + room (it bypasses the encoder/decoder chain), Glide measures the
// full chain above already-calibrated amps. A separate "stage" field would be
// redundant state able to contradict itself.
typedef enum {
    kSeamCalbusPink  = 1,
    kSeamCalbusGlide = 2
} SeamCalbusKind;

// One emitter's announcement. POD, fixed layout, no padding surprises.
//
// The two emitters have opposite temporal natures and one flattened record
// would betray both. Pink is STATE: free-running noise with no beginning,
// identity carried by the claimed slot range, reference computed analytically
// (-3 dB/oct) — passStartSample would be meaningless. Glide is EVENT: its
// precious datum is the exact pass start plus everything needed to regenerate
// the waveform.
typedef struct SeamCalbusRecord {
    uint32_t kind;        // SeamCalbusKind
    uint32_t stoneId;     // 1..8; 0 = undeclared
    uint32_t active;      // claimed && !muted
    uint32_t _pad;        // explicit: keeps the union 8-byte aligned
    double   levelDb;
    double   sampleRate;
    union {
        struct {
            int32_t slotStart;
            int32_t slotCount;
        } pink;
        struct {
            // Monotone, +1 per pass. Load-bearing: the receiver samples at GUI
            // rate, so without a counter it cannot tell "the same pass as
            // before" from "a new pass with identical parameters". Spec 3
            // averages passes, which makes counting them a requirement.
            uint64_t passCounter;
            // ProcessContext::continuousTimeSamples anchor of the head Dirac.
            // -1 when the host does not provide a valid continuous clock.
            int64_t  passStartSample;
            double   f0, f1, durationSec, deltaSec;
            uint32_t sweepMode;   // 0 linear, 1 exponential
            uint32_t diracMode;   // 0 passo, 1 gap
        } glide;
    } u;
} SeamCalbusRecord;

typedef struct SeamCalbus SeamCalbus;

// Returns SEAM_CALBUS_VERSION. Clients call this first and refuse to proceed
// on mismatch, rather than reading records with a layout they do not know.
SEAM_CALBUS_API uint32_t seam_calbus_v1_version(void);

// The process-wide registry. Never null.
SEAM_CALBUS_API SeamCalbus* seam_calbus_v1_get(void);

// Claim a slot. Returns an OPAQUE, non-negative handle, or -1 when the
// registry is full. Takes a mutex — call from setActive, never from process().
//
// The handle is NOT a slot index: it packs the slot's registration epoch
// alongside the index, which is what lets publish() and unregister() reject a
// handle whose slot has since been reclaimed by another plugin. Store it,
// compare it to -1, hand it back — never index anything with it, never assume
// a range beyond "non-negative".
SEAM_CALBUS_API int32_t seam_calbus_v1_register(SeamCalbus* bus);

// Release a slot. Safe with handle == -1. A stale handle (already released,
// slot since reclaimed) is a no-op and cannot evict the current owner.
// Takes a mutex.
SEAM_CALBUS_API void seam_calbus_v1_unregister(SeamCalbus* bus, int32_t handle);

// Overwrite a slot's record. RT-SAFE: wait-free, no allocation, no lock.
// Safe to call from the audio thread.
//
// PRECONDITION THIS API DOES NOT ENFORCE: for a given handle, calls to
// publish() and the matching unregister() must never overlap. The VST3
// contract is what supplies this in practice — process() (where publish()
// lives) and setActive(false) (where unregister() lives) never run
// concurrently on the same plugin instance — but that contract is external
// to this bus and worth stating here explicitly.
//
// What the epoch check below actually buys: it stops an ALREADY-STALE
// handle from writing AGAIN, forever, once its slot has been reclaimed. It
// does NOT serialise a publish() that is already past the check when the
// epoch closes — that call still writes. So "invalid or stale handles are
// silent no-ops" is true only for handles that were already stale before
// the call started; it is not a substitute for the non-overlap precondition
// above, and it never turns two genuinely concurrent writers on one slot
// into a safe interleaving.
SEAM_CALBUS_API void seam_calbus_v1_publish(SeamCalbus* bus, int32_t handle,
                                            const SeamCalbusRecord* rec);

// Copy every registered slot's record into `out`. Returns the count written.
// A record is only emitted when the slot stayed registered to one single
// owner for the whole copy, so a snapshot never mixes two emitters' data and
// never attributes a departed emitter's record to its successor.
// The reader may retry on a torn read and gives up on a slot after a bounded
// number of attempts, so a GUI timer can never spin forever.
SEAM_CALBUS_API int32_t seam_calbus_v1_snapshot(SeamCalbus* bus,
                                                SeamCalbusRecord* out,
                                                int32_t maxCount);

#ifdef __cplusplus
}
#endif

```

- [ ] **Step 4: Write the registry implementation**

Create `plugins/_common/calbus/seam_calbus.cpp`:

```cpp
#include "seam_calbus.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace {

// Per-slot seqlock.
//
// WHY NOT A MUTEX: ltglide publishes pass starts from the audio thread. A
// mutex there could block audio on a lock held by the strx GUI.
//
// WHY NOT THE TRIPLE-BUFFER: that pattern serves ONE producer shipping BULK
// data. Here many independent producers each write a SMALL record into their
// OWN slot, so writers never contend with each other at all.
//
// The seqlock's asymmetry is exactly the one we want: the writer is never
// blocked (increment, write, increment — constant time), and only the reader
// may have to retry. Producer = audio thread, consumer = GUI: the GUI is the
// one that can afford to spin.
//
// THE SEQLOCK ALONE IS NOT ENOUGH. It serialises *record writes* against
// *record reads*, but registration churn is the normal case here (plugins are
// activated and deactivated constantly), and churn is a second, independent
// axis of change:
//
//   - Anything that touches `rec` must go through the seqlock, publish() and
//     register()'s memset alike. A memset outside the seqlock leaves `seq`
//     even and unchanged for the whole race, so the reader's s1 == s2 check —
//     the only thing that can reject a torn read — cannot fire, and the reader
//     accepts a half-old/half-zeroed record as valid.
//
//   - `inUse` cannot detect ABA. A reader that checks inUse, copies, and
//     re-checks inUse sees 1 both times when the slot was released AND
//     reclaimed during the copy, and would emit the previous owner's record
//     attributed to the new owner. Hence `gen` below.
//
// NON-ATOMIC memcpy/memset ON `rec` UNDER A SEQLOCK IS FORMALLY UB IN THE
// ISO C++ MEMORY MODEL: the fences order the surrounding atomic operations,
// they do not make the plain read/write of `rec` itself a data-race-free
// access when a writer and a retrying reader overlap. This is universal
// seqlock practice anyway (same trick as the Linux kernel's), it is
// deliberate here, and it is exactly why ThreadSanitizer stays silent on
// this file even when the invariant genuinely breaks — a considered choice,
// not an oversight.
struct Slot {
    std::atomic<uint32_t> seq{0};     // odd = write in progress, even = stable
    std::atomic<uint32_t> inUse{0};

    // Registration epoch. Bumped under regMutex on BOTH register and
    // unregister, so it changes on every ownership transition and never
    // returns to an earlier value (within the 26-bit window). It is the ABA
    // guard the seqlock cannot provide: the reader captures {gen, inUse}
    // before and after the copy and only accepts the record when both are
    // unchanged, which means one uninterrupted registration epoch spanned the
    // whole copy.
    std::atomic<uint32_t> gen{0};

    SeamCalbusRecord      rec{};
};

// Bounded retries: a GUI timer must return, even against a pathological writer.
constexpr int kMaxReadAttempts = 8;

// Handle layout: gen in the high bits, slot index in the low bits.
//
// The handle is an opaque token, NOT a slot index. Packing the registration
// epoch into it is what lets publish() reject a stale handle for free: one
// relaxed load and a compare, no lock, no allocation, bounded time — still
// wait-free and still audio-thread-safe. Without it, a plugin that publishes
// after unregistering silently corrupts whichever plugin now owns that slot.
//
// 5 bits of index (32 slots) + 26 bits of gen = 31 bits, so a handle is always
// non-negative and -1 stays the unambiguous "no slot" value. `gen` bumps once
// on EACH transition (register AND unregister), so it wraps after 2^26
// transitions of the SAME slot, i.e. 2^25 register/unregister CYCLES; an ABA
// collision would need exactly that many cycles inside one snapshot() call.
constexpr int32_t  kIndexBits = 5;
constexpr int32_t  kIndexMask = (1 << kIndexBits) - 1;
constexpr uint32_t kGenMask   = 0x03FFFFFFu;

static_assert(SEAM_CALBUS_MAX_SLOTS <= kIndexMask + 1,
              "slot index must fit in kIndexBits");

inline int32_t encodeHandle(int32_t index, uint32_t gen) {
    return (int32_t)(((gen & kGenMask) << kIndexBits) | (uint32_t)index);
}

inline int32_t handleIndex(int32_t handle) {
    return handle & kIndexMask;
}

inline uint32_t handleGen(int32_t handle) {
    return ((uint32_t)handle >> kIndexBits) & kGenMask;
}

} // namespace

struct SeamCalbus {
    Slot       slots[SEAM_CALBUS_MAX_SLOTS];
    std::mutex regMutex;              // guards registration only, never publish
};

// The one copy in the process. Static here is correct precisely because this
// translation unit is compiled into exactly one dylib.
static SeamCalbus g_bus;

uint32_t seam_calbus_v1_version(void) {
    return SEAM_CALBUS_VERSION;
}

SeamCalbus* seam_calbus_v1_get(void) {
    return &g_bus;
}

int32_t seam_calbus_v1_register(SeamCalbus* bus) {
    if (!bus) return -1;
    std::lock_guard<std::mutex> lock(bus->regMutex);
    for (int32_t i = 0; i < SEAM_CALBUS_MAX_SLOTS; ++i) {
        Slot& s = bus->slots[i];
        if (s.inUse.load(std::memory_order_relaxed) != 0) continue;

        // Clear the previous owner's record — a fresh slot must never expose
        // it — through the SAME seqlock publish() uses. snapshot() does not
        // take regMutex (it must not: the GUI would then be able to stall on
        // a registering plugin), so regMutex does nothing to protect this
        // memset from a concurrent reader. Only the odd/even dance does.
        //
        // `start | 1u` instead of `start + 1`: the VST3 contract guarantees
        // this memset and any in-flight publish() on the same slot never
        // overlap in INTENDED use (see seam_calbus.h on publish()), but if
        // that contract is ever violated, a plain `+1`/`+2` dance can leave
        // `seq` PERMANENTLY ODD — two interleaved writers both compute their
        // final store as start+2 from their own snapshot of `start`, and
        // parity is preserved by construction, so an odd intermediate value
        // can become the last write. Once `seq` is stuck odd, the reader's
        // `s1 & 1u` check rejects the slot on every future attempt and no
        // amount of unregister+register heals it. Forcing the intermediate
        // value odd via `| 1u` and the final value to exactly one more than
        // that makes every writer's OWN final store even, regardless of what
        // it read, so the slot can never brick — it only recovers to "one of
        // the writers' data, chosen arbitrarily" instead. This does NOT make
        // concurrent writers safe: tearing can still be accepted by a reader
        // that races a genuinely overlapping pair. It only removes the
        // unrecoverable outcome.
        const uint32_t start = s.seq.load(std::memory_order_relaxed);
        s.seq.store(start | 1u, std::memory_order_relaxed);        // -> odd
        std::atomic_thread_fence(std::memory_order_release);
        std::memset(&s.rec, 0, sizeof(SeamCalbusRecord));
        std::atomic_thread_fence(std::memory_order_release);
        s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even

        // Open the new epoch, then advertise the slot. Both are release
        // stores, so a reader that observes them also observes the cleared
        // record and the seq bumps that precede them.
        const uint32_t g =
            (s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask;
        s.gen.store(g, std::memory_order_release);
        s.inUse.store(1, std::memory_order_release);
        return encodeHandle(i, g);
    }
    return -1;
}

void seam_calbus_v1_unregister(SeamCalbus* bus, int32_t handle) {
    if (!bus || handle < 0) return;
    Slot& s = bus->slots[handleIndex(handle)];
    std::lock_guard<std::mutex> lock(bus->regMutex);

    // A stale handle must not evict the slot's current owner. Checked under
    // regMutex, so gen cannot move under us.
    if (s.gen.load(std::memory_order_relaxed) != handleGen(handle)) return;
    if (s.inUse.load(std::memory_order_relaxed) == 0) return;

    // Close the epoch before retiring the slot: a reader that captured the old
    // gen sees it change and rejects whatever it copied.
    s.gen.store((s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask,
                std::memory_order_release);
    s.inUse.store(0, std::memory_order_release);
}

void seam_calbus_v1_publish(SeamCalbus* bus, int32_t handle,
                            const SeamCalbusRecord* rec) {
    if (!bus || !rec || handle < 0) return;
    Slot& s = bus->slots[handleIndex(handle)];

    // Stale-handle guard: one relaxed load, no lock — publish() stays
    // wait-free. gen only ever advances, so a handle whose epoch is closed can
    // never match again and its writes are dropped. (This does not make
    // publish() and register() mutually exclusive — a publish already past
    // this check when the epoch closes still writes. That window is bounded by
    // a few instructions, and an emitter stops publishing before it
    // unregisters; the point is that a *stale* handle stops writing forever.)
    //
    // `g == 0` is rejected on top of the equality check: 0 is the slot's
    // pre-registration sentinel value, never a value register() hands out
    // (it always bumps gen to >= 1 first), so a forged/zero-initialised
    // handle == 0 targeting a never-registered slot 0 would otherwise pass
    // `0 == handleGen(0)` trivially. See SEAM_CALBUS_NO_HANDLE in the header.
    const uint32_t g = s.gen.load(std::memory_order_relaxed);
    if (g == 0 || g != handleGen(handle)) return;

    // `start | 1u` / `(start | 1u) + 1u` instead of `start + 1` / `start + 2`:
    // see the identical comment in register() for why the plain +1/+2 dance
    // can leave `seq` permanently odd if this call and a register() reusing
    // the same slot ever genuinely overlap, and why OR-ing in the odd bit
    // instead makes that outcome recoverable rather than permanent.
    const uint32_t start = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start | 1u, std::memory_order_relaxed);        // -> odd
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&s.rec, rec, sizeof(SeamCalbusRecord));
    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even
}

int32_t seam_calbus_v1_snapshot(SeamCalbus* bus, SeamCalbusRecord* out,
                                int32_t maxCount) {
    if (!bus || !out || maxCount <= 0) return 0;
    int32_t n = 0;
    for (int32_t i = 0; i < SEAM_CALBUS_MAX_SLOTS && n < maxCount; ++i) {
        Slot& s = bus->slots[i];

        SeamCalbusRecord tmp;
        for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
            // Capture the ownership epoch BEFORE the copy. Acquire, so the
            // record loads below cannot be hoisted above it.
            const uint32_t g1 = s.gen.load(std::memory_order_acquire);
            const uint32_t u1 = s.inUse.load(std::memory_order_acquire);
            if (u1 == 0) break;                               // slot is free

            const uint32_t s1 = s.seq.load(std::memory_order_acquire);
            if (s1 & 1u) continue;                            // writer inside

            std::memcpy(&tmp, &s.rec, sizeof(SeamCalbusRecord));

            // Acquire fence: the record loads above may not be reordered after
            // the validation loads below, so what follows really is a check on
            // a completed copy.
            std::atomic_thread_fence(std::memory_order_acquire);

            if (s.seq.load(std::memory_order_relaxed) != s1) continue;  // torn

            // Re-capture the epoch. Unchanged {gen, inUse} means one single
            // uninterrupted registration epoch spanned the whole copy: the
            // slot was neither released (inUse would drop, gen would move) nor
            // released-and-reclaimed (gen would move twice — it never returns
            // to an earlier value). gen alone would do; inUse is kept because
            // the pair is what the invariant is stated in terms of.
            //
            // HONEST NOTE ON WHAT THIS ACTUALLY REJECTS TODAY: with register()'s
            // memset now inside the seqlock (see the struct-level comment
            // above), every ownership transition necessarily moves `seq` by 2,
            // so the `s1 != s2` check a few lines up already rejects every
            // ABA'd copy on its own — there is no reachable state where `seq`
            // is stable, the record changed, and ownership also changed. This
            // gen/inUse re-check is therefore DEFENCE-IN-DEPTH, not the thing
            // that is load-bearing today: it is what would catch an ABA if a
            // future change ever let something touch `rec` (or otherwise moved
            // `seq`) without also touching the seqlock — e.g. "skip the memset
            // when the slot is already zero". Keep it for that reason, but
            // don't credit it with preventing ABA in the current code; the seq
            // check does that.
            const uint32_t u2 = s.inUse.load(std::memory_order_acquire);
            const uint32_t g2 = s.gen.load(std::memory_order_acquire);
            if (u2 != u1 || g2 != g1) continue;               // churned

            out[n++] = tmp;
            break;
        }
    }
    return n;
}

```

- [ ] **Step 5: Write the dylib CMake target**

Create `plugins/_common/calbus/CMakeLists.txt`:

```cmake
# libseamcalbus — the peer-aware calibration bus (Spec 2).
#
# This is the ONLY compiled artefact in _common/ (seam_meter.h, seam_haar.h et
# al. are header-only). It has to be a shared library and not a header: static
# state does not cross .vst3 module boundaries, and multipink, ltglide and strx
# are three separate bundles. See seam_calbus.h for the full rationale.
add_library(seam_calbus SHARED seam_calbus.cpp)

set_target_properties(seam_calbus PROPERTIES
    OUTPUT_NAME seamcalbus
)
target_compile_features(seam_calbus PRIVATE cxx_std_17)
target_include_directories(seam_calbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Install into the user domain so no sudo is needed, consistent with VST3
# bundles landing in ~/Library/Audio/Plug-Ins/VST3. The client searches
# SEAM_CALBUS_PATH, then ~/Library/Application Support/SEAM, then /Library/...
if(APPLE)
    set(SEAM_CALBUS_INSTALL_DIR "$ENV{HOME}/Library/Application Support/SEAM")
    add_custom_command(TARGET seam_calbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SEAM_CALBUS_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:seam_calbus> "${SEAM_CALBUS_INSTALL_DIR}/"
        COMMENT "[SEAM] Installing libseamcalbus.dylib to ${SEAM_CALBUS_INSTALL_DIR}"
    )
endif()
```

- [ ] **Step 6: Register the target and the test**

In the root `CMakeLists.txt`, immediately above the `# ─── Plugins ───` comment block (line ~80), add:

```cmake
# ─── Shared compiled libraries ───────────────────────────────────────────────
add_subdirectory(plugins/_common/calbus)
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(seam_calbus_test
    seam_calbus_test.cpp
)
target_include_directories(seam_calbus_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_link_libraries(seam_calbus_test PRIVATE seam_calbus)
target_compile_features(seam_calbus_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_test COMMAND seam_calbus_test)
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-test -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DSEAM_BUILD_TESTS=ON
cmake --build build-test --target seam_calbus_test --config Release
./build-test/tests/Release/seam_calbus_test
```

Expected: all test cases pass, with `torn == 0` in the hammer case and
`torn == 0 && phantom == 0` in the churn case. Both are concurrency tests, so
run the binary several times — one green run proves little. Note that the
liveness assertions are a large fraction of the iterations, not `reads > 0`:
a reader that gives up on almost every attempt is a broken reader, and
`reads > 0` would pass it.

Do NOT lean on ThreadSanitizer here. It reports no data race against code that
provably corrupts records, because it models the fences and stays silent. The
behavioural cross-field probe is what catches this class of bug.

- [ ] **Step 8: Commit**

```bash
git add plugins/_common/calbus CMakeLists.txt tests/seam_calbus_test.cpp tests/CMakeLists.txt
git commit -m "feat(calbus): shared calibration-bus dylib with per-slot seqlock

Static state does not cross .vst3 module boundaries, so the registry lives
in its own dylib behind a pure C ABI. 32 POD slots, each guarded by a
seqlock: writers (audio thread) are wait-free, the GUI reader retries."
```

---

### Task 2: Client wrapper — dlopen, version gate, null mode

**Files:**
- Create: `plugins/_common/calbus/seam_calbus_client.h`
- Test: `tests/seam_calbus_client_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a new test target)

**Interfaces:**
- Consumes: the Task 1 C ABI (`seam_calbus_v1_get`, `_register`, `_unregister`, `_publish`, `_snapshot`, `_version`), `SeamCalbusRecord`.
- Produces: `Seam::CalbusClient`, used by Tasks 3–5 —
  `static CalbusClient& CalbusClient::instance()`;
  `bool available() const`;
  `int32_t registerSlot()` → handle, or `-1` when unavailable/full;
  `void unregisterSlot(int32_t handle)`;
  `void publish(int32_t handle, const SeamCalbusRecord& rec)`;
  `int32_t snapshot(SeamCalbusRecord* out, int32_t maxCount)`.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_calbus_client_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus_client.h"

// This test runs with SEAM_CALBUS_PATH pointing at the freshly built dylib
// (see tests/CMakeLists.txt), so the client must find and load it.
TEST_CASE("client loads the dylib and reports available") {
    auto& c = Seam::CalbusClient::instance();
    CHECK(c.available());
}

TEST_CASE("client round-trips a record through the real dylib") {
    auto& c = Seam::CalbusClient::instance();
    REQUIRE(c.available());

    int32_t h = c.registerSlot();
    CHECK(h >= 0);

    SeamCalbusRecord in{};
    in.kind    = kSeamCalbusPink;
    in.stoneId = 3;
    in.active  = 1;
    in.levelDb = -23.0;
    in.sampleRate = 48000.0;
    in.u.pink.slotStart = 8;
    in.u.pink.slotCount = 4;
    c.publish(h, in);

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    int32_t n = c.snapshot(out, SEAM_CALBUS_MAX_SLOTS);
    CHECK(n == 1);
    CHECK(out[0].kind == (uint32_t)kSeamCalbusPink);
    CHECK(out[0].stoneId == 3u);
    CHECK(out[0].u.pink.slotStart == 8);
    CHECK(out[0].u.pink.slotCount == 4);
    CHECK(out[0].levelDb == doctest::Approx(-23.0));

    c.unregisterSlot(h);
    CHECK(c.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}

TEST_CASE("null mode: every call is a harmless no-op") {
    // Exercises the same code paths a plugin hits when the dylib is missing.
    Seam::CalbusClient null = Seam::CalbusClient::makeUnavailableForTest();
    CHECK_FALSE(null.available());
    CHECK(null.registerSlot() == -1);

    SeamCalbusRecord r{};
    null.publish(-1, r);           // must not crash
    null.unregisterSlot(-1);       // must not crash

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(null.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target seam_calbus_client_test --config Release
```

Expected: FAIL — `'seam_calbus_client.h' file not found` (and the target does not exist yet).

- [ ] **Step 3: Write the client wrapper**

Create `plugins/_common/calbus/seam_calbus_client.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · calbus client — loads libseamcalbus and adapts its C ABI.
//
// Header-only, included by multipink, ltglide and strx. One instance per
// .vst3 module (function-local static), which owns the dlopen handle for the
// module's lifetime.
//
// WHY dlopen AND NOT FDynLibrary: the SDK's loader takes a `tchar*` path, and
// UNICODE is defined by default (ftypes.h:30), so tchar is char16_t
// (ftypes.h:91). Converting a $HOME-derived filesystem path to UTF-16 only for
// the SDK to convert it back buys nothing. dlopen covers both platforms the
// suite builds for (macmain.cpp, linuxmain.cpp), and the handle lives in a
// static, so FDynLibrary's refcounting is unnecessary too.
//
// DEGRADATION IS THE POINT: when the dylib is missing or its version does not
// match, the client enters null mode and every call becomes a silent no-op.
// The bus is an observer, and an observer that breaks the instrument is
// unacceptable in the room. multipink still makes noise; strx still analyses.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus.h"

#include <cstdlib>
#include <string>
#include <vector>

#if !defined(_WIN32)
  #include <dlfcn.h>
#endif

namespace Seam {

class CalbusClient {
public:
    // One per .vst3 module.
    static CalbusClient& instance() {
        static CalbusClient c;
        return c;
    }

    // For tests: a client that never loaded anything.
    static CalbusClient makeUnavailableForTest() { return CalbusClient(NullTag{}); }

    bool available() const { return bus_ != nullptr; }

    int32_t registerSlot() {
        return available() ? register_(bus_) : -1;
    }

    void unregisterSlot(int32_t handle) {
        if (available() && handle >= 0) unregister_(bus_, handle);
    }

    // RT-safe when the bus is available; a plain branch when it is not.
    void publish(int32_t handle, const SeamCalbusRecord& rec) {
        if (available() && handle >= 0) publish_(bus_, handle, &rec);
    }

    int32_t snapshot(SeamCalbusRecord* out, int32_t maxCount) {
        return available() ? snapshot_(bus_, out, maxCount) : 0;
    }

private:
    struct NullTag {};
    explicit CalbusClient(NullTag) {}

    CalbusClient() { load(); }

    // The handle is intentionally never dlclose()d: the bus must outlive every
    // plugin instance in the module, and the OS drops it when the module goes.
    void load() {
#if !defined(_WIN32)
        for (const std::string& path : candidatePaths()) {
            void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!h) continue;

            auto version = (uint32_t (*)(void))::dlsym(h, "seam_calbus_v1_version");
            if (!version || version() != SEAM_CALBUS_VERSION) { ::dlclose(h); continue; }

            auto get = (SeamCalbus* (*)(void))::dlsym(h, "seam_calbus_v1_get");
            register_   = (int32_t (*)(SeamCalbus*))::dlsym(h, "seam_calbus_v1_register");
            unregister_ = (void (*)(SeamCalbus*, int32_t))::dlsym(h, "seam_calbus_v1_unregister");
            publish_    = (void (*)(SeamCalbus*, int32_t, const SeamCalbusRecord*))
                          ::dlsym(h, "seam_calbus_v1_publish");
            snapshot_   = (int32_t (*)(SeamCalbus*, SeamCalbusRecord*, int32_t))
                          ::dlsym(h, "seam_calbus_v1_snapshot");

            if (!get || !register_ || !unregister_ || !publish_ || !snapshot_) {
                register_ = nullptr; unregister_ = nullptr;
                publish_ = nullptr; snapshot_ = nullptr;
                ::dlclose(h);
                continue;
            }
            bus_ = get();
            return;
        }
#endif
    }

    static std::vector<std::string> candidatePaths() {
        static const char* kLib =
#if defined(__APPLE__)
            "libseamcalbus.dylib";
#else
            "libseamcalbus.so";
#endif
        std::vector<std::string> out;
        if (const char* env = std::getenv("SEAM_CALBUS_PATH")) {
            // Accept either a directory or a full path to the library.
            std::string e(env);
            out.push_back(e);
            out.push_back(e + "/" + kLib);
        }
        if (const char* home = std::getenv("HOME")) {
            out.push_back(std::string(home) + "/Library/Application Support/SEAM/" + kLib);
        }
        out.push_back(std::string("/Library/Application Support/SEAM/") + kLib);
        return out;
    }

    SeamCalbus* bus_ = nullptr;
    int32_t (*register_)(SeamCalbus*) = nullptr;
    void    (*unregister_)(SeamCalbus*, int32_t) = nullptr;
    void    (*publish_)(SeamCalbus*, int32_t, const SeamCalbusRecord*) = nullptr;
    int32_t (*snapshot_)(SeamCalbus*, SeamCalbusRecord*, int32_t) = nullptr;
};

} // namespace Seam
```

- [ ] **Step 4: Add the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(seam_calbus_client_test
    seam_calbus_client_test.cpp
)
target_include_directories(seam_calbus_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
# The client loads the dylib at runtime — it must exist before the test runs.
add_dependencies(seam_calbus_client_test seam_calbus)
target_compile_features(seam_calbus_client_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_client_test COMMAND seam_calbus_client_test)
set_tests_properties(seam_calbus_client_test PROPERTIES
    ENVIRONMENT "SEAM_CALBUS_PATH=$<TARGET_FILE:seam_calbus>"
)
```

Note: `seam_calbus_client_test` deliberately does NOT link `seam_calbus` — it must find it through `dlopen`, which is the code path the plugins take.

- [ ] **Step 5: Write the version-rejection test**

The client is a process-wide singleton that loads once, so the rejection path needs its own executable pointed at a stub library. This is the guard that stops a future v2 layout from being read through v1 eyes — silently misinterpreting records is worse than having no bus at all.

Create `tests/calbus_badversion_stub.cpp`:

```cpp
// A stub that answers the version query with a version the client must refuse.
// It exports the full v1 symbol set, so a client that reaches ANY of these has
// skipped the version gate — which is exactly the bug this test catches.
#include "seam_calbus.h"

static SeamCalbus* g_fake = (SeamCalbus*)1;

extern "C" {
SEAM_CALBUS_API uint32_t seam_calbus_v1_version(void) { return 99u; }
SEAM_CALBUS_API SeamCalbus* seam_calbus_v1_get(void) { return g_fake; }
SEAM_CALBUS_API int32_t seam_calbus_v1_register(SeamCalbus*) { return 0; }
SEAM_CALBUS_API void seam_calbus_v1_unregister(SeamCalbus*, int32_t) {}
SEAM_CALBUS_API void seam_calbus_v1_publish(SeamCalbus*, int32_t, const SeamCalbusRecord*) {}
SEAM_CALBUS_API int32_t seam_calbus_v1_snapshot(SeamCalbus*, SeamCalbusRecord*, int32_t) { return 7; }
}
```

Create `tests/seam_calbus_version_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus_client.h"

// SEAM_CALBUS_PATH points at the version-99 stub (see tests/CMakeLists.txt).
TEST_CASE("client refuses a dylib with a mismatched version") {
    auto& c = Seam::CalbusClient::instance();
    CHECK_FALSE(c.available());
    CHECK(c.registerSlot() == -1);

    // The stub's snapshot returns 7; a client that fell through the gate would
    // report 7 records that do not exist.
    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(c.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
add_library(calbus_badversion_stub SHARED calbus_badversion_stub.cpp)
target_include_directories(calbus_badversion_stub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_compile_features(calbus_badversion_stub PRIVATE cxx_std_17)

add_executable(seam_calbus_version_test
    seam_calbus_version_test.cpp
)
target_include_directories(seam_calbus_version_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
add_dependencies(seam_calbus_version_test calbus_badversion_stub)
target_compile_features(seam_calbus_version_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_version_test COMMAND seam_calbus_version_test)
set_tests_properties(seam_calbus_version_test PROPERTIES
    ENVIRONMENT "SEAM_CALBUS_PATH=$<TARGET_FILE:calbus_badversion_stub>"
)
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-test -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DSEAM_BUILD_TESTS=ON
cmake --build build-test --target seam_calbus_client_test --config Release
cmake --build build-test --target seam_calbus_version_test --config Release
cd build-test && ctest -R "seam_calbus_client_test|seam_calbus_version_test" --output-on-failure
```

Expected: both PASS. (Run through `ctest`, not the binaries directly — `SEAM_CALBUS_PATH` is set per-test by the test properties, and the two tests need different values.)

- [ ] **Step 7: Commit**

```bash
git add plugins/_common/calbus/seam_calbus_client.h tests/seam_calbus_client_test.cpp \
        tests/seam_calbus_version_test.cpp tests/calbus_badversion_stub.cpp tests/CMakeLists.txt
git commit -m "feat(calbus): dlopen client wrapper with version gate and null mode

Header-only adapter over the C ABI. Missing dylib or version mismatch puts
the client in null mode where every call is a silent no-op, so a plugin
never loses audio because the bus is absent."
```

---

### Task 3: multipink publishes

**Files:**
- Modify: `plugins/multipink/source/multipink_ids.h` (add `kParamStoneId`)
- Modify: `plugins/multipink/source/multipink_processor.h` (add members + helper)
- Modify: `plugins/multipink/source/multipink_processor.cpp` (`initialize`, `setActive`, `process`, `readParameterChanges`, `setState`, `getState`)
- Modify: `plugins/multipink/CMakeLists.txt` (include dir for calbus)

**Interfaces:**
- Consumes: `Seam::CalbusClient` (Task 2), `SeamCalbusRecord`, `kSeamCalbusPink`.
- Produces: a `kSeamCalbusPink` record whose `u.pink.slotStart`/`slotCount` mirror the pool claim, `active = claimed && !muted`. Consumed by Task 5's status line.

- [ ] **Step 1: Add the parameter ID**

In `plugins/multipink/source/multipink_ids.h`, extend the enum:

```cpp
enum MULTIPINKParams : Steinberg::Vst::ParamID {
    kParamReference   = 100,   // 0=-23, 1=-20, 2=-18 dBFS RMS  (stepped)
    kParamTrim        = 101,   // -6.0 … +6.0 dB                (continuous)
    kParamMute        = 102,   // 0 / 1                         (bool)
    kParamStoneId     = 103,   // 0=undeclared, 1..8            (stepped)
    // Read-only display parameters, pushed from the audio thread:
    kParamSlotStart   = 200,   // -1..63 (sentinel -1 = unclaimed)
    kParamSlotCount   = 201,   // 0..64
    kParamPoolStatus  = 202,   // PoolStatus enum (0..3)
};

// STONE identity for the calibration bus (Spec 2). Declared by hand and never
// inferred from the pool slot: with four STONEs in the room, an instance that
// guesses is an instance that calibrates the wrong power amp. 0 = undeclared,
// which strx renders as "STONE ?".
static constexpr Steinberg::int32 kStoneIdStepCount = 9;   // "?" + 1..8
```

- [ ] **Step 2: Declare the parameter and the bus members**

In `plugins/multipink/source/multipink_processor.cpp`, inside `initialize()`, after the `Mute` parameter block (line ~48) add:

```cpp
    auto* stoneParam = new StringListParameter(
        STR16("STONE"), kParamStoneId, STR16(""),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    stoneParam->appendString(STR16("?"));
    stoneParam->appendString(STR16("1"));
    stoneParam->appendString(STR16("2"));
    stoneParam->appendString(STR16("3"));
    stoneParam->appendString(STR16("4"));
    stoneParam->appendString(STR16("5"));
    stoneParam->appendString(STR16("6"));
    stoneParam->appendString(STR16("7"));
    stoneParam->appendString(STR16("8"));
    parameters.addParameter(stoneParam);
```

In `plugins/multipink/source/multipink_processor.h`, add the include beside the others:

```cpp
#include "seam_calbus_client.h"
```

and add these members next to the other parameter atomics (after `paramMute_`):

```cpp
    std::atomic<int>    paramStoneId_{0};        // 0 = undeclared, 1..8

    // Calibration bus (Spec 2). The handle is claimed in setActive and
    // released there; publishing happens from process() on the audio thread,
    // which is why the bus uses a seqlock and not a mutex.
    int32_t busHandle_ = -1;

    // Build and publish this instance's record. Safe to call from the audio
    // thread; a no-op when the bus is unavailable or the slot is unclaimed.
    void publishBusRecord();
```

- [ ] **Step 3: Write the publish helper and wire the lifecycle**

In `plugins/multipink/source/multipink_processor.cpp`, add the helper (place it just above `process()`):

```cpp
void MULTIPINKProcessor::publishBusRecord() {
    if (busHandle_ < 0) return;

    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusPink;
    r.stoneId = (uint32_t)paramStoneId_.load();
    // "active" is the conjunction that exists nowhere else: the pool tracks
    // ownership, so with four instances loaded it reports slots 0/4/8/12 all
    // claimed and cannot say which one is sounding. Level stays a separate
    // field — reference (-23/-20/-18) plus trim (±6) never reaches silence,
    // so an "audible level" test would need a threshold nobody can justify.
    r.active  = (claimedStart_ >= 0 && paramMute_.load() == 0) ? 1u : 0u;
    r.levelDb = kReferenceLevelsDb[paramReferenceIdx_.load()] + paramTrimDb_.load();
    r.sampleRate = processSetup.sampleRate;
    r.u.pink.slotStart = claimedStart_;
    r.u.pink.slotCount = claimedChannels_;

    CalbusClient::instance().publish(busHandle_, r);
}
```

In `setActive()`, inside the `if (state)` branch, after the pool claim switch (after line ~90, where `poolStatus_` is set), add:

```cpp
        busHandle_ = CalbusClient::instance().registerSlot();
        publishBusRecord();
```

and in the `else` branch, after `poolStatus_.store((int)PoolStatus::Unclaimed);` (line ~97), add:

```cpp
        CalbusClient::instance().unregisterSlot(busHandle_);
        busHandle_ = -1;
```

In `process()`, immediately after `readParameterChanges(data);` (line 110), add:

```cpp
    // Parameter changes arrive here, on the audio thread — hence the seqlock.
    publishBusRecord();
```

In `readParameterChanges`, add a case beside the existing ones:

```cpp
            case kParamStoneId:
                paramStoneId_.store((int)(v * (kStoneIdStepCount - 1) + 0.5));
                break;
```

- [ ] **Step 4: Persist the parameter**

In `setState()`, after the existing reads, add:

```cpp
    int32 stoneId = 0;
    if (s.readInt32(stoneId)) paramStoneId_.store((stoneId >= 0 && stoneId <= 8) ? stoneId : 0);
```

In `getState()`, after the existing writes, add:

```cpp
    if (!s.writeInt32(paramStoneId_.load())) return kResultFalse;
```

- [ ] **Step 5: Give the target the calbus include path**

In `plugins/multipink/CMakeLists.txt`, after the `smtg_add_vst3plugin` call (line ~22), add:

```cmake
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common/calbus
)
add_dependencies(${target} seam_calbus)
```

- [ ] **Step 6: Build and run the validator**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-release -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build-release --target multipink --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/multipink.vst3
```

Expected: build succeeds; validator reports all tests passed with 0 failures (multipink was 47/47 before the new parameter; the count may rise, the failure count must stay 0).

- [ ] **Step 7: Commit**

```bash
git add plugins/multipink CMakeLists.txt
git commit -m "feat(multipink): publish pink emitter record on the calibration bus

Adds an explicit STONE id parameter (0 = undeclared) and publishes
{kind, stoneId, active, levelDb, slotStart, slotCount} whenever the pool
claim or a parameter changes. active = claimed && !muted: the pool alone
tracks ownership and cannot say which of four instances is sounding."
```

---

### Task 4: ltglide publishes pass events

**Files:**
- Modify: `plugins/ltglide/source/ltglide_dsp.h` (`GlideTransport`: pass counter)
- Modify: `tests/ltglide_dsp_test.cpp` (append test cases)
- Modify: `plugins/ltglide/source/ltglide_ids.h` (add `kParamStoneId`)
- Modify: `plugins/ltglide/source/ltglide_processor.h` (members, `processBlock` signature)
- Modify: `plugins/ltglide/source/ltglide_processor.cpp` (`initialize`, `setActive`, `process`, `processBlock`, `readParameterChanges`, `setState`, `getState`)
- Modify: `plugins/ltglide/CMakeLists.txt` (include dir for calbus)

**Interfaces:**
- Consumes: `Seam::CalbusClient` (Task 2), `SeamCalbusRecord`, `kSeamCalbusGlide`.
- Produces: `uint64_t GlideTransport::passCount() const` (SDK-free, testable); and a `kSeamCalbusGlide` record carrying `passCounter`, `passStartSample` (`-1` when the host clock is invalid), `f0`, `f1`, `durationSec`, `deltaSec`, `sweepMode`, `diracMode`. Consumed by Task 5's status line and, later, by Spec 3.

- [ ] **Step 1: Write the failing DSP test**

Append to `tests/ltglide_dsp_test.cpp`:

```cpp
TEST_CASE("GlideTransport counts passes") {
    Seam::ltglide::GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(false);

    CHECK(t.passCount() == 0u);

    t.trigger();
    CHECK(t.passCount() == 1u);          // beginPass happened at trigger

    // Run the whole pass out: head dirac + lead + glide + tail + tail dirac.
    using GT = Seam::ltglide::GlideTransport;
    const long total = (long)((GT::kLeadSec + 2.0 + GT::kTailSec) * 48000.0) + 16;
    for (long i = 0; i < total; ++i) t.process();
    CHECK_FALSE(t.running());
    CHECK(t.passCount() == 1u);          // one pass, counted once
}

TEST_CASE("GlideTransport increments the counter once per looped pass") {
    Seam::ltglide::GlideTransport t;
    t.prepare(48000.0);
    t.setSweepSeconds(2.0);
    t.setLoop(true);

    using GT = Seam::ltglide::GlideTransport;
    const long onePass = (long)((GT::kLeadSec + 2.0 + GT::kTailSec + GT::kWaitSec) * 48000.0);
    uint64_t seen = 0;
    for (long i = 0; i < onePass * 3; ++i) {
        const uint64_t before = t.passCount();
        t.process();
        if (t.passCount() != before) ++seen;
    }
    CHECK(seen >= 2u);                   // at least two passes started
    CHECK(t.passCount() == seen);        // every increment is exactly +1
}
```

Adjust the `Seam::ltglide::` namespace qualification to match the existing cases in the file if it differs.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target ltglide_dsp_test --config Release
```

Expected: FAIL — `no member named 'passCount' in 'GlideTransport'`.

- [ ] **Step 3: Add the counter to the transport**

In `plugins/ltglide/source/ltglide_dsp.h`, add the accessor beside `running()` (line ~101):

```cpp
    // Monotone pass counter, +1 at each pass start. The receiver samples this
    // at GUI rate and cannot otherwise distinguish a new pass from the
    // previous one when the parameters are identical (calbus, Spec 2).
    uint64_t passCount() const { return passCounter_; }
```

change `beginPass()` (line ~137):

```cpp
    void beginPass() { state_ = State::HeadDirac; counter_ = 0; ++passCounter_; }
```

and add the member beside `counter_`:

```cpp
    uint64_t passCounter_ = 0;
```

Leave `reset()` and `prepare()` alone: the counter is monotone for the life of the instance, so a receiver never sees it go backwards.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-test --target ltglide_dsp_test --config Release
./build-test/tests/Release/ltglide_dsp_test
```

Expected: PASS, including the pre-existing cases.

- [ ] **Step 5: Commit the DSP change**

```bash
git add plugins/ltglide/source/ltglide_dsp.h tests/ltglide_dsp_test.cpp
git commit -m "feat(ltglide): monotone pass counter in GlideTransport

The calibration receiver samples at GUI rate and cannot tell a new pass
from the previous one when parameters match. Counting passes is what makes
Spec 3's pass averaging possible."
```

- [ ] **Step 6: Add the parameter ID**

In `plugins/ltglide/source/ltglide_ids.h`, extend the enum and add the constant:

```cpp
enum LTGLIDEParams : Steinberg::Vst::ParamID {
    kParamLevel   = 100,   // -60 … 0 dBFS       (linear taper)
    kParamF0      = 101,   // 20 … 20000 Hz      (log) sweep start
    kParamF1      = 102,   // 20 … 20000 Hz      (log) sweep end
    kParamSmode   = 103,   // 0 linear / 1 exponential
    kParamDmode   = 104,   // 0 passo / 1 gap
    kParamDelta   = 105,   // grain spacing, seconds
    kParamT       = 106,   // sweep duration, seconds
    kParamLoop    = 108,   // toggle: continuous passes (the sole transport control)
    kParamStoneId = 109,   // 0=undeclared, 1..8 (stepped) — calibration bus
};

// STONE identity for the calibration bus (Spec 2). ltglide has no slot to be
// inferred from and its chain identity lives in the host routing, which the
// receiver cannot read — so it is declared by hand. 0 = undeclared ("STONE ?").
static constexpr Steinberg::int32 kStoneIdStepCount = 9;   // "?" + 1..8
```

- [ ] **Step 7: Wire the processor**

In `plugins/ltglide/source/ltglide_processor.h`, add the include:

```cpp
#include "seam_calbus_client.h"
```

add the members beside `paramLoop_`:

```cpp
    std::atomic<int> paramStoneId_{0};      // 0 = undeclared, 1..8

    // Calibration bus (Spec 2).
    int32_t  busHandle_ = -1;
    uint64_t lastPublishedPass_ = 0;

    // Publish this instance's record. Called from the audio thread.
    // hostStartSample is -1 when the host provides no valid continuous clock.
    void publishBusRecord(int64_t hostStartSample);
```

and change the `processBlock` declaration (line ~63 area) to carry the host clock:

```cpp
    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples,
                      int64_t blockStartSample);
```

In `plugins/ltglide/source/ltglide_processor.cpp`, add the STONE parameter in `initialize()` next to the others:

```cpp
    auto* stoneParam = new StringListParameter(
        STR16("STONE"), kParamStoneId, STR16(""),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    stoneParam->appendString(STR16("?"));
    stoneParam->appendString(STR16("1"));
    stoneParam->appendString(STR16("2"));
    stoneParam->appendString(STR16("3"));
    stoneParam->appendString(STR16("4"));
    stoneParam->appendString(STR16("5"));
    stoneParam->appendString(STR16("6"));
    stoneParam->appendString(STR16("7"));
    stoneParam->appendString(STR16("8"));
    parameters.addParameter(stoneParam);
```

add the case in `readParameterChanges` (beside `kParamLoop`, line ~249):

```cpp
            case kParamStoneId:
                paramStoneId_.store((int)(v * (kStoneIdStepCount - 1) + 0.5));
                break;
```

add the helper above `process()`:

```cpp
void LTGLIDEProcessor::publishBusRecord(int64_t hostStartSample) {
    if (busHandle_ < 0) return;

    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusGlide;
    r.stoneId = (uint32_t)paramStoneId_.load();
    r.active  = transport_.running() ? 1u : 0u;
    r.levelDb = paramLevelDb_.load();
    r.sampleRate = processSetup.sampleRate;
    r.u.glide.passCounter     = transport_.passCount();
    r.u.glide.passStartSample = hostStartSample;
    r.u.glide.f0          = paramF0Hz_.load();
    r.u.glide.f1          = paramF1Hz_.load();
    r.u.glide.durationSec = paramTSec_.load();
    r.u.glide.deltaSec    = paramDeltaSec_.load();
    r.u.glide.sweepMode   = (uint32_t)paramSmode_.load();
    r.u.glide.diracMode   = (uint32_t)paramDmode_.load();

    CalbusClient::instance().publish(busHandle_, r);
}
```

in `setActive()`, inside the activation branch (after `transport_.setLoop(...)`, line ~115):

```cpp
        busHandle_ = CalbusClient::instance().registerSlot();
        lastPublishedPass_ = transport_.passCount();
        publishBusRecord(-1);
```

and in the deactivation branch:

```cpp
        CalbusClient::instance().unregisterSlot(busHandle_);
        busHandle_ = -1;
```

- [ ] **Step 8: Read the host clock and publish pass starts**

In `process()` (line ~132), read the continuous clock before dispatching to `processBlock`:

```cpp
    // ProcessContext::continuousTimeSamples is an OPTIONAL anchor: the host
    // declares its validity with kContTimeValid, and processContext may be
    // null outright. All of Spec 3's synchronisation rests on this field, so
    // detect its absence and say so (-1 -> strx prints "no host clock")
    // instead of silently anchoring passes to a fictional zero.
    int64_t blockStart = -1;
    if (data.processContext &&
        (data.processContext->state & ProcessContext::kContTimeValid)) {
        blockStart = (int64_t)data.processContext->continuousTimeSamples;
    }
```

and pass it through both dispatch calls, e.g.:

```cpp
    if (data.symbolicSampleSize == kSample32) {
        processBlock<float>((float**)out, numChannels, data.numSamples, blockStart);
    } else {
        processBlock<double>((double**)out, numChannels, data.numSamples, blockStart);
    }
```

In `processBlock` (line ~255), detect the pass start per sample and publish with the exact anchor. Inside the per-sample loop, after the `transport_.process()` tick, add:

```cpp
        // The tick that opens a pass is the head Dirac itself, so `i` is the
        // pass's exact sample offset within this block.
        const uint64_t pc = transport_.passCount();
        if (pc != lastPublishedPass_) {
            lastPublishedPass_ = pc;
            publishBusRecord(blockStartSample >= 0 ? blockStartSample + i : -1);
        }
```

After the loop, publish once more so `active` follows the transport back to idle:

```cpp
    if (transport_.running() == false) publishBusRecord(-1);
```

Update the explicit template instantiations at the bottom of the file (line ~290):

```cpp
template void LTGLIDEProcessor::processBlock<float>(float**, int, int, int64_t);
template void LTGLIDEProcessor::processBlock<double>(double**, int, int, int64_t);
```

- [ ] **Step 9: Persist the parameter**

In `setState()`, after the existing reads:

```cpp
    int32 stoneId = 0;
    if (s.readInt32(stoneId)) paramStoneId_.store((stoneId >= 0 && stoneId <= 8) ? stoneId : 0);
```

In `getState()`, after the existing writes:

```cpp
    if (!s.writeInt32(paramStoneId_.load())) return kResultFalse;
```

- [ ] **Step 10: Give the target the calbus include path**

In `plugins/ltglide/CMakeLists.txt`, after the `smtg_add_vst3plugin` call, add:

```cmake
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common/calbus
)
add_dependencies(${target} seam_calbus)
```

- [ ] **Step 11: Build and run the validator**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target ltglide --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/ltglide.vst3
```

Expected: build succeeds; validator reports 0 failures.

- [ ] **Step 12: Commit**

```bash
git add plugins/ltglide CMakeLists.txt
git commit -m "feat(ltglide): publish glide pass events on the calibration bus

Publishes {passCounter, passStartSample, f0, f1, T, delta, modes} at each
pass start, anchored to ProcessContext::continuousTimeSamples. The anchor is
optional: without kContTimeValid we publish -1 rather than inventing a zero
that Spec 3's cross-correlation would silently trust."
```

---

### Task 5: strx status line

**Files:**
- Create: `plugins/strx/source/strx_status.h`
- Modify: `plugins/strx/source/strx_ids.h` (add `kViewStatus`)
- Modify: `plugins/strx/source/strx_processor.cpp` (`createCustomView` branch)
- Modify: `plugins/strx/resource/strx.uidesc` (add the view)
- Modify: `plugins/strx/CMakeLists.txt` (include dir for calbus)

**Interfaces:**
- Consumes: `Seam::CalbusClient::snapshot` (Task 2); the pink record from Task 3 and the glide record from Task 4.
- Produces: `Seam::StrxStatusLine` (a `VSTGUI::CView`), constructed as
  `StrxStatusLine(const VSTGUI::CRect& size, VSTGUI::CFontRef font, const VSTGUI::CColor& textColor)`.

- [ ] **Step 1: Add the view name tag**

In `plugins/strx/source/strx_ids.h`, beside the other tags:

```cpp
static const char* kViewStatus     = "StrxStatus";
```

- [ ] **Step 2: Write the status view**

Create `plugins/strx/source/strx_status.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — calibration-bus status line (Spec 2).
//
// The one user-visible feature of the calibration bus, and its diagnostic:
// it answers "which emitter am I hearing?" — the question the pool alone
// cannot answer, because the pool tracks slot OWNERSHIP and four loaded
// multipink instances all own their slots while only one is sounding.
//
// Spec 2 reads the bus from the GUI thread ONLY; strx's audio thread never
// touches it. That arrives with Spec 3, and the seqlock is already built for
// it.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"

#include "seam_calbus_client.h"

#include <cstdio>
#include <string>

namespace Seam {

class StrxStatusLine : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerMs = 100;   // ~10 Hz: text, not animation

    StrxStatusLine(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                   const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), font_(font), textColor_(textColor) {
        if (font_) font_->remember();
        timer_ = new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer*) { refresh(); }, kTimerMs, /*doStart*/true);
    }

    ~StrxStatusLine() override {
        if (timer_) timer_->forget();
        if (font_) font_->forget();
    }

    void draw(VSTGUI::CDrawContext* c) override {
        c->setDrawMode(VSTGUI::kAntiAliasing);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        c->drawString(text_.c_str(), getViewSize(), VSTGUI::kLeftText);
        setDirty(false);
    }

private:
    void refresh() {
        const std::string next = compose();
        if (next != text_) { text_ = next; invalid(); }
    }

    static void appendStone(char* buf, size_t n, uint32_t stoneId) {
        if (stoneId >= 1 && stoneId <= 8) std::snprintf(buf, n, "STONE %u", stoneId);
        else                              std::snprintf(buf, n, "STONE ?");
    }

    std::string compose() {
        auto& client = CalbusClient::instance();
        if (!client.available()) return "calbus unavailable";

        SeamCalbusRecord recs[SEAM_CALBUS_MAX_SLOTS];
        const int32_t n = client.snapshot(recs, SEAM_CALBUS_MAX_SLOTS);
        if (n == 0) return "calbus: no emitter";

        // One emitter sounds at a time by method (see the design doc), so the
        // first active record is the answer. Registered-but-silent emitters
        // are reported as a count, which is how you notice that the multipink
        // you meant to un-mute is still muted.
        int registered = 0;
        for (int32_t i = 0; i < n; ++i) {
            if (!recs[i].active) { ++registered; continue; }
            return describe(recs[i]);
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "calbus: %d idle, none sounding", registered);
        return buf;
    }

    static std::string describe(const SeamCalbusRecord& r) {
        char stone[16];
        appendStone(stone, sizeof(stone), r.stoneId);
        char buf[192];
        if (r.kind == (uint32_t)kSeamCalbusPink) {
            std::snprintf(buf, sizeof(buf), "multipink · %s · slot %d-%d · %.1f dB",
                          stone, r.u.pink.slotStart,
                          r.u.pink.slotStart + r.u.pink.slotCount - 1, r.levelDb);
        } else {
            char clock[24];
            if (r.u.glide.passStartSample < 0) std::snprintf(clock, sizeof(clock), "no host clock");
            else                               std::snprintf(clock, sizeof(clock), "T=%.0fs", r.u.glide.durationSec);
            std::snprintf(buf, sizeof(buf), "ltglide · %s · pass %llu · %.0f→%.0f Hz · %s",
                          stone, (unsigned long long)r.u.glide.passCounter,
                          r.u.glide.f0, r.u.glide.f1, clock);
        }
        return buf;
    }

    VSTGUI::CVSTGUITimer* timer_ = nullptr;
    VSTGUI::CFontRef      font_ = nullptr;
    VSTGUI::CColor        textColor_;
    std::string           text_ = "calbus: starting";
};

} // namespace Seam
```

- [ ] **Step 3: Wire the custom view**

In `plugins/strx/source/strx_processor.cpp`, add the include beside the other view headers:

```cpp
#include "strx_status.h"
```

and add a branch in `createCustomView` after the `kViewSpectrum` block (line ~137), mirroring how the neighbouring branches fetch their font and colour from the uidesc description:

```cpp
    if (name && std::string(name) == kViewStatus) {
        auto* font  = description->getFont("InfoFont");
        auto  color = VSTGUI::CColor(160, 160, 160, 255);
        if (description) description->getColor("TextColor", color);
        return new Seam::StrxStatusLine(VSTGUI::CRect(0, 0, 300, 26), font, color);
    }
```

If the neighbouring branches obtain their colours differently (they were written first), copy their exact idiom rather than this one — the point is that this branch looks like its neighbours.

- [ ] **Step 4: Add the view to the uidesc**

In `plugins/strx/resource/strx.uidesc`, inside the `editor` template, after the `StrxMeters` view (line ~49) and before the logo view (line ~54), add:

```xml
        <view class="CView" origin="20, 348" size="300, 26" custom-view-name="StrxStatus"/>
```

The editor is 900×440; the three panes occupy y 70–330 and the logo sits at x 330–570, y 345–422. This lands the status line in the free area to the left of the logo.

- [ ] **Step 5: Give the target the calbus include path**

In `plugins/strx/CMakeLists.txt`, after the `smtg_add_vst3plugin` call, add:

```cmake
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common/calbus
)
add_dependencies(${target} seam_calbus)
```

- [ ] **Step 6: Build and run the validator**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake --build build-release --target strx --config Release
./build-release/bin/Release/validator ./build-release/VST3/Release/strx.vst3
```

Expected: build succeeds; validator reports 0 failures.

- [ ] **Step 7: Verify in the host — the real cross-module proof**

This is the step no unit test can replace: the whole point of the design is that state crosses three separate `.vst3` bundles, and only a running host loads three separate bundles.

In Reaper:
1. Load four multipink instances, set their STONE parameters to 1, 2, 3, 4, and mute all four.
2. Load strx on the measurement track. Expect: `calbus: 4 idle, none sounding`.
3. Un-mute the multipink on STONE 2. Expect: `multipink · STONE 2 · slot 4-7 · -23.0 dB` (slot numbers follow the actual pool claim).
4. Mute it, un-mute STONE 3. Expect the line to name STONE 3.
5. Remove the multipinks, load ltglide with STONE = 2 and Loop on. Expect: `ltglide · STONE 2 · pass N · 20000→20 Hz · T=20s`, with N advancing once per pass.
6. Confirm the line does NOT read `no host clock`. If it does, Reaper is not supplying `kContTimeValid`, which is a finding for Spec 3 — record it in the session doc rather than working around it here.

Then confirm the degradation path:
7. Quit Reaper, `mv ~/Library/Application\ Support/SEAM/libseamcalbus.dylib /tmp/`, reopen the project. Expect: strx reads `calbus unavailable`, and multipink and ltglide still make sound normally.
8. Move the dylib back.

- [ ] **Step 8: Commit**

```bash
git add plugins/strx
git commit -m "feat(strx): calibration-bus status line

Reads the bus on a 10 Hz GUI timer and names the sounding emitter. This is
Spec 2's only user-visible feature and its own diagnostic: it is what proves
shared state actually crosses three .vst3 module boundaries."
```

---

### Task 6: Documentation

**Files:**
- Create: `plugins/_common/calbus/README.md`
- Modify: `doc/study/sessions/2026-07-14-stone-dslar.md` (close the resolved open node)

- [ ] **Step 1: Write the calbus README**

Create `plugins/_common/calbus/README.md` covering: what the bus is and why it is a dylib rather than a header (module boundaries); the C ABI surface; how to add a new emitter kind; how to install the dylib and where the client searches for it; how to run `seam_calbus_test` and `seam_calbus_client_test`; and the in-host verification recipe from Task 5 Step 7. Per the repo convention, tooling directories carry a README documenting each piece, how to run it, and how it fits.

- [ ] **Step 2: Update the session doc**

In `doc/study/sessions/2026-07-14-stone-dslar.md`, in the "Nodi aperti che il TEST può sciogliere" list, mark the EQ-location node resolved:

```markdown
- [x] Dove applicare la EQ correttiva (Spec 4) — **risolto dal metodo** (2026-07-16):
      il multipink non passa per encoder/decoder, quindi la curva che se ne ricava
      descrive STONE + finale + stanza + posizione e nient'altro → è per costruzione
      il dominio dei filtri integrati nel finale. La misura ltglide (catena completa)
      si legge *sopra* finali già tarati: le due fasi sono in cascata, non alternative.
```

- [ ] **Step 3: Commit**

```bash
git add plugins/_common/calbus/README.md doc/study/sessions/2026-07-14-stone-dslar.md
git commit -m "docs(calbus): README + close the EQ-location open node

The pink measurement bypasses the encoder/decoder chain, so its curve can
only describe amp + STONE + room: that is where the corrective EQ lives."
```

---

## Notes for the implementer

**The bus is an observer.** Every code path added here must leave the plugin's audio behaviour identical when the dylib is absent, the registry is full, or the host clock is missing. If a change makes a plugin's sound depend on the bus, the change is wrong.

**Do not fold `multipink_pool` into the bus.** The pool is an allocator (asks permission, receives a resource); the bus is a registry of announcements (declares a fact, asks nothing). Merging them would look DRY and would leave the pool unable to allocate when the dylib is missing.

**Publishing happens on the audio thread.** That is not an accident to be refactored away — parameter changes arrive in `process()`, and ltglide's pass starts are sample-accurate events inside `processBlock`. It is the reason the registry uses a seqlock instead of a mutex.
