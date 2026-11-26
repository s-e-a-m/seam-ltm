# hub — the suite's in-process message hub

`libseamhub` is the one place where the suite's plugins can see each other.
It replaced the earlier peer-aware bus, which carried one announcement per emitter and nothing else.
The hub carries two kinds of thing, **states** and **proposals**, and it writes a text **sheet** of everything that happened in a session.

Nothing here changes what any plugin sounds like.
The hub is an observer and a notice board, not a signal path.
Every rule below that looks defensive exists to keep that true.

If you have never seen this code: start with `seam_hub.h`.
It is the contract, and its comments say why, not only what.
This README is the map; the headers are the territory.
The files are:

| File | What it is |
|---|---|
| `seam_hub.h` | The C ABI: structs with pinned sizes and offsets, the fifteen `seam_hub_v1_*` functions. |
| `seam_hub.cpp` | The implementation: registry, seqlock slots, proposal store, session writer. |
| `seam_hub_client.h` | What the plugins include: loads the dylib, adapts the C ABI, degrades to null mode. |
| `seam_hub_sheet.h` | The sheet grammar and its pure formatter (no I/O). |
| `CMakeLists.txt` | Builds `libseamhub.dylib`, and installs it under `~/Library/Application Support/SEAM` from the DAW's tree only. |

## Why a dylib, not a header

**Static state does not cross `.vst3` module boundaries.**

`multipink_pool` (`plugins/multipink/source/multipink_pool.h`) is a process-global allocator written as a header with a `static` inside, and that works only because every `multipink` instance loads from the same `multipink.vst3`, so they all link the same static.
`multipink`, `ltglide` and `strx` are three separate bundles.
A header-only hub would be compiled three times, into three private copies of the "shared" state.
There would be no compile error, no link error and no runtime error: three plugins would talk to three different registries, each convinced it is the only listener.
Nothing in the build would say why it never works.

So the hub is compiled once, into `libseamhub.dylib`, and every plugin `dlopen`s the same file.
One dylib, one process-wide `SeamHub` (`g_hub` in `seam_hub.cpp`), no matter how many bundles talk to it.

### Why the VST3 SDK does not already solve this

VST3 declines to offer cross-plugin communication on purpose: instances are assumed independent.
The pieces of the SDK that look relevant work at another scope.

- `IConnectionPoint` / `IMessage` (`pluginterfaces/vst/ivstmessage.h`) join the processor and controller halves of one instance, through the host.
  They know nothing of a second instance, let alone one in another bundle.
- `UpdateHandler` (`base/source/updatehandler.h`) is in `base/`, which is statically linked into every plugin.
  A host with the whole suite loaded carries one `UpdateHandler` per bundle, each knowing only its own objects.
- `base/thread/` offers locks and condition variables, which synchronise inside a component and reach no other.

None of this is a gap a newer SDK will fill.
Peer awareness is a deliberate anomaly here, justified because calibrating a physical loudspeaker array (STONE) is a fact about the room, not about the host's signal graph.

### Why a plain C ABI

`seam_hub.h` is `extern "C"`, with fixed-width types and POD structs.
C++ has no stable ABI across separately compiled binaries: name mangling, vtable layout, STL layout and the exception ABI can differ between compilers or flags, and the bundles that load this dylib are separate CMake targets that may drift.
Every struct has its `sizeof` pinned by a `static_assert` in `seam_hub.h`, and padding is spelled out (`_pad`).
A layout change must bump `SEAM_HUB_VERSION`, so an old plugin never reads a new struct through the wrong shape.
Version 1 was amended once, on 2026-11-25, before anything outside its branch depended on it (see the comment on `SEAM_HUB_VERSION`); from there on every change bumps.

### The functions

