# calbus — the peer-aware calibration bus

`calbus` is shared state for the STONE auto-calibration system (Spec 2).
**Emitters** (`multipink`, `ltglide`) announce what they are currently
playing; a **receiver** (`strx`, so far) reads those announcements. Nothing
here changes what any plugin sounds like — the bus is an observer, not a
signal path.

If you have never seen this codebase before: start with `seam_calbus.h`. It
is the contract, and every function and struct field carries a comment
explaining *why*, not just *what*. This README is the map; the header is the
territory.

## Why a dylib, not a header

The single most important fact about calbus: **static state does not cross
`.vst3` module boundaries.**

`multipink_pool` (`plugins/multipink/source/multipink_pool.h`) is a
process-global allocator implemented as a plain header with a `static`
inside it, and that works — but only because every `multipink` instance in
the host loads from the *same* `multipink.vst3` bundle, so they all link the
same static. `multipink`, `ltglide` and `strx` are three *separate* bundles.
If calbus were a header-only class the same way `multipink_pool` is, each of
those three `.vst3` files would compile its own private copy of the "shared"
state. There would be no compile error, no link error, no runtime error —
just three plugins silently talking to three different registries, each one
convinced it is the only listener. That failure mode does not announce
itself; it just never works, and nothing in the build tells you why.

So calbus is compiled once, into `libseamcalbus.dylib`, and all three
plugins `dlopen` the *same* file at runtime. One dylib, one process-wide
`static SeamCalbus g_bus` (`seam_calbus.cpp:128`), one registry, no matter
how many `.vst3` bundles are talking to it.

### Why the VST3 SDK doesn't already solve this

It isn't that nobody thought about cross-plugin communication — VST3
deliberately does not offer it, because instances are assumed independent.
The two pieces of the SDK that look like they might help both turn out to
work at a different scope than "let two plugin instances loaded from
different bundles talk to each other":

