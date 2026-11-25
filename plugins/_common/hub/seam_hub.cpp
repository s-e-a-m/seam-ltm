// libseamhub — members, states, proposals and the session sheet's single
// writer (Tasks 2-5). The sheet's grammar is seam_hub_sheet.h.
//
// The slot machinery below is ported from seam_calbus.cpp with the logic
// unchanged (Slot, encodeHandle, seqlock, epoch check, kMaxReadAttempts).
// What is new: each slot also carries the member's name, instance and masks,
// kept under the registration mutex, and a line queue for the session sheet.
#include "seam_hub.h"
#include "seam_hub_sheet.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// Per-slot seqlock.
//
// WHY NOT A MUTEX: members publish their state from the audio thread. A
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
// THE SEQLOCK ALONE IS NOT ENOUGH. It serialises *state writes* against
// *state reads*, but registration churn is the normal case here (plugins are
// activated and deactivated constantly), and churn is a second, independent
// axis of change:
//
//   - Anything that touches `state` must go through the seqlock, publish_state() and
//     register()'s memset alike. A memset outside the seqlock leaves `seq`
//     even and unchanged for the whole race, so the reader's s1 == s2 check —
//     the only thing that can reject a torn read — cannot fire, and the reader
//     accepts a half-old/half-zeroed record as valid.
//
//   - `inUse` alone cannot detect ABA. A reader that checks inUse, copies,
//     and re-checks inUse sees 1 both times when the slot was released AND
//     reclaimed during the copy, and would emit the previous owner's record
//     attributed to the new owner. In THIS file that gap is already closed
//     by the point above: register()'s memset runs through the same
//     seqlock, so a release-and-reclaim during the copy necessarily moves
//     `seq`, and the `s1 != s2` torn-read check below rejects it before
//     `inUse` is ever consulted again. `gen` (below) is NOT what stops this
//     — see the struct-level comment on `gen` and members()'s reader-side
//     re-check for what `gen` is actually for.
//
// NON-ATOMIC memcpy/memset ON `state` UNDER A SEQLOCK IS FORMALLY UB IN THE
// ISO C++ MEMORY MODEL: the fences order the surrounding atomic operations,
// they do not make the plain read/write of `state` itself a data-race-free
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
    // returns to an earlier value (within the 26-bit window).
    //
    // WHAT `gen` IS ACTUALLY FOR: the stale-token path. It is packed into the
    // handle returned by register() (see encodeHandle() below), so publish_state()
    // and unregister() can reject a handle from an epoch that has since
    // closed with one relaxed load and a compare — no lock, wait-free. That
    // is the load-bearing job, tested deterministically by "a stale handle
    // cannot publish into or evict the slot's new owner".
    //
    // WHAT `gen` IS NOT: the thing that rejects ABA on the members() read
    // path. In the current code that job belongs to the seqlock's own `seq`
    // check — register()'s memset runs inside the same odd/even dance
    // publish_state() uses, so a release-and-reclaim during a reader's copy always
    // moves `seq` by 2, and the `s1 != s2` torn-read check already rejects
    // it. members() does re-capture {gen, inUse} after the copy as a second
    // check, but that is defence-in-depth for if a future change ever lets
    // something touch `state` without going through the seqlock (e.g. an
    // optimisation that skips the memset when the slot is already zero) —
    // not what makes ABA detection work today. See members()'s comment on
    // that re-check for the full argument.
    std::atomic<uint32_t> gen{0};


    // Set by publish_state after its first write, cleared by register(). It
    // is what members() reports as hasState: a published state may carry any
    // field value, so no field of the state itself can say "never published".
    std::atomic<uint32_t> published{0};

    SeamHubState          state{};

    // Written under regMutex only, read by members() under regMutex only.
    char     name[SEAM_HUB_NAME_LEN]{};
    uint32_t instance = 0;
    uint32_t says = 0, hears = 0;
};

// Bounded retries: a GUI timer must return, even against a pathological writer.
constexpr int kMaxReadAttempts = 8;

// Handle layout: gen in the high bits, slot index in the low bits.
//
// The handle is an opaque token, NOT a slot index. Packing the registration
// epoch into it is what lets publish_state() reject a stale handle for free: one
// relaxed load and a compare, no lock, no allocation, bounded time — still
// wait-free and still audio-thread-safe. Without it, a plugin that publishes
// after unregistering silently corrupts whichever plugin now owns that slot.
//
// 5 bits of index (32 slots) + 26 bits of gen = 31 bits, so a handle is always
// non-negative and -1 stays the unambiguous "no slot" value. `gen` bumps once
// on EACH transition (register AND unregister), so it wraps after 2^26
// transitions of the SAME slot, i.e. 2^25 register/unregister CYCLES; an ABA
// collision would need exactly that many cycles inside one members() call.
constexpr int32_t  kIndexBits = 5;
constexpr int32_t  kIndexMask = (1 << kIndexBits) - 1;
constexpr uint32_t kGenMask   = 0x03FFFFFFu;

