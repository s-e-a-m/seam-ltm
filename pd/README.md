# Pd externals — the STONE chain

Pure Data externals that run the same DSP as four seam-ltm VST3 plugins, so that the STONE chain and its calibration signal can live inside a Pd patch without a plugin host.

| External | Plugin | What it does |
|---|---|---|
| `m2xhgr~` | [M2XHGR](../plugins/m2xhgr) | mono → AmbiX by Haar wavelet decomposition, trims and rotation |
| `lr2xhgr~` | [LR2XHGR](../plugins/lr2xhgr) | stereo → AmbiX: two Haar banks, a divergence, shared trims, rotation |
| `bamodulex~` | [BAMODULEX](../plugins/bamodulex) | AmbiX → the four feeds of an upright tetrahedron (LFU RFD RBU LBD) |
| `multipink~` | [MULTIPINK](../plugins/multipink) | four independent calibrated pink-noise streams, for calibrating the stone |

Each external is two files.
`<name>_engine.h` holds everything that computes — parameters, ramps, clipping, block processing, noise streams — and includes the plugin's own `*_dsp.h`; it knows nothing about Pd and is tested under ctest (`tests/pd_*_engine_test.cpp`).
`<name>~.cpp` only talks to Pd's `m_pd.h`.
Design: `docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md`.

## Build and install

```bash
make -C pd            # fat x86_64 + arm64 .d_fat, against /Applications/Pd-0.56-2.app
make -C pd install    # → ~/Library/Pd/seam-ltm/
make -C pd smoke      # opens each pd/tests/smoke-*.pd in a headless Pd
```

Another Pd: `make -C pd PDAPP=/Applications/Pd-0.57-0.app`.
No VST3 SDK and nothing to download.
The binaries are for 32-bit (vanilla) Pd, not Pd64.

In a patch, load the installed folder with

```
[declare -stdpath seam-ltm]
```

`-stdpath` searches Pd's static paths only (`~/Library/Pd`, `/Library/Pd`, the app's `extra`), not the user search path set in the preferences — which is why the install goes to `~/Library/Pd` and not to `~/Documents/Pd/externals`: the patch finds the externals even with `-noprefs` or reset preferences.

## Parameters

Creation arguments carry the values and are saved with the patch; messages to the left inlet change them in rehearsal; `print` posts the object line with the current values, ready to paste.

- `[m2xhgr~ yaw pitch roll a1 a2 a3]`
- `[lr2xhgr~ divergence yaw pitch roll a1 a2 a3]` — divergence default 90
- `[bamodulex~]` — no parameters
- `[multipink~]` — no parameters and no inlet: a standard generator, nothing to set

Angles in degrees (−180…180, divergence 0…360), trims in dB (−12…+12); out-of-range values are clipped.
A message reaches its value along a 20 ms ramp so that rehearsal tweaks don't click — the VST3 plugins have no ramp; at steady state the output is the same.

## Sample rate

The Haar decomposition splits at fixed fractions of the sample rate (≈ fs/4 and fs/8), tuned at **48 kHz**.
At any other rate `m2xhgr~` and `lr2xhgr~` post a warning in the console; they correct nothing.
`bamodulex~` is rate-independent.

## multipink~ calibration

Every third-octave band of every outlet is at −39.5 dBFS, at any sample rate: this is the level the calibration holds, and the one strx reads band by band.
The total RMS is the sum of the bands up to fs/2, so it moves with the rate — each doubling adds an octave of pink, about +0.28 dB: ≈ −23.03 dBFS at 44.1 kHz, −23.00 at 48 kHz, −22.72 at 96 kHz.
"−23" names the level by its total RMS at 48 kHz, as MULTIPINK's Reference menu does (`plugins/multipink/doc/calibration.md`).
The four streams are the ones a 4-channel SEAM MULTIPINK instance plays on pool slots 1–4 at Reference −23, Trim 0, sample for sample (`tests/pd_multipink_engine_test.cpp`).
They restart from their seeds whenever DSP is switched on.
