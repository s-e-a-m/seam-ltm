# multipink — VST3 Plugin Design Spec

**Date:** 2026-05-07
**Author:** Giuseppe Silvi (with Claude)
**Repo:** `s-e-a-m/seam-ltm`
**Status:** Approved — pending implementation plan

---

## 1. Identity & Purpose

`multipink` is a multichannel pink-noise generator VST3 plugin for the SEAM-LTM
suite. Its purpose is calibration of multichannel speaker systems (Stone arrays,
ambisonic rigs, cinema setups) with RMS-anchored output levels suitable for SPL
metering with a pressure-level meter.

It replaces the user's previous "agricultural" workflow of pre-baked
`multipink1`, `multipink5`, `multipink7`… variants (each a different DSP file
with a different seed) with a single plugin that:

- adapts to the host track's channel layout (mono → 64 channels);
- guarantees decorrelation between concurrent instances via a shared logical
  pool of 64 channels;
- produces RMS-calibrated output referenced to broadcast/cinema standards.

## 2. DSP Core

The DSP is a thin Faust wrapper around the existing
`sno.multipink(N, g)` from `seam.noise.lib` (which itself is built on
`no.multinoise(N) : par(i,N,no.pink_filter)`).

**Channel count.** Faust DSP graphs are static. Each plugin instance compiles a
generator for the **maximum N = 64**, but emits only the N channels
corresponding to its claimed slot range.

**Logical pool semantics.** Because Faust's `multirandom`/`multinoise` is
deterministic for a given seed, the samples produced at slot index `k` by any
two instances using the same seed are bit-identical. A plugin instance that
claims slots `[a, b]` therefore emits the *exact* samples that a single 64-ch
master generator would emit on those same indices. The "shared pool" is logical
rather than physical: no inter-instance audio communication, no thread
synchronization, but the audible result is indistinguishable from a true shared
generator.

**RMS calibration.** The Faust `pink_filter` (Paul Kellet IIR, 4th order)
attenuates the input white noise. To make the knob's "0 dB" position correspond
to a real reference RMS level, a one-time offline calibration measures the RMS
of the unscaled `pink_noise` output (via `faust2octave` on a long buffer) and
the resulting offset is hard-coded as a constant `kCalibrationOffsetDb` in the
DSP wrapper.

## 3. Bus Configuration

One output bus, no input bus (instrument/generator type).

`IAudioProcessor::setBusArrangements` accepts standard speaker arrangements up
to 64 channels:

- `kMono`, `kStereo`, `kQuad`, `k5_1`, `k7_1`, `k7_1_4`, `k22_2`
- Custom arbitrary channel counts up to 64 (Reaper allows this via track
  channel count).

Rejects requests for >64 channels.

## 4. Slot Allocator (Peer Awareness)

### 4.1 Shared State

In the `.vst3` module (one bundle, shared across all instances within a host
process):

```cpp
static std::atomic<uint64_t> g_claimed_bitmap{0};   // bit i = slot i taken
static std::mutex            g_alloc_mutex;
```

### 4.2 Claim / Release

On `setActive(true)`:

1. Lock `g_alloc_mutex`.
2. If the instance state contains a previously-saved `claimed_start_slot` and
   the contiguous range `[start, start+N)` is free in the bitmap, claim it
   (preferred path — preserves calibration identity across DAW reloads).
3. Otherwise perform **first-fit contiguous** search for N free bits and claim
   that range. Mark the instance with status `FALLBACK` (yellow LED).
4. If no contiguous range of N bits is available, leave bitmap unchanged and
   mark status `EXHAUSTED` (red LED, output silenced).
5. Unlock.

On `setActive(false)`: lock, clear the claimed bits, unlock.

### 4.3 Allocation Policy

**Contiguous only.** Sparse allocation is rejected by design: contiguous slots
make the GUI badge ("Slot 4–7 / 64") unambiguous and align with how users
mentally map plugin instances to physical channel groups.

### 4.4 Persistence

The allocator's bitmap lives only as long as the `.vst3` module is loaded
(i.e., the DAW process). At DAW startup, the bitmap is empty and is
reconstructed as each plugin instance loads its preset and re-claims its
saved `claimed_start_slot`. Order of plugin instantiation by the host
determines the success path; if Reaper instantiates plugins in a stable order
(it does, per project file order), preset-driven re-claim succeeds for all.

## 5. Parameters

| ID | Name | Type | Range | Default | Automatable |
|---|---|---|---|---|---|
| `kParamReference` | Reference | enum (3) | -23 / -20 / -18 dBFS RMS | -23 | yes (stepped) |
| `kParamTrim` | Trim | float | -6.0 … +6.0 dB | 0.0 | yes (continuous) |
| `kParamMute` | Mute | bool | 0 / 1 | 0 | yes |

**Effective output gain (linear)** applied uniformly to all active channels:

```
gain_db   = reference_db + trim_db + kCalibrationOffsetDb   (if !mute)
          = -inf                                            (if mute)
gain_lin  = 10^(gain_db / 20)
```