static_assert(SEAM_HUB_MAX_MEMBERS <= kIndexMask + 1,
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

// ---- the line queue -------------------------------------------------------
//
// Finished sheet lines wait here for the writer (Task 5). Bounded, so a sheet
// that cannot be written (disk full, no folder) never grows the process
// without limit: when full, the OLDEST line is dropped and counted, because
// the newest lines are the ones that describe the present.
constexpr size_t kMaxQueuedLines = 10000;
// Test-only mirror: the last lines enqueued, untouched by the writer, so a
// test can see what was enqueued without racing the consumer. It keeps the
// EVENT line of each entry (a table's continuation rows stay out), and it
// does not see the writer's own `state` lines: those arrive on the writer's
// 250 ms tick, so mirroring them would put lines a test cannot predict
// between the ones it called for.
constexpr size_t kMirrorLines = 256;

// The writer's tick: how often states are compared and the queue is written.
constexpr auto kWriterTick = std::chrono::milliseconds(250);

// How long the writer lingers, sheet open, after the last member leaves.
// Hosts toggle a plugin's activation routinely: without the grace a session
// with one member would be shredded into a sheet per toggle. A register
// inside the window continues the same sheet (no new header, no new file);
// at its end the writer closes the file and its thread exits by itself.
constexpr int32_t kGraceMs = 10000;

// The most the last unregister waits for its `leaves` line to be handled by
// the writer. It runs on the host's thread: it never joins, and never waits
// on a disk longer than this.
constexpr auto kLastLeaveWait = std::chrono::milliseconds(2000);

// Lines are formatted by Seam::hubsheet; this is just "now" in unix ms.
int64_t nowMs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string maskNames(uint32_t mask) {
    static const struct { uint32_t type; const char* name; } kNames[] = {
        {kSeamHubPink, "pink"}, {kSeamHubGlide, "glide"}, {kSeamHubDecoder, "decoder"},
        {kSeamHubBandTable, "bandtable"}, {kSeamHubSections, "sections"}};
    std::string s;
    for (const auto& n : kNames)
        if (mask & SEAM_HUB_BIT(n.type)) { if (!s.empty()) s += ','; s += n.name; }
    return s.empty() ? "-" : s;
}

std::string whoOf(const char* name, uint32_t instance) {
    return Seam::hubsheet::who(name, instance);
}

const char* proposalName(uint32_t type) {
    switch (type) {
        case kSeamHubBandTable: return "band-table";
        case kSeamHubSections:  return "sections";
        default:                return "unknown";
    }
}

// The stone a member is on: its last published state's stoneId, 0 without one.
// Read through the seqlock like members() does, bounded: a GUI-side lookup
// must return, so 0 if every attempt tears.
uint32_t stoneOf(const Slot& s) {
    for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
        const uint32_t s1 = s.seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        const uint32_t pub = s.published.load(std::memory_order_acquire);
        SeamHubState tmp;
        std::memcpy(&tmp, &s.state, sizeof tmp);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != s1) continue;
        return pub ? tmp.stoneId : 0u;
    }
    return 0u;
}

} // namespace

struct SeamHub {
    Slot       slots[SEAM_HUB_MAX_MEMBERS];
    // Guards name/instance/masks of every slot and the open/close of epochs.
    // Never taken by publish_state.
    std::mutex regMutex;

    // Lock order: (storeMutex,) regMutex, then lineMutex. Separate from regMutex so the
    // writer draining the queue never holds up a registering plugin.
    std::mutex                lineMutex;
    std::deque<std::string>   lines;
    std::deque<std::string>   mirror;
    uint64_t                  droppedLines = 0;

    // ---- proposals --------------------------------------------------------
    // LOCK ORDER, whole file: storeMutex, then regMutex, then lineMutex.
    // Proposals are a GUI-thread business, so one mutex guards the store and
    // the lifecycle table; the audio thread never touches it. post() and
    // transition() need member data (masks, name, stone id), so they take
    // regMutex INSIDE storeMutex, copy what they need into locals and release
    // it before enqueueing. Nothing takes storeMutex while holding regMutex
    // or lineMutex, so there is no cycle.
    std::mutex       storeMutex;
    SeamHubProposal  store[SEAM_HUB_STORE]{};   // id N lives at (N - 1) % SEAM_HUB_STORE
    uint64_t         nextId = 1;                // monotone, never reused

    // Lifecycle per (store position, member slot). A cell only counts while
    // its `gen` equals the member slot's current epoch: a member that leaves
    // and a newcomer in the same slot have different gens, so the newcomer
    // never inherits the old lifecycle, and nothing walks the table on
    // (un)registration. `st` 0 = never transitioned.
    struct Cell { uint32_t gen; uint8_t st; };
    Cell             life[SEAM_HUB_STORE][SEAM_HUB_MAX_MEMBERS]{};

    // One queue entry is one event: its line, then its continuation rows,
    // '\n'-separated, no trailing newline. The writer writes an entry whole,
    // so an event and its rows can never be split by another event.
    //
    // Returns the entry's sequence number: the writer acknowledges entries by
    // sequence (`ackSeq`), which is what the last unregister waits on.
    uint64_t enqueue(std::string line, bool mirrored = true) {
        std::lock_guard<std::mutex> lock(lineMutex);
        if (mirrored) {
            mirror.push_back(line.substr(0, line.find('\n')));
            if (mirror.size() > kMirrorLines) mirror.pop_front();
        }
        if (lines.size() >= kMaxQueuedLines) { lines.pop_front(); ++droppedLines; }
        lines.push_back(std::move(line));
        return ++enqSeq;
    }

    // ---- the writer -------------------------------------------------------
    // At most ONE writer thread, possibly lingering. A session (one sheet)
    // runs from the first register until kGraceMs after the last unregister
    // with nobody back; the writer then closes the sheet and exits by itself.
    //
    // ctlMutex guards the writer's control state below. The writer flips
    // `running` to false as the very last thing it does, after closing the
    // file, and only if nobody arrived meanwhile (re-checked under ctlMutex).
    // So a register that finds `running` true never starts a second writer,
    // and one that finds it false joins a thread that has already finished
    // its disk work (the join only waits for the thread's return), then
    // starts the next. Neither register nor unregister waits on the disk
    // without a bound: the last unregister waits for the writer's
    // acknowledgement of its `leaves` line at most kLastLeaveWait.
    //
    // Lock order: lifeMutex, storeMutex, regMutex, lineMutex. lifeMutex only
    // guards the std::thread object (register and teardown()); ctlMutex and
    // statusMutex are leaves, never held while taking another.
    std::mutex   lifeMutex;
    std::thread  writer;                    // guarded by lifeMutex

    std::mutex              ctlMutex;
    std::condition_variable wake;           // the writer waits on it, and so does the last unregister
    bool     running = false;               // a writer thread exists and has not exited
    bool     stopReq = false;               // teardown() only
    bool     renewReq = false;              // sheet_new()
    bool     flushReq = false;              // the last unregister: a pass now
    int32_t  liveMembers = 0;
    std::chrono::steady_clock::time_point lingerUntil{};
    uint64_t ackSeq = 0;                    // every entry <= this is written, or held
    std::atomic<int32_t> graceMs{kGraceMs}; // a test hook can shorten it
    std::atomic<int32_t> stallMs{0};        // test hook: the writer stalls before writing

    // What sheet_status() reports, published by the writer after each change.
    std::mutex statusMutex;
    char       statusPath[512]{};
    uint32_t   statusWriting = 0;

