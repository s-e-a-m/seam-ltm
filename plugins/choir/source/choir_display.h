//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the grid's data between the threads (SDK-free)
//
// 64 independent relaxed atomics, one per band: the audio thread writes
// the block peak of each envelope divided by Q (the amplitude of the
// input's partial in that band), the GUI reads them at 30 Hz. A bar grid
// has no invariant across bands, so each value is valid on its own and no
// snapshot is needed (spec 2026-10-02). -1 marks an inactive band.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <atomic>

namespace choir {

class Display {
public:
    Display() { for (auto& v : v_) v.store(0.0f, std::memory_order_relaxed); }
    void  store(int c, int k, double v) { v_[c * 16 + k].store((float)v, std::memory_order_relaxed); }
    float load(int c, int k) const      { return v_[c * 16 + k].load(std::memory_order_relaxed); }

private:
    std::atomic<float> v_[64];
};

} // namespace choir
