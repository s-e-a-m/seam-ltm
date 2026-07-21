# ltglide — Host-Transport-Gated Sounding

Date: 2026-07-21
Status: approved (GS, during in-host verification session)

## Problem

Today ltglide sounds when LOOP is switched on (or SHOT is pressed), independent of the host transport.
GS's in-host finding: the operator presses play in Reaper anyway, stopping/restarting Reaper makes the engine stall and restart awkwardly, and a dedicated sounding switch duplicates what the transport already expresses.

## Decision

The host transport is the one switch: **play = sounds, stop = silent**.

- Play with LOOP off → exactly one complete pass (head Dirac → lead → sweep → tail → tail Dirac), then silence until the transport stops and starts again.
- Play with LOOP on → passes repeat for as long as the transport runs.
- Stop mid-pass → the in-flight grain releases (the declick path), the transport returns to idle, the partial pass is discarded by strx by construction.
- A new play always starts a fresh pass from its beginning, never resumes mid-pass.
- **SHOT is removed** (GS decision): "one pass" is play-with-LOOP-off; a sounding path outside the transport contradicts the model.
- LOOP's meaning changes from "start sounding" to "repeat while playing"; the parameter ID and saved state are unchanged.

## Mechanism

- The gate is `ProcessContext::kPlaying`, read once per block; it is reliably provided by Reaper (unlike the continuous clock, which still reads `no host clock` — that open question stays with Spec 3, where `projectTimeSamples` is the fallback anchor).
- No process context at all → treated as not playing (silent); documented as the degraded mode.
- The processor edge-detects play: on false→true it arms the transport; `GlideTransport` gains the host-playing gate and a played-once latch so LOOP-off plays a single pass per play edge.
- Calbus publishing is untouched mechanically: `active` still follows `running()`, run-edge republish (BusAnchor) already covers the stop case.

## Interplay with the strx measurement

Transport stop → glide inactive on the bus → the strx sentinel clears → the next play is a SessionStart: every play/stop cycle is a fresh measurement session, consistent with "one continuous run with unchanged parameters = one session".
A LOOP-off single pass never folds into the accumulator (no following boundary, and the first boundary after a session start discards) — a single pass is an audition, not a measurement; this is the already-recorded roadmap note about single-shot passes.

## Out of scope

- GUI restructure beyond removing the SHOT button and its label (the two-column L layout belongs to the UI-standard revision).
- Continuous-clock anchoring (Spec 3).
- multipink stays host-independent: continuous noise gated by its own on/off (POWER, per the UI-standard vocabulary decision).
