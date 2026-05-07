# multipink Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `multipink` VST3 plugin: a 64-slot shared-pool pink-noise generator with RMS-calibrated output, layout-adaptive (mono → 64 ch), with cross-instance decorrelation guaranteed by static bitmap-based slot allocation in the `.vst3` module.

**Architecture:** Hand-written C++ DSP (64 LCGs + 64 Kellet IIR pink filters) re-implementing `seam.noise.lib::sno.multipink`, following the SEAM-LTM hand-written-C++ convention. Single `SingleComponentEffect` class (processor + controller fused, like `ddelay`). Peer awareness via `static std::atomic<uint64_t>` bitmap + `std::mutex` in a separate translation unit (`multipink_pool.cpp`). Declarative GUI via VSTGUI `.uidesc`.

**Tech Stack:** VST3 SDK, VSTGUI, CMake (Xcode generator on macOS), C++17, no external test framework (verification = compile + load in Reaper).

**Spec:** `docs/superpowers/specs/2026-05-07-multipink-plugin-design.md`

**Project conventions:** `seam-ltm/CLAUDE.md`

---

## File Structure

Files to create:

```
plugins/multipink/
├── CMakeLists.txt                           # adapted from ddelay
├── source/
│   ├── multipink_ids.h                      # FUID 0x5E4D0008, parameter enum
│   ├── version.h                            # adapted from ddelay
│   ├── multipink_processor.h                # SingleComponentEffect subclass
│   ├── multipink_processor.cpp              # DSP + VST3 plumbing
│   ├── multipink_pool.h                     # slot allocator interface
│   └── multipink_pool.cpp                   # static bitmap + mutex
├── resource/
│   └── multipink.uidesc                     # VSTGUI declarative GUI
└── doc/
    └── calibration.md                       # offline calibration procedure
```

Files to modify:

```
CMakeLists.txt                               # add: add_subdirectory(plugins/multipink)
```

---

## Task 1: Plugin scaffolding that compiles

Create the minimum viable skeleton: directory tree, CMakeLists, version.h, ids.h with one placeholder parameter, processor.h/.cpp with empty `process()`. Build it, verify the `.vst3` bundle appears. **No DSP, no allocator, no GUI yet** — just confirm the toolchain is happy.

**Files:**
- Create: `plugins/multipink/CMakeLists.txt`
- Create: `plugins/multipink/source/version.h`
- Create: `plugins/multipink/source/multipink_ids.h`
- Create: `plugins/multipink/source/multipink_processor.h`
- Create: `plugins/multipink/source/multipink_processor.cpp`
- Create: `plugins/multipink/resource/multipink.uidesc` (minimal stub)
- Modify: `CMakeLists.txt` (root) — add one line

- [ ] **Step 1.1: Create directory tree**

```bash
mkdir -p plugins/multipink/source plugins/multipink/resource plugins/multipink/doc
```

- [ ] **Step 1.2: Create `plugins/multipink/CMakeLists.txt`**

Copy `plugins/ddelay/CMakeLists.txt` and apply these substitutions:
- Project name: `seam-ddelay` → `seam-multipink`
- Description: `"SEAM DDELAY – Quad Speaker Alignment Delay"` → `"SEAM MULTIPINK – Multichannel pink noise generator with shared 64-slot pool"`
- All `ddelay_sources` → `multipink_sources`
- All file paths `source/ddelay_*` → `source/multipink_*`
- Source list must include both `multipink_processor.cpp` AND `multipink_pool.cpp`
- All `set(target ddelay)` → `set(target multipink)`
- Resource path `resource/ddelay.uidesc` → `resource/multipink.uidesc`
- Bundle identifier: `io.github.s-e-a-m.ddelay` → `io.github.s-e-a-m.multipink`

The full sources list:

```cmake
set(multipink_sources
    source/version.h
    source/multipink_ids.h
    source/multipink_processor.h
    source/multipink_processor.cpp
    source/multipink_pool.h
    source/multipink_pool.cpp
)
```

- [ ] **Step 1.3: Create `plugins/multipink/source/version.h`**

Copy `plugins/ddelay/source/version.h` and substitute:
- `stringOriginalFilename "ddelay.vst3"` → `stringOriginalFilename "multipink.vst3"`
- `stringFileDescription "SEAM DDELAY VST3 (64Bit)"` → `stringFileDescription "SEAM MULTIPINK VST3 (64Bit)"`

All other macros (`stringCompanyName`, `stringCompanyWeb`, `stringCompanyEmail`, `stringLegalCopyright`, `stringLegalTrademarks`) remain identical.

- [ ] **Step 1.4: Create `plugins/multipink/source/multipink_ids.h`**

```cpp
#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 8th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (sdmx=0001, …, ddelay=0007, multipink=0008).
static const Steinberg::FUID MULTIPINKProcessorUID(
    0x5E4D0008, 0xA1B2C3D4, 0x4D554C54, 0x49504E4B);

enum MULTIPINKParams : Steinberg::Vst::ParamID {
    kParamReference = 100,   // 0=-23, 1=-20, 2=-18 dBFS RMS  (stepped)
    kParamTrim      = 101,   // -6.0 … +6.0 dB                (continuous)
    kParamMute      = 102,   // 0 / 1                         (bool)
};

// Number of stepped values for kParamReference. Used by the GUI and by the
// processor to decode the normalized [0,1] parameter into an enum index.
static constexpr Steinberg::int32 kReferenceStepCount = 3;

// The reference levels in dBFS RMS, indexed by the stepped parameter value.
static constexpr double kReferenceLevelsDb[kReferenceStepCount] = {
    -23.0, -20.0, -18.0
};

} // namespace Seam
```

- [ ] **Step 1.5: Create `plugins/multipink/source/multipink_processor.h`**