| Function | Thread | What it does |
|---|---|---|
| `seam_hub_v1_version` | any | The ABI version; the client checks it before looking up anything else. |
| `seam_hub_v1_get` | any | The one hub of the process. |
| `seam_hub_v1_register` | main | A new member: name, `says`, `hears`, preferred instance; returns the handle. |
| `seam_hub_v1_unregister` | main | The member leaves. |
| `seam_hub_v1_publish_state` | audio | The member's state, wait-free (the only call allowed on the audio thread). |
| `seam_hub_v1_members` | GUI | Every member: name, instance, masks, epoch, last state. |
| `seam_hub_v1_post` | GUI | A proposal; returns its id, 0 when refused. |
| `seam_hub_v1_proposals` | GUI | The stored proposals after an id, oldest first. |
| `seam_hub_v1_lifecycle` | GUI | Where a proposal stands for the caller's own handle. |
| `seam_hub_v1_lifecycle_of` | GUI | Where it stands for any receiver, named by `member` and `epoch` (the detector). |
| `seam_hub_v1_transition` | GUI | Moves a proposal for a receiver, with an optional detail for the sheet. |
| `seam_hub_v1_recent_lines` | GUI | The last sheet lines, rows and states included, newest kept. |
| `seam_hub_v1_sheet_status` | GUI | The sheet's path, whether it is written, pending and dropped lines. |
| `seam_hub_v1_sheet_set_folder` | GUI | The folder of the next sheet, written to the conf. |
| `seam_hub_v1_sheet_new` | GUI | Closes the sheet and starts a new one. |

Exported functions named `seam_hub_test_*` are test hooks, never declared in `seam_hub.h` and not part of the ABI.

## `multipink_pool` stays separate

It would look tidy to fold `multipink_pool`'s bookkeeping into the hub.
They are not the same kind of shared state.

- `multipink_pool` is an allocator.
  It is asked for a resource and can refuse (`ClaimResult::Exhausted`); ownership is exclusive, and two instances cannot both own slot 5.
- The hub is a registry of announcements.
  Publishing declares a fact and asks for nothing; nobody is refused.

Merged, a `multipink` instance's ability to make sound would depend on whether `libseamhub.dylib` is present.
That is exactly backwards.
So the pool allocates independently of the hub, and always will.

## Members, and the handle

A **member** is one registered plugin instance.
`register(name, says, hears, preferredInstance)` returns a handle; `says` and `hears` are masks of `SEAM_HUB_BIT(type)` (`seam_hub.h`), declaring which message types the member emits and which it listens to.
There are at most `SEAM_HUB_MAX_MEMBERS` = 32.

The name is the sheet's `who` before `#<instance>`, so it is one token: 1 to 15 characters, no whitespace, or `register` refuses it with `SEAM_HUB_NO_HANDLE`.
Instances are numbered per name, the smallest free from 1.
A member that passes back the number it had (`preferredInstance`) gets it when it is free, so `strx#2` stays `strx#2` across the host's `setActive` cycles instead of swapping names with `strx#1`.
`HubClient::registerMember(name, says, hears, instance)` does this: it passes `instance` and stores the number actually given, which it finds in `members()` by the handle's `member` and `epoch` (`seam_hub_handle_member`, `seam_hub_handle_epoch`).

The handle is opaque.
It packs the slot's registration epoch (`gen`) above the 5-bit slot index (`encodeHandle` in `seam_hub.cpp`), so that `publish_state` can reject a handle whose slot has since been reclaimed, in one relaxed load and a compare, with no lock.
Never index anything with it.

**`SEAM_HUB_NO_HANDLE` is `-1`, and `0` is a valid token.**
The API cannot save you here: a member that defaults its handle to `0` looks registered at slot 0, and would publish into whatever plugin owns that slot.
Every handle member must be initialised to `SEAM_HUB_NO_HANDLE` explicitly (`hubHandle_` in `multipink_processor.h` and the other two emitters do).
This is discipline, not a guarantee; say it out loud when you add an emitter.

**A handle is registered once and always released.**
A plugin registers in `setActive(true)` only when it holds no handle, and unregisters in `setActive(false)` and again in `terminate()`.
A host may activate twice in a row, and may terminate without a last deactivation; either way, a handle overwritten or never released leaves a ghost member on the hub, in the detector and in the sheet, for the life of the process.

## States: wait-free, per-slot seqlock

A **state** is what a member is doing now: `kSeamHubPink`, `kSeamHubGlide`, `kSeamHubDecoder`.
It is overwritten in place and read by anyone.

`publish_state` is the only hub call allowed on the audio thread: `multipink` publishes on a parameter change inside `process()`, and `ltglide` publishes a pass's exact start sample from `processBlock()`.
`members()` runs on a GUI timer.

