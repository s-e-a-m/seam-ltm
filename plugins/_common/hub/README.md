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
| `seam_hub.h` | The C ABI: structs with pinned sizes, the eleven `seam_hub_v1_*` functions. |
| `seam_hub.cpp` | The implementation: registry, seqlock slots, proposal store, session writer. |
| `seam_hub_client.h` | What the plugins include: loads the dylib, adapts the C ABI, degrades to null mode. |
| `seam_hub_sheet.h` | The sheet grammar and its pure formatter (no I/O). |
| `CMakeLists.txt` | Builds `libseamhub.dylib` and installs it under `~/Library/Application Support/SEAM`. |

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
`register(name, says, hears)` returns a handle; `says` and `hears` are masks of `SEAM_HUB_BIT(type)` (`seam_hub.h`), declaring which message types the member emits and which it listens to.
There are at most `SEAM_HUB_MAX_MEMBERS` = 32.

The handle is opaque.
It packs the slot's registration epoch (`gen`) above the 5-bit slot index (`encodeHandle` in `seam_hub.cpp`), so that `publish_state` can reject a handle whose slot has since been reclaimed, in one relaxed load and a compare, with no lock.
Never index anything with it.

**`SEAM_HUB_NO_HANDLE` is `-1`, and `0` is a valid token.**
The API cannot save you here: a member that defaults its handle to `0` looks registered at slot 0, and would publish into whatever plugin owns that slot.
Every handle member must be initialised to `SEAM_HUB_NO_HANDLE` explicitly (`hubHandle_` in `multipink_processor.h` and the other two emitters do).
This is discipline, not a guarantee; say it out loud when you add an emitter.

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

### Lifecycle, per receiver

Each receiver sees each proposal move through `kSeamHubArrived` → `kSeamHubShown` → `kSeamHubApplied` or `kSeamHubIgnored`.
`transition()` returns 0 on success, and a negative code when the proposal is unknown (-1), not heard by that receiver (-2), an illegal step (-3), or a decision without the operator (-4).

**Applying or ignoring requires `byOperator = 1`.**
No member acts on what it hears by itself: a proposal changes a loudspeaker's equalisation, and that change must be a person's decision, taken at the screen.
The refusal is checked before legality, so it holds in every state.
A table showing itself (`Arrived` → `Shown`) needs no operator; it is only the presentation.
The lifecycle row is tied to the receiver's registration epoch, so a newcomer in a freed slot starts at `Arrived` and never inherits its predecessor's decisions.

## The session sheet

The hub keeps a text **sheet** of the session, one event per line, readable by a person and by `tools/hub-sheet.py`.
It is always written, whether or not any detector is loaded: the record of a calibration should not depend on what happened to be running.

The grammar and the formatter are `seam_hub_sheet.h`; its top comment is the reference (event line, continuation rows, verbs `arrives leaves state says shows applies ignores sheet`).
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

### The folder and the conf

The next sheet goes to the folder named by `sheet-folder = <path>` in `~/Library/Application Support/SEAM/hub.conf` (`~` is expanded), else `~/Documents/SEAM/sheets`.
The conf is re-read at every sheet start, so a change takes effect on the next sheet.
`SEAM_HUB_CONF` overrides the conf path, so tests and odd setups never touch the operator's.
With `HOME` unset there is no default folder, and the sheet is treated as unreachable instead of being written relative to the host's working directory.
`sheet_set_folder()` rewrites the conf by `rename()`, so the writer can read it without a lock.
`sheet_status()` reports the path, whether it is writing, the pending lines and the dropped count; `sheet_new()` asks for a fresh sheet.

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
2. In the plugin: include `seam_hub_client.h`, register in `setActive(true)` with the right `says`/`hears`, initialise the handle to `SEAM_HUB_NO_HANDLE`, and unregister in `setActive(false)`.
   Publish states from `process()` only through `publishState`.
3. Bump `SEAM_HUB_VERSION` only when an existing struct changes shape or meaning; adding an enum value to a new arm does not need it.
4. Test the pure decision logic SDK-free (the `BusAnchor` and `HubWatch` tests are the model), and the hub through its tests in `tests/` (`seam_hub_*_test`).

## Building and installing

`cmake --build <tree>` builds `libseamhub.dylib`, universal like the plugins (`smtg_target_setup_universal_binary`), because an x86_64-only dylib would be refused by an arm64 host and the plugins would load and play with no hub, silently.
On Apple a post-build step copies it into `~/Library/Application Support/SEAM` (no `sudo`).
A release installs it by hand; see the root `README.md`.
`tools/check-minos.py` lints its minimum macOS version along with the bundles.