```cpp
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "multipink_ids.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

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

namespace Seam {

class MULTIPINKProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    MULTIPINKProcessor();
    ~MULTIPINKProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new MULTIPINKProcessor();
    }

    // VST3 lifecycle
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSize) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

private:
    // Pool size — the logical shared 64-channel pink-noise pool.
    static constexpr int kPoolSize = 64;

    // Faust master seed, must match no.multinoise(N) = noise_env(12345).
    static constexpr uint32_t kFaustSeed = 12345;

    // Pink filter coefficients (Paul Kellet, noises.lib:402).
    static constexpr double kPinkB[4] = {
         0.049922035, -0.095993537,  0.050612699, -0.004408786 };
    static constexpr double kPinkA[3] = {
        -2.494956002,  2.017265875, -0.522189400 };

    // Calibration constant. After the one-off measurement (see Task 9),
    // replace 0.0 with the measured offset so that
    // "reference = -23 dBFS RMS, trim = 0" produces -23.0 ±0.1 dBFS RMS.
    static constexpr double kCalibrationOffsetDb = 0.0;

    // Active output channel count (set by setBusArrangements).
    int activeChannels_ = 2;

    // Number of channels claimed in the pool (≤ activeChannels_).
    int claimedChannels_ = 0;

    // Starting slot in the pool (0..63), or -1 if not claimed (sentinel).
    int claimedStart_ = -1;

    // Persisted "preferred" start slot from state, used at next setActive(true).
    int preferredStart_ = -1;

    // Allocation status (drives the GUI LED, set in setActive).
    enum class PoolStatus : int { Unclaimed = 0, ClaimedAtPreferred = 1, ClaimedFirstFit = 2, Exhausted = 3 };
    std::atomic<int> poolStatus_{(int)PoolStatus::Unclaimed};

    // Parameters (audio-thread-readable).
    std::atomic<int>    paramReferenceIdx_{0};   // 0..2
    std::atomic<double> paramTrimDb_{0.0};       // -6..+6
    std::atomic<int>    paramMute_{0};           // 0 or 1

    // DSP state — full pool always advanced.
    uint32_t lcgState_[kPoolSize] = {};
    double   pinkX_[kPoolSize][4] = {};   // input history (white noise)
    double   pinkY_[kPoolSize][3] = {};   // output history (pink)

    // Scratch buffer for the full pool: shape [kPoolSize][maxBlockSize].
    std::vector<float>  scratch32_;
    std::vector<double> scratch64_;
    int32_t maxBlockSize_ = 0;

    // Helpers
    void seedLCGs();
    void resetPinkFilters();
    double computeGainLin() const;
    void readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples,
                      std::vector<SampleType>& scratch);
};

} // namespace Seam
```

- [ ] **Step 1.6: Create `plugins/multipink/source/multipink_processor.cpp` (skeleton only)**

Copy the includes and factory boilerplate from `plugins/ddelay/source/ddelay_processor.cpp` (lines L5–L18 for includes, L331–L344 for factory) and adapt names. Stub all method bodies to compile but not yet do real work. Skeleton:

```cpp
#include "multipink_processor.h"
#include "multipink_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

MULTIPINKProcessor::MULTIPINKProcessor() {
    setControllerClass(MULTIPINKProcessorUID);
}

tresult PLUGIN_API MULTIPINKProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // One stereo output bus by default; setBusArrangements may widen it later.
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    // Parameters — minimal stubs, real implementation in Task 5.
    parameters.addParameter(STR16("Reference"), STR16("dBFS RMS"),
                            kReferenceStepCount - 1, 0,
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
                            kParamReference);
    parameters.addParameter(STR16("Trim"), STR16("dB"), 0, 0.5,
                            ParameterInfo::kCanAutomate, kParamTrim);
    parameters.addParameter(STR16("Mute"), STR16(""), 1, 0,
                            ParameterInfo::kCanAutomate, kParamMute);

    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API MULTIPINKProcessor::setActive(TBool state) {
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API MULTIPINKProcessor::setupProcessing(ProcessSetup& setup) {
    maxBlockSize_ = setup.maxSamplesPerBlock;
    scratch32_.assign((size_t)kPoolSize * maxBlockSize_, 0.0f);
    scratch64_.assign((size_t)kPoolSize * maxBlockSize_, 0.0);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API MULTIPINKProcessor::process(ProcessData& data) {
    // Stub: silence outputs.
    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    int n = data.outputs[0].numChannels;
    for (int c = 0; c < n; ++c) {
        if (data.symbolicSampleSize == kSample32)
            std::memset(out[c], 0, sizeof(float)  * data.numSamples);
        else
            std::memset(out[c], 0, sizeof(double) * data.numSamples);
    }
    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API MULTIPINKProcessor::setBusArrangements(
    SpeakerArrangement* /*ins*/, int32 /*numIns*/,
    SpeakerArrangement* outs, int32 numOuts) {
    if (numOuts != 1) return kResultFalse;
    int channels = SpeakerArr::getChannelCount(outs[0]);
    if (channels < 1 || channels > kPoolSize) return kResultFalse;
    activeChannels_ = channels;
    getAudioOutput(0)->setArrangement(outs[0]);
    getAudioOutput(0)->setName(STR16("Output"));
    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::setState(IBStream* /*state*/) { return kResultOk; }
tresult PLUGIN_API MULTIPINKProcessor::getState(IBStream* /*state*/) { return kResultOk; }

IPlugView* PLUGIN_API MULTIPINKProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "multipink.uidesc");
    return nullptr;
}

void MULTIPINKProcessor::seedLCGs()           { /* Task 3 */ }
void MULTIPINKProcessor::resetPinkFilters()   { /* Task 4 */ }
double MULTIPINKProcessor::computeGainLin() const { return 0.0; /* Task 5 */ }
void MULTIPINKProcessor::readParameterChanges(ProcessData&) { /* Task 5 */ }

template <typename SampleType>
void MULTIPINKProcessor::processBlock(SampleType**, int, int, std::vector<SampleType>&) {
    /* Tasks 3,4,5,6 */
}

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::MULTIPINKProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM MULTIPINK", Vst::kDistributable,
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::MULTIPINKProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 1.7: Create `plugins/multipink/source/multipink_pool.h` (stub)**

```cpp
#pragma once
namespace Seam { class MultipinkPool {}; } // real implementation in Task 2
```

- [ ] **Step 1.8: Create `plugins/multipink/source/multipink_pool.cpp` (stub)**

```cpp
#include "multipink_pool.h"
// real implementation in Task 2
```

- [ ] **Step 1.9: Create minimal `plugins/multipink/resource/multipink.uidesc`**

Copy `plugins/ddelay/resource/ddelay.uidesc` verbatim, then:
- Change the `<view class="CTextLabel"` titles from "DDELAY" / "Quad Speaker Alignment Delay" to "MULTIPINK" / "Multichannel Pink Noise Generator".
- Remove ddelay's parameter sliders (we'll add multipink's in Task 8); leave a stub `<CViewContainer>` with title and logo only.
- The GUI is intentionally minimal here — Task 8 fleshes it out.

- [ ] **Step 1.10: Add multipink to root `CMakeLists.txt`**

In `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm/CMakeLists.txt`, after the line `add_subdirectory(plugins/ddelay)`, add:

```cmake
add_subdirectory(plugins/multipink)
```

- [ ] **Step 1.11: Build**

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -GXcode
cmake --build build-release --config Release -j8 --target multipink
```

Expected: `build-release/VST3/Release/multipink.vst3` exists. No compile errors. No warnings about unused parameters that aren't `[[maybe_unused]]` or commented as "Task N".

