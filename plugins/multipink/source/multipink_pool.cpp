#include "multipink_pool.h"

#include <atomic>
#include <mutex>

namespace Seam {

namespace {
    std::atomic<uint64_t> g_claimed{0};
    std::mutex            g_mutex;

    inline uint64_t mask(int count, int start) {
        // count bits set, starting at bit `start`. count must be in [1,64],
        // start in [0, 64-count].
        if (count == 64) return ~uint64_t{0};
        return ((uint64_t{1} << count) - 1) << start;
    }
}

MultipinkPool::ClaimResult MultipinkPool::claim(int count, int preferredStart, int& outActualStart) {
    outActualStart = -1;
    if (count <= 0 || count > kPoolSize) return ClaimResult::Exhausted;

    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t taken = g_claimed.load(std::memory_order_relaxed);

    // Try preferred start first.
    if (preferredStart >= 0 && preferredStart + count <= kPoolSize) {
        uint64_t m = mask(count, preferredStart);
        if ((taken & m) == 0) {
            g_claimed.store(taken | m, std::memory_order_relaxed);
            outActualStart = preferredStart;
            return ClaimResult::ClaimedAtPreferred;
        }
    }

    // First-fit contiguous search.
    for (int s = 0; s + count <= kPoolSize; ++s) {
        uint64_t m = mask(count, s);
        if ((taken & m) == 0) {
            g_claimed.store(taken | m, std::memory_order_relaxed);
            outActualStart = s;
            return ClaimResult::ClaimedFirstFit;
        }
    }

    return ClaimResult::Exhausted;
}

void MultipinkPool::release(int start, int count) {
    if (start < 0 || count <= 0) return;
    if (start + count > kPoolSize) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t taken = g_claimed.load(std::memory_order_relaxed);
    g_claimed.store(taken & ~mask(count, start), std::memory_order_relaxed);
}

} // namespace Seam
