// libseamhub — members, states, proposals and the session sheet's single
// writer (Tasks 2-5). The sheet's grammar is seam_hub_sheet.h.
//
// The slot machinery below is the suite's earlier peer-aware bus, carried
// over with its logic unchanged (Slot, encodeHandle, seqlock, epoch check, kMaxReadAttempts).
// What is new: each slot also carries the member's name, instance and masks,
// kept under the registration mutex, and a line queue for the session sheet.
#include "seam_hub.h"
#include "seam_hub_fold.h"
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
// The layout is declared in seam_hub.h (seam_hub_handle_member/_epoch), so a
// member can find its own entry in members(); these are the same numbers.
constexpr int32_t  kIndexBits = SEAM_HUB_HANDLE_INDEX_BITS;
constexpr int32_t  kIndexMask = (1 << kIndexBits) - 1;
constexpr uint32_t kGenMask   = SEAM_HUB_HANDLE_EPOCH_MASK;

static_assert(SEAM_HUB_MAX_MEMBERS <= kIndexMask + 1,
              "slot index must fit in kIndexBits");

inline int32_t encodeHandle(int32_t index, uint32_t gen) {
    return (int32_t)(((gen & kGenMask) << kIndexBits) | (uint32_t)index);
}

inline int32_t handleIndex(int32_t handle) {
    return (int32_t)seam_hub_handle_member(handle);
}

inline uint32_t handleGen(int32_t handle) {
    return seam_hub_handle_epoch(handle);
}

// ---- the line queue -------------------------------------------------------
//
// Finished sheet lines wait here for the writer (Task 5). Bounded, so a sheet
// that cannot be written (disk full, no folder) never grows the process
// without limit: when full, the OLDEST line is dropped and counted, because
// the newest lines are the ones that describe the present.
constexpr size_t kMaxQueuedLines = 10000;
// Each entry carries what the fold needs (seam_hub_fold.h): a host's leave
// and return of one member, the same declaration, within kFoldMs, is written
// as one `resumes` line. The writer folds before it writes.
using Seam::hubfold::Entry;
using Seam::hubfold::Kind;
// Test-only mirror: the last lines enqueued, untouched by the writer, so a
// test can see what was enqueued without racing the consumer. It keeps the
// EVENT line of each entry (a table's continuation rows stay out), and it
// does not see the writer's own `state` lines: those arrive on the writer's
// 250 ms tick, so mirroring them would put lines a test cannot predict
// between the ones it called for.
constexpr size_t kMirrorLines = 256;
// The test mirror keeps the raw lines, before the fold: a toggle shows there
// as its `leaves` and `arrives`.
// The public recent lines (seam_hub_v1_recent_lines): every sheet line, rows
// and the writer's state lines included, as the detector shows them. They are
// fed after the fold, on the writer's tick, so they say what the sheet says.
constexpr size_t kRecentLines = 256;

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

// After a failed write the writer leaves the sheet alone this long before it
// reopens it. A volume that refuses writes (quota, full disk) answers every
// attempt the same way; trying each 250 ms tick only churns the disk.
constexpr auto kRetryAfterFailure = std::chrono::seconds(5);

// Lines are formatted by Seam::hubsheet; this is just "now" in unix ms.
int64_t nowMs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string maskNames(uint32_t mask) {
    static const struct { uint32_t type; const char* name; } kNames[] = {
        {kSeamHubPink, "pink"}, {kSeamHubGlide, "glide"}, {kSeamHubDecoder, "decoder"},
        {kSeamHubBandTable, "band-table"}, {kSeamHubSections, "sections"}};
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
// must return. False when every attempt tore: the caller then treats the
// receiver as hearing nothing this time (fails closed). Returning 0 would
// mean "undeclared", which hears every stone, so a busy publisher would
// briefly route another unit's proposals to it.
bool stoneOf(const Slot& s, uint32_t& out) {
    for (int attempt = 0; attempt < kMaxReadAttempts; ++attempt) {
        const uint32_t s1 = s.seq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;
        const uint32_t pub = s.published.load(std::memory_order_acquire);
        SeamHubState tmp;
        std::memcpy(&tmp, &s.state, sizeof tmp);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != s1) continue;
        out = pub ? tmp.stoneId : 0u;
        return true;
    }
    return false;
}

// A member name is the sheet's `who` before "#<instance>": one token the
// parser reads with \S+, within the 15 characters a name field holds.
bool validName(const char* name) {
    if (!name || !*name) return false;
    size_t n = 0;
    for (const char* c = name; *c; ++c, ++n)
        if (n >= SEAM_HUB_NAME_LEN - 1 || (unsigned char)*c <= ' ' || *c == 0x7f) return false;
    return true;
}