- [ ] **Step 1.12: Smoke-test in Reaper (manual)**

Open Reaper, add a stereo track, instantiate `SEAM MULTIPINK`. Plugin loads, GUI shows the title "MULTIPINK", no crash, audio is silent. Close project without saving.

- [ ] **Step 1.13: Commit**

```bash
git add plugins/multipink CMakeLists.txt
git commit -m "$(cat <<'EOF'
multipink: add scaffolding (compiles, loads, silent)

Skeleton only — DSP, allocator, parameters, and GUI are stubs.
Plugin registers as Instrument|Synth, FUID 0x5E4D0008..., one
stereo output bus, three placeholder parameters. Verified loads
in Reaper without crash.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Slot allocator (multipink_pool)

Implement the static bitmap-based slot allocator used by all instances of the plugin within a host process.

**Files:**
- Modify: `plugins/multipink/source/multipink_pool.h`
- Modify: `plugins/multipink/source/multipink_pool.cpp`
- Modify: `plugins/multipink/source/multipink_processor.cpp` (wire claim/release into setActive)

- [ ] **Step 2.1: Write `multipink_pool.h`**

```cpp
#pragma once
#include <cstdint>

namespace Seam {

// Static, process-global slot allocator for the multipink shared 64-channel
// logical pool. Lives once per loaded .vst3 module; all plugin instances
// within the host share it.
//
// Allocation policy: contiguous first-fit, with optional preferred-start
// hint (used to preserve calibration identity across DAW reloads).
class MultipinkPool {
public:
    static constexpr int kPoolSize = 64;

    enum class ClaimResult {
        ClaimedAtPreferred = 1,
        ClaimedFirstFit    = 2,
        Exhausted          = 3,
    };

    // Try to claim N contiguous slots. If `preferredStart` is in [0, kPoolSize-N]
    // and that range is free, claim it (returns ClaimedAtPreferred and
    // outActualStart = preferredStart). Otherwise first-fit (returns
    // ClaimedFirstFit). If no contiguous range of N slots is free, returns
    // Exhausted, sets outActualStart = -1, and the caller MUST NOT release
    // (no claim was made).
    static ClaimResult claim(int count, int preferredStart, int& outActualStart);

    // Release N slots starting at `start`. No-op if start == -1.
    static void release(int start, int count);
};

} // namespace Seam
```

- [ ] **Step 2.2: Write `multipink_pool.cpp`**

```cpp
#include "multipink_pool.h"

#include <atomic>
#include <mutex>

namespace Seam {

namespace {
    std::atomic<uint64_t> g_claimed{0};
    std::mutex            g_mutex;

    static inline uint64_t mask(int count, int start) {
        // count bits set, starting at bit `start`. count must be in [1,64],
        // start in [0, 64-count].
        if (count == 64) return ~uint64_t{0};
        return ((uint64_t{1} << count) - 1) << start;
    }
}

MultipinkPool::ClaimResult MultipinkPool::claim(int count, int preferredStart, int& outActualStart) {
    outActualStart = -1;
    if (count <= 0 || count > kPoolSize) return ClaimResult::Exhausted;

    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t taken = g_claimed.load(std::memory_order_relaxed);

    // Try preferred start first.
    if (preferredStart >= 0 && preferredStart + count <= kPoolSize) {
        uint64_t m = mask(count, preferredStart);
        if ((taken & m) == 0) {
            g_claimed.store(taken | m, std::memory_order_relaxed);
            outActualStart = preferredStart;
            return ClaimResult::ClaimedAtPreferred;
        }
    }

    // First-fit contiguous search.
    for (int s = 0; s + count <= kPoolSize; ++s) {
        uint64_t m = mask(count, s);
        if ((taken & m) == 0) {
            g_claimed.store(taken | m, std::memory_order_relaxed);
            outActualStart = s;
            return ClaimResult::ClaimedFirstFit;
        }
    }

    return ClaimResult::Exhausted;
}

void MultipinkPool::release(int start, int count) {
    if (start < 0 || count <= 0) return;
    if (start + count > kPoolSize) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t taken = g_claimed.load(std::memory_order_relaxed);
    g_claimed.store(taken & ~mask(count, start), std::memory_order_relaxed);
}

} // namespace Seam
```

- [ ] **Step 2.3: Wire claim/release into `setActive`**

In `multipink_processor.cpp`, replace the stub `setActive` with:

```cpp
#include "multipink_pool.h"

tresult PLUGIN_API MULTIPINKProcessor::setActive(TBool state) {
    if (state) {
        int actualStart = -1;
        auto r = MultipinkPool::claim(activeChannels_, preferredStart_, actualStart);
        claimedStart_     = actualStart;
        claimedChannels_  = (actualStart >= 0) ? activeChannels_ : 0;
        switch (r) {
            case MultipinkPool::ClaimResult::ClaimedAtPreferred:
                poolStatus_.store((int)PoolStatus::ClaimedAtPreferred); break;
            case MultipinkPool::ClaimResult::ClaimedFirstFit:
                poolStatus_.store((int)PoolStatus::ClaimedFirstFit); break;
            case MultipinkPool::ClaimResult::Exhausted:
                poolStatus_.store((int)PoolStatus::Exhausted); break;
        }
        seedLCGs();
        resetPinkFilters();
    } else {
        if (claimedStart_ >= 0) {
            MultipinkPool::release(claimedStart_, claimedChannels_);
        }
        claimedStart_    = -1;
        claimedChannels_ = 0;
        poolStatus_.store((int)PoolStatus::Unclaimed);
    }
    return SingleComponentEffect::setActive(state);
}
```

Also add `#include "multipink_pool.h"` to `multipink_processor.cpp` near the top.

- [ ] **Step 2.4: Build**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

Expected: clean compile.

- [ ] **Step 2.5: Manual smoke-test in Reaper**

Add 2 instances of multipink to two stereo tracks. Add a 3rd to an 8-channel track (Reaper: track properties → channels = 8). Add a 4th to a 64-channel track. Add a 5th — should still load (we have no GUI feedback yet, but code-side `claimedStart_` should be in the order: 0, 2, 4, 12, 60). The 5th instance with 8 channels would request slots 60..67 — that's 67 > 64 so **last instance: Exhausted**. We can't observe yet without GUI, but no crash should occur.

- [ ] **Step 2.6: Commit**

