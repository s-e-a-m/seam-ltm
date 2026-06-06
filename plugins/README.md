# SEAM-LTM Plugins — conventions & contributor guide

This directory holds the plugins of the **SEAM-LTM** (Learning Through Making)
VST3 suite. This document is the reference for how a plugin is structured,
built, tested, validated, and documented, and how to add a new one. It is meant
for contributors as much as for the maintainers.

If you only read one thing: **the SEAM Faust libraries are the specification;
the C++ is the deliverable.** See [Core convention](#core-convention) below.

> Companion docs: the repo root `../CLAUDE.md` (project guidance), the root
> `../README.md` (user-facing install/build), and the Faust libraries in
> `../../faust-libraries/src`.

---

## Core convention

The SEAM Faust libraries (`../../faust-libraries/src/seam.*.lib`) are the
**mathematical specification** of every plugin's DSP. Each plugin
**re-implements that specification by hand in readable C++** and cites the
canonical Faust function at the top of its processor header:

```cpp
// FAUST REFERENCE (seam.ambisonics.lib): sam.x2uhj
```

We do **not** run `faust -lang cpp` to generate plugin DSP. The point of the
suite is to teach reading DSP from C++ source; hand-porting also forces explicit
thought about lifecycle, memory layout, smoothing, and SIMD, and the human
porting step doubles as a review step. `faust -lang cpp` is acceptable only as a
throwaway sketch during design — generated code never lands in
`plugins/*/source/`.

Two corollaries:

- **Every plugin ships a finished GUI** — even stateless, parameterless ones
  (e.g. `sdmx`, `x2uhj`). A plugin without an editor falls back to the host's
  generic view and breaks suite consistency. Model the GUI on
  `sdmx/resource/sdmx.uidesc`.
- **Every plugin's `doc/<name>.dsp` points to the canonical library function**
  (it does not inline DSP). This keeps a single source of truth and keeps the
  library honest — generating the docs actually compiles the Faust spec.

---

## Anatomy of a plugin

```
plugins/<name>/
├── CMakeLists.txt              # build target (registered in ../../CMakeLists.txt)
├── source/
│   ├── <name>_ids.h           # plugin FUID (unique, never changes)
│   ├── <name>_processor.h     # opens with the FAUST REFERENCE comment block
│   ├── <name>_processor.cpp   # IAudioProcessor + DSP implementation + factory
│   ├── <name>_dsp.h           # (optional) SDK-free header-only DSP core
│   └── version.h              # metadata strings
├── resource/
│   └── <name>.uidesc          # VSTGUI editor (logo + any controls)
└── doc/
    ├── <name>.dsp             # Faust spec pointer (see below)
    ├── <name>-svg/            # generated block diagrams (faust -svg)
    ├── <name>.pdf             # generated mathematical doc (faust2mathdoc)
    └── references/            # (optional) primary-source PDFs
```

Add complementary `.cpp/.h` pairs only when a real concern justifies it (e.g.
`multipink/source/multipink_pool.*` for cross-instance shared state, or a
header-only SDK-free DSP core like `x2uhj/source/x2uhj_dsp.h` so the math can be
unit-tested without the VST3 SDK). Do not split prematurely.

### Shared code

`_common/` is **not a plugin** — the leading underscore sorts it apart from the
plugin directories. It holds shared headers (e.g. `seam_haar.h`,
`seam_rotation.h`, `seam_btox.h`) and shared GUI assets
(`_common/resource/seam_logo.png`, fonts). Plugins reference it via relative
paths in their `CMakeLists.txt` (`../_common`); it is not added with
`add_subdirectory`.

### Naming

- Plugin target/dir: lowercase short name (`x2uhj`, `m2xhgr`, `ddelay`).
- FUID in `<name>_ids.h`: four 32-bit words, each exactly 8 hex digits,
  generated once and never changed.

---

## Faust library link & canonical functions

`doc/<name>.dsp` keeps a rich `declare` + prose header (it is the human-readable
spec and feeds the math-doc metadata), then points `process` at the library:

```faust
declare name "SEAM X2UHJ"; …               // metadata + prose + references
import("seam.lib");
process = sam.x2uhj;                         // canonical function, no inlined DSP
```

Library namespace prefixes (declared in `seam.lib`): `sba` basic · `sma` math ·
`sfi` filters · `sre` reverbs · `sms` schroeder · `sst` stereophony ·
`sam` ambisonics · `sdw` dwt · `sno` noises · `scs` csound · `scy` cyclone ·
`sjm` moorer · `scr` roads · `sfv` freeverb · `sff` ffunctions · `san` analyzers ·
`smg` gerzon.

Canonical function per plugin:

| Plugin | Canonical Faust | Library |
|---|---|---|
| sdmx | `sst.sdmx` | seam.stereophony.lib |
| xyprrot | `sam.rotateYPR` | seam.ambisonics.lib |
| b2xrot | `sam.btox` · `sam.rotateYPR` | seam.ambisonics.lib |
| m2xhgr | `sam.m2xhgr` (Haar via `sdw.haarmn`) | seam.ambisonics.lib |
| lr2xhgr | `sam.lr2xhgr` | seam.ambisonics.lib |
| bamodulex | `sam.bamodulex` | seam.ambisonics.lib |
| ddelay | `sma.imdelay` | seam.math.lib |
| multipink | `sno.multipink` | seam.noises.lib |
| x2uhj | `sam.x2uhj` | seam.ambisonics.lib |

If you add a plugin whose DSP is not yet in the library, **add the canonical
function to the appropriate `seam.*.lib` first** (in the `faust-libraries`
repo), then point the plugin at it.

---

## Generating documentation

Each plugin's `doc/` carries block diagrams (`<name>-svg/`) and a mathematical
documentation PDF (`<name>.pdf`), both generated from `doc/<name>.dsp`.

```bash
# from the repo root
tools/gen-faust-doc.sh            # all plugins
tools/gen-faust-doc.sh x2uhj sdmx # specific plugins
```

The script sets `FAUST_LIB_PATH=../faust-libraries/src` (override by exporting
it), runs `faust -svg` and `faust2mathdoc`, copies the PDF out of the `-mdoc`
scaffold, and cleans up.

**Toolchain**

```bash
# macOS
brew install faust svg2pdf          # svg2pdf is required by faust2mathdoc
# plus a TeX distribution providing pdflatex + breqn (MacTeX or BasicTeX)
```

Notes:
- `faust -svg` names sub-diagram files by memory address, so they change on
  every run — expect noisy diffs in `<name>-svg/`. That is normal.
- Because the `.dsp` import the seam libraries, doc generation requires
  `FAUST_LIB_PATH` to point at `faust-libraries/src` (the script handles it).

---

## Building

The VST3 SDK is expected at `../vst3sdk` (sibling of the repo); override with
`-DSEAM_VST3SDK_DIR=/path/to/vst3sdk`. The macOS generator is **Xcode**
(multi-config), so always pass `--config`.

```bash
# configure once
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -GXcode \
      -DSEAM_VST3SDK_DIR=/path/to/vst3sdk

# build everything …
cmake --build build-release --config Release -j8
# … or a single plugin
cmake --build build-release --config Release --target x2uhj -j8
```

Output bundles land in `build-release/VST3/Release/` and are symlinked into
`~/Library/Audio/Plug-Ins/VST3/` (macOS). On Linux, copy them to `~/.vst3/`.

Each plugin's `CMakeLists.txt` links `sdk vstgui_support`, registers its
resources (`.uidesc` + shared logo/font from `../_common/resource`), and is added
to the root `CMakeLists.txt` via `add_subdirectory(plugins/<name>)`. VSTGUI live
editing is disabled suite-wide (`VSTGUI_LIVE_EDITING=0`).

---

## Testing

Unit tests use **doctest** (vendored at `../tests/doctest/doctest.h`) and target
SDK-free DSP cores, so they compile with just a C++ compiler.

```bash
# fast loop — no CMake, no SDK
c++ -std=c++17 -I plugins/x2uhj/source -I tests tests/x2uhj_dsp_test.cpp \
    -o /tmp/t && /tmp/t

# or via CTest (multi-config: pass -C)
ctest --test-dir build-release -C Release --output-on-failure
```

To make a plugin's DSP testable, factor the math into an SDK-free header-only
core (see `x2uhj/source/x2uhj_dsp.h`) and add a test in `../tests/` wired in
`../tests/CMakeLists.txt`. Tests are built when `SEAM_BUILD_TESTS=ON` (default).

---

## Validating

Run the Steinberg validator (built with the SDK) on a bundle:

```bash
build-release/bin/Release/validator build-release/VST3/Release/<name>.vst3
```

A clean plugin reports `tests passed, 0 tests failed`. Unsupported speaker
arrangements being rejected is expected for layout-specific plugins.

---

## Adding a new plugin (checklist)

1. **Spec first.** If the DSP is not already in the Faust libraries, add the
   canonical function to the right `seam.*.lib` and verify it compiles
   (`faust …`). This is the spec.
2. Create `plugins/<name>/` with the [anatomy](#anatomy-of-a-plugin) above.
   Generate a fresh, unique FUID in `<name>_ids.h`.
3. Implement the DSP by hand in C++. Prefer an SDK-free `*_dsp.h` core when the
   math benefits from unit tests. Open the processor header with the
   `// FAUST REFERENCE (seam.<lib>.lib): …` block.
4. Add the GUI (`resource/<name>.uidesc`, modeled on `sdmx`) and a `createView`.
5. Write `CMakeLists.txt` (copy a sibling), and register it in the root
   `CMakeLists.txt`.
6. Add tests in `../tests/` for the DSP core.
7. Build, run the validator.
8. Write `doc/<name>.dsp` as a library pointer; run `tools/gen-faust-doc.sh
   <name>` to produce the diagrams and the math-doc PDF.
9. Add the plugin to the table and screenshots in the root `../README.md`.

---

## Tree hygiene

- **Never commit build output.** `build/`, `build-release/`, and `*-mdoc/` are
  ignored. If you ever see a `build*` directory *inside* `plugins/<name>/`, it is
  a stray local build — remove it (it is ignored, so nothing is lost).
- `.DS_Store`, `*.wav` (measurement captures), `.venv/`, and `__pycache__/` are
  ignored — see `../.gitignore`.
- Top-level documentation lives in `../docs/` (single tree). Per-plugin docs
  live in `plugins/<name>/doc/`. Do not reintroduce a top-level `doc/`.
- Design specs and plans live under `../docs/superpowers/`.