// A transition's detail as it may appear in a line: at most
// SEAM_HUB_DETAIL_LEN - 1 characters, control characters as spaces.
std::string cleanDetail(const char* d) {
    std::string out;
    if (!d) return out;
    for (; *d && out.size() < SEAM_HUB_DETAIL_LEN - 1; ++d)
        out += ((unsigned char)*d < ' ' || *d == 0x7f) ? ' ' : *d;
    const size_t e = out.find_last_not_of(' ');
    return e == std::string::npos ? std::string() : out.substr(0, e + 1);
}

} // namespace

struct SeamHub {
    Slot       slots[SEAM_HUB_MAX_MEMBERS];
    // Guards name/instance/masks of every slot and the open/close of epochs.
    // Never taken by publish_state.
    std::mutex regMutex;

    // Lock order: (storeMutex,) regMutex, then lineMutex. Separate from regMutex so the
    // writer draining the queue never holds up a registering plugin. The fold
    // and the recent lines run under lineMutex alone (foldLocked).
    std::mutex                lineMutex;
    std::deque<Entry>         lines;
    std::deque<std::string>   mirror;
    std::deque<std::string>   recent;       // kRecentLines, for seam_hub_v1_recent_lines
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
    //
    // The entry reaches the recent lines later, after the fold (foldLocked),
    // so the detector never shows a `leaves` the sheet will not carry.
    uint64_t enqueue(Entry e, bool mirrored = true) {
        std::lock_guard<std::mutex> lock(lineMutex);
        if (mirrored) {
            mirror.push_back(e.text.substr(0, e.text.find('\n')));
            if (mirror.size() > kMirrorLines) mirror.pop_front();
        }
        if (lines.size() >= kMaxQueuedLines) { lines.pop_front(); ++droppedLines; }
        lines.push_back(std::move(e));
        return ++enqSeq;
    }

    // Any line that is neither an arrival nor a departure: nothing to fold.
    uint64_t enqueue(std::string line, bool mirrored = true) {
        Entry e;
        e.text = std::move(line);
        e.unixMs = nowMs();
        return enqueue(std::move(e), mirrored);
    }

    // Folds the queue (seam_hub_fold.h) and returns how many leading entries
    // are ready to write; those not yet shown go into the recent lines.
    // lineMutex held, and nothing else: the fold walks the queue in memory.
    //
    // A ready `leaves` is final: it is shown, and written or held for its
    // sheet, so it no longer pairs with an arrival that comes later (a clock
    // stepped back, or a return at exactly the window's edge). Marking it
    // Other is what keeps the recent lines and the sheet saying the same.
    //
    // windowMs 0 makes every entry ready: the writer uses it whenever a sheet
    // is about to be left (quit, renew, close, spill), so no young `leaves`
    // outlives its sheet.
    size_t foldLocked(int64_t windowMs) {
        const size_t ready = Seam::hubfold::fold(lines, nowMs(), windowMs);
        for (size_t i = 0; i < ready; ++i) {
            Entry& e = lines[i];
            if (e.remembered) continue;
            rememberLocked(e.text);
            e.remembered = true;
            if (e.kind == Kind::Leaves) e.kind = Kind::Other;
        }
        return ready;
    }

    // Every line of an entry into `recent`, bounded. lineMutex held.
    void rememberLocked(const std::string& entry) {
        for (size_t p = 0; p <= entry.size();) {
            size_t e = entry.find('\n', p);
            if (e == std::string::npos) e = entry.size();
            recent.push_back(entry.substr(p, e - p));
            if (recent.size() > kRecentLines) recent.pop_front();
            p = e + 1;
        }
    }

    // ---- the writer -------------------------------------------------------
    // At most ONE writer thread, possibly lingering. A session (one sheet)
    // runs from the first register until kGraceMs after the last unregister
    // with nobody back, and no line held for its folder (Sheet, held lines);
    // the writer then closes the sheet and exits by itself. A register while
    // lines are still held continues the same sheet.
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
    // Every entry <= this is written, or held: for its sheet's folder, or, a
    // young `leaves`, for the fold window (at most kFoldMs, and never past
    // its sheet's close). So the last unregister never waits for the window.
    uint64_t ackSeq = 0;
    std::atomic<int32_t> graceMs{kGraceMs}; // a test hook can shorten it
    std::atomic<int32_t> stallMs{0};        // test hook: the writer stalls before writing
    std::atomic<int32_t> teardownGapMs{0};  // test hook: widens teardown's window between its locks

    // What sheet_status() reports, published by the writer after each change.
    std::mutex statusMutex;
    char       statusPath[512]{};
    uint32_t   statusWriting = 0;

    // Under lineMutex: entries the writer has swapped out and not yet written
    // (so pendingLines does not dip while a batch is on its way to the disk),
    // and the sequence number of the last entry enqueued.
    size_t   inFlight = 0;
    uint64_t enqSeq = 0;

    // Writer-thread only; successive writers are ordered by the join. Held
    // lines never outlive their writer: it stays alive while they wait for
    // their sheet's folder, and spills them before it exits (writerMain).
    bool     unreachable = false;
    uint64_t droppedAtOutage = 0;

