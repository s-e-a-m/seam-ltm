//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — the arena broker (SDK-free)
//
// ALOGOS sizes its memory for the setting in use: each attached section
// exactly delay + 1 doubles. A setting can need from a few MiB to about
// 3 GiB (sqrt 2, D = 4, 1 m, 96 kHz), so the memory cannot be reserved once
// for the worst case, and it cannot be allocated on the audio thread either:
// new[] may take a lock, and zeroing gigabytes takes longer than a block.
//
// The broker owns a worker thread that allocates and frees for the audio
// thread. The audio thread never locks, never allocates, never frees: it
// talks to the worker through three lock-free channels.
//
//   request(n)   one atomic word, (tag << 40) | n; the latest request wins,
//                n = 0 cancels. The tag makes every request unique, so a
//                request for the same size twice is still two requests.
//   take(n)      one atomic word, the request word of the delivered arena
//                (0 = empty). The audio thread takes the arena only when the
//                word equals its latest request, by compare-and-swap to 0.
//   retire(p)    a lock-free stack threaded through the retired arenas
//                themselves (the first 8 bytes hold the next pointer), so
//                handing an arena back allocates nothing.
//
// The worker wakes every 2 ms (its own condition variable, which only the
// destructor notifies) and: frees the retired arenas; frees a delivered
// arena that a newer request has superseded; and serves the latest request,
// zeroing the arena in 64 MiB slices so every page is resident before the
// audio thread sees it, and abandoning the work as soon as the request
// changes or the broker stops.
//
// The destructor stops and joins the worker, then frees every arena still
// held: the retired stack and a delivered arena nobody took.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>

namespace alogos {

class ArenaBroker {
public:
    // byteLimit: an arena above it is refused (the failure path, testable).
    explicit ArenaBroker(std::size_t byteLimit = SIZE_MAX)
        : limit_(byteLimit / sizeof(double)), worker_([this] { run(); }) {}

    ~ArenaBroker() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_.store(true);
        }
        cv_.notify_all();
        worker_.join();
        drainRetired();
        const uint64_t s = slot_.exchange(0);
        if (s != 0) freeArena(slotPtr_.load());
    }

    ArenaBroker(const ArenaBroker&) = delete;
    ArenaBroker& operator=(const ArenaBroker&) = delete;

    // Calling thread (setActive): a zeroed arena now, or nullptr when it is
    // above the limit or the memory is not there.
    double* allocateNow(std::size_t doubles) {
        bool aborted = false;
        return allocate(doubles, 0, aborted);
    }

    // Audio thread. The latest request wins; 0 cancels.
    void request(std::size_t doubles) {
        if (doubles > kDoublesMask) doubles = kDoublesMask;    // refused by the worker
        tag_ = (tag_ + 1) & kTagMask;
        if (tag_ == 0) tag_ = 1;                                // word 0 means "nothing"
        mine_ = (tag_ << kTagShift) | (uint64_t)doubles;
        req_.store(mine_, std::memory_order_release);
    }

    // Audio thread. The zeroed arena of the latest request, or nullptr while
    // it is not ready (or failed, or was cancelled).
    double* take(std::size_t& doubles) {
        uint64_t s = slot_.load(std::memory_order_acquire);
        if (s == 0 || s != mine_) return nullptr;
        double* p = slotPtr_.load(std::memory_order_relaxed);
        const std::size_t n = slotDoubles_.load(std::memory_order_relaxed);
        if (!slot_.compare_exchange_strong(s, 0, std::memory_order_acq_rel)) return nullptr;
        doubles = n;
        return p;
    }

    // Audio thread (or any thread): the worker frees it within 2 ms.
    void retire(double* p) {
        if (!p) return;
        void* head = retired_.load(std::memory_order_relaxed);
        do { std::memcpy(p, &head, sizeof head); }
        while (!retired_.compare_exchange_weak(head, p, std::memory_order_release,
                                               std::memory_order_relaxed));
    }

    // The latest request was refused (above the limit, or no memory).
    bool failed() const {
        const uint64_t r = req_.load(std::memory_order_acquire);
        return r != 0 && failedReq_.load(std::memory_order_acquire) == r;
    }

    // Test hooks.
    long liveArenas() const { return live_.load(); }           // allocated, not yet freed
    bool delivered() const  { return slot_.load() != 0; }      // an arena waits in the slot
    static long liveArenasTotal() { return total().load(); }   // every broker of the process