    // Under lineMutex: entries the writer has swapped out and not yet written
    // (so pendingLines does not dip while a batch is on its way to the disk),
    // and the sequence number of the last entry enqueued.
    size_t   inFlight = 0;
    uint64_t enqSeq = 0;

    // Writer-thread only. They outlive one writer (held lines wait for the
    // next session), and successive writers are ordered by the join.
    bool     unreachable = false;
    uint64_t droppedAtOutage = 0;

    // The host quits (or the dylib is unloaded): ask the writer for a last
    // pass, which drains into a sheet that is already open and never opens
    // one (writerMain), and wait for it to exit at most kLastLeaveWait. A
    // writer that has exited is joined; one still stuck in the disk (a hung
    // network share) is detached and left to the process exit. That is safe
    // only because the hub is never destroyed (see g_hub below): the detached
    // thread can never touch freed memory. Returns 1 when the writer exited
    // (or there was none), 0 when it was detached.
    int teardown() {
        std::unique_lock<std::mutex> lk(ctlMutex);
        stopReq = true;
        wake.notify_all();
        const bool exited = wake.wait_for(lk, kLastLeaveWait, [this] { return !running; });
        lk.unlock();
        std::lock_guard<std::mutex> life(lifeMutex);
        if (writer.joinable()) {
            if (exited) writer.join();     // returned, or returning: no disk left
            else        writer.detach();
        }
        return exited ? 1 : 0;
    }
};

// The one copy in the process. Static here is correct precisely because this
// translation unit is compiled into exactly one dylib.
//
// IMMORTAL ON PURPOSE: allocated once and never destroyed. A writer stuck in
// the disk at quit is detached (SeamHub::teardown), and it may wake up while
// the process is being torn down; with a destroyed hub it would lock freed
// mutexes and write into freed queues. Never destroying it costs one object
// the process exit reclaims anyway.
static SeamHub& g_hub = *new SeamHub;

// The bounded stop runs from the dylib's own teardown. A destructor
// function rather than an atexit handler: it runs both at process exit and
// if the dylib is ever unloaded (an atexit handler would have to be
// registered from register(), on a host's thread, and would still run after
// an unload), and it needs no registration that a failed first start could
// skip. It never waits more than kLastLeaveWait.
__attribute__((destructor)) static void seamHubTeardown() { g_hub.teardown(); }

namespace { void writerMain(SeamHub* hub); }

