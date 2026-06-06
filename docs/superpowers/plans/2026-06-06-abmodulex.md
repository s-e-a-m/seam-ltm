# abmodulex Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `abmodulex`, a stateless VST3 plugin converting 4-channel A-format (tetrahedral mic: LFU,RFD,RBU,LBD) into First-Order AmbiX (ACN/SN3D), the front-end of the TETRAREC → x2uhj → L,R chain.

**Architecture:** A pure involutory Hadamard/2 matrix (identical coefficients to `bamodulex`). The math lives in an SDK-free header-only core (`abmodulex_dsp.h`) unit-tested with doctest; a thin `SingleComponentEffect` wraps it; the canonical Faust function `sam.abmodulex` is deposited in `seam.ambisonics.lib`.

**Tech Stack:** C++17, VST3 SDK (at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`), CMake ≥3.25 (Xcode multi-config), doctest, Faust (for the library + docs).

---

## Key conventions

- **SDK path:** configure with `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- **Matrix (A-format → AmbiX ACN):**
  ```
  a0 = (LFU + RFD + RBU + LBD) / 2     // W
  a1 = (LFU − RFD − RBU + LBD) / 2     // Y
  a2 = (LFU − RFD + RBU − LBD) / 2     // Z
  a3 = (LFU + RFD − RBU − LBD) / 2     // X
  ```
  Same coefficients as `bamodulex`; the matrix is involutory (M²=I).
- **Fast C++ test loop (no SDK):**
  `c++ -std=c++17 -I plugins/abmodulex/source -I tests tests/abmodulex_dsp_test.cpp -o /tmp/ab_test && /tmp/ab_test`
- Reference siblings: `plugins/bamodulex/source/*` (matrix processor + GUI) and `plugins/x2uhj/source/x2uhj_dsp.h` (SDK-free core pattern).
- Commit messages end with the `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` trailer. One commit per task.

---

## Task 0: Branch

- [ ] **Step 1:** `cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm && git checkout -b feature/abmodulex`
- [ ] **Step 2:** `git status` → clean, on `feature/abmodulex`.

---

## Task 1: DSP core + doctest (TDD)

**Files:**
- Create: `plugins/abmodulex/source/abmodulex_dsp.h`
- Create: `tests/abmodulex_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** — `tests/abmodulex_dsp_test.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "abmodulex_dsp.h"

using namespace Seam::abmodulex;

TEST_CASE("abmodulex: unit LFU spreads equally to all ACN channels") {
    double a0,a1,a2,a3;
    encode(1.0, 0.0, 0.0, 0.0, a0,a1,a2,a3);
    CHECK(a0 == doctest::Approx(0.5));
    CHECK(a1 == doctest::Approx(0.5));
    CHECK(a2 == doctest::Approx(0.5));
    CHECK(a3 == doctest::Approx(0.5));
}

TEST_CASE("abmodulex is involutory (round-trips with itself / bamodulex)") {
    const double lfu=0.3, rfd=-0.7, rbu=0.1, lbd=0.9;
    double a0,a1,a2,a3;
    encode(lfu,rfd,rbu,lbd, a0,a1,a2,a3);
    double b0,b1,b2,b3;
    encode(a0,a1,a2,a3, b0,b1,b2,b3);   // applying the same map twice == identity
    CHECK(b0 == doctest::Approx(lfu));
    CHECK(b1 == doctest::Approx(rfd));
    CHECK(b2 == doctest::Approx(rbu));
    CHECK(b3 == doctest::Approx(lbd));
}
```

- [ ] **Step 2: Verify it fails to compile** — Run:
  `c++ -std=c++17 -I plugins/abmodulex/source -I tests tests/abmodulex_dsp_test.cpp -o /tmp/ab_test`
  Expected: FAIL — `abmodulex_dsp.h` not found.

- [ ] **Step 3: Implement** — `plugins/abmodulex/source/abmodulex_dsp.h`:
```cpp
// SEAM-LTM · abmodulex — SDK-free DSP core (header-only, unit-testable).
//
// A-format tetrahedral mic (LFU,RFD,RBU,LBD) -> First-Order AmbiX (ACN/SN3D:
// a0=W, a1=Y, a2=Z, a3=X). Involutory Hadamard/2 matrix — identical
// coefficients to bamodulex; applying the map twice is the identity.
#pragma once

namespace Seam { namespace abmodulex {

inline void encode(double lfu, double rfd, double rbu, double lbd,
                   double& a0, double& a1, double& a2, double& a3) {
    a0 = (lfu + rfd + rbu + lbd) * 0.5; // W
    a1 = (lfu - rfd - rbu + lbd) * 0.5; // Y
    a2 = (lfu - rfd + rbu - lbd) * 0.5; // Z
    a3 = (lfu + rfd - rbu - lbd) * 0.5; // X
}

}} // namespace
```

- [ ] **Step 4: Verify pass** — Run:
  `c++ -std=c++17 -I plugins/abmodulex/source -I tests tests/abmodulex_dsp_test.cpp -o /tmp/ab_test && /tmp/ab_test`
  Expected: PASS (2 cases).

- [ ] **Step 5: Wire into CMake** — in `tests/CMakeLists.txt`, after the existing `x2uhj_dsp_test` block, append:
```cmake
add_executable(abmodulex_dsp_test
    abmodulex_dsp_test.cpp
)
target_include_directories(abmodulex_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/abmodulex/source
)
target_compile_features(abmodulex_dsp_test PRIVATE cxx_std_17)
add_test(NAME abmodulex_dsp_test COMMAND abmodulex_dsp_test)
```

- [ ] **Step 6: Commit**
```bash
git add plugins/abmodulex/source/abmodulex_dsp.h tests/abmodulex_dsp_test.cpp tests/CMakeLists.txt
git commit -m "$(printf 'feat(abmodulex): A-format->AmbiX matrix core + involution test\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 2: IDs + version

**Files:**
- Create: `plugins/abmodulex/source/abmodulex_ids.h`
- Create: `plugins/abmodulex/source/version.h`

- [ ] **Step 1:** `plugins/abmodulex/source/abmodulex_ids.h`:
```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — A-format → AmbiX
// Unique identifier. Stateless: no parameter IDs.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"

namespace Seam {
// Generated once, never change. Each word is exactly 8 hex digits.
static const Steinberg::FUID ABMODULEXProcessorUID (0x5E4D000A, 0xA1B2C3D4, 0x5E4DAB7D, 0x0000000A);
} // namespace Seam
```

- [ ] **Step 2:** `plugins/abmodulex/source/version.h`:
```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — Version and metadata
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "abmodulex.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM ABMODULEX – A-format to AmbiX (64Bit)"
#else
#define stringFileDescription   "SEAM ABMODULEX – A-format to AmbiX"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 3: Commit**
```bash
git add plugins/abmodulex/source/abmodulex_ids.h plugins/abmodulex/source/version.h
git commit -m "$(printf 'feat(abmodulex): plugin UID and version metadata\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 3: Processor (header + implementation)

**Files:**
- Create: `plugins/abmodulex/source/abmodulex_processor.h`
- Create: `plugins/abmodulex/source/abmodulex_processor.cpp`

- [ ] **Step 1:** `plugins/abmodulex/source/abmodulex_processor.h`:
```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — A-format → AmbiX
//
// Converts a 4-channel A-format tetrahedral microphone signal
// (LFU, RFD, RBU, LBD) into First-Order AmbiX (ACN/SN3D: W, Y, Z, X).
// Front-end of the TETRAREC → x2uhj → L,R chain. Inverse of bamodulex;
// the matrix is involutory (M² = I), so the coefficients are identical.
//
//   a0 = (LFU + RFD + RBU + LBD) / 2   // W
//   a1 = (LFU − RFD − RBU + LBD) / 2   // Y
//   a2 = (LFU − RFD + RBU − LBD) / 2   // Z
//   a3 = (LFU + RFD − RBU − LBD) / 2   // X
//
// Pure matrix — no capsule-compensation filtering.
//
// FAUST REFERENCE (seam.ambisonics.lib): sam.abmodulex
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "abmodulex_dsp.h"

namespace Seam {

class ABMODULEXProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    ABMODULEXProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ABMODULEXProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    template <typename S> void processBlock(S** in, S** out, Steinberg::int32 n);
};

} // namespace Seam
```

- [ ] **Step 2:** `plugins/abmodulex/source/abmodulex_processor.cpp`:
```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "abmodulex_processor.h"
#include "abmodulex_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include <cstring>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

ABMODULEXProcessor::ABMODULEXProcessor() {}

tresult PLUGIN_API ABMODULEXProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;
    // 4ch A-format in, 4ch AmbiX out (host routes the 4 channels by index).
    addAudioInput (STR16("A-format In"), SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("AmbiX Out"),   SpeakerArr::kAmbi1stOrderACN);
    return kResultOk;
}

tresult PLUGIN_API ABMODULEXProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API ABMODULEXProcessor::process(ProcessData& data) {
    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    int32 numCh = data.inputs[0].numChannels;
    uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    if (data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels)) {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        for (int32 i = 0; i < numCh; ++i)
            if (in[i] != out[i]) memset(out[i], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;

    if (numCh < 4) {  // need all four capsules; otherwise pass through
        for (int32 i = 0; i < numCh; ++i)
            if (in[i] != out[i]) memcpy(out[i], in[i], bytes);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processBlock<Sample32>(reinterpret_cast<Sample32**>(in),
                               reinterpret_cast<Sample32**>(out), data.numSamples);
    else
        processBlock<Sample64>(reinterpret_cast<Sample64**>(in),
                               reinterpret_cast<Sample64**>(out), data.numSamples);
    return kResultOk;
}

template <typename S>
void ABMODULEXProcessor::processBlock(S** in, S** out, int32 n) {
    for (int32 i = 0; i < n; ++i) {
        double a0,a1,a2,a3;
        abmodulex::encode((double)in[0][i], (double)in[1][i],
                          (double)in[2][i], (double)in[3][i],
                          a0,a1,a2,a3);
        out[0][i] = (S)a0; out[1][i] = (S)a1; out[2][i] = (S)a2; out[3][i] = (S)a3;
    }
}

tresult PLUGIN_API ABMODULEXProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ABMODULEXProcessor::setState(IBStream*) { return kResultOk; }
tresult PLUGIN_API ABMODULEXProcessor::getState(IBStream*) { return kResultOk; }

tresult PLUGIN_API ABMODULEXProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0]) == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API ABMODULEXProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "abmodulex.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::ABMODULEXProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM ABMODULEX",
        0,
        "Fx|Tools",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::ABMODULEXProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 3: Commit**
```bash
git add plugins/abmodulex/source/abmodulex_processor.h plugins/abmodulex/source/abmodulex_processor.cpp
git commit -m "$(printf 'feat(abmodulex): VST3 processor (A-format 4ch -> AmbiX 4ch)\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 4: GUI resource

**Files:**
- Create: `plugins/abmodulex/resource/abmodulex.uidesc`

- [ ] **Step 1:** `plugins/abmodulex/resource/abmodulex.uidesc`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="12"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="TextDim" rgba="#888888ff"/>
    </colors>

    <template name="view" class="CViewContainer" origin="0, 0" size="300, 200"
              minSize="300, 200" maxSize="300, 200"
              background-color="BgDark" background-color-draw-style="filled">

        <view class="CTextLabel" origin="0, 18" size="300, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM ABMODULEX" transparent="true"/>
        <view class="CTextLabel" origin="0, 46" size="300, 18" font="SubtitleFont"
              font-color="TextDim" text-alignment="center" title="A-format &#x2192; AmbiX" transparent="true"/>
        <view class="CTextLabel" origin="0, 68" size="300, 16" font="InfoFont"
              font-color="TextDim" text-alignment="center"
              title="LFU RFD RBU LBD &#x2192; W Y Z X" transparent="true"/>

        <view class="CView" origin="30, 100" size="240, 77" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags/>
</vstgui-ui-description>
```

- [ ] **Step 2: Commit**
```bash
git add plugins/abmodulex/resource/abmodulex.uidesc
git commit -m "$(printf 'feat(abmodulex): branded GUI\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 5: CMake + register + build

**Files:**
- Create: `plugins/abmodulex/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)

- [ ] **Step 1:** `plugins/abmodulex/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-abmodulex
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM ABMODULEX – A-format to AmbiX"
)