    // The host quits: ask the writer for a last pass, which drains (with no
    // fold window, so a young `leaves` is written too) into a
    // sheet that is already open and never opens one, and spills what it
    // still holds to the local fallback folder (writerMain), and wait
    // for it to exit at most kLastLeaveWait. A writer that has exited is
    // joined; one still stuck in the disk (a hung network share) is detached
    // and left to the process exit. Detaching is safe for two reasons
    // together: the hub is never destroyed (see g_hub below), so the
    // detached thread never touches freed DATA; and the dylib is never
    // unloaded (the hub client never dlclose()s a hub it loaded), so it
    // never runs unmapped CODE. Returns 1 when the writer had exited (or
    // there was none), 0 when it was detached.
    int teardown() {
        {
            std::unique_lock<std::mutex> lk(ctlMutex);
            stopReq = true;
            wake.notify_all();
            wake.wait_for(lk, kLastLeaveWait, [this] { return !running; });
        }
        if (const int32_t gap = teardownGapMs.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(gap));
        std::lock_guard<std::mutex> life(lifeMutex);
        // Re-read under lifeMutex, not the result of the wait above: between
        // the two locks a register may have found `running` false, joined the
        // old thread and started a new writer (it resets stopReq), and joining
        // THAT one would wait for as long as it lives. With lifeMutex held,
        // `writer` cannot change; `running` false means it is the thread that
        // has done its last act (a register that sets `running` now waits on
        // lifeMutex and finds nothing to join).
        bool exited;
        {
            std::lock_guard<std::mutex> lk(ctlMutex);
            exited = !running;
        }
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
// the process exit reclaims anyway. This protects the DATA only: the CODE the
// detached thread runs stays mapped because the dylib is never unloaded (the
// hub client never dlclose()s a hub it loaded, seam_hub_client.h). A real dlclose with a detached writer alive would
// run unmapped code; that rule is what makes detaching safe.
static SeamHub& g_hub = *new SeamHub;

// The bounded stop runs from the dylib's own teardown. A destructor
// function rather than an atexit handler: it needs no registration from
// register() on a host's thread, none that a failed first start could skip,
// and it is tied to this image rather than to whoever called atexit. In
// practice it runs at process exit only, since the dylib is never unloaded
// (see g_hub). It never waits more than kLastLeaveWait.
__attribute__((destructor)) static void seamHubTeardown() { g_hub.teardown(); }

namespace { void writerMain(SeamHub* hub); }

extern "C" {

uint32_t seam_hub_v1_version(void) { return SEAM_HUB_VERSION; }
SeamHub* seam_hub_v1_get(void) { return &g_hub; }

namespace {

int32_t registerLocked(SeamHub* hub, const char* name, uint32_t says, uint32_t hears,
                       uint32_t preferred) {
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
        std::strncpy(s.name, name, SEAM_HUB_NAME_LEN - 1);
        s.says = says;
        s.hears = hears;
        // Instances are numbered per name, smallest free from 1, so a name
        // that left and came back is #1 again rather than an ever-growing
        // counter; the sheet's `who` then names the same role across sessions.
        // A member that asks for the number it had gets it back when it is
        // free: without that, two strx toggling activation in turn would swap
        // #1 and #2 and the sheet would confuse them.
        auto taken = [&](uint32_t inst) {
            for (int32_t j = 0; j < SEAM_HUB_MAX_MEMBERS; ++j) {
                const Slot& o = hub->slots[j];
                if (j != i && o.inUse.load(std::memory_order_relaxed) &&
                    o.instance == inst && std::strncmp(o.name, s.name, SEAM_HUB_NAME_LEN) == 0)
                    return true;
            }
            return false;
        };
        uint32_t inst = preferred;
        if (inst == 0 || taken(inst))
            for (inst = 1; taken(inst); ++inst) {}
        s.instance = inst;

        // Open the new epoch, then advertise the slot. Both are release
        // stores, so a reader that observes them also observes the cleared
        // state and the seq bumps that precede them.
        const uint32_t g = (s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask;
        s.gen.store(g, std::memory_order_release);
        s.inUse.store(1, std::memory_order_release);

        Entry e;
        e.unixMs = nowMs();
        e.kind = Kind::Arrives;
        e.who = whoOf(s.name, inst);
        e.masks = "says " + maskNames(says) + "  hears " + maskNames(hears);
        e.text = Seam::hubsheet::event(e.unixMs, e.who, "arrives", e.masks);
        hub->enqueue(std::move(e));
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

    // The masks go with the `leaves` (they are not printed): a return with the
    // same declaration folds into `resumes`, a different one is a real change.
    Entry e;
    e.unixMs = nowMs();
    e.kind = Kind::Leaves;
    e.who = whoOf(s.name, s.instance);
    e.masks = "says " + maskNames(s.says) + "  hears " + maskNames(s.hears);
    e.text = Seam::hubsheet::event(e.unixMs, e.who, "leaves", "");
    const uint64_t seq = hub->enqueue(std::move(e));

    // Close the epoch before retiring the slot: a reader that captured the old
    // gen sees it change and rejects whatever it copied.
    s.gen.store((s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask,
                std::memory_order_release);
    s.inUse.store(0, std::memory_order_release);
    return seq;
}

} // namespace

int32_t seam_hub_v1_register(SeamHub* hub, const char* name, uint32_t says, uint32_t hears,
                             uint32_t preferredInstance) {
    if (!hub || !validName(name)) return SEAM_HUB_NO_HANDLE;
    const int32_t h = registerLocked(hub, name, says, hears, preferredInstance);
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
    // until the writer has handled the `leaves` line: written, or held (a
    // young `leaves` waits for the fold window, and that counts as handled,
    // so this never waits for the window). Never a join.
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
    m.epoch = g1;
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

// The newest lines of `src` that fit `cap` with their '\n' and a final NUL.
// lineMutex held by the caller.
namespace {
int32_t copyNewest(const std::deque<std::string>& src, char* out, int32_t cap) {
    // Keep the NEWEST lines: start from the first line from which the rest
    // fits (each line costs its size plus '\n'; one byte stays for the NUL).
    size_t first = src.size(), used = 1;
    while (first > 0 && used + src[first - 1].size() + 1 <= (size_t)cap) {
        used += src[first - 1].size() + 1;
        --first;
    }
    size_t pos = 0;
    int32_t count = 0;
    for (size_t k = first; k < src.size(); ++k) {
        const std::string& l = src[k];
        std::memcpy(out + pos, l.data(), l.size());
        pos += l.size();
        out[pos++] = '\n';
        ++count;
    }
    out[pos] = '\0';
    return count;
}
} // namespace

int32_t seam_hub_v1_recent_lines(SeamHub* hub, char* out, int32_t cap) {
    if (!hub || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lock(hub->lineMutex);
    return copyNewest(hub->recent, out, cap);
}

// Test-only: the last 256 EVENT lines enqueued (no rows, no writer state
// lines, see kMirrorLines), newline-separated, NUL-terminated within `cap`.
// Returns the number of lines written. Declared in the tests, never in
// seam_hub.h: it is not part of the ABI. The tests keep it beside the public
// recent lines because its content is exactly what they called for.
SEAM_HUB_API int32_t seam_hub_test_recent_lines(char* out, int32_t cap) {
    if (!out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_hub.lineMutex);
    return copyNewest(g_hub.mirror, out, cap);
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
    // Types below 16 are states: they are published, never posted, and the
    // store and the sheet have no shape for them.
    if (!hub || !inout || inout->type < 16 || inout->type >= 32) return 0;
    std::lock_guard<std::mutex> store(hub->storeMutex);

    std::string who;
    uint32_t member = 0, instance = 0;
    char name[SEAM_HUB_NAME_LEN];
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        Slot* s = nullptr;
        if (!liveSlot(hub, handle, s)) return 0;
        if (!(s->says & SEAM_HUB_BIT(inout->type))) return 0;
        member = (uint32_t)handleIndex(handle);
        instance = s->instance;
        std::memcpy(name, s->name, sizeof name);
        who = whoOf(s->name, s->instance);
    }

    inout->id = hub->nextId++;
    inout->authorMember = member;
    inout->authorInstance = instance;
    inout->postedUnixMs = nowMs();
    // The author's name with the record: a reader of an old proposal must not
    // have to find a member that may have left long ago.
    std::memcpy(inout->authorName, name, sizeof inout->authorName);

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
    // An afterId at or past the newest asks for nothing; checked first, since
    // afterId + 1 wraps to 0 at UINT64_MAX and would return the whole store.
    if (afterId >= hub->nextId - 1) return 0;
    uint64_t from = hub->nextId > SEAM_HUB_STORE ? hub->nextId - SEAM_HUB_STORE : 1;
    if (afterId + 1 > from) from = afterId + 1;
    int32_t n = 0;
    for (uint64_t id = from; id < hub->nextId && n < max; ++id)
        out[n++] = hub->store[(id - 1) % SEAM_HUB_STORE];
    return n;
}

namespace {

// Where proposal `id` stands for the registration (idx, gen), and that
// registration's who. kSeamHubNone when the proposal is unknown, the
// registration has ended, or it does not hear the proposal (its stone read
// tore: fails closed). storeMutex held by the caller.
struct Receiver { int32_t idx; uint32_t gen; std::string who; };
int32_t standing(SeamHub* hub, int32_t idx, uint32_t gen, uint64_t id, Receiver* r) {
    const SeamHubProposal* p = find(hub, id);
    if (!p || idx < 0 || idx >= SEAM_HUB_MAX_MEMBERS) return kSeamHubNone;
    uint32_t hears = 0, stone = 0;
    {
        std::lock_guard<std::mutex> lock(hub->regMutex);
        Slot& s = hub->slots[idx];
        if (s.inUse.load(std::memory_order_relaxed) == 0 ||
            s.gen.load(std::memory_order_relaxed) != gen) return kSeamHubNone;
        hears = s.hears;
        if (!stoneOf(s, stone)) return kSeamHubNone;
        if (r) *r = Receiver{idx, gen, whoOf(s.name, s.instance)};
    }
    if (!heard(hears, stone, *p)) return kSeamHubNone;
    const auto& c = hub->life[(id - 1) % SEAM_HUB_STORE][idx];
    return (c.gen == gen && c.st != 0) ? c.st : kSeamHubArrived;
}

} // namespace

int32_t seam_hub_v1_lifecycle(SeamHub* hub, int32_t receiver, uint64_t id) {
    if (!hub || receiver < 0) return kSeamHubNone;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    return standing(hub, handleIndex(receiver), handleGen(receiver), id, nullptr);
}

int32_t seam_hub_v1_lifecycle_of(SeamHub* hub, uint32_t member, uint32_t epoch, uint64_t id) {
    if (!hub || member >= SEAM_HUB_MAX_MEMBERS) return kSeamHubNone;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    return standing(hub, (int32_t)member, epoch & kGenMask, id, nullptr);
}

int32_t seam_hub_v1_transition(SeamHub* hub, int32_t receiver, uint64_t id, int32_t to, int32_t byOperator,
                               const char* detail) {
    if (!hub) return SEAM_HUB_ERR_UNKNOWN;
    std::lock_guard<std::mutex> store(hub->storeMutex);
    const SeamHubProposal* p = find(hub, id);
    if (!p) return SEAM_HUB_ERR_UNKNOWN;
    Receiver r{};
    const int32_t from = receiver < 0 ? (int32_t)kSeamHubNone
                       : standing(hub, handleIndex(receiver), handleGen(receiver), id, &r);
    if (from == kSeamHubNone) return SEAM_HUB_ERR_NOT_HEARD;   // no such receiver, or it hears nothing

    const bool decision = (to == kSeamHubApplied || to == kSeamHubIgnored);
    // Checked before legality: applying without the operator is refused
    // whatever the state (no member acts on what it hears by itself).
    if (decision && !byOperator) return SEAM_HUB_ERR_OPERATOR;

    const bool legal = (from == kSeamHubArrived) ? (to == kSeamHubShown)
                                                 : decision;   // shown/applied/ignored -> applied|ignored
    if (!legal) return SEAM_HUB_ERR_ILLEGAL;
    auto& c = hub->life[(id - 1) % SEAM_HUB_STORE][r.idx];
    c.gen = r.gen;
    c.st = (uint8_t)to;

    std::string obj = std::string(proposalName(p->type)) + " #" + std::to_string(p->id);
    std::string tail;
    const std::string d = cleanDetail(detail);
    if (!d.empty()) tail = "  (" + d + ")";
    if (to == kSeamHubShown)
        hub->enqueue(Seam::hubsheet::event(nowMs(), r.who, "shows", obj + tail));
    else
        hub->enqueue(Seam::hubsheet::event(nowMs(), "operator",
                     to == kSeamHubApplied ? "applies" : "ignores", obj + "  on " + r.who + tail));
    return SEAM_HUB_OK;
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

// Where held lines go when their sheet's folder never came back: beside the
// conf, so ~/Library/Application Support/SEAM/sheets-held for the operator,
// and inside the test tree when SEAM_HUB_CONF points there. A local folder
// that is not privacy-protected: it cannot hang like a share, and writing it
// raises no prompt. "" when there is no conf path (HOME unset).
std::string heldFolder() {
    const std::string conf = confPath();
    if (conf.empty()) return "";
    return (std::filesystem::path(conf).parent_path() / "sheets-held").string();
}

// The writer thread's own state for one sheet. Only the writer touches it.
//
// HELD LINES BELONG TO THEIR SHEET. When the folder is unreachable, the queue
// is left alone: its entries are the held lines (bounded by enqueue()'s
// drop-oldest). The sheet's path is kept, and every tick retries it, so when
// the folder returns the held lines are written into the session's own file,
// after a notice. The writer does not exit while it holds lines, even after
// the grace. They leave the sheet's path only in three cases, all spilled to
// heldFolder()/<the sheet's file name> with a notice: at quit (teardown's
// last pass, still within its 2 s bound), on sheet_new() (the operator moved
// on; the new sheet names where they went), and at the end of a session
// whose sheet has no path at all (no folder, or no free name). If even the
// spill fails, they are lost, and counted as dropped.
struct Sheet {
    SeamHub*    hub;
    std::string name;            // the file name, kept even when there is no path
    std::string path;            // folder/name, "" when there is nowhere to write
    std::time_t start = 0;
    std::FILE*  f = nullptr;
    struct Last { uint32_t epoch; bool written; SeamHubState st; };
    Last        last[SEAM_HUB_MAX_MEMBERS]{};
    // The file's header is written once per sheet; `committed` is the file's
    // size after the last flush that succeeded. A failed write may have left
    // a torn fragment (or a whole unflushed batch) after it: the reopen
    // truncates back to `committed` and the batch is written again, whole, in
    // the same file. So a failure never splits a session into two files.
    bool        opened = false;  // this sheet has created its file at `path`
    bool        headerDone = false;
    long        committed = 0;
    std::chrono::steady_clock::time_point retryAfter{};
    std::string notice;          // a `sheet` notice owed to this file, written after the header

    void publishStatus() {
        std::lock_guard<std::mutex> lock(hub->statusMutex);
        std::snprintf(hub->statusPath, sizeof hub->statusPath, "%s", path.c_str());
        hub->statusWriting = f ? 1u : 0u;
    }

    // A line the writer writes itself goes into the recent lines too, so the
    // detector shows the notices the file carries.
    void remember(const std::string& line) {
        std::lock_guard<std::mutex> lock(hub->lineMutex);
        hub->rememberLocked(line);
    }

    bool holding() {
        if (f) return false;
        std::lock_guard<std::mutex> lock(hub->lineMutex);
        return !hub->lines.empty();
    }

    std::string stem() const {
        std::tm tm{};
        localtime_r(&start, &tm);
        char b[96];
        std::snprintf(b, sizeof b, "%04d-%02d-%02d-%02d%02d%02d-%d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec, (int)getpid());
        return b;
    }

    // The first name of this sheet's stem not already on disk in `folder`
    // (two sheets of this process in the same second and folder: -2, -3, ...
    // after the pid). A sheet is never appended to another: when every name
    // is taken, there is no path, and the sheet is unreachable.
    void pickName(const std::string& folder) {
        const std::string st = stem();
        name = st + "-seam-sheet.txt";
        path.clear();
        opened = false;
        if (folder.empty()) return;
        std::error_code ec;
        for (int n = 1; n < 1000; ++n) {
            const std::string nm = st + (n > 1 ? "-" + std::to_string(n) : "") + "-seam-sheet.txt";
            const std::string p = (std::filesystem::path(folder) / nm).string();
            // An error (the folder is unreachable) reads as free: the name is
            // checked again when the file is first opened.
            if (!std::filesystem::exists(p, ec)) { name = nm; path = p; return; }
        }
    }

    // A new sheet: new start time, the conf re-read, states written afresh
    // so the new file stands on its own.
    void begin() {
        close();
        start = std::time(nullptr);
        pickName(sheetFolder());
        headerDone = false;
        committed = 0;
        retryAfter = {};
        std::memset(last, 0, sizeof last);
        publishStatus();
    }

    void close() {
        if (f) { std::fclose(f); f = nullptr; publishStatus(); }
    }

    // The open file's size, after a flush. An append-mode stream need not
    // report the end before its first write, so it is asked to seek there.
    long endOffset() {
        if (std::fseek(f, 0, SEEK_END) != 0) return committed;
        const long pos = std::ftell(f);
        return pos >= 0 ? pos : committed;
    }

    void markUnreachable(uint64_t droppedNow) {
        if (!hub->unreachable) { hub->unreachable = true; hub->droppedAtOutage = droppedNow; }
    }

    // Opens the sheet when it is not open. On failure the queue is left
    // alone: its lines are the held lines, bounded by enqueue()'s drop-oldest.
    void open() {
        if (f) return;
        if (std::chrono::steady_clock::now() < retryAfter) return;   // backing off a failed write
        uint64_t droppedNow;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            droppedNow = hub->droppedLines;
        }
        std::error_code ec;
        if (!path.empty() && !opened) {
            // Never append to a file this sheet did not start: the name was
            // picked while the folder may have been unreachable.
            if (std::filesystem::exists(path, ec))
                pickName(std::filesystem::path(path).parent_path().string());
        }
        if (path.empty()) { markUnreachable(droppedNow); publishStatus(); return; }
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        if (opened) {
            // A reopen after a failure: drop whatever followed the last good
            // flush (it is written again from the queue, or the header is);
            // a file that has gone missing starts over with a header.
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (ec) { headerDone = false; committed = 0; }
            else if (size > (uintmax_t)committed && ::truncate(path.c_str(), (off_t)committed) != 0) {
                markUnreachable(droppedNow);
                retryAfter = std::chrono::steady_clock::now() + kRetryAfterFailure;
                return;
            }
        }
        f = std::fopen(path.c_str(), "a");
        if (!f) { markUnreachable(droppedNow); return; }
        opened = true;
        std::string pre;
        if (!headerDone) {
            std::tm tm{};
            localtime_r(&start, &tm);
            pre = Seam::hubsheet::header(tm, (int)getpid());
        }
        std::vector<std::string> notes;
        if (!notice.empty()) notes.push_back(Seam::hubsheet::event(nowMs(), "hub", "sheet", notice));
        if (hub->unreachable) {
            size_t held;
            uint64_t dropped;
            {
                std::lock_guard<std::mutex> lock(hub->lineMutex);
                held = hub->lines.size();
                dropped = hub->droppedLines - hub->droppedAtOutage;
            }
            std::string what = "folder unreachable, " + std::to_string(held) + " lines held";
            if (dropped) what += ", " + std::to_string(dropped) + " dropped";
            notes.push_back(Seam::hubsheet::event(nowMs(), "hub", "sheet", what));
        }
        for (const auto& n : notes) pre += n + "\n";
        const bool ok = std::fputs(pre.c_str(), f) >= 0 && std::fflush(f) == 0;
        if (!ok) {
            // The header or a notice did not make it: closed, and the reopen
            // truncates back to `committed` (0 for a new file) and writes
            // them again. `unreachable` stays set: the notice is still owed.
            markUnreachable(droppedNow);
            close();
            retryAfter = std::chrono::steady_clock::now() + kRetryAfterFailure;
            return;
        }
        for (const auto& n : notes) remember(n);
        headerDone = true;
        notice.clear();
        committed = endOffset();
        hub->unreachable = false;
        publishStatus();
    }

    // Moves every held line out of the queue into heldFolder()/<name>, after
    // a header and a notice saying why. Returns the file written, or "" when
    // the lines were lost (no fallback folder, or it failed too: they are
    // then counted as dropped). Nothing held: returns "" and does nothing.
    //
    // The held lines leave their sheet here, so the fold runs with no window
    // first: a young `leaves` goes with them, and every spilled line has
    // reached the recent lines.
    std::string spill(const std::string& why, size_t* moved = nullptr) {
        std::deque<Entry> batch;
        uint64_t dropped;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->foldLocked(0);
            batch.swap(hub->lines);
            hub->inFlight = batch.size();
            dropped = hub->unreachable ? hub->droppedLines - hub->droppedAtOutage : 0;
        }
        if (moved) *moved = batch.size();
        if (batch.empty()) {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->inFlight = 0;
            return "";
        }
        const std::string dir = heldFolder();
        std::string target;
        bool ok = false;
        if (!dir.empty() && !name.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            target = (std::filesystem::path(dir) / name).string();
            if (std::FILE* g = std::fopen(target.c_str(), "a")) {
                std::tm tm{};
                localtime_r(&start, &tm);
                std::string what = why + " (" + (path.empty() ? std::string("no path") : path) +
                                   "), " + std::to_string(batch.size()) + " lines held here";
                if (dropped) what += ", " + std::to_string(dropped) + " dropped";
                const std::string note = Seam::hubsheet::event(nowMs(), "hub", "sheet", what);
                ok = std::fputs((Seam::hubsheet::header(tm, (int)getpid()) + note + "\n").c_str(), g) >= 0;
                for (size_t k = 0; ok && k < batch.size(); ++k)
                    ok = std::fwrite(batch[k].text.data(), 1, batch[k].text.size(), g) == batch[k].text.size() &&
                         std::fputc('\n', g) != EOF;
                ok = (std::fflush(g) == 0) && ok;
                ok = (std::fclose(g) == 0) && ok;
                if (ok) remember(note);
            }
        }
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->inFlight = 0;
            if (!ok) hub->droppedLines += batch.size();
        }
        hub->unreachable = false;
        return ok ? target : "";
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

    // Fold, then swap the ready prefix out under lineMutex, write with it
    // released: a slow or full disk must never stall a GUI caller of
    // members() through the regMutex -> lineMutex chain. The fold and the
    // recent lines come first, whether or not the sheet is open: held lines
    // must reach the recent lines too. Returns the sequence number up to which
    // every entry is now either written or held (in the queue, waiting for
    // the folder, or a young `leaves` and what follows it, waiting for the
    // fold window): what the writer acknowledges.
    //
    // windowMs is kFoldMs on the writer's ticks, and 0 whenever the sheet is
    // about to be left (quit, renew, close), so nothing stays behind it.
    uint64_t drain(int64_t windowMs) {
        std::deque<Entry> batch;
        uint64_t upTo;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            const size_t ready = hub->foldLocked(windowMs);
            upTo = hub->enqSeq;
            if (!f) return upTo;
            if (ready == hub->lines.size()) {
                batch.swap(hub->lines);
            } else {
                batch.assign(std::make_move_iterator(hub->lines.begin()),
                             std::make_move_iterator(hub->lines.begin() + (std::ptrdiff_t)ready));
                hub->lines.erase(hub->lines.begin(), hub->lines.begin() + (std::ptrdiff_t)ready);
            }
            hub->inFlight = batch.size();
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
        bool failed = false;
        for (const Entry& e : batch)
            if (std::fwrite(e.text.data(), 1, e.text.size(), f) != e.text.size() || std::fputc('\n', f) == EOF) {
                failed = true;
                break;
            }
        failed = (std::fflush(f) != 0) || failed;
        {
            std::lock_guard<std::mutex> lock(hub->lineMutex);
            hub->inFlight = 0;
            if (failed) {
                // The outage starts before the put-back, so the drops the
                // put-back itself causes are counted in the notice.
                markUnreachable(hub->droppedLines);
                // Put the WHOLE batch back, ahead of what arrived since: an
                // fwrite that "succeeded" may only have reached stdio's buffer,
                // which the failed flush did not empty. The reopen truncates
                // the file to `committed`, so nothing is written twice. The
                // batch was the ready prefix, so it goes back ahead of the
                // young lines still waiting for the window: order is kept,
                // and its entries stay `remembered`, never shown twice.
                for (size_t k = batch.size(); k-- > 0;) hub->lines.push_front(std::move(batch[k]));
                while (hub->lines.size() > kMaxQueuedLines) { hub->lines.pop_front(); ++hub->droppedLines; }
            }
        }
        if (failed) {
            close();
            retryAfter = std::chrono::steady_clock::now() + kRetryAfterFailure;
        } else {
            committed = endOffset();
        }
        return upTo;
    }
};

void writerMain(SeamHub* hub) {
    // Every entry up to `acked` is written or held: the last unregister may go.
    auto acknowledge = [hub](uint64_t acked) {
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            if (acked > hub->ackSeq) hub->ackSeq = acked;
        }
        hub->wake.notify_all();
    };
    Sheet sheet{hub};
    sheet.begin();
    for (bool first = true;; first = false) {
        bool stop, renew, members;
        {
            std::unique_lock<std::mutex> lk(hub->ctlMutex);
            if (!first) {
                const auto now = std::chrono::steady_clock::now();
                auto until = now + kWriterTick;
                // The end of the grace wakes the writer early; once it has
                // passed (a writer kept alive by held lines) the tick rules,
                // or the wait would return at once, forever.
                if (hub->liveMembers == 0 && hub->lingerUntil > now && hub->lingerUntil < until)
                    until = hub->lingerUntil;
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
        if (renew) {
            // What the old sheet still has goes into it, a young `leaves`
            // included (no window: a return belongs to the new sheet); what
            // it holds for an unreachable folder is spilled, and the new
            // sheet says where.
            sheet.drain(0);
            if (sheet.holding()) {
                const std::string old = sheet.path.empty() ? sheet.name : sheet.path;
                size_t n = 0;
                const std::string to = sheet.spill("sheet renewed, folder unreachable", &n);
                sheet.begin();
                sheet.notice = "previous sheet " + old + " unreachable, " + std::to_string(n) +
                               (to.empty() ? " lines lost" : " lines moved to " + to);
            } else {
                sheet.begin();
            }
        }
        if (members && !stop) sheet.snapshotStates();
        // Under stop no sheet is opened (no create_directories, no fopen on a
        // share that may hang the quit): an open one is drained and closed,
        // and what is still held is spilled to the local fallback.
        // A tick folds with the window; the quit's pass with none, so a
        // young `leaves` is written (or spilled) before the writer exits.
        if (!stop) sheet.open();
        const uint64_t acked = sheet.drain(stop ? 0 : Seam::hubfold::kFoldMs);
        if (stop && sheet.holding()) sheet.spill("quit, folder unreachable");
        acknowledge(acked);

        bool closeNow;
        {
            std::lock_guard<std::mutex> lk(hub->ctlMutex);
            closeNow = hub->stopReq ||
                       (hub->liveMembers == 0 && !hub->flushReq &&
                        std::chrono::steady_clock::now() >= hub->lingerUntil);
        }
        if (!closeNow) continue;
        // The sheet is about to close: what the window still holds (a
        // `leaves` younger than kFoldMs, when the grace is shorter than the
        // window, and the lines after it) is written now, so no young
        // `leaves` outlives its sheet or slips into the next one. A return
        // after this point is a new sheet's arrival, never folded across.
        // With the folder unreachable it only joins the held lines.
        acknowledge(sheet.drain(0));
        if (!stop && sheet.holding()) {
            // The session is over but its lines wait for its folder: stay,
            // and retry each tick. With no path at all there is nothing to
            // wait for: spill now.
            if (!sheet.path.empty()) continue;
            sheet.spill("session ended with nowhere to write");
        }
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

// Makes teardown() sleep `ms` after its bounded wait and before it takes
// lifeMutex: the window in which a register can start a new writer.
SEAM_HUB_API void seam_hub_test_teardown_gap_ms(int32_t ms) {
    g_hub.teardownGapMs.store(ms < 0 ? 0 : ms, std::memory_order_relaxed);
}

// 1 while a writer thread is alive (working or lingering), 0 once it exited.
SEAM_HUB_API int32_t seam_hub_test_writer_alive(void) {
    std::lock_guard<std::mutex> lk(g_hub.ctlMutex);
    return g_hub.running ? 1 : 0;
}

} // extern "C"