extern "C" {

uint32_t seam_hub_v1_version(void) { return SEAM_HUB_VERSION; }
SeamHub* seam_hub_v1_get(void) { return &g_hub; }

namespace {

int32_t registerLocked(SeamHub* hub, const char* name, uint32_t says, uint32_t hears) {
    std::lock_guard<std::mutex> lock(hub->regMutex);
    for (int32_t i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) {
        Slot& s = hub->slots[i];
        if (s.inUse.load(std::memory_order_relaxed) != 0) continue;

        // Clear the previous owner's state — a fresh slot must never expose
        // it — through the SAME seqlock publish_state() uses. members() takes
        // regMutex once per slot, only around the registration data, and
        // releases it BEFORE the state copy (holding it there would let the
        // GUI stall on a registering plugin), so regMutex does nothing to
        // protect this memset from a concurrent state copy. Only the odd/even
        // dance does.
        //
        // `start | 1u` instead of `start + 1`: if this memset and an
        // in-flight publish_state() on the same slot ever genuinely overlap
        // (the VST3 contract says they must not, in intended use), a plain
        // `+1`/`+2` dance can leave `seq` PERMANENTLY ODD: two interleaved
        // writers both compute their final store as start+2 from their own
        // snapshot of `start`, and parity is preserved by construction, so an
        // odd intermediate value can become the last write. Once `seq` is
        // stuck odd the reader rejects the slot on every attempt and no
        // unregister+register heals it. Forcing the intermediate value odd
        // via `| 1u` and the final value to exactly one more makes every
        // writer's OWN final store even, so the slot can never brick; it
        // only recovers to "one writer's data, chosen arbitrarily". This does
        // NOT make concurrent writers safe, it only removes the unrecoverable
        // outcome.
        const uint32_t start = s.seq.load(std::memory_order_relaxed);
        s.seq.store(start | 1u, std::memory_order_relaxed);        // -> odd
        std::atomic_thread_fence(std::memory_order_release);
        std::memset(&s.state, 0, sizeof(SeamHubState));
        s.published.store(0, std::memory_order_relaxed);   // inside the window, with the state
        std::atomic_thread_fence(std::memory_order_release);
        s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even

        // Name, masks, instance: registration data, under regMutex.
        std::memset(s.name, 0, sizeof s.name);
        if (name) std::strncpy(s.name, name, SEAM_HUB_NAME_LEN - 1);
        s.says = says;
        s.hears = hears;
        // Instances are numbered per name, smallest free from 1, so a name
        // that left and came back is #1 again rather than an ever-growing
        // counter; the sheet's `who` then names the same role across sessions.
        uint32_t inst = 1;
        for (bool clash = true; clash;) {
            clash = false;
            for (int32_t j = 0; j < SEAM_HUB_MAX_MEMBERS; ++j) {
                const Slot& o = hub->slots[j];
                if (j != i && o.inUse.load(std::memory_order_relaxed) &&
                    o.instance == inst && std::strncmp(o.name, s.name, SEAM_HUB_NAME_LEN) == 0) {
                    ++inst; clash = true; break;
                }
            }
        }
        s.instance = inst;

        // Open the new epoch, then advertise the slot. Both are release
        // stores, so a reader that observes them also observes the cleared
        // state and the seq bumps that precede them.
        const uint32_t g = (s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask;
        s.gen.store(g, std::memory_order_release);
        s.inUse.store(1, std::memory_order_release);

        hub->enqueue(Seam::hubsheet::event(nowMs(), whoOf(s.name, inst), "arrives",
                     "says " + maskNames(says) + "  hears " + maskNames(hears)));
        return encodeHandle(i, g);
    }
    return SEAM_HUB_NO_HANDLE;
}

// The sequence number of the `leaves` line, 0 when the handle was stale.
uint64_t unregisterLocked(SeamHub* hub, int32_t handle) {
    Slot& s = hub->slots[handleIndex(handle)];
    std::lock_guard<std::mutex> lock(hub->regMutex);

    // A stale handle must not evict the slot's current owner. Checked under
    // regMutex, so gen cannot move under us.
    if (s.gen.load(std::memory_order_relaxed) != handleGen(handle)) return 0;
    if (s.inUse.load(std::memory_order_relaxed) == 0) return 0;

    const uint64_t seq =
        hub->enqueue(Seam::hubsheet::event(nowMs(), whoOf(s.name, s.instance), "leaves", ""));

    // Close the epoch before retiring the slot: a reader that captured the old
    // gen sees it change and rejects whatever it copied.
    s.gen.store((s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask,
                std::memory_order_release);
    s.inUse.store(0, std::memory_order_release);
    return seq;
}

} // namespace

int32_t seam_hub_v1_register(SeamHub* hub, const char* name, uint32_t says, uint32_t hears) {
    if (!hub) return SEAM_HUB_NO_HANDLE;
    const int32_t h = registerLocked(hub, name, says, hears);
    if (h == SEAM_HUB_NO_HANDLE) return h;
    bool start = false;
    {
        std::lock_guard<std::mutex> lk(hub->ctlMutex);
        ++hub->liveMembers;
        // A lingering or closing writer sees liveMembers > 0 and carries on
        // (the same sheet while it lingers, a new one if it had closed it).
        if (!hub->running) { hub->running = true; start = true; }
    }
    if (start) {
        std::lock_guard<std::mutex> life(hub->lifeMutex);
        // `running` was false: the previous thread has done its last disk
        // access and is returning, so this join waits for no disk.
        if (hub->writer.joinable()) hub->writer.join();
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            hub->stopReq = false;
            hub->renewReq = false;   // a sheet_new() between sessions: this one is new anyway
        }
        // No exception may cross the C ABI. If the thread cannot be made,
        // lines stay queued (bounded) and the next register tries again.
        try {
            hub->writer = std::thread(writerMain, hub);
        } catch (...) {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            hub->running = false;
        }
    }
    return h;
}

void seam_hub_v1_unregister(SeamHub* hub, int32_t handle) {
    if (!hub || handle < 0) return;
    const uint64_t seq = unregisterLocked(hub, handle);
    if (seq == 0) return;
    std::unique_lock<std::mutex> lk(hub->ctlMutex);
    if (--hub->liveMembers > 0) return;
    // The last one: start the grace, ask for a pass now, and wait (bounded)
    // until the writer has handled the `leaves` line. Never a join.
    hub->lingerUntil = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(hub->graceMs.load(std::memory_order_relaxed));
    hub->flushReq = true;
    hub->wake.notify_all();
    hub->wake.wait_for(lk, kLastLeaveWait, [hub, seq] { return hub->ackSeq >= seq || !hub->running; });
}

void seam_hub_v1_publish_state(SeamHub* hub, int32_t handle, const SeamHubState* st) {
    if (!hub || !st || handle < 0) return;
    Slot& s = hub->slots[handleIndex(handle)];

    // Stale-handle guard: one relaxed load, no lock — publish_state() stays
    // wait-free. gen only ever advances, so a handle whose epoch is closed can
    // never match again and its writes are dropped. (This does not make
    // publish_state() and register() mutually exclusive — a publish already
    // past this check when the epoch closes still writes. That window is a few
    // instructions, and a member stops publishing before it unregisters; the
    // point is that a *stale* handle stops writing forever.)
    //
    // Parity invariant: `gen` bumps exactly once per successful register() and
    // once per unregister(), strictly alternating under regMutex, so gen even
    // <=> slot free, gen odd <=> slot live. A live slot's gen is never 0, and
    // the 26-bit wrap point (2^26, even) preserves parity. `(g & 1u) == 0`
    // rejects the pre-registration sentinel 0 and a forged handle carrying an
    // even gen alike, in one instruction.
    const uint32_t g = s.gen.load(std::memory_order_relaxed);
    if ((g & 1u) == 0 || g != handleGen(handle)) return;

    // `start | 1u` dance: see register() for why.
    const uint32_t start = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start | 1u, std::memory_order_relaxed);        // -> odd
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(&s.state, st, sizeof(SeamHubState));
    // Inside the window, so the reader's seq check validates the flag with the
    // state: stored after the final seq, a first-publish reader could see the
    // state yet hasState 0.
    s.published.store(1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even
}

namespace {

// One member's registration data and state, consistent with each other, and
// the epoch they belong to; false when the slot is free or every attempt
// tore. Shared by members() and the writer's state snapshot.
bool readMember(SeamHub* hub, int32_t i, SeamHubMember& out, uint32_t& epoch) {
    Slot& s = hub->slots[i];

    // Registration data and the epoch it belongs to, together, under the
    // lock: name/instance/masks are only ever written under regMutex, so
    // this copy is consistent, and `g` names the epoch it came from. The
    // state copy below then has to see the same epoch.
    SeamHubMember m;
    std::memset(&m, 0, sizeof m);
    uint32_t g1;
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        if (s.inUse.load(std::memory_order_relaxed) == 0) return false;   // free
        g1 = s.gen.load(std::memory_order_relaxed);
        m.member = (uint32_t)i;
        m.instance = s.instance;
        m.says = s.says;
        m.hears = s.hears;
        std::memcpy(m.name, s.name, SEAM_HUB_NAME_LEN);
    }

    SeamHubState tmp;
    uint32_t pub = 0;
    bool ok = false;
    for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
        // Acquire, so the state loads below cannot be hoisted above it.
        if (s.gen.load(std::memory_order_acquire) != g1) break;       // churned already
        const uint32_t s1 = s.seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;                            // writer inside

        pub = s.published.load(std::memory_order_acquire);
        std::memcpy(&tmp, &s.state, sizeof(SeamHubState));

        // Acquire fence: the state loads above may not be reordered after
        // the validation loads below, so what follows really is a check on
        // a completed copy.
        std::atomic_thread_fence(std::memory_order_acquire);

        if (s.seq.load(std::memory_order_relaxed) != s1) continue;  // torn

        // Re-capture the epoch. Unchanged {gen, inUse} means one single
        // uninterrupted registration epoch spanned the whole copy: the
        // slot was neither released (inUse would drop, gen would move) nor
        // released-and-reclaimed (gen would move twice and never returns
        // to an earlier value).
        //
        // HONEST NOTE: register()'s memset runs inside the seqlock, so
        // every ownership transition already moves `seq` by 2 and the
        // `s1 != s2` check above rejects every ABA'd copy on its own. This
        // gen/inUse re-check is DEFENCE-IN-DEPTH for the day something
        // touches the state without going through the seqlock (e.g. "skip
        // the memset when the slot is already zero"). Keep it for that,
        // but don't credit it with ABA detection in the current code.
        const uint32_t u2 = s.inUse.load(std::memory_order_acquire);
        const uint32_t g2 = s.gen.load(std::memory_order_acquire);
        if (u2 == 0 || g2 != g1) continue;                // churned
        ok = true;
        break;
    }
    if (!ok) return false;
    m.hasState = pub ? 1u : 0u;
    m.state = tmp;
    out = m;
    epoch = g1;
    return true;
}

} // namespace