set(abmodulex_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/abmodulex_ids.h
    source/abmodulex_dsp.h
    source/abmodulex_processor.cpp
    source/abmodulex_processor.h
    source/version.h
    resource/abmodulex.uidesc
)

set(target abmodulex)

smtg_add_vst3plugin(${target} ${abmodulex_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/abmodulex.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.abmodulex"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

- [ ] **Step 2: Register** — in root `CMakeLists.txt`, after `add_subdirectory(plugins/x2uhj)` add:
```cmake
add_subdirectory(plugins/abmodulex)
```

- [ ] **Step 3: Configure + build**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -GXcode \
  -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build-release --config Release --target abmodulex -j8
```
Expected: `** BUILD SUCCEEDED **`, bundle at `build-release/VST3/Release/abmodulex.vst3`.

- [ ] **Step 4: Run unit tests via CTest**
```bash
cmake --build build-release --config Release --target abmodulex_dsp_test -j8
ctest --test-dir build-release -C Release -R abmodulex_dsp_test --output-on-failure
```
Expected: test passes.

- [ ] **Step 5: Commit**
```bash
git add plugins/abmodulex/CMakeLists.txt CMakeLists.txt
git commit -m "$(printf 'build(abmodulex): register plugin target\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 6: Validate

**Files:** none (verification only)

- [ ] **Step 1:** Run the validator
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
VALIDATOR=$(find build-release -name validator -type f -perm +111 | head -1)
PLUGIN=$(find build-release/VST3/Release -name "abmodulex.vst3" | head -1)
"$VALIDATOR" "$PLUGIN" 2>&1 | grep -E "Result:"
```
Expected: `Result: N tests passed, 0 tests failed`.

---

## Task 7: Faust library + doc generation

**Files:**
- Modify: `../faust-libraries/src/seam.ambisonics.lib`
- Create: `plugins/abmodulex/doc/abmodulex.dsp`

- [ ] **Step 1: Add the canonical function** — append to `../faust-libraries/src/seam.ambisonics.lib` (use Bash, the Read tool refuses .lib):
```bash
cat >> /Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src/seam.ambisonics.lib <<'EOF'
//
//------------------------------------------------ A-FORMAT -> AMBIX (ACN) ---
// Tetrahedral microphone A-format (LFU,RFD,RBU,LBD) -> First-order AmbiX
// (ACN: a0=W, a1=Y, a2=Z, a3=X). Inverse of bamodulex; the Hadamard/2
// matrix is involutory (M^2 = I), so the coefficients are identical.
// SEAM-LTM abmodulex plugin (TETRAREC front-end).
abmodulex(lfu,rfd,rbu,lbd) = a0, a1, a2, a3
  with {
    a0 = (lfu + rfd + rbu + lbd) / 2;
    a1 = (lfu - rfd - rbu + lbd) / 2;
    a2 = (lfu - rfd + rbu - lbd) / 2;
    a3 = (lfu + rfd - rbu - lbd) / 2;
  };
//process = sno.multipink(4,0.5) : abmodulex;
EOF
```

- [ ] **Step 2: Verify it compiles**
```bash
SRC=/Users/giuseppe/Documents/github/seam/librerie/faust-libraries/src
printf 'import("seam.lib");\nprocess = sam.abmodulex;\n' > /tmp/abtest.dsp
faust -I "$SRC" /tmp/abtest.dsp >/dev/null 2>/tmp/abe && echo "OK" || { echo FAIL; head -5 /tmp/abe; }
```
Expected: OK.

- [ ] **Step 3: Commit the library** (in the faust-libraries repo)
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/faust-libraries
git add src/seam.ambisonics.lib
git commit -m "$(printf 'ambisonics: add abmodulex (A-format -> AmbiX, ACN)\n\nInverse of bamodulex (involutory). Canonical Faust for the seam-ltm\nabmodulex plugin. The FuMa smg.abmodule stays as the Gerzon reference.\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
```

- [ ] **Step 4: Create the doc .dsp** — `plugins/abmodulex/doc/abmodulex.dsp`:
```faust
// SEAM-LTM · ABMODULEX — Faust spec / block-diagram source.
//
// Canonical DSP: sam.abmodulex in seam.ambisonics.lib
//   A-format tetrahedral mic (LFU,RFD,RBU,LBD) -> First-order AmbiX (ACN).
//   Inverse of bamodulex; the matrix is involutory. The C++ plugin
//   re-implements this by hand (see plugins/abmodulex/source).
//
// Regenerate diagrams:
//   ../../tools/gen-faust-doc.sh abmodulex
//
import("seam.lib");
process = sam.abmodulex;
```

- [ ] **Step 5: Generate diagrams + math doc**
```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
tools/gen-faust-doc.sh abmodulex
```
Expected: `done abmodulex: N svg + abmodulex.pdf`.

- [ ] **Step 6: Commit (seam-ltm)**
```bash
git add plugins/abmodulex/doc/abmodulex.dsp plugins/abmodulex/doc/abmodulex-svg plugins/abmodulex/doc/abmodulex.pdf
git commit -m "$(printf 'docs(abmodulex): library-pointer .dsp + svg/pdf diagrams\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Task 8: README row

**Files:**
- Modify: `README.md`

- [ ] **Step 1:** In `README.md`, add a row to the Plugins table after the X2UHJ row:
```markdown
| **ABMODULEX** | A-format 4ch &rarr; AmbiX 4ch | Tetrahedral mic A-format (LFU,RFD,RBU,LBD) to First-Order AmbiX (ACN/SN3D). Front-end of the TETRAREC chain (→ X2UHJ → stereo). Involutory matrix (inverse of BAMODULEX). Stateless |
```

- [ ] **Step 2: Commit**
```bash
git add README.md
git commit -m "$(printf 'docs: add ABMODULEX to README plugin table\n\nCo-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>')"
```

---

## Self-review notes (resolved)

- **Spec coverage:** §1 purpose → Task 3 header; §2 matrix/involution → Task 1 core+test, Task 7 lib; §3 SDK-free testable core → Task 1; §4 wrapper → Task 3; §5 GUI → Task 4; §6 backbone deposit → Task 7; §7 layout → Tasks 1–7; §8 testing/validation → Tasks 1,5,6; §9 out-of-scope respected (no filters, no params).
- **Type/name consistency:** `Seam::abmodulex::encode(lfu,rfd,rbu,lbd, a0,a1,a2,a3)` used identically in core, test, and processor; UID `ABMODULEXProcessorUID`; class `ABMODULEXProcessor`; target `abmodulex`.
- **No placeholders:** all code blocks complete; commands have expected output.
- The DSP core uses the same SDK-free header pattern as `x2uhj_dsp.h`; tests run via the doctest foundation already in `tests/`.
```
