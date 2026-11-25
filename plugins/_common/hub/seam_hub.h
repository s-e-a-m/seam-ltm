//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hub — the suite's in-process message hub
//
// It replaced the suite's earlier bus, which carried one record per
// emitter. The hub
// carries two kinds of thing: STATES (what a member is doing right now,
// overwritten in place, read by anyone) and PROPOSALS (something one member
// suggests to the others, kept in a bounded store with a lifecycle per
// receiver).
//
// WHY A SEPARATE DYLIB: same reason as for the earlier bus. Static state does not cross
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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define SEAM_HUB_API __declspec(dllexport)
#else
  #define SEAM_HUB_API __attribute__((visibility("default")))
#endif

// v1 was amended once, before anything outside the branch that introduced it
// depended on it (2026-11-25: member epoch, lifecycle_of, recent_lines, the
// transition detail, the author's name, the payload reserve, the table's
// emitter fields, the preferred instance). From here on, every change bumps.
#define SEAM_HUB_VERSION        1u
#define SEAM_HUB_MAX_MEMBERS    32   // one per registered plugin instance
// The explicit "no member" sentinel. Initialise handles with this, never a
// bare 0, which is a valid registry index.
#define SEAM_HUB_NO_HANDLE      (-1)
#define SEAM_HUB_NAME_LEN       16
#define SEAM_HUB_BANDS          31   // third-octave bands of a band table
#define SEAM_HUB_MAX_SECTIONS   16   // EQ sections in one proposal
#define SEAM_HUB_STORE          64   // proposals kept; oldest evicted
#define SEAM_HUB_PAYLOAD        4096 // bytes reserved for a proposal's payload
#define SEAM_HUB_DETAIL_LEN     32   // a transition's detail, NUL included

// A handle is the member's registration epoch above its 5-bit registry index.
// It stays opaque for every purpose but one: a member that wants to find its
// OWN entry in members() (to learn the instance it was given) matches
// `member` and `epoch` against these two. Never build a handle from them.
#define SEAM_HUB_HANDLE_INDEX_BITS 5
#define SEAM_HUB_HANDLE_EPOCH_MASK 0x03FFFFFFu
static inline uint32_t seam_hub_handle_member(int32_t h) {
    return (uint32_t)h & ((1u << SEAM_HUB_HANDLE_INDEX_BITS) - 1u);
}
static inline uint32_t seam_hub_handle_epoch(int32_t h) {
    return ((uint32_t)h >> SEAM_HUB_HANDLE_INDEX_BITS) & SEAM_HUB_HANDLE_EPOCH_MASK;
}

// What transition() returns. The hub itself returns 0 or -1..-4; -5 comes
// only from the client (seam_hub_client.h), when there is no hub or the
// receiver holds no handle, so a caller can never mistake "nothing happened"
// for a recorded transition.
#define SEAM_HUB_OK              0
#define SEAM_HUB_ERR_UNKNOWN    (-1)   // no such proposal (never posted, or evicted)
#define SEAM_HUB_ERR_NOT_HEARD  (-2)   // the receiver does not hear it (type, stone, stale handle)
#define SEAM_HUB_ERR_ILLEGAL    (-3)   // not a legal step from where it stands
#define SEAM_HUB_ERR_OPERATOR   (-4)   // applying or ignoring needs byOperator = 1
#define SEAM_HUB_ERR_NO_HUB     (-5)   // client only: null mode, or no handle

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

// One member's current state. Field names are kept from the earlier bus's
// record on purpose, so the emitters ported with a rename and nothing else.
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

