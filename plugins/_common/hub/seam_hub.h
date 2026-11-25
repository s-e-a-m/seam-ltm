//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hub — the suite's in-process message hub
//
// Replaces calbus. Where calbus carried one record per emitter, the hub
// carries two kinds of thing: STATES (what a member is doing right now,
// overwritten in place, read by anyone) and PROPOSALS (something one member
// suggests to the others, kept in a bounded store with a lifecycle per
// receiver).
//
// WHY A SEPARATE DYLIB: same reason as calbus. Static state does not cross
// .vst3 module boundaries, and the plugins are separate bundles. The shared
// ground is declared explicitly here, in one dylib they all load.
//
// Pure C ABI with fixed-width types: C++ has no stable ABI, and the plugins
// that load this library may be built with different compilers or flags.
//
// LAYOUT RULE: every struct below has its sizeof pinned by a static_assert.
// A layout change MUST bump SEAM_HUB_VERSION, so an old plugin never reads a
// new struct through the wrong shape. Padding is always spelled out (_pad):
// no struct has implicit padding, so the pinned size means the same thing on
// every compiler.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define SEAM_HUB_API __declspec(dllexport)
#else
  #define SEAM_HUB_API __attribute__((visibility("default")))
#endif

#define SEAM_HUB_VERSION        1u
#define SEAM_HUB_MAX_MEMBERS    32   // one per registered plugin instance
// The explicit "no member" sentinel. Initialise handles with this, never a
// bare 0, which is a valid registry index.
#define SEAM_HUB_NO_HANDLE      (-1)
#define SEAM_HUB_NAME_LEN       16
#define SEAM_HUB_BANDS          31   // third-octave bands of a band table
#define SEAM_HUB_MAX_SECTIONS   16   // EQ sections in one proposal
#define SEAM_HUB_STORE          64   // proposals kept; oldest evicted

// What a message is. Values below 16 are states, from 16 up proposals; the
// numbers are bit positions in the says/hears masks, so they stay below 32.
typedef enum {
    kSeamHubPink = 1, kSeamHubGlide = 2, kSeamHubDecoder = 3,      // states
    kSeamHubBandTable = 16, kSeamHubSections = 17                  // proposals
} SeamHubType;
#define SEAM_HUB_BIT(t) (1u << (t))   // says/hears masks

// Where a proposal stands for one receiver.
typedef enum {
    kSeamHubNone = 0, kSeamHubArrived = 1, kSeamHubShown = 2,
    kSeamHubApplied = 3, kSeamHubIgnored = 4
} SeamHubLifecycle;

// The decoder's setting, carried both as a state and inside proposals.
typedef struct {
    uint32_t test;          // 0..4: Normal, W, X, Y, Z
    uint32_t bypass;
    uint32_t stoneOfModel;
    uint32_t _pad;
    double   cutHz;
    uint64_t commonId;      // id of the applied sections proposal, 0 none
    uint64_t differenceId;  // same, for the difference block
    uint32_t pieceOn;
    uint32_t subOn;
    double   subHz;
} SeamHubDecoder;

// One member's current state. Field names are kept from SeamCalbusRecord on
// purpose, so the emitters port with a rename and nothing else.
typedef struct SeamHubState {
    uint32_t kind;          // SeamHubType (a state type)
    uint32_t stoneId;
    uint32_t active;
    uint32_t _pad;
    double   levelDb;
    double   sampleRate;
    union {
        struct { int32_t slotStart, slotCount; } pink;
        struct {
            uint64_t passCounter;
            int64_t  passStartSample;
            double   f0, f1, durationSec, deltaSec;
            uint32_t sweepMode, diracMode;
        } glide;
        SeamHubDecoder decoder;
    } u;
} SeamHubState;

typedef struct { double hz, compDb, levelDb, settled; } SeamHubBand;

typedef struct {
    int32_t        bandCount, hasDecoder;
    double         integrationSec, sourceLevelDb;
    SeamHubDecoder decoder;
    char           source[128];
    SeamHubBand    bands[SEAM_HUB_BANDS];
} SeamHubBandTable;

