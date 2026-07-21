//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · calbus — peer-aware calibration bus (Spec 2)
//
// Shared state for the STONE auto-calibration system. Emitters (multipink,
// ltglide) publish what they are playing; the receiver (strx) subscribes.
//
// WHY A SEPARATE DYLIB: static state does not cross .vst3 module boundaries.
// multipink_pool works because its bitmap lives inside multipink.vst3 and all
// multipink instances load from that one dylib. multipink, ltglide and strx
// are three separate bundles, so a header-only bus would give each plugin its
// own private copy — silently, with no compile error. The VST3 SDK offers no
// cross-plugin channel by design (IConnectionPoint connects the two halves of
// a single instance, through the host), so the shared ground is declared here
// explicitly instead.
//
// This is a pure C ABI: C++ has no stable ABI, and the plugins that load this
// library may one day be built with different compilers or flags.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define SEAM_CALBUS_API __declspec(dllexport)
#else
  #define SEAM_CALBUS_API __attribute__((visibility("default")))
#endif

#define SEAM_CALBUS_VERSION   1u
#define SEAM_CALBUS_MAX_SLOTS 32

// The explicit "no slot" sentinel. Use this, never a bare 0, to initialise a
// handle that has not registered yet: 0 is index 0 / gen 0, i.e. slot 0
// before it has ever been claimed — a value publish()/unregister() will
// happily accept. A handle that was actually returned by register() has
// gen >= 1 (register() bumps gen before handing the handle out) and, more
// specifically, a live slot's gen is always ODD (gen alternates parity on
// every register/unregister transition), so a zero-initialised handle can
// never collide with a real one. See publish()'s parity guard below.
#define SEAM_CALBUS_NO_HANDLE (-1)

// Emitter kind. The kind IS the calibration stage: Pink measures power amp +
// STONE + room (it bypasses the encoder/decoder chain), Glide measures the
// full chain above already-calibrated amps. A separate "stage" field would be
// redundant state able to contradict itself.
typedef enum {
    kSeamCalbusPink  = 1,
    kSeamCalbusGlide = 2
} SeamCalbusKind;

// One emitter's announcement. POD, fixed layout, no padding surprises.
//
// The two emitters have opposite temporal natures and one flattened record
// would betray both. Pink is STATE: free-running noise with no beginning,
// identity carried by the claimed slot range, reference computed analytically
// (-3 dB/oct) — passStartSample would be meaningless. Glide is EVENT: its
// precious datum is the exact pass start plus everything needed to regenerate
// the waveform.
typedef struct SeamCalbusRecord {
    uint32_t kind;        // SeamCalbusKind
    uint32_t stoneId;     // 1..8; 0 = undeclared
    uint32_t active;      // claimed && !muted
    uint32_t _pad;        // explicit: keeps the union 8-byte aligned
    double   levelDb;
    double   sampleRate;
    union {
        struct {
            int32_t slotStart;
            int32_t slotCount;
        } pink;
        struct {
            // Monotone, +1 per pass. Load-bearing: the receiver samples at GUI
            // rate, so without a counter it cannot tell "the same pass as
            // before" from "a new pass with identical parameters". Spec 3
            // averages passes, which makes counting them a requirement.
            uint64_t passCounter;
            // ProcessContext::continuousTimeSamples anchor of the head Dirac.
            // -1 when the host does not provide a valid continuous clock.
            int64_t  passStartSample;
            double   f0, f1, durationSec, deltaSec;
            uint32_t sweepMode;   // 0 linear, 1 exponential
            uint32_t diracMode;   // 0 step, 1 gap
        } glide;
    } u;
} SeamCalbusRecord;