int32_t seam_hub_v1_members(SeamHub* hub, SeamHubMember* out, int32_t max) {
    if (!hub || !out || max <= 0) return 0;
    int32_t n = 0;
    uint32_t epoch = 0;
    for (int32_t i = 0; i < SEAM_HUB_MAX_MEMBERS && n < max; ++i)
        if (readMember(hub, i, out[n], epoch)) ++n;
    return n;
}

// Test-only: the last 256 lines enqueued, newline-separated, NUL-terminated
// within `cap`. Returns the number of lines written. Declared in the tests,
// never in seam_hub.h: it is not part of the ABI.
SEAM_HUB_API int32_t seam_hub_test_recent_lines(char* out, int32_t cap) {
    if (!out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_hub.lineMutex);
    // Keep the NEWEST lines: start from the first line from which the rest
    // fits (each line costs its size plus '\n'; one byte stays for the NUL).
    size_t first = g_hub.mirror.size(), used = 1;
    while (first > 0 && used + g_hub.mirror[first - 1].size() + 1 <= (size_t)cap) {
        used += g_hub.mirror[first - 1].size() + 1;
        --first;
    }
    size_t pos = 0;
    int32_t count = 0;
    for (size_t k = first; k < g_hub.mirror.size(); ++k) {
        const std::string& l = g_hub.mirror[k];
        std::memcpy(out + pos, l.data(), l.size());
        pos += l.size();
        out[pos++] = '\n';
        ++count;
    }
    out[pos] = '\0';
    return count;
}

// ---- proposals ------------------------------------------------------------
namespace {

// A live slot for this handle, checked under regMutex (the caller holds it).
bool liveSlot(SeamHub* hub, int32_t handle, Slot*& out) {
    if (handle < 0) return false;
    Slot& s = hub->slots[handleIndex(handle)];
    if (s.inUse.load(std::memory_order_relaxed) == 0 ||
        s.gen.load(std::memory_order_relaxed) != handleGen(handle)) return false;
    out = &s;
    return true;
}

// Does a receiver with these hears/stone hear this proposal?
bool heard(uint32_t hears, uint32_t recvStone, const SeamHubProposal& p) {
    if (p.type >= 32 || !(hears & SEAM_HUB_BIT(p.type))) return false;
    return p.stoneId == 0 || recvStone == 0 || p.stoneId == recvStone;
}

// The stored record for an id, or null when unknown or evicted. storeMutex held.
SeamHubProposal* find(SeamHub* hub, uint64_t id) {
    if (id == 0 || id >= hub->nextId) return nullptr;
    SeamHubProposal& p = hub->store[(id - 1) % SEAM_HUB_STORE];
    return p.id == id ? &p : nullptr;
}

} // namespace

uint64_t seam_hub_v1_post(SeamHub* hub, int32_t handle, SeamHubProposal* inout) {
    if (!hub || !inout || inout->type >= 32) return 0;
    std::lock_guard<std::mutex> store(hub->storeMutex);

    std::string who;
    uint32_t member = 0, instance = 0;
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        Slot* s = nullptr;
        if (!liveSlot(hub, handle, s)) return 0;
        if (!(s->says & SEAM_HUB_BIT(inout->type))) return 0;
        member = (uint32_t)handleIndex(handle);
        instance = s->instance;
        who = whoOf(s->name, s->instance);
    }

    inout->id = hub->nextId++;
    inout->authorMember = member;
    inout->authorInstance = instance;
    inout->postedUnixMs = nowMs();

    // The position of the oldest proposal is reused: that IS the eviction. Its
    // lifecycle row goes with it, so the newcomer starts at "arrived" for all.
    const size_t pos = (inout->id - 1) % SEAM_HUB_STORE;
    hub->store[pos] = *inout;
    std::memset(hub->life[pos], 0, sizeof hub->life[pos]);

    // Tables and sections in full (spec, The sheet): the object, then one
    // continuation row per band or section, as one queue entry.
    std::string object, rows;
    if (inout->type == kSeamHubBandTable) {
        object = Seam::hubsheet::tableObject(inout->id, inout->stoneId, inout->u.table);
        rows = Seam::hubsheet::rows(inout->u.table);
    } else if (inout->type == kSeamHubSections) {
        object = Seam::hubsheet::sectionsObject(inout->id, inout->u.sections);
        rows = Seam::hubsheet::rows(inout->u.sections);
    } else {
        object = std::string(proposalName(inout->type)) + " #" + std::to_string(inout->id) +
                 "  stone " + std::to_string(inout->stoneId);
    }
    std::string entry = Seam::hubsheet::event(inout->postedUnixMs, who, "says", object);
    if (!rows.empty()) { rows.pop_back(); entry += "\n" + rows; }   // drop the last '\n'
    hub->enqueue(std::move(entry));
    return inout->id;
}

int32_t seam_hub_v1_proposals(SeamHub* hub, uint64_t afterId, SeamHubProposal* out, int32_t max) {
    if (!hub || !out || max <= 0) return 0;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    // Ids are contiguous: the store holds [nextId - SEAM_HUB_STORE, nextId).
    uint64_t from = hub->nextId > SEAM_HUB_STORE ? hub->nextId - SEAM_HUB_STORE : 1;
    if (afterId + 1 > from) from = afterId + 1;
    int32_t n = 0;
    for (uint64_t id = from; id < hub->nextId && n < max; ++id)
        out[n++] = hub->store[(id - 1) % SEAM_HUB_STORE];
    return n;
}

