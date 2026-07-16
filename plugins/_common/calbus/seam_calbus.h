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
            uint32_t diracMode;   // 0 passo, 1 gap
        } glide;
    } u;
} SeamCalbusRecord;

typedef struct SeamCalbus SeamCalbus;

// Returns SEAM_CALBUS_VERSION. Clients call this first and refuse to proceed
// on mismatch, rather than reading records with a layout they do not know.
SEAM_CALBUS_API uint32_t seam_calbus_v1_version(void);

// The process-wide registry. Never null.
SEAM_CALBUS_API SeamCalbus* seam_calbus_v1_get(void);

// Claim a slot. Returns a handle in [0, SEAM_CALBUS_MAX_SLOTS), or -1 when the
// registry is full. Takes a mutex — call from setActive, never from process().
SEAM_CALBUS_API int32_t seam_calbus_v1_register(SeamCalbus* bus);

// Release a slot. Safe with handle == -1. Takes a mutex.
SEAM_CALBUS_API void seam_calbus_v1_unregister(SeamCalbus* bus, int32_t handle);

// Overwrite a slot's record. RT-SAFE: wait-free, no allocation, no lock.
// Safe to call from the audio thread. Invalid arguments are silent no-ops.
SEAM_CALBUS_API void seam_calbus_v1_publish(SeamCalbus* bus, int32_t handle,
                                            const SeamCalbusRecord* rec);

// Copy every registered slot's record into `out`. Returns the count written.
// The reader may retry on a torn read and gives up on a slot after a bounded
// number of attempts, so a GUI timer can never spin forever.
SEAM_CALBUS_API int32_t seam_calbus_v1_snapshot(SeamCalbus* bus,
                                                SeamCalbusRecord* out,
                                                int32_t maxCount);

#ifdef __cplusplus
}
#endif
