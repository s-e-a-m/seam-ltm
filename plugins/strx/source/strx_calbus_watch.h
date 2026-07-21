//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · strx — the GUI thread's cached view of the calibration bus.
//
// GUI THREAD ONLY. strx's audio thread must never touch the bus (calbus
// Spec 2's contract); what the DSP needs reaches it through the processor's
// atomics, which this watch writes.
//
// Two views want the bus now: the status line at ~10 Hz and the spectrum at
// ~30 Hz. Two independent snapshots would show different instants and could
// disagree about which emitter is "the" emitter. This watch snapshots at most
// once per kMinIntervalMs and hands both views the SAME digest — the same
// lesson reference_lockfree_spsc_triplebuffer records for the analysis frame,
// applied to the bus.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus_client.h"
#include "strx_calbus_digest.h"

#include <atomic>
#include <chrono>

namespace Seam { namespace strx {

class CalbusWatch {
public:
    static constexpr int kMinIntervalMs = 80;   // finer than the 10 Hz status line

    // `glideOut`, `holdEpochOut` and `sessionEpochOut` are the processor's
    // atomics, read by the audio thread once per process() block.
    CalbusWatch(std::atomic<bool>& glideOut, std::atomic<uint32_t>& holdEpochOut,
                std::atomic<uint32_t>& sessionEpochOut)
        : glide_(glideOut), holdEpoch_(holdEpochOut), sessionEpoch_(sessionEpochOut) {}

    // GUI thread. Re-snapshots at most every kMinIntervalMs; otherwise returns
    // the cached digest, so two views polling at different rates see one state.
    const CalbusDigest& poll() {
        const auto now = std::chrono::steady_clock::now();
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();
        if (primed_ && ms < kMinIntervalMs) return digest_;
        last_   = now;
        primed_ = true;

        auto& client = CalbusClient::instance();
        const bool avail = client.available();
        const int32_t n = avail ? client.snapshot(recs_, SEAM_CALBUS_MAX_SLOTS) : 0;
        digest_ = digest(recs_, n, avail);

        // Drive the DSP. setGlideMode is idempotent, so publishing every poll
        // costs nothing; the hold epoch only moves when a new pass starts.
        // The actual decision — session start vs pass boundary, with the
        // per-emitter sentinel — is holdAction(), next to digest(), pure and
        // unit-tested, so this watch keeps only the snapshot + rate-limit +
        // atomic-store duties.
        glide_.store(digest_.glide, std::memory_order_relaxed);
        switch (holdAction(digest_, lastPass_)) {
        case HoldAction::SessionStart:
            // The processor's session branch clears BOTH accumulation and
            // hold, so a session start bumps only this epoch: bumping
            // holdEpoch too would race a fold against the just-cleared hold.
            sessionEpoch_.fetch_add(1, std::memory_order_relaxed);
            break;
        case HoldAction::PassBoundary:
            holdEpoch_.fetch_add(1, std::memory_order_relaxed);
            break;
        case HoldAction::None:
            break;
        }
        return digest_;
    }

    const SeamCalbusRecord* records() const { return recs_; }

private:
    std::atomic<bool>&     glide_;
    std::atomic<uint32_t>& holdEpoch_;
    std::atomic<uint32_t>& sessionEpoch_;
    SeamCalbusRecord       recs_[SEAM_CALBUS_MAX_SLOTS] = {};
    CalbusDigest           digest_;
    uint64_t               lastPass_ = 0;
    bool                   primed_   = false;
    std::chrono::steady_clock::time_point last_{};
};

}} // namespace Seam::strx
