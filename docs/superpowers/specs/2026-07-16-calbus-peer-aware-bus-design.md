# Spec 2 — Calibration bus (peer-aware infrastructure)

Date: 2026-07-16
Author: Giuseppe Silvi + Claude
Status: design approved, ready for implementation planning

Part of the STONE auto-calibration system.
See the spec map in `doc/study/sessions/2026-07-14-stone-dslar.md`.
Spec 1 (STRX observation analyser) is implemented and validated in the host.

---

## 1. Purpose

Emitter plugins publish what they are currently playing.
The receiver (STRX) subscribes and identifies which emitter is sounding.

This is infrastructure.
Spec 3 consumes it to compute the transfer function `H = Y/X`.
Spec 2 ships one user-visible feature — a status line in STRX — which doubles as the diagnostic tool for the bus itself.

## 2. The room, as it actually works

Four STONEs, each driven by one multipink instance with four decorrelated slots (slot starts 0, 4, 8, 12).
The measurement point is fixed and identical for every STONE.

**Pink stage.** One multipink is active at a time.
Its signal reaches the drivers directly and bypasses the encoder/decoder chain.
The resulting curve therefore describes STONE + power amp + room + position, which is exactly the domain of the filters integrated in the power amp.
This resolves the "where does the corrective EQ live" question left open in the session doc: for this stage it lives on the amp, because the measurement contains nothing else.

**Glide stage.** multipink comes out, and ltglide feeds the complete chain: mono → m2xhgr (AmbiX) → bamodulex (tetra) → power amps already equalised by the pink stage.
Each STONE has its own complete chain, and one chain is active at a time.
The behaviour is impulsive and pitched, so STRX can extract both frequency and time information about the room and the chain.

The two stages are **cascaded, not alternative**: the pink measurement is by construction an input to the glide measurement.
Identity is always declared explicitly, never inferred.

## 3. The constraint that shapes everything

Static state does not cross module boundaries.

`multipink_pool` works because its bitmap lives inside `multipink.vst3`, and every multipink instance is loaded from that single dylib.
The root `CMakeLists.txt` calls `smtg_add_vst3plugin` once per plugin, so multipink, ltglide and strx are three separate bundles with three separate copies of any `static`.
A header-only bus modelled on the pool would give each plugin its own private bus, and the receiver would see nothing — silently, with no compile error.

The VST3 SDK offers no remedy, by design.
`IMessage`/`IConnectionPoint` (`pluginterfaces/vst/ivstmessage.h:37`) connects the processor and controller halves of a *single* instance through the host.
`UpdateHandler` lives in `base/`, which is statically linked into every bundle, so the suite already carries fifteen distinct UpdateHandler singletons in one process.
`base/thread/include/` provides `flock.h` and `fcondition.h` only: blocking primitives, nothing lock-free.
VST3 assumes instances are independent and that everything flows through the host.
Peer awareness is an anomaly against that model, adopted deliberately, because calibrating a STONE is a physical fact that lives outside the host graph.

## 4. Architecture

A fourth artefact joins the three plugins: `libseamcalbus.dylib`, a new CMake target under `_common/calbus/`.
It sits beside `seam_meter.h` and `seam_quadrature.h`, which stay header-only; this one compiles exactly once, which is its entire reason to exist.

**`seam_calbus.h`** — pure C ABI across the boundary.
No `std::` types, no classes, no exceptions cross it, because C++ has no stable ABI and the three plugins may one day be built with different compilers or flags.