int32_t seam_hub_v1_lifecycle(SeamHub* hub, int32_t receiver, uint64_t id) {
    if (!hub) return kSeamHubNone;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    const SeamHubProposal* p = find(hub, id);
    if (!p) return kSeamHubNone;
    uint32_t hears = 0, gen = 0, stone = 0;
    int32_t idx = 0;
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        Slot* s = nullptr;
        if (!liveSlot(hub, receiver, s)) return kSeamHubNone;
        idx = handleIndex(receiver);
        hears = s->hears;
        gen = s->gen.load(std::memory_order_relaxed);
        stone = stoneOf(*s);
    }
    if (!heard(hears, stone, *p)) return kSeamHubNone;
    const auto& c = hub->life[(id - 1) % SEAM_HUB_STORE][idx];
    return (c.gen == gen && c.st != 0) ? c.st : kSeamHubArrived;
}

int32_t seam_hub_v1_transition(SeamHub* hub, int32_t receiver, uint64_t id, int32_t to, int32_t byOperator) {
    if (!hub) return -1;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    const SeamHubProposal* p = find(hub, id);
    if (!p) return -1;
    uint32_t hears = 0, gen = 0, stone = 0;
    int32_t idx = 0;
    std::string who;
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        Slot* s = nullptr;
        if (!liveSlot(hub, receiver, s)) return -2;     // no such receiver: it hears nothing
        idx = handleIndex(receiver);
        hears = s->hears;
        gen = s->gen.load(std::memory_order_relaxed);
        stone = stoneOf(*s);
        who = whoOf(s->name, s->instance);
    }
    if (!heard(hears, stone, *p)) return -2;

    const bool decision = (to == kSeamHubApplied || to == kSeamHubIgnored);
    // Checked before legality: applying without the operator is refused
    // whatever the state (no member acts on what it hears by itself).
    if (decision && !byOperator) return -4;

    auto& c = hub->life[(id - 1) % SEAM_HUB_STORE][idx];
    const int32_t from = (c.gen == gen && c.st != 0) ? c.st : kSeamHubArrived;
    const bool legal = (from == kSeamHubArrived) ? (to == kSeamHubShown)
                                                 : decision;   // shown/applied/ignored -> applied|ignored
    if (!legal) return -3;
    c.gen = gen;
    c.st = (uint8_t)to;

    const std::string obj = std::string(proposalName(p->type)) + " #" + std::to_string(p->id);
    if (to == kSeamHubShown)
        hub->enqueue(Seam::hubsheet::event(nowMs(), who, "shows", obj));
    else
        hub->enqueue(Seam::hubsheet::event(nowMs(), "operator",
                     to == kSeamHubApplied ? "applies" : "ignores", obj + "  on " + who));
    return 0;
}

// ---- the session sheet ----------------------------------------------------
//
// Nothing below runs on the audio thread, and the disk is touched only by the
// writer thread (the sheet) and by sheet_set_folder() on its caller's thread
// (the conf). No hub mutex is ever held across a disk access.

void seam_hub_v1_sheet_status(SeamHub* hub, SeamHubSheetStatus* out) {
    if (!hub || !out) return;
    std::memset(out, 0, sizeof *out);
    {
        std::lock_guard<std::mutex> lock(hub->statusMutex);
        std::memcpy(out->path, hub->statusPath, sizeof out->path);
        out->writing = hub->statusWriting;
    }
    std::lock_guard<std::mutex> lock(hub->lineMutex);
    out->pendingLines = (uint32_t)(hub->lines.size() + hub->inFlight);
    out->droppedLines = hub->droppedLines;
}

} // extern "C"

namespace {

// "" when HOME is unset: then there is no default folder (and no default
// conf), and the sheet is treated as unreachable rather than written
// somewhere relative to the host's working directory.
std::string homeDir() {
    const char* h = std::getenv("HOME");
    return (h && *h == '/') ? h : "";
}

// SEAM_HUB_CONF wins, so tests and odd setups never touch the operator's conf.
std::string confPath() {
    if (const char* c = std::getenv("SEAM_HUB_CONF")) if (*c) return c;
    const std::string home = homeDir();
    return home.empty() ? "" : home + "/Library/Application Support/SEAM/hub.conf";
}

std::string trim(const std::string& v) {
    const char* ws = " \t\r\n";
    const size_t a = v.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    return v.substr(a, v.find_last_not_of(ws) - a + 1);
}

// "~" and "~/..." against HOME; "" when HOME is needed and unset.
std::string expandHome(const std::string& p) {
    if (p == "~" || p.rfind("~/", 0) == 0) {
        const std::string home = homeDir();
        return home.empty() ? "" : home + p.substr(1);
    }
    return p;
}

// The folder of the next sheet: `sheet-folder = <path>` in the conf (key and
// value trimmed, spaces around '=' accepted), else the default. Read at
// every sheet start, so a changed conf takes effect on the next sheet, as
// sheet_set_folder() promises. "" means no usable folder: unreachable.
std::string sheetFolder() {
    std::string folder;
    const std::string conf = confPath();
    if (!conf.empty()) {
        std::ifstream f(conf);
        for (std::string l; std::getline(f, l);) {
            const size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            if (trim(l.substr(0, eq)) == "sheet-folder") folder = trim(l.substr(eq + 1));
        }
    }
    if (folder.empty()) {
        const std::string home = homeDir();
        return home.empty() ? "" : home + "/Documents/SEAM/sheets";
    }
    return expandHome(folder);
}

// Serialises sheet_set_folder() callers; the writer reads the conf without
// it, which is safe because the conf is replaced by rename().
std::mutex g_confMutex;

// Does the state differ in a field that means something? Field by field, by
// kind: a memcmp of the whole record would also compare the bytes of the
// union's inactive members and any padding, which a publisher need not zero.
// Equal doubles, with NaN equal to NaN: a member that publishes a NaN level
// must not write a state line every tick.
bool same(double x, double y) { return x == y || (x != x && y != y); }

bool sameState(const SeamHubState& a, const SeamHubState& b) {
    if (a.kind != b.kind || a.stoneId != b.stoneId || a.active != b.active ||
        !same(a.levelDb, b.levelDb) || !same(a.sampleRate, b.sampleRate)) return false;
    switch (a.kind) {
        case kSeamHubPink:
            return a.u.pink.slotStart == b.u.pink.slotStart && a.u.pink.slotCount == b.u.pink.slotCount;
        case kSeamHubGlide: {
            const auto &x = a.u.glide, &y = b.u.glide;
            return x.passCounter == y.passCounter && x.passStartSample == y.passStartSample &&
                   same(x.f0, y.f0) && same(x.f1, y.f1) && same(x.durationSec, y.durationSec) &&
                   same(x.deltaSec, y.deltaSec) && x.sweepMode == y.sweepMode && x.diracMode == y.diracMode;
        }
        case kSeamHubDecoder: {
            const auto &x = a.u.decoder, &y = b.u.decoder;
            return x.test == y.test && x.bypass == y.bypass && x.stoneOfModel == y.stoneOfModel &&
                   same(x.cutHz, y.cutHz) && x.commonId == y.commonId && x.differenceId == y.differenceId &&
                   x.pieceOn == y.pieceOn && x.subOn == y.subOn && same(x.subHz, y.subHz);
        }
        default:
            return true;   // an unknown kind: only the common fields are known
    }
}

// The writer thread's own state for one sheet. Only the writer touches it.
struct Sheet {
    SeamHub*    hub;
    std::string path;
    std::time_t start = 0;
    std::FILE*  f = nullptr;
    struct Last { uint32_t epoch; bool written; SeamHubState st; };
    Last        last[SEAM_HUB_MAX_MEMBERS]{};
    // Set after a failed write: the next open() starts a new file first. Not
    // begin() on the spot, because begin() touches the disk (conf, exists)
    // and the failure may happen on the last pass under stop.
    bool        needBegin = false;

