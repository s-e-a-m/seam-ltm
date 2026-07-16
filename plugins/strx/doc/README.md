# strx — STONE observation M/S analyzer

Stereo pass-through analyzer for the STONE calibration receiver work. `strx`
is spec (1) of the 4-spec map described in
`project_stone_calibration_receiver_specs.md`: an M/S observation analyzer
that never alters the signal. See the full design spec:

- [`docs/superpowers/specs/2026-07-16-strx-observation-analyzer-design.md`](../../../docs/superpowers/specs/2026-07-16-strx-observation-analyzer-design.md)

## Status (Task 10 — feature-complete)

The SDK-free DSP core (`source/strx_dsp.h`, `Seam::strx::Analyzer`), the
plugin scaffold, and all three custom views are done and verified:

- `source/strx_dsp.h` — `prepare(fs)`, `reset()`, `analyzeScalars()`,
  `analyze()`, `process(L, R, n)`, `tryReadFrame(AnalysisFrame&)`, `frame()`.
  Computes M/S levels, correlation/width/panorama/vector-angle scalars, a
  decimated goniometer point cloud, and Welch M/S spectra, publishing frames
  through a lock-free SPSC triple buffer for the GUI thread.
- `source/strx_processor.{h,cpp}` (`Seam::StrxProcessor`) — stereo in/out,
  audio passed through **unchanged** (`strx` observes, it does not process);
  zero automatable parameters; `setState`/`getState` are no-ops.
- `source/strx_goniometer.h` (`StrxGoniometer`) — decaying M/S scatter
  (Lissajous) with an Angle/Panorama readout.
- `source/strx_spectrum.h` (`StrxSpectrum`) — overlaid M/S Welch curves on a
  log-frequency (20 Hz–20 kHz) / dB axis pair.
- `source/strx_meters.h` (`StrxMeters`) — In L, In R, M, S, Width bars.
- `resource/strx.uidesc` — the finalized three-zone layout: goniometer │
  spectrum │ meters, each zone the same 260 px height, with the SEAM logo
  parked in its own footer strip below the zone row so it never steals a
  plot's height (mirrors `plugins/dslar/resource/dslar.uidesc`).

## FAUST reference

- `seam.stereophony.lib` : `sst.sdmx` — Blumlein M/S sum-and-difference matrix.
- `seam.analyzers.lib` : `san.correlation`, `san.width`, `san.panorama`,
  `san.vectorangle`.
- `seam.analyzers.lib` : `an.mth_octave_spectral_level` (idiomatic
  SR-independent filterbank equivalent; the on-screen curve itself is a
  Welch FFT, `_common/seam_fft.h`, for finer display resolution).

## Lock-free review (Task 10, Step 3)

`strx_dsp.h::Analyzer::process()` — the sole audio-thread entry point — was
read end to end and confirmed to be safe for the real-time thread:

- **No locks.** The only synchronization primitive touched by `process()`
  is a single `std::atomic<uint8_t> back_`, accessed with `exchange()`
  (acquire/release). No mutex, no spinlock, no condition variable anywhere
  on the audio path.
- **No heap allocation.** `AnalysisFrame` (including the fixed-size
  `gx[kMaxPoints]`, `gy[kMaxPoints]`, `specM[kNumBins]`, `specS[kNumBins]`
  arrays) lives in a fixed `AnalysisFrame slots_[3]` member array allocated
  once at construction. `analyzeScalars()`/`analyze()`/`process()` only
  write into pre-sized member state (`LevelFollower`, `seam::fft::Welch`,
  the triple buffer) — no `new`, no `std::vector` growth, no string
  formatting on the audio path. (`StrxProcessor::processBlock` also
  pre-sizes its `kSample64` scratch buffers `convL_`/`convR_` once in
  `setupProcessing()`, so the float-conversion path allocates nothing per
  block either.)
- **No I/O.** No file, console, or logging calls in `process()`/`analyze()`.
- **Triple-buffer publish is a single atomic exchange.** `process()` fills
  the private `slots_[writeBuf_]` scratch slot, then publishes it with one
  `back_.exchange(writeBuf_ | kDirty, acq_rel)` and reclaims whatever index
  came back as the new `writeBuf_`. Ownership of an entire `AnalysisFrame`
  transfers atomically — there is no load-then-store gap in which the
  writer and a concurrent reader could touch the same buffer, regardless of
  thread interleaving. This was already reviewed in Tasks 3–5 as provably
  tear-free; Task 10 re-confirms the invariant still holds after the view
  wiring (Tasks 7–9) landed. `tryReadFrame()` (GUI thread) mirrors the same
  single-exchange pattern on the consumer side, and all three custom views
  read through `StrxProcessor::latestFrame()` — a single shared,
  GUI-thread-only cache — so the SPSC triple buffer is drained by exactly
  one consumer, never raced by the three view timers.

## Verification (Task 10)

- `cmake --build build --target strx --config Debug` — clean build.
- `ctest --test-dir build -R "strx_dsp_test|seam_fft_test" --output-on-failure -C Debug` — all core doctests green.
- SMTG `validator` on the built `strx.vst3` — 47/47, parity with `dslar`.

Full command output and self-review are logged in
`.superpowers/sdd/task-10-report.md` (implementation log, not shipped
documentation).

**Host-visual confirmation pending.** The functional matrix described in the
design spec (decorrelated pink noise → wide goniometer cloud / high Side
energy / Width ~100%; mono → vertical goniometer line / Side at floor /
"mono"; anti-phase `L=-R` → horizontal goniometer line / "inv") and the
on-screen legibility of the new three-zone layout were **not** visually
confirmed by the implementer — that check requires eyes on a running host
(Reaper) and is the user's to do.
