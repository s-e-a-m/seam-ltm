# SEAM-LTM — Learning Through Making

<img src="seam_p_b.png" alt="SEAM logo" width="120" align="right">

A pedagogical VST3 plugin suite for the [SEAM](https://github.com/s-e-a-m)
project (Sustained Electro-Acoustic Music).

Five plugins built directly on the Steinberg VST3 SDK — no JUCE, no
frameworks. Clean C++17, VSTGUI for the interface, and the minimum code
needed to do the job.

## Plugins

| Plugin | I/O | Description |
|---|---|---|
| **SDMX** | stereo &rarr; stereo | Sum and Difference Matrix (MS encoding/decoding). Involutory: A = A⁻¹ |
| **B2Xrot** | B-format &rarr; AmbiX | Classic B-format to first-order AmbiX conversion with YPR rotation |
| **M2XHGR** | mono &rarr; AmbiX | Mono to first-order AmbiX via Haar encoding with YPR rotation |
| **LR2XHGR** | stereo &rarr; AmbiX | Stereo to first-order AmbiX via Haar with divergence control and YPR rotation |
| **XYPRrot** | AmbiX &rarr; AmbiX | First-order AmbiX rotation (Yaw, Pitch, Roll) |

### Screenshots

| SDMX | B2Xrot | XYPRrot |
|:---:|:---:|:---:|
| ![SDMX](doc/img/sdmx.png) | ![B2Xrot](doc/img/b2xrot.png) | ![XYPRrot](doc/img/xyprrot.png) |

| M2XHGR | LR2XHGR |
|:---:|:---:|
| ![M2XHGR](doc/img/m2xhgr.png) | ![LR2XHGR](doc/img/lr2xhgr.png) |

## Requirements

### VST3 SDK

The plugins depend on the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk),
cloned **with submodules** alongside this repo:

```bash
git clone --recursive https://github.com/steinbergmedia/vst3sdk.git
```

> **Note:** the `--recursive` flag is essential — it pulls VSTGUI and other
> components needed for the build.

The expected directory layout is:

```
some-folder/
├── vst3sdk/       ← git clone --recursive here
└── seam-ltm/      ← this repo
```

If your SDK is elsewhere, pass the path at configure time:

```bash
cmake -B build-release -DSEAM_VST3SDK_DIR=/path/to/vst3sdk
```

### CMake

- CMake 3.25+

### macOS

- Xcode (not just Command Line Tools — the SDK cmake needs the full app)
- Tested on macOS 15 Sequoia

### Linux (Arch / Manjaro)

```bash
sudo pacman -S base-devel cmake gcc pkg-config \
    libx11 libxcb xcb-util xcb-util-cursor \
    cairo pango fontconfig freetype2 \
    libxkbcommon gtk3
```

### Linux (Debian / Ubuntu)

```bash
sudo apt install build-essential cmake pkg-config \
    libx11-dev libxcb1-dev libxcb-util-dev libxcb-cursor-dev \
    libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype6-dev \
    libxkbcommon-dev libgtk-3-dev
```

### GCC 15 note

The VSTGUI header `iplatformtaskexecutor.h` is missing `#include <cstdint>`,
which causes a build failure on GCC 15+ (Arch, Manjaro, Fedora).
The CMake configuration **patches this automatically** at configure time —
no manual intervention needed.

## Build

### macOS

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -GXcode
cmake --build build-release --config Release -j8
```

Plugins are output to `build-release/VST3/Release/` and automatically
symlinked to `~/Library/Audio/Plug-Ins/VST3/`.

### Linux

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release -j$(nproc)
```

Plugins are output to `build-release/VST3/Release/`.
To install system-wide:

```bash
# Copy all .vst3 bundles to the standard VST3 path
cp -r build-release/VST3/Release/*.vst3 ~/.vst3/
```

Most Linux DAWs (Ardour, REAPER, Bitwig, Carla) scan `~/.vst3/` automatically.

## Design

### DSP
Each plugin implements its DSP in a single `processMatrix` or
`processAudio` template, supporting both 32-bit and 64-bit sample formats.
The math follows the Faust reference implementations in
[seam.stereophony.lib](https://github.com/s-e-a-m/faust-libraries) and
the Gerzon/Blumlein theoretical framework.

### GUI
All plugins share a consistent visual identity:

- **300px wide**, variable height
- **Source Code Pro Light** monospace font throughout
- Dark background (#292c2f), light text
- Horizontal sliders with center-origin fill (bipolar rotation controls)
- SEAM logo at bottom

### Architecture
Built on `SingleComponentEffect` (processor + controller in one class).
No external dependencies beyond the VST3 SDK and VSTGUI.
Cross-platform: macOS (Xcode) and Linux (GCC/Clang).

## Theory

The plugin suite covers the Blumlein &rarr; Gerzon lineage of spatial audio:

- **Blumlein (1933)**: sum-and-difference matrix for MS stereophony
- **Gerzon (1973)**: Ambisonics as a generalization of Blumlein's principles
- **Haar transform**: orthogonal encoding of stereo/mono signals into Ambisonic B-format

## License

GPL-3.0 — see [LICENSE](LICENSE).

## Author

Giuseppe Silvi — [s-e-a-m](https://github.com/s-e-a-m)