private:
    static constexpr int      kTagShift    = 40;
    static constexpr uint64_t kDoublesMask = (1ull << kTagShift) - 1;
    static constexpr uint64_t kTagMask     = (1ull << (64 - kTagShift)) - 1;
    static constexpr std::size_t kSlice    = (64u << 20) / sizeof(double);   // 64 MiB

    static std::atomic<long>& total() { static std::atomic<long> t{0}; return t; }

    // req != 0: a worker allocation, abandoned (aborted = true) when the
    // request changes or the broker stops.
    double* allocate(std::size_t n, uint64_t req, bool& aborted) {
        aborted = false;
        if (n == 0 || n > limit_ || n > kDoublesMask) return nullptr;
        double* p = new (std::nothrow) double[n];
        if (!p) return nullptr;
        live_.fetch_add(1); total().fetch_add(1);
        for (std::size_t off = 0; off < n; off += kSlice) {
            if (req != 0 && (stop_.load() || req_.load(std::memory_order_acquire) != req)) {
                freeArena(p);
                aborted = true;
                return nullptr;
            }
            const std::size_t m = n - off < kSlice ? n - off : kSlice;
            std::memset(p + off, 0, m * sizeof(double));
        }
        return p;
    }

    void freeArena(double* p) {
        if (!p) return;
        delete[] p;
        live_.fetch_sub(1); total().fetch_sub(1);
    }

    void drainRetired() {
        void* h = retired_.exchange(nullptr, std::memory_order_acquire);
        while (h) {
            void* next;
            std::memcpy(&next, h, sizeof next);
            freeArena(static_cast<double*>(h));
            h = next;
        }
    }

    void run() {
        std::unique_lock<std::mutex> lk(m_);
        while (!stop_.load()) {
            lk.unlock();
            serve();
            lk.lock();
            cv_.wait_for(lk, std::chrono::milliseconds(2), [this] { return stop_.load(); });
        }
    }

    // One round of the worker.
    void serve() {
        drainRetired();
        const uint64_t req = req_.load(std::memory_order_acquire);
        // A delivered arena that a newer request (or a cancel) superseded.
        uint64_t s = slot_.load(std::memory_order_acquire);
        if (s != 0 && s != req) {
            double* p = slotPtr_.load(std::memory_order_relaxed);
            if (slot_.compare_exchange_strong(s, 0, std::memory_order_acq_rel)) freeArena(p);
        }
        if (req == served_ || slot_.load(std::memory_order_acquire) != 0) return;
        const std::size_t n = (std::size_t)(req & kDoublesMask);
        if (n == 0) { served_ = req; return; }                 // cancelled
        bool aborted = false;
        double* p = allocate(n, req, aborted);
        if (aborted) return;                                    // the next round serves the newer one
        served_ = req;
        if (!p) { failedReq_.store(req, std::memory_order_release); return; }
        slotPtr_.store(p, std::memory_order_relaxed);
        slotDoubles_.store(n, std::memory_order_relaxed);
        slot_.store(req, std::memory_order_release);
    }

    const std::size_t limit_;                       // in doubles

    // Audio thread only.
    uint64_t tag_ = 0, mine_ = 0;
    // Worker only.
    uint64_t served_ = 0;

    std::atomic<uint64_t>    req_{0}, failedReq_{0}, slot_{0};
    std::atomic<double*>     slotPtr_{nullptr};
    std::atomic<std::size_t> slotDoubles_{0};
    std::atomic<void*>       retired_{nullptr};
    std::atomic<long>        live_{0};
    std::atomic<bool>        stop_{false};

    std::mutex m_;
    std::condition_variable cv_;
    std::thread worker_;                            // last: starts after every member above
};

} // namespace alogos