```bash
git add plugins/multipink/source/multipink_pool.{h,cpp} plugins/multipink/source/multipink_processor.cpp
git commit -m "multipink: add pool slot allocator (static bitmap + mutex)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: White-noise LCG core (64 channels)

Implement the 64-channel uniform white-noise generator as a hand port of `no.multinoise(N) = noise_env(12345).multinoise(N)`. Verify bit-identity against a reference Faust dump.

The Faust LCG (in `noises.lib`, `multirandom`) is a classic 32-bit LCG with multiplier `1103515245`, increment `12345`, modulo `2^32`. The per-channel seed in `multinoise(N)` derives N independent LCG streams from a single root seed by **decimation**: channel `i` of `N` advances the same LCG `i` times further per sample than the master. Equivalently — and more efficient for our N=64 case — each channel has its own independent LCG, all seeded from the master `noise_env(seed)` by running it once and using its output as the next channel's seed.

**Reference implementation algorithm (verified against Faust source at `noises.lib:81`, `multirandom`):**

```cpp
// Per-channel state initialization, mirroring no.multinoise(64) exactly:
uint32_t s = kFaustSeed;                          // master seed = 12345
for (int i = 0; i < kPoolSize; ++i) {
    // Faust's per-channel seed derivation: each channel i is seeded with
    // the master LCG advanced i+1 times.
    s = s * 1103515245u + 12345u;                 // master LCG step
    lcgState_[i] = s;
}

// Per-sample tick on channel i:
lcgState_[i] = lcgState_[i] * 1103515245u + 12345u;
double white = (int32_t)lcgState_[i] / 2147483648.0;   // -> [-1, 1)
```

**This formula MUST be verified bit-identical against a Faust reference dump before locking it.** The verification is a one-time scratch step using `faust -lang cpp` (allowed by `seam-ltm/CLAUDE.md` as a sketch tool — its output is not committed).

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.cpp` (`seedLCGs`, beginning of `processBlock`)

- [ ] **Step 3.1: Implement `seedLCGs`**

In `multipink_processor.cpp`:

```cpp
void MULTIPINKProcessor::seedLCGs() {
    uint32_t s = kFaustSeed;
    for (int i = 0; i < kPoolSize; ++i) {
        s = s * 1103515245u + 12345u;
        lcgState_[i] = s;
    }
}
```

- [ ] **Step 3.2: Implement white-noise generation in `processBlock` template (DSP only — gain comes in Task 5)**

```cpp
template <typename SampleType>
void MULTIPINKProcessor::processBlock(SampleType** outputs, int numChannels,
                                      int numSamples,
                                      std::vector<SampleType>& scratch) {
    // 1. Advance ALL 64 LCGs into scratch[ch * numSamples + s].
    //    (Reason for "all 64": see spec §2.2.)
    for (int ch = 0; ch < kPoolSize; ++ch) {
        uint32_t st = lcgState_[ch];
        SampleType* row = scratch.data() + (size_t)ch * numSamples;
        for (int s = 0; s < numSamples; ++s) {
            st = st * 1103515245u + 12345u;
            row[s] = (SampleType)((int32_t)st / 2147483648.0);
        }
        lcgState_[ch] = st;
    }
    // 2. (Pink filter — Task 4.)
    // 3. (Slot routing + gain — Task 5/6.)
    //    For now, copy claimed slots straight to outputs at unity gain.
    if (claimedStart_ < 0 || claimedChannels_ == 0) {
        for (int c = 0; c < numChannels; ++c)
            std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
        return;
    }
    int n = std::min(numChannels, claimedChannels_);
    for (int c = 0; c < n; ++c) {
        SampleType* src = scratch.data() + (size_t)(claimedStart_ + c) * numSamples;
        std::memcpy(outputs[c], src, sizeof(SampleType) * numSamples);
    }
    // Zero any host channels beyond claimed range (defensive).
    for (int c = n; c < numChannels; ++c)
        std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
}
```

Also replace the stub `process()` body with a proper dispatch:

```cpp
tresult PLUGIN_API MULTIPINKProcessor::process(ProcessData& data) {
    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;
    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32) {
        processBlock<float>((float**)out, numChannels, data.numSamples, scratch32_);
    } else {
        processBlock<double>((double**)out, numChannels, data.numSamples, scratch64_);
    }
    return kResultOk;
}
```

- [ ] **Step 3.3: Generate Faust reference dump (one-off scratch verification)**

This step is **not committed**. Create a temporary file `/tmp/multipink_ref.dsp`:

```faust
import("stdfaust.lib");
process = no.multinoise(64);
```

Generate a binary dump of the first 1024 samples × 64 channels:

```bash
faust2sndfile -i /tmp/multipink_ref.dsp /tmp/ref_gen.cpp 2>/dev/null || true
# Simpler approach: use faust2plot or write a tiny driver. Easiest:
faust -a sndfile.cpp -i /tmp/multipink_ref.dsp -o /tmp/multipink_ref.cpp
clang++ -std=c++17 /tmp/multipink_ref.cpp -lsndfile -o /tmp/multipink_ref \
    -I$(faust --includedir 2>/dev/null || echo /usr/local/include/faust)
/tmp/multipink_ref empty.wav /tmp/ref_out.wav
```

