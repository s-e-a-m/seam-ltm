# strx — STONE observation M/S analyzer

Stereo pass-through analyzer for the STONE calibration receiver work
(see `project_stone_calibration_receiver_specs.md`). `strx` is spec (1) of
the 4-spec map: an M/S observation analyzer that never alters the signal.

## Status (Task 6)

The SDK-free DSP core (`source/strx_dsp.h`, `Seam::strx::Analyzer`) was built
and tested in Tasks 1-5: `prepare(fs)`, `reset()`, `process(L, R, n)`,
`tryReadFrame(AnalysisFrame&)`, `frame()`. It computes M/S levels,
correlation/width/panorama/vector-angle scalars, a decimated goniometer
point cloud, and Welch M/S spectra, publishing frames through a lock-free
SPSC triple buffer for the GUI thread.

Task 6 wraps that core in a loadable VST3 plugin (`Seam::StrxProcessor`):

- Stereo in / stereo out, audio passed through **unchanged** — `strx`
  observes, it does not process.
- Zero automatable parameters; `setState`/`getState` are no-ops.
- `setupProcessing` calls `analyzer_.prepare(setup.sampleRate)`;
  `setActive(true)` calls `analyzer_.reset()`.
- `process()` copies input to output verbatim, then feeds the same buffers
  to `analyzer_.process(...)`.
- `createView` returns a default `VST3Editor` over a minimal `strx.uidesc`
  (background + title only — no custom views yet).

## FAUST reference

- `seam.stereophony.lib` : `sst.sdmx` — Blumlein M/S sum-and-difference matrix.
- `seam.analyzers.lib` : `san.correlation`, `san.width`, `san.panorama`,
  `san.vectorangle`.
- `seam.analyzers.lib` : `an.mth_octave_spectral_level` (idiomatic spectrum
  equivalent to the Welch FFT used for the display curve).

## Next (Tasks 7-9)

Custom `CView`s read `Analyzer::tryReadFrame()` on the GUI thread and draw:

1. **Goniometer** (`StrxGoniometer`) — the `AnalysisFrame::gx/gy` point cloud.
2. **Spectrum** (`StrxSpectrum`) — the `specM`/`specS` Welch curves.
3. **Meters** (`StrxMeters`) — levels, correlation, width, panorama readouts.

The custom-view name tags (`kViewGoniometer`, `kViewSpectrum`, `kViewMeters`)
are already reserved in `source/strx_ids.h` so the uidesc and processor stay
in sync as those views land.
