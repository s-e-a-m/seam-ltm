# CLAUDE.md — seam-ltm

Project-specific guidance. Read alongside the workspace root
`seam/CLAUDE.md`.

## Project identity

`seam-ltm` (Learning Through Making) is a **pedagogical VST3 plugin suite**.
Audience: students and practitioners learning DSP and plugin internals.
Stack: VST3 SDK + VSTGUI, CMake, hand-written C++.

## Core convention — Faust is the spec, C++ is the deliverable

The SEAM Faust libraries (`librerie/faust-libraries/src/seam.*.lib`) are the
**mathematical specification** for plugin DSP. Each `seam-ltm` plugin
**re-implements that specification by hand in readable C++**, citing the
relevant Faust source as a comment block at the top of its processor header
(`// FAUST REFERENCE (seam.<lib>.lib): ...`).

We do **not** use `faust -lang cpp` to generate plugin DSP code. Reasons:

- The whole point of the suite is to teach students to *read DSP from C++
  source*. Faust-generated code is opaque and would defeat the goal.
- No external build step (`faust` binary not required to build the suite).
- Manual port forces explicit thought about object lifecycle, memory layout,
  parameter smoothing, and SIMD opportunities — all of which are pedagogically
  valuable.
- When the Faust library evolves, the C++ port doesn't update silently; the
  human porting step is also a review step (especially valuable for
  calibration/measurement plugins).

When `faust -lang cpp` *is* acceptable: as a one-off **sketch/scratch tool**
during design (e.g., to verify a coefficient or compare a reference
implementation). The generated code never lands in `plugins/*/source/`.

## Plugin file layout (canonical)

Established by `ddelay` and `bamodulex`:

```
plugins/<name>/
├── CMakeLists.txt
├── source/
│   ├── <name>_ids.h           # plugin UID, parameter IDs
│   ├── <name>_processor.h     # opens with FAUST REFERENCE comment block
│   ├── <name>_processor.cpp   # IAudioProcessor + DSP implementation
│   └── version.h
├── resource/                  # VSTGUI .uidesc + assets (when GUI present)
└── doc/                       # plugin-specific documentation
```

Add complementary `.cpp/.h` pairs only when a real concern justifies it
(e.g., shared static state across instances). Do not split prematurely.

## Build

CMake at the repo root. Each plugin is registered with
`add_subdirectory(plugins/<name>)` in the root `CMakeLists.txt`.
VSTGUI live editing is disabled (`VSTGUI_LIVE_EDITING=0`) — the suite ships
finished GUIs, not editable templates.

VST3 SDK expected at `../vst3sdk` (sibling of `seam-ltm`), overridable via
`-DSEAM_VST3SDK_DIR=...`.

## Peer-aware plugins (pattern introduced by `multipink`)

`multipink` is the first plugin in the suite to use **shared static state in
the `.vst3` module** for cross-instance coordination (a 64-slot allocation
bitmap). The implementation lives in `plugins/multipink/source/multipink_pool.{h,cpp}`.

If a future plugin needs a similar peer-aware behaviour (instances that need
to know about each other to function correctly), reuse `multipink_pool` as
the reference implementation rather than reinventing the pattern. **Do not
retrofit peer awareness into existing plugins speculatively** — only when a
real user-facing problem motivates it. Each candidate upgrade gets its own
brainstorming and spec.

## Working language

Giuseppe communicates in Italian. Code, commits, and documentation are in
English (consistent with the rest of the SEAM workspace).