- `IConnectionPoint` / `IMessage`
  (`pluginterfaces/vst/ivstmessage.h:37`: "Messages are sent from a VST
  controller component to a VST editor component and vice versa.") connects
  the processor and controller halves of **one instance**, routed through
  the host. It has no notion of a second, unrelated instance, let alone one
  living in another bundle.
- `UpdateHandler` (`base/source/updatehandler.h`) lives in `base/`, which is
  statically linked into every plugin's binary. The suite has fifteen
  plugins under `plugins/` today (everything except `_common/`), so a
  running host with all of them loaded is carrying **fifteen distinct
  `UpdateHandler` singletons** in one process — each plugin's copy knows
  only about objects registered from inside that same bundle.
- `base/thread/` (`fcondition.h`, `flock.h`) offers a lock and a condition
  variable — blocking primitives for synchronizing *within* a component, not
  a channel for reaching another one.

None of this is a gap waiting to be filled by a newer SDK version. VST3's
model is that plugin instances are independent black boxes coordinated only
through the host graph. Peer awareness — one instance needing to know a fact
that another instance owns — is a deliberate anomaly here, justified because
calibrating a physical loudspeaker array (STONE) is a fact about the room,
not about the host's signal graph. The bus exists because the problem is
real and the SDK correctly declines to solve it for you.

### Why the bus is a plain C ABI

`seam_calbus.h` is `extern "C"`. C++ has no stable ABI across separately
compiled binaries — name mangling, vtable layout, STL container layout and
exception-handling ABI can all differ between compiler versions or flags,
and the plugins loading this dylib are built as separate CMake targets that
may drift over time. A plain C function/struct surface (fixed-width types
from `<stdint.h>`, POD structs, no STL types crossing the boundary) is the
only thing all three bundles can agree on regardless of how each was built.

## `multipink_pool` stays separate — do not fold it into calbus

It would look tidy to merge `multipink_pool`'s bookkeeping into calbus:
both are "shared state coordinating plugin instances." They are not the same
kind of shared state, though, and merging them would break one of them:

- **`multipink_pool` is an allocator.** It *asks permission* and *receives a
  resource*: `claim(count, preferredStart, outActualStart)` can fail
  (`ClaimResult::Exhausted`), and a failed claim means the caller doesn't get
  channels. Ownership is exclusive and consequential — two instances cannot
  both believe they own slot 5.
- **calbus is a registry of announcements.** Publishing *declares a fact*
  and *asks nothing*: `seam_calbus_v1_publish()` cannot fail in any way a
  caller needs to check, and nobody is refused. If the dylib is missing,
  `multipink` still makes sound; if `multipink_pool`'s allocation fails,
  the instance is silent by construction.

If the two were merged, a `multipink` instance's ability to make sound would
end up depending on whether `libseamcalbus.dylib` is present. That is
exactly backwards — see "the bus is an observer" below — so the pool
allocates independently of whatever calbus is doing, and always will.

## The C ABI surface

Five functions, declared in `seam_calbus.h`:

| Function | Called from | Effect |
|---|---|---|
| `seam_calbus_v1_version()` | anywhere | Returns `SEAM_CALBUS_VERSION` (currently `1`). Callers must check this before touching anything else. |
| `seam_calbus_v1_get()` | anywhere | Returns the one process-wide `SeamCalbus*`. Never null. |
| `seam_calbus_v1_register(bus)` | `setActive(true)` | Claims a slot, mutex-guarded. Returns an opaque handle, or `-1` when all 32 slots are taken. |
| `seam_calbus_v1_unregister(bus, handle)` | `setActive(false)` | Releases a slot, mutex-guarded. Safe with `handle == -1` or a stale handle (no-op). |
| `seam_calbus_v1_publish(bus, handle, rec)` | `process()` / `processBlock()`, i.e. the **audio thread** | Wait-free overwrite of one slot's record. See the seqlock section below. |
| `seam_calbus_v1_snapshot(bus, out, maxCount)` | a GUI timer | Copies every currently-registered slot's record into `out`. Returns the count written. |

`register`/`unregister` take a `std::mutex` (`SeamCalbus::regMutex`,
`seam_calbus.cpp:123`) because registration is rare (plugin
activate/deactivate) and can tolerate blocking. `publish`/`snapshot` never
take that mutex — see the seqlock section.

Client code never calls these five functions directly. `seam_calbus_client.h`
(below) wraps them.

## The handle is an opaque token, not a slot index

`seam_calbus_v1_register()` does not return a slot index. It returns a
32-bit value that packs the slot's **registration epoch** (`gen`) above the
5-bit slot index (`encodeHandle()`, `seam_calbus.cpp:107`). This is what
lets `publish()` and `unregister()` reject a handle whose slot has since
been reclaimed by a different plugin, in one relaxed load and a compare — no
lock, wait-free (`seam_calbus.cpp:227-228`).

Treat the return value as opaque: store it, compare it to
`SEAM_CALBUS_NO_HANDLE`, hand it back to `publish()`/`unregister()`. Never
index anything with it, never assume it falls in any particular range beyond
"non-negative when valid."

**`SEAM_CALBUS_NO_HANDLE` is `-1`, and `0` is a valid token.** This is the
one place in this file where the API cannot save you from a mistake: if a
plugin member defaults its handle to a plain `int32_t handle_ = 0;` instead
of `SEAM_CALBUS_NO_HANDLE`, that `0` is indistinguishable from a real
registration at slot 0, epoch 0. An instance that has never called
`register()` — say, because the dylib load failed and the client is in null
mode, or because `setActive(true)` hasn't run yet — would happily call
`publish(0, rec)` from the audio thread and silently corrupt slot 0, which
might belong to a completely different plugin's `stoneId 3` announcement.
There is no assertion or sentinel value that can catch this from inside the
bus; every caller (`multipink_processor.h`, `ltglide_processor.h`,
`seam_calbus_client.h` itself) has to initialise its handle member to
`SEAM_CALBUS_NO_HANDLE` explicitly and never let it fall back to a
default-constructed `0`. This is discipline, not a guarantee — say so out
loud whenever you add a new emitter.

## The seqlock, and why not a mutex or the triple-buffer

`publish()` runs on the **audio thread**: `multipink` publishes on every
parameter change inside `process()`, and `ltglide` publishes a pass's exact
start sample from inside `processBlock()`. `snapshot()` runs on a **GUI
timer** (`strx`'s 10 Hz `CVSTGUITimer`, `strx_status.h:32`).

**Not a mutex:** if `publish()` took a lock, a GUI thread holding that lock
(inside `snapshot()`, mid-copy) could stall the audio thread that wants to
publish the next block's data. Audio thread contention against a GUI thread
is exactly what real-time code must never risk.

**Not the triple-buffer** (`seam-ltm`'s existing lock-free SPSC pattern,
used elsewhere for bulk audio→GUI data): that pattern is built for **one**
producer shipping **bulk** data to **one** consumer. calbus has the opposite
shape — up to 32 independent producers, each writing a **small**, fixed-size
record into **its own slot**, so writers never contend with each other at
all. A triple-buffer is the wrong tool for "many small independent
producers"; a per-slot seqlock is the right one.

The seqlock (`Slot` in `seam_calbus.cpp:53`, one per registry slot) gives
exactly the asymmetry this needs: the writer is never blocked — increment
`seq` to odd, write the record, increment `seq` to even, constant time,
no waiting on anyone. Only the **reader** may have to retry, and the reader
(a GUI timer) is the side that can afford to. `snapshot()` retries up to
`kMaxReadAttempts = 8` times per slot before giving up on that slot for this
call, so a pathological writer can never make the GUI timer spin forever.

Registration churn (`register()`/`unregister()`) is a second axis of change
layered on top of the seqlock: `register()`'s `memset` of a freshly claimed
slot runs through the *same* odd/even dance `publish()` uses
(`seam_calbus.cpp:168-173`), and the `gen` field lets a reader detect
"this slot changed owners entirely during my copy" as defence in depth
beyond what the `seq` parity check already catches. Read the long comments
on `Slot` and inside `snapshot()` if you need the full argument — they are
deliberately verbose because this is exactly the kind of code that looks
correct after a quick read and isn't.

## The precondition this API does not enforce

`publish()` and the matching `unregister()`, for one handle, **must never
run concurrently.** The bus cannot check this itself — enforcing it would
need its own lock on the hot audio path, defeating the point.

In practice this holds because of the VST3 lifecycle contract: `process()`
(where `publish()` lives) and `setActive(false)` (where `unregister()`
lives) never run concurrently on the same plugin instance — the host
serializes them. That external contract is what makes the precondition
true, not anything inside `seam_calbus.cpp`. If a future emitter publishes
from somewhere other than `process()`/`processBlock()`, re-derive whether
this precondition still holds before assuming it does.

## The record is discriminated: STATE vs EVENT

`SeamCalbusRecord` (`seam_calbus.h:63`) has a `kind` field
(`kSeamCalbusPink` / `kSeamCalbusGlide`) and a `union` tail. The two kinds
are not just "different plugins" — they are temporally opposite:

- **Pink is STATE.** Free-running noise with no beginning; its identity is
  the claimed slot range (`u.pink.slotStart`, `u.pink.slotCount`), and its
  reference level is computed analytically (-3 dB/oct), not measured from a
  start time. `passStartSample` would be meaningless for it — there is no
  "start."
- **Glide is EVENT.** Its precious datum is the exact sample a pass began
  (`u.glide.passStartSample`) plus everything needed to regenerate the
  sweep (`f0`, `f1`, `durationSec`, `deltaSec`, `sweepMode`, `diracMode`),
  anchored by a monotone `passCounter` so a GUI-rate reader can tell "still
  the same pass" from "a new pass with identical parameters."

Flattening these into one record — say, giving Pink a `passStartSample` that
is always `-1`, or giving Glide a `slotStart` that is always meaningless —
would let each kind's irrelevant fields carry stale or nonsensical values
that a careless reader might use anyway. The union plus `kind` discriminator
makes it a compile-time reminder: `strx_status.h:67`'s `describe()` branches
on `kind` and only ever reads the matching arm.

## `passStartSample == -1` has exactly one meaning

`-1` means **the host gave process() no valid continuous clock**
(`ProcessContext::kContTimeValid` was unset, or `processContext` was null).
It never means "the pass ended," "the transport is idle," or anything else.

This sounds obvious written down, and it was not obvious in code. An early
version of `ltglide`'s per-block bookkeeping republished the record on every
idle block by calling `publishBusRecord(-1)` unconditionally whenever the
transport was not running. That silently turned a **just-completed pass**
into something indistinguishable from a pass that was **never anchored** —
`passCounter` stayed at `N` (correct) while `passStartSample` was
overwritten to `-1` (wrong), and a receiver reading exactly then would see
"pass N, no anchor" for a pass that in fact had a perfectly good anchor a
few samples earlier. It could also flicker a false unanchored record
mid-measurement if `Wait → Idle → beginPass` landed on a block's last
sample.

This was rated a Critical bug in review and took **two commits** to
actually remove: `729e0b4` replaced the per-block idle republish with
`BusAnchor::onRunningChanged()`, which republishes the *latched* last real
anchor instead of `-1` on the run/idle edge; `9d4e310` then extracted that
edge-detection bookkeeping into `Seam::ltglide::BusAnchor`
(`plugins/ltglide/source/ltglide_dsp.h`) specifically because the fix had
shipped with zero automated coverage, and added five mutation-verified test
cases in `tests/ltglide_dsp_test.cpp` that fail if the bug is reintroduced.
If you touch `ltglide`'s bus-publishing path, or write a new EVENT-kind
emitter, read `BusAnchor`'s class comment before writing your own
run/idle bookkeeping — the reasoning is recorded there so this does not
have to be relearned.

## How to add a new emitter kind

1. Add a `kSeamCalbusYourKind` value to the `SeamCalbusKind` enum in
   `seam_calbus.h`, and a new arm in `SeamCalbusRecord`'s union if the new
   kind's data doesn't fit the existing `pink`/`glide` shapes. Keep the
   struct POD, fixed-layout, no implicit padding surprises — `seam_calbus.h`
   already reserves `_pad` for exactly this reason, and pins
   `sizeof(SeamCalbusRecord)` with a `static_assert` right after the struct
   so a field reorder that introduces new padding is a compile error for
   *every* consumer (not just when `SEAM_BUILD_TESTS` is on).
   `tests/seam_calbus_test.cpp` carries a second, narrower copy of the same
   assert, for `isFresh()`'s whole-struct `memcmp`. Update both
   `static_assert`s' expected size deliberately, in the same change that
   bumps `SEAM_CALBUS_VERSION`, if you grow the struct.
2. In the emitting plugin: include `seam_calbus_client.h`, register a slot
   in `setActive(true)` (store the handle, `SEAM_CALBUS_NO_HANDLE`-initialised
   — never a bare `0`), unregister in `setActive(false)`, and publish from
   the audio thread whenever the announced state changes. Decide up front
   whether the new kind is STATE (publish on parameter change, like Pink) or
   EVENT (publish on a discrete happening, with a monotone counter, like
   Glide) — that decision drives whether you need a `BusAnchor`-style
   edge-detector.
3. Bump `SEAM_CALBUS_VERSION` **only** if the change is not
   binary-compatible with already-built consumers (e.g. reordering existing
   fields, changing an existing field's type or meaning). Adding a new
   `kind` value and a new, unused union arm is backward compatible — old
   readers simply never see the new `kind` — so it does not require a
   version bump by itself. If you do bump the version, every already-built
   `.vst3` in the suite needs rebuilding, since the version gate
   (`seam_calbus_client.h:87`) makes an old client refuse a new dylib and
   vice versa.
4. Add the emitter to the include path and dependency in its plugin's
   `CMakeLists.txt`:
   ```cmake
   target_include_directories(${target} PRIVATE
       ${CMAKE_CURRENT_SOURCE_DIR}/../_common/calbus
   )
   add_dependencies(${target} seam_calbus)
   ```
5. Teach the receiver (`strx_status.h`'s `describe()`, or whatever reads
   `snapshot()` next) to render the new kind. `describe()` branches on
   `kind` — add an arm rather than guessing from union contents.
6. Write the failing test first (this repo's convention), covering at least:
   registration/publish/unregister round-trips through the real dylib
   (extend `seam_calbus_client_test.cpp`), and any new edge-detection logic
   in isolation (see `BusAnchor`'s tests in `tests/ltglide_dsp_test.cpp` for
   the shape).

## Install location and search order

The CMake target (`plugins/_common/calbus/CMakeLists.txt`) builds
`libseamcalbus.dylib` and, on Apple, copies it via a `POST_BUILD` step to:

```
~/Library/Application Support/SEAM/libseamcalbus.dylib
```

The **user** domain, deliberately — no `sudo` needed, consistent with VST3
bundles themselves landing in `~/Library/Audio/Plug-Ins/VST3`.

`Seam::CalbusClient` (`seam_calbus_client.h`) searches, in order:

1. **`SEAM_CALBUS_PATH`**, if set. This is **authoritative when present —
   there is no fallback to the paths below.** A caller who points
   `SEAM_CALBUS_PATH` at a specific (possibly deliberately mismatched)
   dylib means exactly that dylib, full stop.
2. `~/Library/Application Support/SEAM/libseamcalbus.dylib`
3. `/Library/Application Support/SEAM/libseamcalbus.dylib`

The "no fallback" rule in step 1 is not an obvious design choice — it was
discovered the hard way. An earlier version of `candidatePaths()` always
appended the `HOME`/global fallbacks *after* the env-var candidate. That
looks harmless until you notice a dev machine that has ever built calbus
already has a real, version-matching dylib sitting at
`~/Library/Application Support/SEAM/`. `seam_calbus_version_test` sets
`SEAM_CALBUS_PATH` to a **stub** dylib that deliberately reports the wrong
version (`calbus_badversion_stub.cpp`, version `99`), expecting the client
to refuse it and enter null mode. With the fallback in place, the client
rejected the stub as intended — and then quietly fell through to the real
installed dylib, loaded *that* instead, and the test passed... for
completely the wrong reason (it never actually exercised the version-gate
rejection path it claimed to test). See `seam_calbus_client.h:109-120` for
the full comment. The fix: when `SEAM_CALBUS_PATH` is set, it is the *only*
candidate tried.

## Running the tests

Four test binaries live in `tests/`, driven by the top-level test suite.
If `build-test/` doesn't exist yet (it's gitignored and gets deleted during
cleanup), regenerate it first:

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-test -G Xcode \
    -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk \
    -DSEAM_BUILD_TESTS=ON
```

**Build per-target, never `ALL_BUILD`** (see the tooling-lessons section
below for why):

```bash
cmake --build build-test --target seam_calbus_test --config Release
cmake --build build-test --target seam_calbus_client_test --config Release
cmake --build build-test --target seam_calbus_version_test --config Release
```

`seam_calbus_test` links the dylib directly and can run standalone:

```bash
./build-test/tests/Release/seam_calbus_test
```

`seam_calbus_client_test` and `seam_calbus_version_test` exercise the
`dlopen` path and need `SEAM_CALBUS_PATH` pointed at the right dylib
(`tests/CMakeLists.txt` sets this via each test's `ENVIRONMENT` property —
the real dylib for `seam_calbus_client_test`, the version-99 stub for
`seam_calbus_version_test`), so run them through `ctest` rather than
invoking the binaries directly:

```bash
ctest --test-dir build-test -C Release -R 'seam_calbus'
```

All three were run against the current branch while writing this README:

```
Test project .../seam-ltm/build-test
    Start 12: seam_calbus_test
1/3 Test #12: seam_calbus_test .................   Passed    0.04 sec
    Start 13: seam_calbus_client_test
2/3 Test #13: seam_calbus_client_test ..........   Passed    0.26 sec
    Start 14: seam_calbus_version_test
3/3 Test #14: seam_calbus_version_test .........   Passed    0.51 sec

100% tests passed, 0 tests failed out of 3
```

`seam_calbus_test`'s own output is worth reading once — the two stress
tests (`seqlock: reader never observes a torn record under a hammering
writer` and `churn: reader never observes a torn or phantom record while
slots churn`) print how many reads, torn reads and churn cycles actually
happened, e.g. `hammer: reads=199984 fresh=1310 torn=0` and
`churn: reads=79223 fresh=45161 torn=0 phantom=0 finalEpoch=81448
cycles=81448`. If `cycles` (or `reads`) is suspiciously low on a future run,
the test may be passing vacuously rather than proving anything — see the
long comment above `CHECK(finalCycles > 1000)` in `seam_calbus_test.cpp`.

## The in-host verification recipe (still pending)

Unit tests prove the seqlock and the handle bookkeeping are correct in one
process. They **cannot** prove state actually crosses three separately
loaded `.vst3` bundles, because a unit test binary is not a DAW loading
three plugin bundles. That proof only exists by actually doing it, and as
of this writing **it has not been done** — this is the one item in Spec 2
still waiting on a human at a real host. Do this before considering calbus
"done," not just "built":

In Reaper:

1. Load four `multipink` instances, set their STONE parameters to 1, 2, 3,
   4, and turn POWER off on all four.
2. Load `strx` on the measurement track. Expect: `calbus: 4 idle, none
   sounding`.
3. Turn POWER on for the `multipink` on STONE 2. Expect:
   `multipink · STONE 2 · slot 4-7 · -23.0 dB` (slot numbers follow the
   actual pool claim).
4. Turn its POWER off, turn STONE 3's POWER on. Expect the line to name
   STONE 3.
5. Without turning STONE 3's POWER off, also turn STONE 2's POWER on (the
   operator-error case: two STONEs left sounding at once). Expect the line
   to still name one emitter but append a count, e.g. `... · +1 more` —
   this is the diagnostic that catches "turned on the wrong one, forgot to
   turn off the last one" in the room. Turn one of the two back off before
   continuing.
6. Remove the multipinks, load `ltglide` with STONE = 2 and Loop on, then
   press play in the host. Sounding follows the host transport (play =
   sound, stop = silent, since 219fa9e) — Loop on alone does not make
   ltglide sound. Expect: `ltglide · STONE 2 · pass N · 20000→20 Hz ·
   T=20s`, with `N` advancing once per pass.
7. Confirm the line does **not** read `no host clock`. If it does, Reaper
   is not supplying `kContTimeValid` — that's a finding for Spec 3, to be
   recorded in the session doc rather than worked around here.

Then confirm the degradation path — this is the other half of the proof,
that a missing bus never touches audio:

8. Quit Reaper, `mv ~/Library/Application\ Support/SEAM/libseamcalbus.dylib
   /tmp/`, reopen the project. Expect: `strx` reads `calbus unavailable`,
   and `multipink` and `ltglide` still make sound normally.
9. Move the dylib back.

## Two hard-won tooling lessons

**ThreadSanitizer is useless on this file — do not reach for it.** TSan
verifies against the C++ memory model's happens-before relation, which the
seqlock's fences correctly establish; TSan has no way to know that the
plain `memcpy`/`memset` of `rec` *inside* those fences is still, separately,
formally UB when a writer and a retrying reader genuinely overlap (the
same tension that makes every seqlock in the Linux kernel technically UB
under the ISO model too — see the long comment above `struct Slot` in
`seam_calbus.cpp`). TSan models the fences, sees them ordered correctly, and
stays silent — even on a build where the invariant is provably broken. The
thing that actually catches tearing here is the behavioural cross-field
probe in `seam_calbus_test.cpp`: `makeProbe()`/`probeIsConsistent()` derive
every field of a record from one counter, spread across the struct's head,
middle and union tail, so a torn read breaks the cross-field invariant even
when each individual field still looks plausible in isolation. Trust that
probe, not a sanitizer, for this file.

**Build per-target, not `ALL_BUILD`.** An unscoped `cmake --build build-test
--config Release` (i.e. building the default `ALL_BUILD` target) hit an
Xcode parallel-build race: multiple plugin targets' codesign steps
contended over the shared `~/Library/Audio/Plug-Ins/VST3` symlink that
`smtg_add_vst3plugin`'s post-build step points at, non-deterministically.
Building one named target at a time
(`cmake --build build-test --target <name> --config Release`) avoids the
race entirely and is also just faster when you only need one binary.
