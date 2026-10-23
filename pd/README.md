# Pd externals — the STONE chain

Pure Data externals that run the same DSP as three seam-ltm VST3 plugins, so that the STONE chain can live inside a Pd patch without a plugin host.

| External | Plugin | What it does |
|---|---|---|
| `m2xhgr~` | [M2XHGR](../plugins/m2xhgr) | mono → AmbiX by Haar wavelet decomposition, trims and rotation |
| `lr2xhgr~` | [LR2XHGR](../plugins/lr2xhgr) | stereo → AmbiX: two Haar banks, a divergence, shared trims, rotation |
| `bamodulex~` | [BAMODULEX](../plugins/bamodulex) | AmbiX → the four feeds of an upright tetrahedron (LFU RFD RBU LBD) |

Each external is two files.
`<name>_engine.h` holds everything that computes — parameters, ramps, clipping, block processing — and includes the plugin's own `*_dsp.h`; it knows nothing about Pd and is tested under ctest (`tests/pd_*_engine_test.cpp`).
`<name>~.cpp` only talks to Pd's `m_pd.h`.
Design: `docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md`.

## Build and install

```bash
make -C pd            # fat x86_64 + arm64 .d_fat, against /Applications/Pd-0.56-2.app
make -C pd install    # → ~/Documents/Pd/externals/seam-ltm/
make -C pd smoke      # opens each pd/tests/smoke-*.pd in a headless Pd
```

Another Pd: `make -C pd PDAPP=/Applications/Pd-0.57-0.app`.
No VST3 SDK and nothing to download.
The binaries are for 32-bit (vanilla) Pd, not Pd64.

In a patch, load the installed folder with

```
[declare -stdpath seam-ltm]
```

## Parameters

Creation arguments carry the values and are saved with the patch; messages to the left inlet change them in rehearsal; `print` posts the object line with the current values, ready to paste.

- `[m2xhgr~ yaw pitch roll a1 a2 a3]`
- `[lr2xhgr~ divergence yaw pitch roll a1 a2 a3]` — divergence default 90
- `[bamodulex~]` — no parameters

Angles in degrees (−180…180, divergence 0…360), trims in dB (−12…+12); out-of-range values are clipped.
A message reaches its value along a 20 ms ramp so that rehearsal tweaks don't click — the VST3 plugins have no ramp; at steady state the output is the same.

## Sample rate

The Haar decomposition splits at fixed fractions of the sample rate (≈ fs/4 and fs/8), tuned at **48 kHz**.
At any other rate `m2xhgr~` and `lr2xhgr~` post a warning in the console; they correct nothing.
`bamodulex~` is rate-independent.