    void publishStatus() {
        std::lock_guard<std::mutex> lock(hub->statusMutex);
        std::snprintf(hub->statusPath, sizeof hub->statusPath, "%s", path.c_str());
        hub->statusWriting = f ? 1u : 0u;
    }

    // A new sheet: new start time, the conf re-read, states written afresh
    // so the new file stands on its own. A name already taken (two sheets of
    // this process in the same second and folder) gets -2, -3, ... after the
    // pid, so a sheet is never appended to another.
    void begin() {
        close();
        needBegin = false;
        start = std::time(nullptr);
        std::tm tm{};
        localtime_r(&start, &tm);
        char stem[96];
        std::snprintf(stem, sizeof stem, "%04d-%02d-%02d-%02d%02d%02d-%d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
        const std::string folder = sheetFolder();
        path.clear();
        if (!folder.empty()) {
            std::error_code ec;
            for (int n = 1; n < 1000; ++n) {
                const std::string name = std::string(stem) + (n > 1 ? "-" + std::to_string(n) : "") +
                                         "-seam-sheet.txt";
                path = (std::filesystem::path(folder) / name).string();
                if (!std::filesystem::exists(path, ec)) break;
            }
        }
        std::memset(last, 0, sizeof last);
        publishStatus();
    }

    void close() {
        if (f) { std::fclose(f); f = nullptr; publishStatus(); }
    }

    void markUnreachable(uint64_t droppedNow) {
        if (!hub->unreachable) { hub->unreachable = true; hub->droppedAtOutage = droppedNow; }
    }

    // Opens the sheet when it is not open. On failure the queue is left
    // alone: its lines are the held lines, bounded by enqueue()'s drop-oldest.
    void open() {
        if (f) return;
        if (needBegin) begin();
        uint64_t droppedNow;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            droppedNow = hub->droppedLines;
        }
        if (path.empty()) { markUnreachable(droppedNow); return; }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        f = std::fopen(path.c_str(), "a");
        if (!f) { markUnreachable(droppedNow); return; }
        std::tm tm{};
        localtime_r(&start, &tm);
        bool ok = std::fputs(Seam::hubsheet::header(tm, (int)getpid()).c_str(), f) >= 0;
        if (ok && hub->unreachable) {
            size_t held;
            uint64_t dropped;
            {
                std::lock_guard<std::mutex> lock(hub->lineMutex);
                held = hub->lines.size();
                dropped = hub->droppedLines - hub->droppedAtOutage;
            }
            std::string what = "folder unreachable, " + std::to_string(held) + " lines held";
            if (dropped) what += ", " + std::to_string(dropped) + " dropped";
            ok = std::fputs((Seam::hubsheet::event(nowMs(), "hub", "sheet", what) + "\n").c_str(), f) >= 0;
        }
        ok = (std::fflush(f) == 0) && ok;
        if (!ok) {
            // The header or the notice did not make it: this file may end in
            // a torn fragment, so it is abandoned and the next attempt starts
            // a new one. `unreachable` stays set: the notice is still owed.
            markUnreachable(droppedNow);
            close();
            needBegin = true;
            return;
        }
        hub->unreachable = false;
        publishStatus();
    }

    // A `state` line for each member whose state differs from the last one
    // written for its epoch. Read through readMember(), the same seqlock read
    // as members(); enqueued like any line, so order stays order of arrival.
    void snapshotStates() {
        for (int32_t i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) {
            SeamHubMember m;
            uint32_t epoch = 0;
            if (!readMember(hub, i, m, epoch) || !m.hasState) continue;
            Last& l = last[i];
            if (l.written && l.epoch == epoch && sameState(l.st, m.state)) continue;
            l = Last{epoch, true, m.state};
            std::string entry = Seam::hubsheet::event(nowMs(), whoOf(m.name, m.instance), "state",
                                                      Seam::hubsheet::stateObject(m.state));
            std::string rows = Seam::hubsheet::rows(m.state);
            if (!rows.empty()) { rows.pop_back(); entry += "\n" + rows; }
            hub->enqueue(std::move(entry), /*mirrored=*/false);
        }
    }

