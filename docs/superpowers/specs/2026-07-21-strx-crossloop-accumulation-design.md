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
The first ~80 ms of each new pass is therefore wiped when the fold finally runs, then refills from the fast EMA-backed hold rather than a full sweep excitation.
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

One continuous LOOP run = one measurement session.
The accumulator clears at the **start of the next** glide session, not when the loop stops: the measured curve stays visible after the loop is stopped, until a new glide begins.

### Boundary detection

`shouldResetHold()` (pure, unit-tested, in `strx_calbus_digest.h`) is extended from a boolean to three outcomes:

- `None` — nothing to do;
- `SessionStart` — first pass after a non-glide state (the existing `lastPass == 0` sentinel);
- `PassBoundary` — `passCounter` increment within a session.

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

The visible curve never resets per cycle:

- pass 1 (no fold yet): building hold at α255 + faint live at α90 — today's glide rendering;
- from the first fold on (`accPasses ≥ 1`): **acc at α255 + faint live at α90**; the per-pass hold is no longer drawn, it exists only as the fold ingredient;
- loop stopped: while `accPasses ≥ 1`, keep drawing acc + live even with no glide active (the result survives stopping the loop); a new session replaces it;
- pink takeover: while a non-glide emitter (pink noise) sounds, the plain live view wins — full-alpha live spectrum only, no acc/hold overlay; the preserved accumulation returns as soon as that emitter stops (implemented as `otherActive_` in `strx_spectrum.h`).

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
