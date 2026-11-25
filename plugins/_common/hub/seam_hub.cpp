// libseamhub — members and states (Task 2). Proposals, the sheet writer and
// the real line formatter arrive in later tasks; their entry points stay stubs.
//
// The slot machinery below is ported from seam_calbus.cpp with the logic
// unchanged (Slot, encodeHandle, seqlock, epoch check, kMaxReadAttempts).
// What is new: each slot also carries the member's name, instance and masks,
// kept under the registration mutex, and a line queue for the session sheet.
#include "seam_hub.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

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
// test can see what was enqueued without racing the consumer.
constexpr size_t kMirrorLines = 256;

// Minimal private formatter, "HH:MM:SS.mmm  who  arrives  says pink  hears -".
// Task 4 replaces this with Seam::hubsheet; kept in one place so the swap is
// local.
std::string clockStamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const int ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
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
    return std::string(name) + "#" + std::to_string(instance);
}

} // namespace

struct SeamHub {
    Slot       slots[SEAM_HUB_MAX_MEMBERS];
    // Guards name/instance/masks of every slot and the open/close of epochs.
    // Never taken by publish_state.
    std::mutex regMutex;

    // Lock order: regMutex, then lineMutex. Separate from regMutex so the
    // writer draining the queue never holds up a registering plugin.
    std::mutex                lineMutex;
    std::deque<std::string>   lines;
    std::deque<std::string>   mirror;
    uint64_t                  droppedLines = 0;

    void enqueue(std::string line) {
        std::lock_guard<std::mutex> lock(lineMutex);
        mirror.push_back(line);
        if (mirror.size() > kMirrorLines) mirror.pop_front();
        if (lines.size() >= kMaxQueuedLines) { lines.pop_front(); ++droppedLines; }
        lines.push_back(std::move(line));
    }
};

// The one copy in the process. Static here is correct precisely because this
// translation unit is compiled into exactly one dylib.
static SeamHub g_hub;

extern "C" {

uint32_t seam_hub_v1_version(void) { return SEAM_HUB_VERSION; }
SeamHub* seam_hub_v1_get(void) { return &g_hub; }

int32_t seam_hub_v1_register(SeamHub* hub, const char* name, uint32_t says, uint32_t hears) {
    if (!hub) return SEAM_HUB_NO_HANDLE;
    std::lock_guard<std::mutex> lock(hub->regMutex);
    for (int32_t i = 0; i < SEAM_HUB_MAX_MEMBERS; ++i) {
        Slot& s = hub->slots[i];
        if (s.inUse.load(std::memory_order_relaxed) != 0) continue;

        // Clear the previous owner's state — a fresh slot must never expose
        // it — through the SAME seqlock publish_state() uses. members() does
        // not take regMutex around the state copy (it must not: the GUI would
        // then be able to stall on a registering plugin), so regMutex does
        // nothing to protect this memset from a concurrent reader. Only the
        // odd/even dance does.
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
        std::atomic_thread_fence(std::memory_order_release);
        s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even
        s.published.store(0, std::memory_order_release);

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

        hub->enqueue(clockStamp() + "  " + whoOf(s.name, inst) + "  arrives  says " +
                     maskNames(says) + "  hears " + maskNames(hears));
        return encodeHandle(i, g);
    }
    return SEAM_HUB_NO_HANDLE;
}

void seam_hub_v1_unregister(SeamHub* hub, int32_t handle) {
    if (!hub || handle < 0) return;
    Slot& s = hub->slots[handleIndex(handle)];
    std::lock_guard<std::mutex> lock(hub->regMutex);

    // A stale handle must not evict the slot's current owner. Checked under
    // regMutex, so gen cannot move under us.
    if (s.gen.load(std::memory_order_relaxed) != handleGen(handle)) return;
    if (s.inUse.load(std::memory_order_relaxed) == 0) return;

    hub->enqueue(clockStamp() + "  " + whoOf(s.name, s.instance) + "  leaves");

    // Close the epoch before retiring the slot: a reader that captured the old
    // gen sees it change and rejects whatever it copied.
    s.gen.store((s.gen.load(std::memory_order_relaxed) + 1u) & kGenMask,
                std::memory_order_release);
    s.inUse.store(0, std::memory_order_release);
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
    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store((start | 1u) + 1u, std::memory_order_relaxed); // -> even
    s.published.store(1, std::memory_order_release);
}

int32_t seam_hub_v1_members(SeamHub* hub, SeamHubMember* out, int32_t max) {
    if (!hub || !out || max <= 0) return 0;
    int32_t n = 0;
    for (int32_t i = 0; i < SEAM_HUB_MAX_MEMBERS && n < max; ++i) {
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
            if (s.inUse.load(std::memory_order_relaxed) == 0) continue;   // free
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
        if (!ok) continue;
        m.hasState = pub ? 1u : 0u;
        m.state = tmp;
        out[n++] = m;
    }
    return n;
}

// Test-only: the last 256 lines enqueued, newline-separated, NUL-terminated
// within `cap`. Returns the number of lines written. Declared in the tests,
// never in seam_hub.h: it is not part of the ABI.
SEAM_HUB_API int32_t seam_hub_test_recent_lines(char* out, int32_t cap) {
    if (!out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_hub.lineMutex);
    size_t pos = 0;
    int32_t count = 0;
    for (const std::string& l : g_hub.mirror) {
        if (pos + l.size() + 1 >= (size_t)cap) break;
        std::memcpy(out + pos, l.data(), l.size());
        pos += l.size();
        out[pos++] = '\n';
        ++count;
    }
    out[pos] = '\0';
    return count;
}

// Later tasks.
uint64_t seam_hub_v1_post(SeamHub*, int32_t, SeamHubProposal*) { return 0; }
int32_t  seam_hub_v1_proposals(SeamHub*, uint64_t, SeamHubProposal*, int32_t) { return 0; }
int32_t  seam_hub_v1_lifecycle(SeamHub*, int32_t, uint64_t) { return kSeamHubNone; }
int32_t  seam_hub_v1_transition(SeamHub*, int32_t, uint64_t, int32_t, int32_t) { return 0; }
void     seam_hub_v1_sheet_status(SeamHub*, SeamHubSheetStatus*) {}
int32_t  seam_hub_v1_sheet_set_folder(SeamHub*, const char*) { return -1; }
void     seam_hub_v1_sheet_new(SeamHub*) {}

} // extern "C"