- **Not a mutex.** A GUI thread holding the lock mid-copy could stall the audio thread that wants to publish the next block.
- **Not a triple buffer.** That pattern serves one producer shipping bulk data to one consumer.
  The hub has up to 32 independent producers, each writing a small fixed record into its own slot, so writers never contend with each other.
- **A seqlock per slot** (`Slot` in `seam_hub.cpp`).
  The writer bumps `seq` to odd, writes, bumps it to even: constant time, never waiting.
  Only the reader may retry, and the reader is the side that can afford to.
  `members()` retries up to `kMaxReadAttempts` = 8 times per slot, so a pathological writer cannot make the GUI timer spin forever.

`register`/`unregister` take a mutex: they are rare and run on non-audio threads.
Registration churn is a second axis of change over the seqlock, so `register`'s reset of a slot runs through the same odd/even dance, and `gen` lets a reader detect a slot that changed owners during its copy.
The long comments on `Slot` and in `members()` carry the full argument; they are verbose on purpose.

### The precondition the hub does not enforce

For one handle, `publish_state` and `unregister` must never run concurrently.
Enforcing it would need a lock on the hot path.
It holds because of the VST3 lifecycle: `process()` and `setActive(false)` never run concurrently on one instance.
If a future emitter publishes from anywhere else, re-derive this before assuming it.

## Proposals: something one member suggests to the others

A **proposal** is a band table (`kSeamHubBandTable`) or a section list (`kSeamHubSections`): a suggestion, not a command.
`post(handle, proposal)` assigns `id`, `authorMember`, `authorInstance` and the time, stores it, and returns the id.
It returns 0 (refused) if the handle is stale, the type is out of range, or the member did not declare the type in its `says`.

The store holds the last `SEAM_HUB_STORE` = 64 proposals and evicts the oldest; ids are contiguous, so `proposals(afterId, ...)` is a simple range.
A proposal is heard by a receiver only if the type is in its `hears` and the STONE ids are compatible (`heard()` in `seam_hub.cpp`: either side undeclared, or equal).
`post` refuses state types (below 16), and fills `authorName` beside the author's member and instance, so a proposal names its author after the author has gone.
The payload is a union with a reserve arm `raw[SEAM_HUB_PAYLOAD]` (4096 bytes), room for the tetrahedral and cubic tables of the next design.
A band table carries its sample rate and what was sounding when it was saved (`emitterKind`, and `emitterSlots` for `multipink`), so a decorrelated D measurement can be told from a correlated Normal one.

### Lifecycle, per receiver

Each receiver sees each proposal move through `kSeamHubArrived` → `kSeamHubShown` → `kSeamHubApplied` or `kSeamHubIgnored`.
`transition()` returns `SEAM_HUB_OK` (0) on success, and a negative code when the proposal is unknown (-1), not heard by that receiver (-2), an illegal step (-3), or a decision without the operator (-4).
The client adds -5 (`SEAM_HUB_ERR_NO_HUB`) when there is no hub or the receiver holds no handle: nothing was recorded, so it must never read as success.
A `detail` (up to 31 characters) goes into the sheet line, so the line says what the operator did: `applies band-table #12  on bastone#1  (block common)`.
`lifecycle_of(member, epoch, id)` reads where a proposal stands for any receiver, by the `member` and `epoch` that `members()` reports; it is how the detector shows every receiver's state without holding their handles.

**Applying or ignoring requires `byOperator = 1`.**
No member acts on what it hears by itself: a proposal changes a loudspeaker's equalisation, and that change must be a person's decision, taken at the screen.
The refusal is checked before legality, so it holds in every state.
A table showing itself (`Arrived` → `Shown`) needs no operator; it is only the presentation.
The lifecycle row is tied to the receiver's registration epoch, so a newcomer in a freed slot starts at `Arrived` and never inherits its predecessor's decisions.

**The lifecycle follows the receiver's current stone.**
Whether a receiver hears a proposal is decided at each call, from the `stoneId` of the receiver's last published state.
A decoder whose stone changes from 1 to 2 stops hearing the stone-1 tables it had shown or applied: `lifecycle` then answers `None` for them, and the row recorded under stone 1 can no longer be read, even though the sheet keeps the transitions.
A receiver's window should therefore list proposals from what `lifecycle` says now, and keep its own record of what it applied (BASTOOGE keeps the applied sections in its state).
When a stone read tears eight times in a row (a publisher hammering its slot), the receiver hears nothing for that call rather than everything.