typedef enum { kSeamHubPeak = 1, kSeamHubLowShelf = 2, kSeamHubHighShelf = 3 } SeamHubSectionType;

typedef struct { uint32_t type, _pad; double hz, q, gainDb; } SeamHubSection;

typedef enum { kSeamHubBlockCommon = 1, kSeamHubBlockDifference = 2 } SeamHubBlock;

typedef struct {
    uint32_t block, count;
    double   residualRmsDb, residualMaxDb, offsetDb;
    double   fitLoHz, fitHiHz, roomLimitHz;
    uint64_t fromIds[4];    // proposals this one was fitted from
    SeamHubSection sections[SEAM_HUB_MAX_SECTIONS];
} SeamHubSections;

typedef struct SeamHubProposal {
    uint64_t id;            // assigned by post(); 0 before
    uint32_t type, stoneId;
    uint32_t authorMember, authorInstance;   // assigned by post()
    int64_t  postedUnixMs;                   // assigned by post()
    union { SeamHubBandTable table; SeamHubSections sections; } u;
} SeamHubProposal;

typedef struct {
    uint32_t member;        // registry index
    uint32_t instance;
    uint32_t says, hears;   // SEAM_HUB_BIT masks
    char     name[SEAM_HUB_NAME_LEN];
    uint32_t hasState, _pad;
    SeamHubState state;
} SeamHubMember;

typedef struct {
    char     path[512];
    uint32_t writing, pendingLines;
    uint64_t droppedLines;
} SeamHubSheetStatus;

static_assert(sizeof(SeamHubDecoder)     == 56,   "SeamHubDecoder layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubState)       == 88,   "SeamHubState layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubBand)        == 32,   "SeamHubBand layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubBandTable)   == 1200, "SeamHubBandTable layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSection)     == 32,   "SeamHubSection layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSections)    == 600,  "SeamHubSections layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubProposal)    == 1232, "SeamHubProposal layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubMember)      == 128,  "SeamHubMember layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSheetStatus) == 528,  "SeamHubSheetStatus layout changed: bump SEAM_HUB_VERSION");

// Opaque: the plugins never see the hub's internals.
typedef struct SeamHub SeamHub;

SEAM_HUB_API uint32_t seam_hub_v1_version(void);
SEAM_HUB_API SeamHub* seam_hub_v1_get(void);
SEAM_HUB_API int32_t  seam_hub_v1_register(SeamHub*, const char* name, uint32_t says, uint32_t hears);  // mutex
SEAM_HUB_API void     seam_hub_v1_unregister(SeamHub*, int32_t handle);                                 // mutex
SEAM_HUB_API void     seam_hub_v1_publish_state(SeamHub*, int32_t handle, const SeamHubState*);         // RT-safe
SEAM_HUB_API int32_t  seam_hub_v1_members(SeamHub*, SeamHubMember* out, int32_t max);                   // GUI
SEAM_HUB_API uint64_t seam_hub_v1_post(SeamHub*, int32_t handle, SeamHubProposal* inout);               // GUI; 0 = refused
SEAM_HUB_API int32_t  seam_hub_v1_proposals(SeamHub*, uint64_t afterId, SeamHubProposal* out, int32_t max); // GUI
SEAM_HUB_API int32_t  seam_hub_v1_lifecycle(SeamHub*, int32_t receiver, uint64_t id);                   // GUI; SeamHubLifecycle
SEAM_HUB_API int32_t  seam_hub_v1_transition(SeamHub*, int32_t receiver, uint64_t id, int32_t to, int32_t byOperator); // GUI
SEAM_HUB_API void     seam_hub_v1_sheet_status(SeamHub*, SeamHubSheetStatus* out);
SEAM_HUB_API int32_t  seam_hub_v1_sheet_set_folder(SeamHub*, const char* folder);                       // 0 ok
SEAM_HUB_API void     seam_hub_v1_sheet_new(SeamHub*);

#ifdef __cplusplus
}
#endif
