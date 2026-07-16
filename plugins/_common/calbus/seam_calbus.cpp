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
struct Slot {
    std::atomic<uint32_t> seq{0};     // odd = write in progress, even = stable
    std::atomic<uint32_t> inUse{0};
    SeamCalbusRecord      rec{};
};

// Bounded retries: a GUI timer must return, even against a pathological writer.
constexpr int kMaxReadAttempts = 8;

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
        if (bus->slots[i].inUse.load(std::memory_order_relaxed) == 0) {
            std::memset(&bus->slots[i].rec, 0, sizeof(SeamCalbusRecord));
            bus->slots[i].inUse.store(1, std::memory_order_release);
            return i;
        }
    }
    return -1;
}

void seam_calbus_v1_unregister(SeamCalbus* bus, int32_t handle) {
    if (!bus || handle < 0 || handle >= SEAM_CALBUS_MAX_SLOTS) return;
    std::lock_guard<std::mutex> lock(bus->regMutex);
    bus->slots[handle].inUse.store(0, std::memory_order_release);
}

void seam_calbus_v1_publish(SeamCalbus* bus, int32_t handle,
                            const SeamCalbusRecord* rec) {
    if (!bus || !rec || handle < 0 || handle >= SEAM_CALBUS_MAX_SLOTS) return;
    Slot& s = bus->slots[handle];

    const uint32_t start = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start + 1, std::memory_order_relaxed);        // -> odd
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&s.rec, rec, sizeof(SeamCalbusRecord));
    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store(start + 2, std::memory_order_relaxed);        // -> even
}

int32_t seam_calbus_v1_snapshot(SeamCalbus* bus, SeamCalbusRecord* out,
                                int32_t maxCount) {
    if (!bus || !out || maxCount <= 0) return 0;
    int32_t n = 0;
    for (int32_t i = 0; i < SEAM_CALBUS_MAX_SLOTS && n < maxCount; ++i) {
        Slot& s = bus->slots[i];
        if (s.inUse.load(std::memory_order_acquire) == 0) continue;

        SeamCalbusRecord tmp;
        for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
            const uint32_t s1 = s.seq.load(std::memory_order_acquire);
            if (s1 & 1u) continue;                            // writer inside
            std::memcpy(&tmp, &s.rec, sizeof(SeamCalbusRecord));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s.seq.load(std::memory_order_relaxed) != s1) continue;  // torn
            // Re-check: the slot may have been released while we copied.
            if (s.inUse.load(std::memory_order_acquire) != 0) out[n++] = tmp;
            break;
        }
    }
    return n;
}
