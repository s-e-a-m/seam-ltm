# VST3 State Short-Read — Suite-Wide Fix — Design

Status: approved (brainstorming, 2026-07-26).
Scope: one shared helper + five call-site fixes; no format change.
Debt tracked in memory `project_vst3_state_shortread_rotation_family`.

## The defect

Five plugins restore their state with a single bulk read:

```cpp
double saved[6];
if (state->read (saved, sizeof (saved)) != kResultOk)
    return kResultFalse;
```

`IBStream::read` reports how many bytes it delivered through its optional
`numBytesRead` out-parameter — which this call ignores. Many host stream
implementations return `kResultOk` on a partial read, so a state blob shorter
than `sizeof (saved)` sails past the check and the tail of `saved[]` is applied
as parameter values while holding **uninitialised stack memory**.

Shorter blobs are not hypothetical: they are the suite's own history.
`m2xhgr` and `lr2xhgr` gained three gain-trim fields after release, so every
session saved before the trims landed carries the shorter legacy layout.
Opening one today yields garbage trims instead of the identity the session
actually sounded like.

## Affected inventory

| Plugin | Current layout | Legacy layout in the wild | Risk |
|---|---|---|---|
| `m2xhgr` | 6 doubles (yaw, pitch, roll, trim A1–A3) | 3 doubles (pre-trim) | high — live bug |
| `lr2xhgr` | 7 doubles (divergence, yaw, pitch, roll, trim A1–A3) | 4 doubles (pre-trim) | high — live bug |
| `xyprrot` | 3 doubles | none | latent |
| `b2xrot` | 3 doubles | none | latent |
| `ddelay` | 1 double | none | latent |

The six IBStreamer-based plugins (`addelay`, `dslar`, `hilbert`, `ltburst`,
`ltglide`, `multipink`) already check each field read and are out of scope.
The five stateless plugins (`sdmx`, `strx`, `x2uhj`, `abmodulex`, `bamodulex`)
have no-op `setState` and are out of scope.

## Decisions (from brainstorming)

1. **Short state → apply + defaults.** Fields present in the blob are applied;
   missing tail fields take their parameter defaults (identity: 0 dB trims,
   centred rotation). A pre-trim session opens sounding exactly as it was
   saved. This matches the pattern `addelay` already ships
   (`addelay_processor.cpp:149`).
2. **Format stays a bare append-only double array.** The version is implicit
   in the length; new fields may only ever be appended. Legacy states need no
   special casing, and the short-read fix automatically covers every future
   growth of the layout. No header, no migration.
3. **The rule lives once, in `_common`.** A shared header keeps the five call
   sites uniform and makes the decode directly unit-testable — the lesson of
   this debt ("fix suite-wide, not piecemeal").

## Architecture

### `plugins/_common/seam_state.h` (new, header-only)

```cpp
// Reads up to `count` little-endian doubles from `state` into `values`.
// `values` must arrive pre-loaded with each field's default: fields past
// the first short read keep those defaults (append-only state rule —
// legacy states are simply shorter, never reordered).
// Returns how many fields were actually read.
inline int readStateDoubles (Steinberg::IBStream* state,
                             double* values, int count)
{
    Steinberg::IBStreamer s (state, kLittleEndian);
    for (int i = 0; i < count; ++i) {
        double v = 0.0;
        if (!s.readDouble (v))   // false ⇔ fewer than 8 bytes available
            return i;
        values[i] = v;
    }
    return count;
}
```

`IBStreamer::readDouble` passes `numBytesRead` down to `IBStream::read` and
returns `false` on an incomplete field — exactly the check the bulk read
skips. The bytes consumed for a fully present field are identical to today's
format on every platform the suite targets (little-endian).

### The five call sites

Each `setState` becomes:

1. Pre-load `saved[]` with the registered parameter defaults via
   `parameters.getParameter (id)->getInfo ().defaultNormalizedValue` —
   one source of truth, no duplicated identity constants.
2. `Seam::readStateDoubles (state, saved, N);`
3. Apply **all** fields — read or defaulted — through the existing mapping
   code, unchanged: both the VST3 parameters and the DSP member variables
   (`fYaw`, `fTrimA1Db`, `distanceMeters_`, …). A defaulted field must reset
   its member too; a legacy load is a full state, not a partial overlay.

An empty stream reads zero fields and restores full defaults with
`kResultOk`. Only `state == nullptr` returns `kResultFalse`.

Each `getState` switches to `IBStreamer::writeDouble` for symmetry.
The bytes produced are identical to today's.

## Error handling

| Input | Behaviour |
|---|---|
| `state == nullptr` | `kResultFalse`, nothing touched |
| empty stream | all defaults applied, `kResultOk` |
| legacy short blob | prefix applied, tail defaulted, `kResultOk` |
| blob truncated mid-field | complete fields applied, partial field ignored, `kResultOk` |
| longer blob than expected (newer plugin's state) | first N fields applied, `kResultOk` |

## Testing

New ctest `seam_state_test` (SDK `MemoryStream`; precedent: the calbus tests).

| Case | Assertion |
|---|---|
| full round-trip (write N, read N) | returns N, values exact |
| legacy 3 → 6 | returns 3, fields 3–5 keep pre-loaded sentinel defaults |
| empty stream | returns 0, all sentinels intact |
| 3 doubles + 4 stray bytes, read 6 | returns 3, partial field never applied |
| write 9, read 6 | returns 6, values exact |

The sentinel pre-load is the canary: it proves missing fields hold the
caller's defaults, never stack garbage. Every test is verified by mutation —
reverting the helper to a bulk `state->read` must turn the suite red
(memory `feedback_verify_tests_by_mutation`).

The processor call sites do not link into the test binaries; they are covered
by the VST3 validator (already part of the build) exercising the
`getState`/`setState` round-trip, plus the full ctest suite.

## Out of scope

No state-format migration, no version headers, no changes to the six
IBStreamer plugins or the five stateless ones, and no refactoring of the
processors beyond their `setState`/`getState` blocks.