(If `faust2sndfile` doesn't take an empty input gracefully, adapt to write 1024 silence samples to `empty.wav` first via `sox -n empty.wav synth 0.0213 sine 0` for 48 kHz × 1024.)

- [ ] **Step 3.4: Write a temporary C++ verifier (not committed)**

Create `/tmp/verify_lcg.cpp`:

```cpp
#include <cstdio>
#include <cstdint>
#include <sndfile.h>

int main() {
    SNDFILE* f; SF_INFO info{};
    f = sf_open("/tmp/ref_out.wav", SFM_READ, &info);
    if (!f) { printf("can't open ref\n"); return 1; }
    int N = info.channels;
    int frames = info.frames;
    if (N != 64) { printf("expected 64 ch, got %d\n", N); return 1; }
    double* buf = new double[N * frames];
    sf_readf_double(f, buf, frames);
    sf_close(f);

    // Replicate the C++ LCG seeding & ticking
    uint32_t lcg[64];
    uint32_t s = 12345;
    for (int i = 0; i < 64; ++i) { s = s * 1103515245u + 12345u; lcg[i] = s; }

    int mismatches = 0;
    for (int t = 0; t < frames && t < 1024; ++t) {
        for (int i = 0; i < 64; ++i) {
            lcg[i] = lcg[i] * 1103515245u + 12345u;
            double cpp = (int32_t)lcg[i] / 2147483648.0;
            double faust = buf[t * N + i];
            if (std::abs(cpp - faust) > 1e-9) {
                if (mismatches < 5)
                    printf("mismatch t=%d ch=%d cpp=%g faust=%g\n", t, i, cpp, faust);
                ++mismatches;
            }
        }
    }
    printf("%s: %d mismatches in %d samples × 64 ch\n",
           mismatches == 0 ? "OK" : "FAIL", mismatches, std::min(frames, 1024));
    delete[] buf;
    return mismatches == 0 ? 0 : 1;
}
```

```bash
clang++ -std=c++17 /tmp/verify_lcg.cpp -lsndfile -o /tmp/verify_lcg
/tmp/verify_lcg
```

Expected output: `OK: 0 mismatches in 1024 samples × 64 ch`.

If mismatches > 0: the per-channel seed derivation differs from Faust's `multirandom`. Read `noises.lib:81` and adjust `seedLCGs()` until the verifier reports OK. Then update the comment in `multipink_processor.cpp` near `seedLCGs` with the date and result of this verification.

- [ ] **Step 3.5: Add a comment recording the verification**

After `seedLCGs` body, add:

```cpp
// LCG seeding and ticking verified bit-identical against
// no.multinoise(64) (Faust reference dump, 1024 samples × 64 ch,
// zero mismatches at 1e-9 tolerance) on YYYY-MM-DD.
```

(Replace `YYYY-MM-DD` with the actual date of verification.)

- [ ] **Step 3.6: Build & smoke-test**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

Load in Reaper (stereo track). Output should be **white noise** (not pink yet) at unity amplitude (peaks near ±1.0 — likely clipping, that's fine for now). Verify it's not silence and not a tone.

- [ ] **Step 3.7: Commit**

```bash
git add plugins/multipink/source/multipink_processor.{h,cpp}
git commit -m "multipink: add 64-channel white-noise LCG core, verified bit-identical to Faust no.multinoise(64)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: Pink filter (Kellet IIR, 64 channels)

Add the per-channel 4-input/3-output Direct Form I IIR filter implementing Paul Kellet's pink shaping (`noises.lib:402`).

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.cpp`

- [ ] **Step 4.1: Implement `resetPinkFilters`**

```cpp
void MULTIPINKProcessor::resetPinkFilters() {
    for (int i = 0; i < kPoolSize; ++i) {
        for (int k = 0; k < 4; ++k) pinkX_[i][k] = 0.0;
        for (int k = 0; k < 3; ++k) pinkY_[i][k] = 0.0;
    }
}
```

- [ ] **Step 4.2: Insert pink-filter pass into `processBlock`**

Replace the comment `// 2. (Pink filter — Task 4.)` with:

```cpp
// 2. Pink-shape ALL 64 channels in place (Direct Form I IIR).
//    y[n] =  b0*x[n] + b1*x[n-1] + b2*x[n-2] + b3*x[n-3]
//          - a1*y[n-1] - a2*y[n-2] - a3*y[n-3]
for (int ch = 0; ch < kPoolSize; ++ch) {
    SampleType* row = scratch.data() + (size_t)ch * numSamples;
    double x1 = pinkX_[ch][0], x2 = pinkX_[ch][1], x3 = pinkX_[ch][2], x4 = pinkX_[ch][3];
    double y1 = pinkY_[ch][0], y2 = pinkY_[ch][1], y3 = pinkY_[ch][2];
    for (int s = 0; s < numSamples; ++s) {
        double x0 = (double)row[s];
        double y0 = kPinkB[0]*x0 + kPinkB[1]*x1 + kPinkB[2]*x2 + kPinkB[3]*x3
                  - kPinkA[0]*y1 - kPinkA[1]*y2 - kPinkA[2]*y3;
        row[s] = (SampleType)y0;
        x4 = x3; x3 = x2; x2 = x1; x1 = x0;
        y3 = y2; y2 = y1; y1 = y0;
    }
    pinkX_[ch][0] = x1; pinkX_[ch][1] = x2; pinkX_[ch][2] = x3; pinkX_[ch][3] = x4;
    pinkY_[ch][0] = y1; pinkY_[ch][1] = y2; pinkY_[ch][2] = y3;
}
```

Note: `kPinkB[0]…kPinkB[3]` = b0…b3 (4 feedforward taps); `kPinkA[0]…kPinkA[2]` = a1, a2, a3 (3 feedback taps; sign convention is "subtracted from output", matching Faust `fi.iir`).

- [ ] **Step 4.3: Build & smoke-test**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

In Reaper, load on a stereo track. The output should now sound like **pink noise** — distinctly warmer/lower than the white noise of Task 3. Use a spectrum analyzer (e.g., MeldaProduction MAnalyzer free, or Reaper's ReaEQ with the spectral display) to verify ~3 dB/octave roll-off.

- [ ] **Step 4.4: Bit-identity verification (optional, recommended)**

Repeat Task 3.3–3.5 with the reference DSP changed to:

```faust
process = no.multinoise(64) : par(i,64,no.pink_filter);
```

The C++ verifier needs to also run the pink filter; copy the inner loop from Step 4.2. Tolerance: 1e-9. If mismatches: re-check coefficient signs against `noises.lib:402`.

- [ ] **Step 4.5: Commit**

```bash
git add plugins/multipink/source/multipink_processor.cpp
git commit -m "multipink: add Kellet pink IIR filter (64 channels)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: Parameters + gain stage

Wire the three parameters (`Reference`, `Trim`, `Mute`) end-to-end: declare with proper ranges, drain `inputParameterChanges` in `process()`, compute linear gain, apply per-sample on the output.

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.cpp`

- [ ] **Step 5.1: Replace the parameter declarations in `initialize()`**

Use `RangeParameter` and `StringListParameter` for clarity:

```cpp
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"

// In initialize(), after the addAudioOutput line, replace the three stub
// addParameter calls with:

auto* refParam = new StringListParameter(
    STR16("Reference"), kParamReference, STR16("dBFS RMS"),
    ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
refParam->appendString(STR16("-23"));
refParam->appendString(STR16("-20"));
refParam->appendString(STR16("-18"));
parameters.addParameter(refParam);

parameters.addParameter(new RangeParameter(
    STR16("Trim"), kParamTrim, STR16("dB"),
    -6.0, 6.0, 0.0, 0,
    ParameterInfo::kCanAutomate));

parameters.addParameter(new RangeParameter(
    STR16("Mute"), kParamMute, STR16(""),
    0.0, 1.0, 0.0, 1,
    ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
```

- [ ] **Step 5.2: Implement `readParameterChanges`**

```cpp
void MULTIPINKProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        ParamID id = q->getParameterId();
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (id) {
            case kParamReference: {
                // v in [0,1], discretize to [0..2]
                int idx = (int)std::round(v * (kReferenceStepCount - 1));
                if (idx < 0) idx = 0;
                if (idx > kReferenceStepCount - 1) idx = kReferenceStepCount - 1;
                paramReferenceIdx_.store(idx);
            } break;
            case kParamTrim:
                // v in [0,1] maps to [-6, +6] dB
                paramTrimDb_.store(v * 12.0 - 6.0);
                break;
            case kParamMute:
                paramMute_.store(v >= 0.5 ? 1 : 0);
                break;
        }
    }
}
```

- [ ] **Step 5.3: Implement `computeGainLin`**

```cpp
double MULTIPINKProcessor::computeGainLin() const {
    if (paramMute_.load()) return 0.0;
    if (poolStatus_.load() == (int)PoolStatus::Exhausted) return 0.0;
    int idx = paramReferenceIdx_.load();
    double refDb = kReferenceLevelsDb[idx];
    double trim = paramTrimDb_.load();
    double db = refDb + trim + kCalibrationOffsetDb;
    return std::pow(10.0, db / 20.0);
}
```

- [ ] **Step 5.4: Apply gain in `processBlock`**

Modify the slot-routing block in `processBlock`:

```cpp
SampleType g = (SampleType)computeGainLin();
int n = std::min(numChannels, claimedChannels_);
for (int c = 0; c < n; ++c) {
    SampleType* src = scratch.data() + (size_t)(claimedStart_ + c) * numSamples;
    SampleType* dst = outputs[c];
    for (int s = 0; s < numSamples; ++s) dst[s] = src[s] * g;
}
for (int c = n; c < numChannels; ++c)
    std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
```

And insert at the top of `process()` (before the dispatch):

```cpp
readParameterChanges(data);
```

- [ ] **Step 5.5: Build & smoke-test**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

In Reaper:
- Load on a stereo track. Default output should be ~roughly -23 dBFS RMS (well below clipping). Verify with Reaper's track meter showing peaks ~-11 dBFS.
- Move Trim slider to +6 dB. Output rises by 6 dB (peaks ~-5 dBFS).
- Move Reference dropdown to -18. Output rises by 5 dB more (peaks ~-1 dBFS — borderline, expected per spec §5).
- Toggle Mute. Output → silence.

(Exact RMS calibration comes in Task 9. For now we just verify the controls behave sanely.)

- [ ] **Step 5.6: Commit**

```bash
git add plugins/multipink/source/multipink_processor.cpp
git commit -m "multipink: wire Reference/Trim/Mute parameters and gain stage

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6: Bus arrangements (mono → 64 channels)

Generalize `setBusArrangements` to accept any standard speaker layout from mono to 64 channels.

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.cpp`

- [ ] **Step 6.1: Replace `setBusArrangements`**

The Task 1 stub already accepts 1..64. Refine it slightly to also explicitly publish the new arrangement on the bus (already there — verify) and to log/clamp gracefully:

```cpp
tresult PLUGIN_API MULTIPINKProcessor::setBusArrangements(
    SpeakerArrangement* /*ins*/, int32 /*numIns*/,
    SpeakerArrangement* outs, int32 numOuts) {
    if (numOuts != 1) return kResultFalse;
    int channels = SpeakerArr::getChannelCount(outs[0]);
    if (channels < 1 || channels > kPoolSize) return kResultFalse;
    activeChannels_ = channels;
    if (auto* bus = getAudioOutput(0)) {
        bus->setArrangement(outs[0]);
        bus->setName(STR16("Output"));
    }
    return kResultOk;
}
```

(If the Task 1 stub already matches this, leave as-is.)

- [ ] **Step 6.2: Build & multi-layout smoke-test**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

In Reaper:
1. Stereo track → loads, 2 channels of pink.
2. Add a track, set its channel count to 4, load multipink. → 4 channels of pink, all decorrelated.
3. Add a track set to 16 channels, load multipink. → 16 channels.
4. Add a track set to 64 channels, load multipink. → 64 channels (all the way to slot 63).
5. Try a 65-channel track — Reaper allows up to 64, so this should not be reachable. If reached: plugin must refuse to load (`kResultFalse`).

- [ ] **Step 6.3: Commit (only if changes)**

```bash
git add plugins/multipink/source/multipink_processor.cpp
git commit -m "multipink: refine setBusArrangements (1..64 channels)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>" || echo "no changes to commit"
```

---

## Task 7: Persisted state (parameters + preferred slot)

Save and restore the three parameters plus the last successfully claimed start slot. On reload, the plugin uses that slot as a hint to `MultipinkPool::claim`, falling back to first-fit if unavailable.

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.cpp`

- [ ] **Step 7.1: Replace `getState`**

```cpp
tresult PLUGIN_API MULTIPINKProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = paramReferenceIdx_.load();
    double trim  = paramTrimDb_.load();
    int32 mute   = paramMute_.load();
    int32 prefStart = claimedStart_;   // remember the slot we're currently using
    if (!s.writeInt32(refIdx))     return kResultFalse;
    if (!s.writeDouble(trim))      return kResultFalse;
    if (!s.writeInt32(mute))       return kResultFalse;
    if (!s.writeInt32(prefStart))  return kResultFalse;
    return kResultOk;
}
```

- [ ] **Step 7.2: Replace `setState`**

```cpp
tresult PLUGIN_API MULTIPINKProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = 0; double trim = 0.0; int32 mute = 0; int32 prefStart = -1;
    if (!s.readInt32(refIdx))    return kResultFalse;
    if (!s.readDouble(trim))     return kResultFalse;
    if (!s.readInt32(mute))      return kResultFalse;
    if (!s.readInt32(prefStart)) return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    paramMute_.store(mute ? 1 : 0);
    preferredStart_ = (prefStart >= -1 && prefStart < kPoolSize) ? prefStart : -1;

    // Mirror normalized values into the parameter container so the host
    // and GUI see them on next refresh.
    parameters.getParameter(kParamReference)->setNormalized(
        (double)paramReferenceIdx_.load() / (kReferenceStepCount - 1));
    parameters.getParameter(kParamTrim)->setNormalized(
        (paramTrimDb_.load() + 6.0) / 12.0);
    parameters.getParameter(kParamMute)->setNormalized(
        paramMute_.load() ? 1.0 : 0.0);

    return kResultOk;
}
```

Add `#include "pluginterfaces/base/ibstream.h"` and `#include "base/source/fstreamer.h"` at top of file (the latter provides `IBStreamer`).

- [ ] **Step 7.3: Build**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

- [ ] **Step 7.4: Persistence smoke-test**

In Reaper:
1. Add 3 stereo tracks, multipink on each (slots [0,1], [2,3], [4,5]).
2. Set Reference=-18 on instance #2. Trim=+3.
3. Save project, close, reopen. Verify instance #2 still shows Reference=-18 and Trim=+3.
4. (Slot identity test) Without quitting Reaper, remove instance #1. Re-add it. The new instance should claim slots [0,1] (pool now has those free).
5. Quit Reaper, relaunch, reopen project. All three instances should land on their original slots [0,1], [2,3], [4,5] — preferred-slot reload working.

- [ ] **Step 7.5: Commit**

```bash
git add plugins/multipink/source/multipink_processor.cpp
git commit -m "multipink: persist parameters and preferred start slot

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 8: GUI (multipink.uidesc)

Build the declarative VSTGUI layout: title, Reference dropdown, Trim slider+display, Mute button, status LED (color-coded by `poolStatus_`), and a slot badge text.

**Files:**
- Replace: `plugins/multipink/resource/multipink.uidesc`

The slot badge and status LED need a way to read `claimedStart_`, `claimedChannels_`, and `poolStatus_` from the GUI thread. Steinberg's pattern for "read-only display from processor state" is to expose them as additional **read-only parameters** with `ParameterInfo::kIsReadOnly`. We add three such parameters (`kParamSlotStart`, `kParamSlotCount`, `kParamPoolStatus`) and update them from `process()` via `data.outputParameterChanges`.

- [ ] **Step 8.1: Add read-only parameter IDs**

In `multipink_ids.h`, append to the enum:

```cpp
    kParamSlotStart   = 200,   // read-only: 0..63 or -1
    kParamSlotCount   = 201,   // read-only: 1..64
    kParamPoolStatus  = 202,   // read-only: PoolStatus enum (0..3)
```

- [ ] **Step 8.2: Register the read-only parameters in `initialize()`**

```cpp
parameters.addParameter(new RangeParameter(
    STR16("Slot Start"), kParamSlotStart, STR16(""),
    -1.0, 63.0, -1.0, 64,
    ParameterInfo::kIsReadOnly));
parameters.addParameter(new RangeParameter(
    STR16("Slot Count"), kParamSlotCount, STR16(""),
    0.0, 64.0, 0.0, 64,
    ParameterInfo::kIsReadOnly));
parameters.addParameter(new RangeParameter(
    STR16("Pool Status"), kParamPoolStatus, STR16(""),
    0.0, 3.0, 0.0, 3,
    ParameterInfo::kIsReadOnly));
```

- [ ] **Step 8.3: Push display state from `process()`**

After `readParameterChanges(data);` in `process()`:

```cpp
if (auto* outChanges = data.outputParameterChanges) {
    int32 idx;
    auto pushDouble = [&](ParamID id, double v01) {
        auto* q = outChanges->addParameterData(id, idx);
        if (q) { int32 off = 0; q->addPoint(0, v01, off); }
    };
    // Normalize:
    //   slotStart in [-1, 63] -> [0,1]
    //   slotCount in [0,  64] -> [0,1]
    //   poolStatus in [0,  3] -> [0,1]
    pushDouble(kParamSlotStart,  ((double)claimedStart_ + 1.0) / 64.0);
    pushDouble(kParamSlotCount,  (double)claimedChannels_ / 64.0);
    pushDouble(kParamPoolStatus, (double)poolStatus_.load() / 3.0);
}
```

- [ ] **Step 8.4: Author `multipink.uidesc`**

Replace the file with a layout based on `ddelay.uidesc`. Width 300, height ~340. Sections:

- Title `MULTIPINK` (TitleFont, top, centered)
- Subtitle `Multichannel Pink Noise` (SubtitleFont)
- Reference dropdown (`COptionMenu`, control-tag = 100)
  - 3 entries: `-23 dBFS RMS`, `-20 dBFS RMS`, `-18 dBFS RMS`
- Trim slider (`CSlider`, horizontal, control-tag = 101) + `CTextEdit` showing dB
- Mute toggle button (`COnOffButton`, control-tag = 102)
- Status LED (`CView` with bitmap or simple colored `CLabel`, control-tag = 202; use `CParamDisplay` with custom value-to-color binding via a `CTextLabel` whose foreground is bound — easiest is a `COnOffButton` per status reused as colored squares; for simplicity start with a `CParamDisplay` showing text "OK"/"FALLBACK"/"EXHAUSTED")
- Slot badge (`CTextLabel`, control-tag binding to slotStart/slotCount via two side-by-side `CParamDisplay`s with prefix/suffix labels)
- Logo at bottom (`bitmap="logo"`)

Use the same color/font palette as `ddelay.uidesc`. Refer to `ddelay.uidesc` for the exact XML attribute syntax.

For the slot badge, a pragmatic minimal version is two `CParamDisplay` widgets side by side:

```xml
<view class="CTextLabel" origin="20, 280" size="60, 16" title="Slot:" font="InfoFont" font-color="TextDim"/>
<view class="CParamDisplay" origin="80, 280" size="40, 16" control-tag="200" font="InfoFont" font-color="TextLight" precision="0"/>
<view class="CTextLabel" origin="125, 280" size="20, 16" title="of 64" font="InfoFont" font-color="TextDim"/>
<view class="CTextLabel" origin="170, 280" size="60, 16" title="N:" font="InfoFont" font-color="TextDim"/>
<view class="CParamDisplay" origin="200, 280" size="40, 16" control-tag="201" font="InfoFont" font-color="TextLight" precision="0"/>
```

For the status LED, similarly:

```xml
<view class="CParamDisplay" origin="20, 305" size="260, 16" control-tag="202" font="InfoFont" font-color="TextLight" precision="0"/>
```

(A polished colored LED can come later; the read-only numeric display verifies the data flow first.)

- [ ] **Step 8.5: Build & GUI smoke-test**

```bash
cmake --build build-release --config Release -j8 --target multipink
```

In Reaper, load multipink. Verify:
- Title and subtitle render.
- Reference dropdown has 3 entries; selecting changes the gain.
- Trim slider moves; the numeric display updates.
- Mute button silences output.
- Slot Start shows "0" on the first instance, "2" on a second stereo instance, etc.
- Slot Count shows "2" on stereo, "4" on quad track.
- Pool Status shows "1" (ClaimedAtPreferred) or "2" (ClaimedFirstFit).

- [ ] **Step 8.6: Commit**

```bash
git add plugins/multipink/source/multipink_ids.h plugins/multipink/source/multipink_processor.cpp plugins/multipink/resource/multipink.uidesc
git commit -m "multipink: add GUI (Reference, Trim, Mute, slot badge, status)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 9: Calibration measurement (lock kCalibrationOffsetDb)

Measure the actual RMS produced by the plugin with `Reference=-23`, `Trim=0`, and adjust `kCalibrationOffsetDb` so that long-term RMS = -23.0 ±0.1 dBFS.

**Files:**
- Modify: `plugins/multipink/source/multipink_processor.h` (the `kCalibrationOffsetDb` constant)

- [ ] **Step 9.1: Render a 30 s reference**

In Reaper, on a stereo track:
1. multipink with Reference=-23, Trim=0, Mute off.
2. Render to file: 30 s of audio at the project sample rate (e.g., 48 kHz), 32-bit float WAV, stereo.
3. Save as `/tmp/multipink_cal.wav`.

- [ ] **Step 9.2: Measure RMS**

```bash
sox /tmp/multipink_cal.wav -n stat 2>&1 | grep "RMS"
```

The "RMS amplitude" line shows the linear amplitude. Convert to dBFS:

```bash
python3 -c "import math, sys; rms_lin = float('PASTE_RMS_HERE'); print(20*math.log10(rms_lin), 'dBFS RMS')"
```

(Or compute by hand: dB = 20·log10(rms_lin).)

- [ ] **Step 9.3: Compute the offset**

If measured = `M` dBFS RMS and target = `-23` dBFS RMS:
```
new_kCalibrationOffsetDb = -23.0 - M
```

- [ ] **Step 9.4: Update the constant**

In `multipink_processor.h`:

```cpp
// Calibration constant. Measured YYYY-MM-DD against a 30s render at 48 kHz,
// Reference=-23, Trim=0: raw RMS was M dBFS, so offset = -23.0 - M.
static constexpr double kCalibrationOffsetDb = <computed value>;
```

- [ ] **Step 9.5: Verify**

Rebuild, re-render 30 s, re-measure. Expected: `-23.0 ± 0.1 dBFS RMS`. If outside tolerance, iterate.

- [ ] **Step 9.6: Verify with -20 and -18**

Switch Reference to -20, render 30 s, measure: expect -20.0 ±0.1.
Switch Reference to -18, render 30 s, measure: expect -18.0 ±0.1.

- [ ] **Step 9.7: Commit**

```bash
git add plugins/multipink/source/multipink_processor.h
git commit -m "multipink: lock kCalibrationOffsetDb from RMS measurement

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 10: Calibration documentation

Write `doc/calibration.md` so future users / students can re-verify or recalibrate.

**Files:**
- Create: `plugins/multipink/doc/calibration.md`

- [ ] **Step 10.1: Write `doc/calibration.md`**

```markdown
# multipink — Calibration Procedure

This document explains how to verify (and, if needed, recalibrate) the
RMS reference levels of the `multipink` plugin.

## Background

`multipink` claims that with `Reference = -23 dBFS RMS` and `Trim = 0`, its
long-term per-channel output RMS is exactly -23.0 dBFS. The pink IIR
attenuates the upstream white noise by an amount that depends on the
filter coefficients (see `multipink_processor.h`, `kPinkB`/`kPinkA`). To
make the knob "0 dB" position correspond to a real reference RMS, a
hard-coded compensation `kCalibrationOffsetDb` is applied.

## Re-verifying

1. Add `multipink` to a stereo track in Reaper.
2. Set `Reference = -23 dBFS RMS`, `Trim = 0.0`, `Mute = off`.
3. Render 30 s at 48 kHz, 32-bit float WAV, stereo, to `multipink_cal.wav`.
4. Measure RMS:
   ```
   sox multipink_cal.wav -n stat 2>&1 | grep "RMS amplitude"
   ```
5. Convert to dBFS: `dB = 20 · log10(RMS_amplitude)`.
6. Expected: -23.0 ± 0.1 dBFS RMS per channel.

## Recalibrating

If the measured value drifts outside ±0.1 dB (e.g., after a coefficient
change in the pink filter), update the constant:

1. With `kCalibrationOffsetDb = 0.0`, render and measure as above. Call
   the measurement `M` (in dBFS).
2. Set `kCalibrationOffsetDb = -23.0 - M` in
   `plugins/multipink/source/multipink_processor.h`.
3. Rebuild.
4. Re-render and re-measure. Expect -23.0 ±0.1.
5. Verify the same constant works for Reference = -20 and -18 (the offset
   is filter-intrinsic, not reference-dependent — all three should land
   within ±0.1 dB of their nominal values).

## Why long-term RMS

The 64 LCG/IIR pairs are mutually independent; per-channel RMS converges
to the same value, but only over long integration times (~1 s and up).
Short windows (< 100 ms) will show channel-to-channel variation that
disappears in the 30-s integral.
```

- [ ] **Step 10.2: Commit**

```bash
git add plugins/multipink/doc/calibration.md
git commit -m "multipink: add calibration procedure doc

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Final Integration Check

- [ ] **Build a clean release**

```bash
rm -rf build-release
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -GXcode
cmake --build build-release --config Release -j8
```

Expected: **all** plugins (sdmx, xyprrot, b2xrot, lr2xhgr, m2xhgr, bamodulex, ddelay, **multipink**) build with no errors.

- [ ] **End-to-end test in Reaper**

1. Two stereo tracks, multipink on each. Verify Slot Start = 0 on instance #1, 2 on instance #2.
2. One quad track, multipink. Verify Slot Start = 4, Slot Count = 4.
3. Save project. Quit Reaper. Reopen project. Verify all three instances reclaim their original slots (Pool Status = 1).
4. Render 30 s on instance #1, measure RMS. Expected: -23.0 ±0.1 dBFS.
5. Verify two simultaneous instances on different tracks produce **decorrelated** output (sum-to-mono should not show comb filtering — quick check: route both instances to a single mono track; the spectrum should remain pink, not gain peaks/nulls).

- [ ] **Update the suite version**

In root `CMakeLists.txt`, bump `VERSION 0.1.3` → `VERSION 0.1.4`. Update the README "Plugins" section to list multipink.

- [ ] **Final commit (suite-level)**

```bash
git add CMakeLists.txt README.md
git commit -m "v0.1.4 — register multipink plugin in build, update README

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Notes for the Implementer

- **No test framework exists in this repo.** Verification is build + DAW + measurement tools (`sox`, Reaper meters, spectrum analyzers). Don't invent a test infrastructure unless the user asks.
- **Faust as a scratch tool is allowed** (Tasks 3.3, 4.4) but the generated `.cpp` files never land in `plugins/*/source/`. Use `/tmp/` for them.
- **Real-time safety:** `process()` must not allocate, lock, or call non-trivial system calls. The pool mutex is only acquired in `setActive`, which is called outside the audio thread. Keep it that way.
- **Atomic semantics:** `paramReferenceIdx_` etc. are `std::atomic<int>` / `std::atomic<double>` for safe handoff between the host thread (which calls `setState`/parameter writes) and the audio thread. Don't replace them with plain types.
- **Don't add per-channel trim.** The spec explicitly forbids it (§11). Resist the temptation.
- **Don't add a limiter.** Same reason (§11). The math says it's never needed.
