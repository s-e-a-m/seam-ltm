#pragma once
#include <cstdint>

namespace Seam {

// Static, process-global slot allocator for the multipink shared 64-channel
// logical pool. Lives once per loaded .vst3 module; all plugin instances
// within the host share it.
//
// Allocation policy: contiguous first-fit, with optional preferred-start
// hint (used to preserve calibration identity across DAW reloads).
class MultipinkPool {
public:
    static constexpr int kPoolSize = 64;

    enum class ClaimResult {
        ClaimedAtPreferred = 1,
        ClaimedFirstFit    = 2,
        Exhausted          = 3,
    };

    // Try to claim N contiguous slots. If `preferredStart` is in [0, kPoolSize-N]
    // and that range is free, claim it (returns ClaimedAtPreferred and
    // outActualStart = preferredStart). Otherwise first-fit (returns
    // ClaimedFirstFit). If no contiguous range of N slots is free, returns
    // Exhausted, sets outActualStart = -1, and the caller MUST NOT release
    // (no claim was made).
    static ClaimResult claim(int count, int preferredStart, int& outActualStart);

    // Release N slots starting at `start`. No-op if start == -1.
    static void release(int start, int count);
};

} // namespace Seam