## The session sheet

The hub keeps a text **sheet** of the session, one event per line, readable by a person and by `tools/hub-sheet.py`.
It is always written, whether or not any detector is loaded: the record of a calibration should not depend on what happened to be running.

The grammar and the formatter are `seam_hub_sheet.h`; its top comment is the reference (event line, continuation rows, verbs `arrives leaves state says shows applies ignores sheet resumes`).
`tools/hub-sheet.py` is the independent parser: `--check FILE` validates a sheet, `--count FILE` counts events.
The formatter is pure and has no I/O, so the grammar can be tested without a disk.

### One writer, and why nothing else touches the disk

Only the hub's writer thread writes the sheet (`writerMain` in `seam_hub.cpp`).
Everyone else formats a line and enqueues it, bounded at `kMaxQueuedLines` = 10000; when full the oldest line is dropped and counted (`droppedLines`), because the newest lines describe the present.
No hub mutex is held across a disk access, and the audio thread never enqueues anything.
The writer ticks every `kWriterTick` = 250 ms, comparing member states and writing a `state` line for each change.

### The session rules, as built

- **A session is the life of the hub's membership.**
  The first registration starts the writer and opens a sheet; a second member joins the same sheet.
- **A 10 s grace after the last member** (`kGraceMs`).
  Hosts toggle `setActive` routinely: on a transport stop, a sample-rate change, a project reload.
  Without a grace, one member in one session would be shredded into a sheet per toggle.
  A register inside the window continues the same sheet, with no new header and no new file.
  At the end of the window the writer closes the file and its thread exits by itself.
- **Bounded waits, 2 s** (`kLastLeaveWait`).
  The last `unregister` waits at most 2 s for the writer to handle its `leaves` line, and never joins.
  The same bound applies at quit.
  Both run on the host's thread, and a hung network share must not freeze the host: after the bound the host goes on, and the line is written when the disk answers.
- **The hub is immortal, and the client never `dlclose`s it.**
  At quit, a writer still stuck in the disk is detached rather than joined.
  `g_hub` is allocated with `new` and never destroyed (`seam_hub.cpp`), so a detached writer that wakes later does not touch freed data (queues, mutexes).
  The client never `dlclose`s a library it has used (`seam_hub_client.h`), so the code the detached writer runs stays mapped.
  Both halves are needed: the first protects the data, the second the code.
  Only a library rejected before any use (version mismatch, missing symbols) is closed.

### A toggle is one line

Hosts switch a plugin off and on again without the operator asking: Reaper does it within the same millisecond.
Written as it happens, the sheet would read `leaves` then `arrives`, as if the member had gone and come back, when the host only bypassed it.
So the writer folds a `leaves` and the next event of the same `name#instance`, when that event is an `arrives` with the same declaration (`says` and `hears`) at most 1 s later (`kFoldMs`), into one line at the time of the arrival: `strx#1  resumes  after 0 ms`.
A different declaration is a real change, and both lines stay; two instances are two members.
The decision is pure and tested without a disk (`seam_hub_fold.h`); the writer applies it to its queue under `lineMutex` before every write (`foldLocked` in `seam_hub.cpp`).

To keep the sheet in order of arrival, a `leaves` younger than the window waits in the queue, and the lines after it wait with it.
That costs the host nothing.
The last `unregister` waits for its `leaves` to be written or held, and a young `leaves` held for the window counts as handled, so it never waits the second.
A sheet never keeps a young `leaves` past its own end: at quit, on `sheet_new()`, before the sheet closes and before held lines are spilled, the writer folds with no window and writes or spills everything.
A return after that is an `arrives` in the next sheet, never folded across.
A `leaves` that has been shown in the recent lines is final, and no later arrival folds it.

### Held lines belong to their sheet