    // Swap the queue out under lineMutex, write with it released: a slow or
    // full disk must never stall a GUI caller of members() through the
    // regMutex -> lineMutex chain. Returns the sequence number up to which
    // every entry is now either written or held (in the queue, waiting for
    // the folder): what the writer acknowledges.
    uint64_t drain() {
        if (!f) {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            return hub->enqSeq;
        }
        std::deque<std::string> batch;
        uint64_t upTo;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            batch.swap(hub->lines);
            hub->inFlight = batch.size();
            upTo = hub->enqSeq;
        }
        // Test hook: a writer stuck on the disk, without holding any lock.
        while (hub->stallMs.load(std::memory_order_relaxed) > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            hub->stallMs.fetch_sub(5, std::memory_order_relaxed);
        }
        if (batch.empty()) {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->inFlight = 0;
            return upTo;
        }
        size_t done = 0;
        for (; done < batch.size(); ++done) {
            const std::string& e = batch[done];
            if (std::fwrite(e.data(), 1, e.size(), f) != e.size() || std::fputc('\n', f) == EOF) break;
        }
        const bool failed = done < batch.size() || std::fflush(f) != 0;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->inFlight = 0;
            if (failed) {
                // The outage starts before the put-back, so the drops the
                // put-back itself causes are counted in the notice.
                markUnreachable(hub->droppedLines);
                // Put back what was not written, ahead of what arrived since,
                // within the same bound and with the same drop-oldest count.
                for (size_t k = batch.size(); k-- > done;) hub->lines.push_front(std::move(batch[k]));
                while (hub->lines.size() > kMaxQueuedLines) { hub->lines.pop_front(); ++hub->droppedLines; }
            }
        }
        // A failed write may have left a torn fragment at the end of the
        // file: never glue the next header onto it, start a new file.
        if (failed) { close(); needBegin = true; }
        return upTo;
    }
};

void writerMain(SeamHub* hub) {
    Sheet sheet{hub};
    sheet.begin();
    for (bool first = true;; first = false) {
        bool stop, renew, members;
        {
            std::unique_lock<std::mutex> lk(hub->ctlMutex);
            if (!first) {
                auto until = std::chrono::steady_clock::now() + kWriterTick;
                if (hub->liveMembers == 0 && hub->lingerUntil < until) until = hub->lingerUntil;
                hub->wake.wait_until(lk, until, [hub] { return hub->stopReq || hub->renewReq || hub->flushReq; });
            }
            stop = hub->stopReq;
            members = hub->liveMembers > 0;
            const bool closing = !members && std::chrono::steady_clock::now() >= hub->lingerUntil;
            // A renew while a stop or the end of the grace is pending would
            // open a sheet only to close it at once: ignored.
            renew = hub->renewReq && !stop && !closing;
            hub->renewReq = false;
            hub->flushReq = false;
        }
        if (renew) sheet.begin();
        if (members && !stop) sheet.snapshotStates();
        // Under stop no sheet is opened (no create_directories, no fopen on a
        // share that may hang the quit): an open one is drained and closed,
        // otherwise the held lines stay in memory.
        if (!stop) sheet.open();
        const uint64_t acked = sheet.drain();
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            if (acked > hub->ackSeq) hub->ackSeq = acked;
        }
        hub->wake.notify_all();

        bool closeNow;
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            closeNow = hub->stopReq ||
                       (hub->liveMembers == 0 && !hub->flushReq &&
                        std::chrono::steady_clock::now() >= hub->lingerUntil);
        }
        if (!closeNow) continue;
        sheet.close();
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            // Re-checked: a register (or a register and its unregister) that
            // arrived while the file was closing keeps this writer alive, on
            // a new sheet. Otherwise this is the writer's last act.
            const bool back = !hub->stopReq &&
                (hub->liveMembers > 0 || hub->flushReq ||
                 std::chrono::steady_clock::now() < hub->lingerUntil);
            if (!back) {
                hub->running = false;
                hub->wake.notify_all();
                return;
            }
        }
        sheet.begin();
    }
}

} // namespace

extern "C" {

int32_t seam_hub_v1_sheet_set_folder(SeamHub* hub, const char* folder) {
    // Absolute paths only: the conf is read by a host whose working directory
    // is anyone's guess.
    if (!hub || !folder || folder[0] != '/' || std::strchr(folder, '\n')) return -1;
    std::lock_guard<std::mutex> lock(g_confMutex);
    const std::string conf = confPath();
    if (conf.empty()) return -1;
    // Keep every other line of the conf; replace the folder line.
    std::vector<std::string> keep;
    {
        std::ifstream in(conf);
        for (std::string l; std::getline(in, l);) {
            const size_t eq = l.find('=');
            if (eq != std::string::npos && trim(l.substr(0, eq)) == "sheet-folder") continue;
            keep.push_back(l);
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(conf).parent_path(), ec);
    const std::string tmp = conf + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return -1;
        for (const auto& l : keep) out << l << "\n";
        out << "sheet-folder=" << folder << "\n";
        if (!out.flush()) return -1;
    }
    return std::rename(tmp.c_str(), conf.c_str()) == 0 ? 0 : -1;
}

// Closes the current sheet and opens a new one at once: the writer is woken
// rather than waited for, so the detector's button never blocks on the disk.
// Outside a session it does nothing (the next session opens a new sheet).
void seam_hub_v1_sheet_new(SeamHub* hub) {
    if (!hub) return;
    {
        std::lock_guard<std::mutex> lk(hub->ctlMutex);
        hub->renewReq = true;
    }
    hub->wake.notify_all();
}

// ---- test hooks: exported, never declared in seam_hub.h, not ABI ----------

// The grace after the last member leaves, in ms (default kGraceMs), so the
// writer tests do not wait 10 s per case. Takes effect at the next last
// unregister.
SEAM_HUB_API void seam_hub_test_set_grace_ms(int32_t ms) {
    g_hub.graceMs.store(ms < 0 ? 0 : ms, std::memory_order_relaxed);
}

// Makes the writer stall for `ms` before its next write, holding no lock:
// a stuck disk, as the last unregister must survive it. 0 releases it.
SEAM_HUB_API void seam_hub_test_stall_writer(int32_t ms) {
    g_hub.stallMs.store(ms < 0 ? 0 : ms, std::memory_order_relaxed);
}

// Runs the dylib's teardown routine (the same one its destructor runs):
// 1 when the writer exited within the bound, 0 when it was detached.
SEAM_HUB_API int32_t seam_hub_test_teardown(void) {
    return g_hub.teardown();
}

// 1 while a writer thread is alive (working or lingering), 0 once it exited.
SEAM_HUB_API int32_t seam_hub_test_writer_alive(void) {
    std::lock_guard<std::mutex> lk(g_hub.ctlMutex);
    return g_hub.running ? 1 : 0;
}

} // extern "C"
