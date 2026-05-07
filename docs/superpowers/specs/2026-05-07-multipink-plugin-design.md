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

Per project convention (see `seam-ltm/CLAUDE.md`), the DSP is **hand-written
C++**, with `seam.noise.lib::sno.multipink` cited as the mathematical
reference in a `FAUST REFERENCE` comment block at the top of
`multipink_processor.h`. No Faust-generated code is committed.

The reference implementation in Faust is:

```faust
// from librerie/faust-libraries/src/seam.noise.lib
multipink(N,g) = no.multinoise(N) : par(i,N,no.pink_filter : *(g));
```

Which expands to N independent linear-congruential white-noise generators,
each fed into a Paul Kellet 4th-order IIR pink-shaping filter. The IIR
coefficients are explicit in `noises.lib:402`:

```
b = (0.049922035, -0.095993537, 0.050612699, -0.004408786)
a = (-2.494956002, 2.017265875, -0.522189400)
```

**C++ structure.** The processor holds:

- 64 × `uint32_t` LCG states (seeded deterministically from the Faust seed
  `12345`, see §2.1).
- 64 × `PinkFilterState` (4 input history + 3 output history).
- A scratch buffer of `64 × maxBlockSize` floats filled in `process()`.

Per call to `IAudioProcessor::process`:

1. Advance all 64 LCG/IIR pairs for `numSamples` samples into the scratch
   buffer. (Always all 64 — see §2.2 for why.)
2. Copy the channels `[start_slot, start_slot + N_active)` into the host's
   output buffers, multiplied by the linear gain from §5.

### 2.1 Seeding for cross-instance bit-identity

To preserve the "logical shared pool" semantics — i.e., slot `k` emits
identical samples in any plugin instance — all instances seed their 64 LCGs
identically. The seed of slot `i` is derived deterministically from the Faust
master seed and the slot index:

```cpp
constexpr uint32_t kFaustSeed = 12345;
uint32_t seed_for_slot(int i) {
    // Mirrors no.multirandom's per-channel seed derivation.
    // To be locked once, verified bit-identical against a Faust-generated
    // reference buffer during initial calibration.
    return ...;
}
```

The exact derivation will be locked during implementation by generating a
short reference buffer with `faust -lang cpp` (used as a *one-off scratch
tool*, not committed) and verifying the C++ port produces bit-identical
output. The locked formula is then frozen in code with a comment citing the
Faust source line.

### 2.2 Always compute all 64 channels

Even when only N (≤64) channels are emitted, all 64 LCG/IIR pairs are
advanced every sample. This is intentional:

- It guarantees that slot `k` produces a stream that does not depend on which
  *other* slots are active in the same instance. (If we only ran the active
  slots, the LCG of slot 4 would advance at the same rate regardless, but the
  semantic of "the pool is one big 64-ch generator running continuously"
  would be a lie.)
- CPU cost is trivial: 64 LCG + 64 four-tap IIR ≈ ~1.3k FLOPs/sample =
  ~0.06 GFLOPs at 48 kHz, i.e. well under 1% of one core on modern CPUs.

### 2.3 RMS calibration

The pink IIR attenuates the input white noise. To make knob "0 dB"
correspond to the chosen reference RMS, a one-time measurement determines
`kCalibrationOffsetDb`:

1. Build the plugin with `kCalibrationOffsetDb = 0`, reference = -23 dBFS RMS,
   trim = 0.
2. Render 30 s of mono output to a WAV.
3. Measure long-term RMS with `sox stat` (or equivalent).
4. Set `kCalibrationOffsetDb = -23.0 - measured_dBFS_RMS` and rebuild.
5. Re-render, verify within ±0.1 dB.

This constant is locked in `multipink_processor.cpp` with a comment recording
the measurement date and method.

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

Following the `ddelay` / `bamodulex` convention (see `seam-ltm/CLAUDE.md`):

```
plugins/multipink/
├── CMakeLists.txt
├── source/
│   ├── multipink_ids.h               # plugin UID, parameter IDs
│   ├── multipink_processor.h         # opens with FAUST REFERENCE comment
│   ├── multipink_processor.cpp       # IAudioProcessor + DSP (LCGs + IIRs)
│   ├── multipink_pool.h
│   ├── multipink_pool.cpp            # static bitmap + claim/release
│                                     # (justified extra file: shared static
│                                     #  state across instances)
│   └── version.h
├── resource/
│   └── multipink.uidesc              # VSTGUI XML
└── doc/
    └── calibration.md                # offline calibration & verification proc
```

Plus one line added to root `CMakeLists.txt`:

```cmake
add_subdirectory(plugins/multipink)
```

Note: no separate `_controller.cpp/.h` (existing plugins keep controller in
the processor file or omit it where VSTGUI's default suffices — to confirm
during implementation by mirroring `ddelay` exactly).

## 9. Faust Reference (header comment)

The top of `multipink_processor.h` carries a comment block citing the Faust
source, in the same form as `ddelay_processor.h:23` and
`bamodulex_processor.h:26`:

```cpp
// FAUST REFERENCE (seam.noise.lib):
//
//   multipink(N,g) = no.multinoise(N) : par(i,N,no.pink_filter : *(g));
//
// where no.pink_filter is (noises.lib:402):
//
//   pink_filter = fi.iir(
//       (0.049922035, -0.095993537, 0.050612699, -0.004408786),
//       (-2.494956002, 2.017265875, -0.522189400));
//
// and no.multinoise(N) is N parallel LCGs seeded from noise_env(12345).
//
// This plugin re-implements the above in hand-written C++ (project
// convention — see seam-ltm/CLAUDE.md). N is fixed at 64 (the shared
// logical pool size). Per-instance gain is applied in C++ after the IIR.
```

**Library Faust changes:** none required. `sno.multipink` already exists in
`librerie/faust-libraries/src/seam.noise.lib`.

## 10. Calibration Procedure (doc/calibration.md)

1. Render 30 s of plugin output with `reference = -23 dBFS RMS`, `trim = 0`,
   on a stereo track in Reaper.
2. Measure long-term RMS with `sox stat`, iZotope Insight, or equivalent.
3. Expected: `-23.0 ±0.1 dBFS RMS` per channel.
4. If offset > 0.1 dB: adjust `kCalibrationOffsetDb` in
   `multipink_processor.cpp` (a `constexpr` near the top) and rebuild.
5. Repeat for `-20` and `-18` references — they should all match within
   ±0.1 dB if the constant is correct (the offset is filter-intrinsic, not
   reference-dependent).

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