// This layout IS the ABI: SEAM_CALBUS_VERSION and the seam_calbus_v1_*
// symbol names are the whole contract a client checks before trusting a
// dylib's records (seam_calbus_v1_version(), above). That machinery only
// works if the layout and the version change together — a client that sees
// a matching version is trusting that SeamCalbusRecord means what it meant
// when that version was cut. A field reorder or a new member that changes
// sizeof/alignment without bumping SEAM_CALBUS_VERSION is exactly the bug
// the version gate cannot catch, because the gate never inspects the layout
// itself, only the number a (possibly stale) dylib chooses to report.
//
// Pinning the size here, in the header every consumer includes, turns that
// mistake into a compile error for all of them — not only when
// SEAM_BUILD_TESTS happens to be on. (tests/seam_calbus_test.cpp also pins
// this, for a second, narrower reason: its isFresh() does a whole-struct
// memcmp that assumes no padding.) Update this number deliberately, in the
// same change that bumps SEAM_CALBUS_VERSION, if you ever grow or reorder
// the struct.
#ifdef __cplusplus
static_assert(sizeof(SeamCalbusRecord) == 88,
              "SeamCalbusRecord's layout is the calbus ABI; a size change "
              "must come with a SEAM_CALBUS_VERSION bump (see comment above)");
#endif

typedef struct SeamCalbus SeamCalbus;

// Returns SEAM_CALBUS_VERSION. Clients call this first and refuse to proceed
// on mismatch, rather than reading records with a layout they do not know.
SEAM_CALBUS_API uint32_t seam_calbus_v1_version(void);

// The process-wide registry. Never null.
SEAM_CALBUS_API SeamCalbus* seam_calbus_v1_get(void);

// Claim a slot. Returns an OPAQUE, non-negative handle, or -1 when the
// registry is full. Takes a mutex — call from setActive, never from process().
//
// The handle is NOT a slot index: it packs the slot's registration epoch
// alongside the index, which is what lets publish() and unregister() reject a
// handle whose slot has since been reclaimed by another plugin. Store it,
// compare it to -1, hand it back — never index anything with it, never assume
// a range beyond "non-negative".
SEAM_CALBUS_API int32_t seam_calbus_v1_register(SeamCalbus* bus);

// Release a slot. Safe with handle == -1. A stale handle (already released,
// slot since reclaimed) is a no-op and cannot evict the current owner.
// Takes a mutex.
SEAM_CALBUS_API void seam_calbus_v1_unregister(SeamCalbus* bus, int32_t handle);

// Overwrite a slot's record. RT-SAFE: wait-free, no allocation, no lock.
// Safe to call from the audio thread.
//
// PRECONDITION THIS API DOES NOT ENFORCE: for a given handle, calls to
// publish() and the matching unregister() must never overlap. The VST3
// contract is what supplies this in practice — process() (where publish()
// lives) and setActive(false) (where unregister() lives) never run
// concurrently on the same plugin instance — but that contract is external
// to this bus and worth stating here explicitly.
//
// What the epoch check below actually buys: it stops an ALREADY-STALE
// handle from writing AGAIN, forever, once its slot has been reclaimed. It
// does NOT serialise a publish() that is already past the check when the
// epoch closes — that call still writes. So "invalid or stale handles are
// silent no-ops" is true only for handles that were already stale before
// the call started; it is not a substitute for the non-overlap precondition
// above, and it never turns two genuinely concurrent writers on one slot
// into a safe interleaving.
SEAM_CALBUS_API void seam_calbus_v1_publish(SeamCalbus* bus, int32_t handle,
                                            const SeamCalbusRecord* rec);

// Copy every registered slot's record into `out`. Returns the count written.
// A record is only emitted when the slot stayed registered to one single
// owner for the whole copy, so a snapshot never mixes two emitters' data and
// never attributes a departed emitter's record to its successor.
// The reader may retry on a torn read and gives up on a slot after a bounded
// number of attempts, so a GUI timer can never spin forever.
SEAM_CALBUS_API int32_t seam_calbus_v1_snapshot(SeamCalbus* bus,
                                                SeamCalbusRecord* out,
                                                int32_t maxCount);

#ifdef __cplusplus
}
#endif
