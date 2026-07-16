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