When the folder cannot be written (an unmounted share, a missing volume), the lines wait in the queue, bounded as above, and `sheet_status()` reports them as pending.
The sheet keeps its path, and the writer retries it every tick; it does not exit while it holds lines, even after the grace, and a member that registers meanwhile continues the same sheet.
When the folder returns, the lines are written into that same file, after a `sheet` notice saying how many were held (and dropped).
A failed write does not start a new file either: the whole batch goes back to the queue, and the reopen truncates the file to its last good flush, so a session is never split and no line is written twice.
After a failure the writer waits 5 s before it reopens, so a volume that refuses writes (quota, full disk) is not hammered.

Held lines leave their sheet's path in three cases, and always for the same place, `sheets-held/` beside the conf (`~/Library/Application Support/SEAM/sheets-held/<the sheet's file name>`), with a header and a notice naming the original path:

- **at quit**, in the writer's last pass, still within the 2 s bound: a local folder cannot hang like a share;
- **on `sheet_new()`**: the operator has moved on, and the new sheet carries a notice saying where the old one's lines went;
- **at the end of a session whose sheet has no path at all** (no folder, or no free name), since there is nothing to wait for.

If even the local spill fails, those lines are lost: they are counted in `droppedLines`, and nothing else records them.
A sheet is never appended to another file: when every name of its second (`-2` … `-999`) is taken it has no path, and a name found taken at the first open (picked while the folder was unreachable) is picked again.

### The folder and the conf

The next sheet goes to the folder named by `sheet-folder = <path>` in `~/Library/Application Support/SEAM/hub.conf` (`~` is expanded), else `~/Documents/SEAM/sheets`.
The conf is re-read at every sheet start, so a change takes effect on the next sheet.
`SEAM_HUB_CONF` overrides the conf path, so tests and odd setups never touch the operator's.
With `HOME` unset there is no default folder, and the sheet is treated as unreachable instead of being written relative to the host's working directory.
`sheet_set_folder()` rewrites the conf by `rename()`, so the writer can read it without a lock.
`sheet_status()` reports the path, whether it is writing, the pending lines and the dropped count; `sheet_new()` asks for a fresh sheet.
`recent_lines()` returns the last 256 lines as written or held, table rows and the writer's state lines included, the newest kept when the buffer is short: the detector's "last lines".
They are fed after the fold, on the writer's tick, so the detector shows what the sheet carries: a `resumes`, never the `leaves` it replaced.

**The privacy prompt.**
`~/Documents` is protected by macOS: the first process that writes there triggers a "would like to access files in your Documents folder" prompt, per application.
The hub writes from inside whatever process loaded it, so the prompt can come from the DAW, and also from a host's separate plugin scanner or a validator, which may have no window to show it in: the write is then refused, the folder reads as unreachable, and the lines are held, and spilled to `sheets-held/` at quit.
Point `sheet-folder` outside `~/Documents` (or grant the host access in System Settings, Privacy & Security, Files and Folders) on a machine where this matters.
The suite's own build never writes there: the validator runs with `SEAM_HUB_CONF` naming a conf in the build tree (root `CMakeLists.txt`).

## The client, and null mode

Plugins include `seam_hub_client.h` and never call the C functions directly.
`HubClient::instance()` is one per `.vst3` module and loads the dylib with `dlopen`, from `SEAM_HUB_PATH` if set (it is then the only candidate, so an explicitly pointed-at library, such as the version test's stub, is the one that loads), else `~/Library/Application Support/SEAM`, else `/Library/Application Support/SEAM`.

**A missing or mismatched dylib never changes a member's sound.**
The client then enters null mode: `available()` is false, `registerMember` returns `SEAM_HUB_NO_HANDLE`, and every other call is a no-op.
A mismatch is a `seam_hub_v1_version()` that differs from the client's `SEAM_HUB_VERSION`.
The suite's plugins must keep working with no dylib at all; `strx` reports the absence on its status line and nothing else changes.

## Adding a new member or message type

1. Add the type to `SeamHubType` in `seam_hub.h` (states below 16, proposals from 16; the number is a bit position, so it stays below 32), and a new arm in the relevant union if needed.
   Pin the new `sizeof` with a `static_assert`, and update the formatter and `tools/hub-sheet.py` if it adds a verb or row shape.
2. In the plugin: include `seam_hub_client.h`, initialise the handle to `SEAM_HUB_NO_HANDLE` and keep a `uint32_t` instance at 0; register in `setActive(true)` only when the handle is `SEAM_HUB_NO_HANDLE`, passing the instance (`registerMember(name, says, hears, instance)`), and unregister in `setActive(false)` and in `terminate()`.
   Publish states from `process()` only through `publishState`.
3. Bump `SEAM_HUB_VERSION` only when an existing struct changes shape or meaning; adding an enum value to a new arm that fits in `raw` does not need it.
4. Test the pure decision logic SDK-free (the `BusAnchor` and `HubWatch` tests are the model), and the hub through its tests in `tests/` (`seam_hub_*_test`).

## Building and installing

`cmake --build <tree>` builds `libseamhub.dylib`, universal like the plugins (`smtg_target_setup_universal_binary`), because an x86_64-only dylib would be refused by an arm64 host and the plugins would load and play with no hub, silently.
On Apple a post-build step copies it into `~/Library/Application Support/SEAM` (no `sudo`), **only from the tree that feeds the DAW**: `SEAM_BUILD_PLUGINS=ON` and `SMTG_CREATE_PLUGIN_LINK=ON`.
Every plugin loaded by the DAW talks through that one file, so a tests-only tree (`build-test`) or a throwaway plugin tree (`build-hub`, links off) must not replace it, perhaps with one of another layout.
The tests never need it: they link the `seam_hub` target, or point `SEAM_HUB_PATH` at it (`tests/CMakeLists.txt`).

The SDK's validator, which loads every plugin after it is built, is turned off and run again by the root `CMakeLists.txt` with `SEAM_HUB_CONF` naming `<build>/validator-hub.conf` and `SEAM_HUB_PATH` the dylib of the same tree, so a build writes its sheets into `<build>/validator-sheets` and never among the operator's.

**A release must carry `libseamhub.dylib`.**
A release is installed by hand (root `README.md`), and the bundles do not contain the hub: without the dylib in `seam-ltm-<version>-macOS-all.zip`, `multipink`, `ltglide` and `strx` load and play in null mode, and nothing says why the exchange never happens except `strx`'s status line.
`tools/check-minos.py` lints its minimum macOS version along with the bundles.

## Open points

Minor findings from the reviews of plan 1 (`docs/superpowers/plans/2026-11-25-seam-hub.md`), left for later because none changes what an operator sees today.
They were recorded in the plan's working ledger, which is not kept; this list replaces it.

**Behaviour.**

- When a member arrives in a held sheet after the grace period or after midnight, no date marker is written.
- `sheet_new` is ignored while the writer is holding lines after the grace period with no members.
- On a volume with a quota, the writer leaves nearly empty `-N` sheets at every pass; it should back off.
- `levelDb` is printed with `%.1f` but compared exactly, so two state lines can look the same.
- The proposal lifecycle follows the receiver's current stone, so a recorded state cannot be reached after a stone change; the rivelatore's GUI has to say so.
- `stoneOf` fails open (0, hears everything) after 8 torn reads, and `members()` drops a live member after 8 torn attempts, as `calbus` did.
- `proposals(afterId = UINT64_MAX)` wraps around and returns the whole store.

**ABI and build.**

- Only struct sizes are pinned: add `offsetof` pins for `State::u`, `Proposal::u` and `Decoder::cutHz`.
- A C consumer's `static_assert` needs `<assert.h>` or `_Static_assert`.
- Windows `dllexport` on the consumer side is not handled, because the suite is macOS-only today.
- Add `static_assert(SEAM_HUB_BANDS == 31)` next to the band-name table.

**Tests and tools.**

- Not covered by tests: `rows(State)` for glide and decoder, `stateObject`, the difference block of `sectionsObject`, the lowshelf and unknown words, and the clamp when `bandCount > 31`.
- The reclaim test lacks a `hasState == 0` assertion; the stateless-member test lacks count assertions; the client test assumes one member.
- The version test cannot tell "rejected" from "stub not found"; an empty `SEAM_HUB_PATH` is not tested.
- `tools/hub-sheet.py` parses leniently (no column-width check, `# ` accepted anywhere, header not required) and prints a traceback on an unreadable file with `--check`.
- `makeGlideState` takes 12 positional arguments, and adjacent doubles can be swapped by mistake.
- The test for "applied again" depends on the 256-entry mirror; some strings are formatted under `storeMutex`.