**`seam_calbus_client.h`** — header-only C++ RAII wrapper included by each plugin.
It loads the dylib on first instance via `FDynLibrary` (`base/source/fdynlib.h`, the SDK's own dynamic-loading wrapper, which already handles refcounting and portability), resolves the symbols, and exposes a decent API to plugin code.
When loading fails it enters null mode: every call becomes a silent no-op.

**Search order.** `SEAM_CALBUS_PATH` (tests) → `~/Library/Application Support/SEAM/` → `/Library/Application Support/SEAM/`.
User domain first means installation needs no `sudo`, consistent with VST3 bundles already installing to `~/Library/Audio/Plug-Ins/VST3`.

**Lifetime.** The OS guarantees a single copy per process and refcounts it: the state lives as long as at least one plugin holds it open, and vanishes when the last one leaves, which is the correct behaviour.

**Roles.** multipink writes, ltglide writes, strx reads.

**`multipink_pool` stays where it is and keeps its job.**
The pool is an *allocator*: it asks permission and receives a resource.
The bus is a *registry of announcements*: it declares a fact and asks for nothing.
The pool keeps deciding which slots belong to an instance; the bus reports the outcome of that decision to whoever listens.
Merging them would look DRY and would couple allocation to observation, leaving the pool unable to function without the dylib.

## 5. Data model

One registry, fixed array of 32 slots, POD layout, no runtime allocation.

```c
typedef enum { kSeamCalbusPink = 1, kSeamCalbusGlide = 2 } SeamCalbusKind;

typedef struct {
    uint32_t kind;        // SeamCalbusKind
    uint32_t stoneId;     // 1..8, 0 = undeclared
    uint32_t active;      // claimed && !muted
    double   levelDb;
    double   sampleRate;
    union {
        struct { int32_t slotStart, slotCount; } pink;
        struct {
            uint64_t passCounter;      // monotone, +1 per pass
            int64_t  passStartSample;  // continuousTimeSamples anchor, -1 = unavailable
            double   f0, f1, durationSec, deltaSec;
            uint32_t sweepMode, diracMode;
        } glide;
    } u;
} SeamCalbusRecord;
```

The two emitters have opposite temporal natures, and one flattened record would betray both.

**Pink is state.** Free-running noise with no beginning, identity carried by the claimed slot range, reference computed analytically (−3 dB/oct).
The receiver needs "active, level L, slots [s..s+n)" and nothing more.
`passStartSample` has no meaning for pink.

**Glide is event.** `PassTransport` (`plugins/ltglide/source/ltglide_dsp.h:81`) runs `HeadDirac → Lead → Glide → Tail → TailDirac → Wait`, and its precious datum is the exact pass start plus the parameters needed to regenerate the waveform.

`passCounter` is load-bearing.
The receiver samples at GUI rate, so without a monotone counter it cannot distinguish "the same pass as before" from "a new pass with identical parameters".
Spec 3 averages passes, which makes counting them a requirement.

**No calibration-stage field.** The stage *is* the `kind`: pink measures amp + STONE + room, glide measures the full chain above already-calibrated amps.
A separate field would be redundant state able to contradict itself.

**Changes to existing plugins.** One new stepped parameter `kParamStoneId` on multipink and on ltglide, range 0–8, default 0.
Declared by hand; never inferred from slot start or from routing.
The default is 0 = undeclared, and STRX renders it as `STONE ?`, so an instance that was never told which STONE it drives says so instead of claiming to be STONE 1.

## 6. Concurrency — seqlock per slot

ltglide publishes pass starts from the audio thread, so `publish` must not take a mutex: it would block audio on a lock held by the STRX GUI.

This is not the case for the triple-buffer pattern, which serves a single producer shipping bulk data.
Here many independent producers each write a small record into their own slot, so writers never contend.

Each slot carries an atomic sequence counter: odd means write in progress, even means stable.
The writer increments, writes, increments — never blocked, constant time, RT-safe.
The reader reads the counter, copies the record, re-reads the counter, and retries when it changed or was odd.
The reader may spin, the writer never does, which is the asymmetry to want when the producer is the audio thread and the consumer is the GUI.

Registration and unregistration take a mutex, as `multipink_pool` does: they happen in `setActive`, outside `process`.

## 7. Data flow

**multipink.**
In `setActive(true)`, after the pool claim, register a bus slot and publish `{Pink, stoneId, active, levelDb, slotStart, slotCount}`.
Republish whenever mute, reference, trim or stone id changes; these arrive as parameter changes inside `process`, hence on the audio thread.
`active` is a conjunction that exists nowhere today: `claimedStart_ >= 0 && !mute`.
Level stays a separate field rather than folding into `active`, because multipink's reference (−23/−20/−18 dBFS) plus trim (±6 dB) never reaches silence: an "audible level" test would need a threshold nobody can justify.
The pool tracks *ownership*, so with four instances loaded and one sounding it would report slots 0, 4, 8 and 12 all claimed, and the receiver could not tell which one plays.
Publishing *activity* rather than presence is precisely the "individua quale slot sta suonando" requirement of the session doc.
In `setActive(false)`, unregister.

**ltglide.**
Register in `setActive`.
On each `beginPass` (entering `HeadDirac`), increment `passCounter` and publish with `passStartSample` = the block's `continuousTimeSamples` + the sample offset within the block.
When the transport returns to `Idle`, set `active = 0`.

`continuousTimeSamples` is an optional anchor.
The host declares its validity through the `kContTimeValid` flag, and `processContext` may be null entirely.
All of Spec 3's synchronisation rests on this field, so Spec 2 detects its absence and says so: `passStartSample = -1` and STRX prints `no host clock`.
Discovering this now, in a line of text, beats discovering it inside a Spec 3 cross-correlation returning plausible and wrong Δt values.

**strx.**
A GUI timer at ~10 Hz reads a snapshot of the registry and draws the status line:
`multipink · STONE 2 · slot 4-7 · −23.0 dB`, or `ltglide · STONE 2 · pass 7 · 20k→20 Hz · T=20s`.
For Spec 2 the reader lives on the GUI thread only; STRX's audio thread never touches the bus.
That arrives with Spec 3, and the seqlock is already built to serve it.

## 8. Degradation

No error path may stop a plugin from doing its audio work.
The bus is an observer, and an observer that breaks the instrument is unacceptable in the room.

| Condition | Behaviour |
|---|---|
| dylib absent | Client enters null mode; STRX prints `calbus unavailable`; all three plugins behave exactly as today. |
| Version mismatch | Symbols are versioned (`seam_calbus_v1_acquire`); a future `v2` declines a `v1` client instead of corrupting records with a different layout. |
| Registry full (32 slots) | `register` fails; that plugin stays silent on the bus and plays normally. |
| Host clock unavailable | `passStartSample = -1`; STRX prints `no host clock`. |

## 9. Testing

In the `tests/` target, linked directly against the dylib:

1. **Seqlock under stress** — a writer thread hammering records carrying a verifiable internal invariant, a reader validating that it never observes a torn read.
2. **Registration lifecycle** — register/unregister, including exhaustion of the 32 slots.
3. **Version rejection.**

The real cross-module proof resists automation: it is the status line in Reaper with four multipink instances loaded and one active, which must name the right one.

VST3 validator on multipink and ltglide, which now carry a new parameter.

## 10. Build and installation

A CMake rule copies `libseamcalbus.dylib` into `~/Library/Application Support/SEAM/` after the build, in the spirit of the VST3 symlink already in use.
No `sudo`, user domain.

VST3 SDK expected at `../vst3sdk`, overridable via `-DSEAM_VST3SDK_DIR=...` (local checkout: `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`).
Build generator: Xcode (`-G Xcode`).

## 11. Out of scope

Transfer-function mathematics (Spec 3).
EQ synthesis and application (Spec 4).
Any STRX audio-thread consumption of the bus.
Cross-process transport: the room runs one DAW in one process, so a POSIX shared-memory backend buys nothing today.
Should that change, the dylib's internals get replaced without touching the three plugins, which is exactly what the boundary is worth.

## 12. Decisions recorded

- Transport: runtime-loaded shared dylib (option A), over a `dlsym(RTLD_DEFAULT)` rendezvous (option B) and POSIX shared memory (option C).
  B was rejected because the winning bus lives *inside* an image: when the host unloads that bundle while other plugins still hold pointers into it, they dangle — a crash during a calibration session.
- Spec 2 proves itself through the STRX status line, which remains useful as a diagnostic afterwards.
- Both emitters publish from the start, so the pluggable "measurement source" abstraction the session doc asks for is tested against two genuinely different cases before it hardens.