Where `reference_db ∈ {-23, -20, -18}`, `trim_db ∈ [-6, +6]`, and
`kCalibrationOffsetDb` is the hard-coded compensation for the pink filter's
attenuation.

**Crest-factor safety.** Pink noise crest factor ≈ 11–12 dB. Worst case:
reference -18, trim +6 → RMS -12 dBFS, peaks ≈ -1 dBFS. Always below 0 dBFS
across the full parameter space. **No limiter or soft-clipper is needed**, and
deliberately none is included (a never-active limiter is untestable code).

## 6. Persisted State

Per-instance state saved by the host (preset, project file):

- `reference` (enum)
- `trim` (float dB)
- `mute` (bool)
- `claimed_start_slot` (int, -1 or 0–63) — last successfully claimed start
  slot; sentinel `-1` means "no prior claim" (fresh instance) and forces
  first-fit on next activation

## 7. GUI (VSTGUI)

Visual style consistent with the other SEAM-LTM plugins (`ddelay`,
`bamodulex`).

**Elements:**

- **Trim knob** (master): centered, -6 / +6 dB, detent at 0.
- **Reference selector**: three-state radio or short dropdown
  (`-23 dBFS RMS` / `-20 dBFS RMS` / `-18 dBFS RMS`).
- **Calibration readout** (text): live-updated, e.g.
  `Trim: +0.0 dB  →  -23.0 dBFS RMS`.
- **Slot badge** (text, refreshed via timer ~200 ms):
  `Slot 4–7 / 64  ·  12 used  ·  52 free`.
- **Status LED**:
  - green = claim ok at saved slot
  - yellow = fallback first-fit
  - red = pool exhausted (silenced)
- **Mute button**.

No live editor; `VSTGUI_LIVE_EDITING=0` per repo convention.

## 8. File Layout

```
plugins/multipink/
├── CMakeLists.txt
├── source/
│   ├── multipink_cids.h              # plugin UID, parameter IDs
│   ├── multipink_processor.h
│   ├── multipink_processor.cpp       # IAudioProcessor + setBusArrangements
│   ├── multipink_controller.h
│   ├── multipink_controller.cpp      # IEditController + GUI binding
│   ├── multipink_dsp.h
│   ├── multipink_dsp.cpp             # thin C++ wrapper around Faust output
│   ├── multipink_pool.h
│   ├── multipink_pool.cpp            # static bitmap + claim/release
│   └── multipink_faust.cpp           # GENERATED:
│                                     #   faust -lang cpp -cn multipink_faust \
│                                     #         -a minimal.cpp source/multipink.dsp
├── resource/
│   └── multipink.uidesc              # VSTGUI XML
└── doc/
    └── calibration.md                # offline calibration & verification proc
```

Plus one line added to root `CMakeLists.txt`:

```cmake
add_subdirectory(plugins/multipink)
```

## 9. Faust Source

A single tiny `.dsp` file in `plugins/multipink/source/multipink.dsp`:

```faust
declare name "multipink";
import("seam.lib");

N = 64;

// gain is applied in C++; here we just emit the 64 raw pink channels.
process = sno.multipink(N, 1.0);
```

The C++ DSP wrapper:

- runs the Faust `compute()` which fills a 64-channel scratch buffer;
- copies the channels `[start_slot, start_slot+N_active)` into the host's
  output buffers;
- multiplies by the linear gain computed from `reference + trim` (or zeroes if
  mute / exhausted).

**Library Faust changes:** none required. `sno.multipink` exists at
`librerie/faust-libraries/src/seam.noise.lib`.

## 10. Calibration Procedure (doc/calibration.md)

1. Render 10 s of plugin output with `reference = -23 dBFS RMS`, `trim = 0`,
   on a stereo track in Reaper.
2. Measure file RMS with `sox stat`, iZotope Insight, or equivalent.
3. Expected: `-23.0 ±0.1 dBFS RMS` per channel (long-term integrated).
4. If offset > 0.1 dB: adjust `kCalibrationOffsetDb` in `multipink_dsp.cpp`
   and rebuild.
5. Repeat for `-20` and `-18` references.

## 11. Out of Scope

Explicitly excluded from this design:

- **Per-channel trim.** Master gain only; per-speaker compensation belongs in
  the DAW's mixer, not in the generator.
- **Soft clip / limiter.** Mathematically unnecessary with the chosen
  parameter ranges (§5).
- **Inter-instance audio communication.** Each instance is DSP-autonomous;
  only the slot bitmap is shared.
- **Cross-DAW-session bitmap persistence.** Bitmap is rebuilt from preset
  state at each DAW launch.
- **Sparse (non-contiguous) slot allocation.**
- **Custom seed parameter** for reproducibility. Not needed: the deterministic
  Faust seed (`12345`) combined with claimed slot index already gives
  reproducible per-slot streams across instances and sessions.

## 12. Open Items

None at design freeze. Implementation plan to be authored next.
