# strx — Cross-Loop Accumulation Design

Date: 2026-07-21
Status: approved (brainstorm with GS)

## Problem

When strx observes ltglide in LOOP, every new pass clears the max-hold spectrum (`shouldResetHold()` fires on each `passCounter` increment), so the visible measurement resets each cycle and information from previous passes is thrown away.
That per-cycle visual reset is the reported malfunction.
Looping should refine the measurement instead: discard intermittent room noise and stabilize the curve as passes accumulate.

## Design

### Estimator — per-bin minimum over completed passes

`acc[k] = min` over passes of that pass's hold curve `hold[k]`, kept per channel (M and S), in dB (min in dB equals min in linear).
The system response is present in every pass; an intermittent disturbance is not, so any noise absent from even one pass is discarded entirely.
Honest limit, stated up front: stationary background noise does not go down — coherent averaging belongs to Spec 3 (transfer-function measurement).

A second, subtler honest limit: pass boundaries are detected on the GUI poll, not the audio thread.
`CalbusWatch::poll()` re-snapshots at most every ~80 ms, and the audio thread only picks up the resulting epoch bump on its next block — so every boundary is detected ~80 ms plus one block late.
The first ~80 ms of each new pass is therefore wiped when the fold finally runs; the 4096-sample analysis ring survives the reset (~85 ms at 48 kHz), so the next frames still span most of the wiped interval, Hann-attenuated at the frame edge.
This is a systematic few-dB droop at the sweep-start frequencies, and the MIN accumulator keeps it forever once folded.
Exact boundary timing is out of scope here; it belongs to Spec 3 (transfer-function measurement).

Only **completed** passes fold.
A partial pass (loop stopped mid-cycle) has unswept bins at the noise floor and would destroy the accumulated curve under MIN, so it is discarded.

A session start can itself land mid-pass: ltglide's LOOP may already be running when the editor opens, or when processing reactivates.
The pass boundary immediately following a session start is therefore of unknown origin — its hold may cover only the tail of a pass, not the whole sweep from its beginning.
Folding that partial hold under MIN would floor-pin every bin the pass never got to re-excite, for the rest of the session.
So the accumulator only folds a pass it observed from its very beginning: the first pass boundary after every session start (and after every analyzer reset) discards instead of folding.
The discard clears the hold and arms folding, contributing nothing to the accumulator; only the boundaries after that fold.
This costs one good pass in the clean workflow — glide starts, then the first full sweep is thrown away — which is irrelevant next to a multi-minute LOOP run.

### Session lifecycle

One continuous LOOP run with unchanged generation parameters = one measurement session.
The accumulator clears at the **start of the next** glide session, not when the loop stops: the measured curve stays visible after the loop is stopped, until a new glide begins.
Any change to the emitted stimulus starts a new session at the next pass boundary: level, f0, f1, sweep time, sweep mode, delta, dirac mode, STONE, or sample rate — everything ltglide publishes on the bus.
MIN over passes is only meaningful over identical stimuli; a pass measured at a different level (or frequency range, or sweep shape) is not comparable to the passes already folded, and folding it in would poison the accumulation with no way to undo it (MIN keeps whatever it sees once, in either direction).
This is GS's in-host finding: changing ltglide's Level during LOOP left stale passes at the old level baked into the MIN curve forever.
Note for the future analysis/log stages (Spec 3+), also a GS decision: generation parameters are NOT locked while a measurement is running.
Instead, every change simply resets pass counting and analysis memories, so the operator stays free to retune mid-session at the cost of restarting the measurement.

### Boundary detection

`shouldResetHold()` (pure, unit-tested, in `strx_calbus_digest.h`) is extended from a boolean to three outcomes:

- `None` — nothing to do;
- `SessionStart` — first pass after a non-glide state (the `lastPass == 0` sentinel), OR a generation-parameter change mid-run (the `GlideParams` sentinel: stoneId, levelDb, sampleRate, f0, f1, durationSec, deltaSec, sweepMode, diracMode — everything ltglide's glide record publishes);
- `PassBoundary` — `passCounter` increment within a session with unchanged parameters.

`CalbusWatch` (GUI thread) maps the outcomes onto two atomics read by the audio thread: the existing `holdEpoch` (pass boundary) plus a new `sessionEpoch` (session start).
A session start bumps **only** `sessionEpoch`.
The processor's session branch already clears both accumulation and hold, so bumping `holdEpoch` too would race a fold against the just-cleared hold.

Audio thread, once per block:

- `sessionEpoch` changed → clear acc and hold, no fold;
- only `holdEpoch` changed → fold hold into acc (per-bin min), then clear hold.

### Data and publication

The accumulator lives in `Seam::strx::Analyzer` (`strx_dsp.h`): `accM_/accS_[kNumBins]` in dB plus `passCount_` (completed passes folded).
`AnalysisFrame` grows by `accM/accS` arrays and an `accPasses` int (~16 KB × 3 triple-buffer slots — negligible) and travels on the existing SPSC triple-buffer unchanged.

### Spectrum view (`strx_spectrum.h`)

The visible curve resets exactly once per session — at the first pass boundary, when the discarded arming pass clears the hold — and never again within the session:

- pass 1 (no fold yet): building hold at α255 + faint live at α90 — today's glide rendering;
- from the first fold on (`accPasses ≥ 1`): **acc at α255 + faint live at α90**; the per-pass hold is no longer drawn, it exists only as the fold ingredient;
- loop stopped: while `accPasses ≥ 1`, keep drawing acc + live even with no glide active (the result survives stopping the loop); a new session replaces it;
- pink takeover: while a non-glide emitter (pink noise) sounds, the plain live view wins — full-alpha live spectrum only, no acc/hold overlay.
A pink takeover permanently discards the interrupted glide accumulation: `pinkTakeover()` (`strx_calbus_digest.h`) fires once on the edge where a non-glide emitter becomes active, and `CalbusWatch` bumps the session epoch on that edge, so the processor clears acc and hold before the next glide session can start (GS decision, 2026-07-21: "last measure wins" — superseding the earlier "the preserved accumulation returns" design).
When the last active emitter goes silent — the loop stopped, or the pink observation itself stops — the view freezes on the last-shown curves instead of reverting to a stale or unrelated live frame, and draws a small HELD tag so a frozen display is honest about being stale.
A new glide or pink session replaces the frozen picture as soon as it starts sounding.

### Status line (`strx_status.h`)

Appends `· PASS n` while a glide is active; `passCounter` is already in the digest, so this costs nothing and shows convergence progress.

## Testing

Mutation-verified (per feedback_verify_tests_by_mutation):

- three-outcome boundary decision in the digest helper (pure unit test, extends the existing digest tests);
- Analyzer fold/clear behavior, feeding sines between epoch signals in the style of the existing `strx_dsp_test`;
- the critical partial-pass case: stop mid-cycle → acc unchanged.

## Out of scope

- Goniometer zoom (shelved, not convinced).
- Any emitter-side change to ltglide or calbus: the published `passCounter` already suffices.
- Coherent (time-aligned) averaging — Spec 3.