// emitterKind and emitterSlots say what was sounding when the table was
// saved (the emitter's state kind, kSeamHubPink or kSeamHubGlide, 0 none;
// and multipink's slotCount, 0 otherwise): a decorrelated D measurement
// (several pink slots) and a correlated Normal one look alike otherwise.
typedef struct {
    int32_t        bandCount, hasDecoder;
    double         integrationSec, sourceLevelDb;
    double         sampleRate;
    uint32_t       emitterKind;
    int32_t        emitterSlots;
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

// The payload is a union with a reserve arm, `raw`, sized for the next
// design's tetrahedral and cubic tables, so they can join v1 as new arms
// without moving a byte of the record.
typedef struct SeamHubProposal {
    uint64_t id;            // assigned by post(); 0 before
    uint32_t type, stoneId;
    uint32_t authorMember, authorInstance;   // assigned by post()
    int64_t  postedUnixMs;                   // assigned by post()
    char     authorName[SEAM_HUB_NAME_LEN];  // assigned by post()
    union {
        SeamHubBandTable table;
        SeamHubSections  sections;
        uint8_t          raw[SEAM_HUB_PAYLOAD];
    } u;
} SeamHubProposal;

typedef struct {
    uint32_t member;        // registry index
    uint32_t instance;
    uint32_t says, hears;   // SEAM_HUB_BIT masks
    char     name[SEAM_HUB_NAME_LEN];
    uint32_t hasState;
    uint32_t epoch;         // registration epoch: with `member`, names this registration
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
static_assert(sizeof(SeamHubBandTable)   == 1216, "SeamHubBandTable layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSection)     == 32,   "SeamHubSection layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSections)    == 600,  "SeamHubSections layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubProposal)    == 4144, "SeamHubProposal layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubMember)      == 128,  "SeamHubMember layout changed: bump SEAM_HUB_VERSION");
static_assert(sizeof(SeamHubSheetStatus) == 528,  "SeamHubSheetStatus layout changed: bump SEAM_HUB_VERSION");
// Sizes alone would let a field move inside an unchanged size; the offsets
// that a reader reaches first are pinned too.
static_assert(offsetof(SeamHubDecoder, cutHz)    == 16,  "SeamHubDecoder layout changed: bump SEAM_HUB_VERSION");
static_assert(offsetof(SeamHubState, u)          == 32,  "SeamHubState layout changed: bump SEAM_HUB_VERSION");
static_assert(offsetof(SeamHubBandTable, decoder) == 40, "SeamHubBandTable layout changed: bump SEAM_HUB_VERSION");
static_assert(offsetof(SeamHubProposal, u)       == 48,  "SeamHubProposal layout changed: bump SEAM_HUB_VERSION");
static_assert(offsetof(SeamHubMember, state)     == 40,  "SeamHubMember layout changed: bump SEAM_HUB_VERSION");

// Opaque: the plugins never see the hub's internals.
typedef struct SeamHub SeamHub;

SEAM_HUB_API uint32_t seam_hub_v1_version(void);
SEAM_HUB_API SeamHub* seam_hub_v1_get(void);

// register: a name of 1..15 characters with no whitespace (it is the sheet's
// `who`, one token), else SEAM_HUB_NO_HANDLE. preferredInstance 0 takes the
// smallest free instance number for the name; otherwise that number is used
// when free, so a member that passes back the instance it had keeps its name
// (strx#2 stays strx#2) across the host's setActive cycles.
SEAM_HUB_API int32_t  seam_hub_v1_register(SeamHub*, const char* name, uint32_t says, uint32_t hears,
                                           uint32_t preferredInstance);                                 // mutex
SEAM_HUB_API void     seam_hub_v1_unregister(SeamHub*, int32_t handle);                                 // mutex
SEAM_HUB_API void     seam_hub_v1_publish_state(SeamHub*, int32_t handle, const SeamHubState*);         // RT-safe
SEAM_HUB_API int32_t  seam_hub_v1_members(SeamHub*, SeamHubMember* out, int32_t max);                   // GUI
SEAM_HUB_API uint64_t seam_hub_v1_post(SeamHub*, int32_t handle, SeamHubProposal* inout);               // GUI; 0 = refused
SEAM_HUB_API int32_t  seam_hub_v1_proposals(SeamHub*, uint64_t afterId, SeamHubProposal* out, int32_t max); // GUI
SEAM_HUB_API int32_t  seam_hub_v1_lifecycle(SeamHub*, int32_t receiver, uint64_t id);                   // GUI; SeamHubLifecycle
// Any receiver's lifecycle, named by the `member` and `epoch` that members()
// reports: how an observer (the detector) reads where a proposal stands for
// each receiver without holding their handles. kSeamHubNone when that
// registration has ended or does not hear the proposal.
SEAM_HUB_API int32_t  seam_hub_v1_lifecycle_of(SeamHub*, uint32_t member, uint32_t epoch, uint64_t id); // GUI
// transition: SEAM_HUB_OK or a SEAM_HUB_ERR_* code (above). `detail` may be
// null; otherwise its first 31 characters (control characters as spaces) are
// written in the sheet line, e.g. "block common".
SEAM_HUB_API int32_t  seam_hub_v1_transition(SeamHub*, int32_t receiver, uint64_t id, int32_t to, int32_t byOperator,
                                             const char* detail);                                       // GUI
// The last sheet lines, as written or held: events, their rows and the
// writer's state lines, newline-separated and NUL-terminated within `cap`,
// the newest kept when `cap` is short. Returns the number of lines.
SEAM_HUB_API int32_t  seam_hub_v1_recent_lines(SeamHub*, char* out, int32_t cap);                      // GUI
SEAM_HUB_API void     seam_hub_v1_sheet_status(SeamHub*, SeamHubSheetStatus* out);
SEAM_HUB_API int32_t  seam_hub_v1_sheet_set_folder(SeamHub*, const char* folder);                       // 0 ok
SEAM_HUB_API void     seam_hub_v1_sheet_new(SeamHub*);

#ifdef __cplusplus
}
#endif
