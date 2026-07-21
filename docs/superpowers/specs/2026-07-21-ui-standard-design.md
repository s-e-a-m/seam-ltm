# SEAM LTM UI Standard — Design

Date: 2026-07-21
Status: approved (brainstorm with GS)

## Problem

The suite grew plugin by plugin and the GUIs show it.
Titles mix cases (`SEAM B2Xrot`, `SEAM XYPRrot`).
Grey `TextDim` labels survive in older plugins while the newest (`strx`) is all white.
Operational buttons sit in three different places (dslar: top; multipink: middle; ltglide: bottom).
STONE selectors sit glued to operational buttons with no conceptual separation.
`ltglide` is a 300×800 vertical strip while the recent working plugins are horizontal two-column windows.
Nothing enforces any of this, so drift returns with every new plugin.

## Goal

One written, machine-checked UI standard that makes every plugin read as "thought the same way", applied in two tiers: cosmetic everywhere, structural where a plugin is out of shape.

## The standard

### Window anatomy — zone order

Zones appear in this vertical order; an absent zone is simply omitted.

```
HEADER   title, subtitle, tagline
SETUP    user-entered station identity (STONE id; later: room coordinates)
OPS      operational buttons (POWER, RESET, LOOP, SHOT, MUTE)
FINE     fine controls (sliders, menus), two columns in L format
FOOTER   runtime readouts, status line, logo
```

SETUP precedes OPS: identity is set once before working.
Runtime feedback (slot/pool status, meters) is FOOTER content, never SETUP.

### Typography and casing

- Title: `SEAM <NAME>`, all caps; `<NAME>` equals the plugin directory name uppercased.
- Subtitle: Title Case.
- Zone and column headers: dslar's divider style `— NAME —`, centered.
- Operational button labels: all caps.

### Color — all white text

- Every text element uses `TextLight` (#fcfbfdff) and declares `font-color` explicitly.
- `TextDim` is removed from every palette (definition included), so the lint checks absence, not usage.
- Canonical palette: `BgDark #292c2fff`, `TextLight #fcfbfdff`, `SliderTrack #444444ff`, `SliderActive #4a9ec8ff`.
- Optional functional accents where already in use: `MeterFill #c8a24aff`, `MeterInv #c04040ff`, `SliderDiv #c8874aff`, `Structure #888888ff` (graphic structure only — circles, axes — never text).

### Window formats

- **S**: 300×N single column — plugins with up to ~4 fine controls and no OPS/SETUP zone (rotators, matrices).
- **L**: horizontal two-column, width ≥460 (dslar/strx model) — required when a plugin has an OPS zone or more than ~5 fine controls.

The split is also semantic: working/calibration plugins (dslar, ltglide, multipink, strx) are L; passive converters stay S.

## Interventions

### Tier 1 — cosmetic, all 15 plugins

| Plugin | Change |
|---|---|
| b2xrot, xyprrot | Title → `SEAM B2XROT`, `SEAM XYPRROT` |
| hilbert | Subtitle → "Wideband Quadrature Transformer" |
| dslar | Tagline → "Homeostatic Loop" |
| strx | Subtitle → "Stereo M/S Analyser" |
| all | Every `font-color="TextDim"` → `TextLight`; remove the `TextDim` color definition; labels without an explicit `font-color` declare one |

### Tier 2 — restructure

**ltglide** — 300×800 vertical → L format, ~460×480.
HEADER → SETUP (`— STONE —`, StoneId menu) → OPS (LOOP and SHOT side by side) → FINE two columns: `— SWEEP —` (F0, F1, Sweep, Sweep Time) | `— TIMING —` (Timing, Delta, Level) → FOOTER (logo).
The editor size constant in C++ and the SHOT custom-view position change with it.

**multipink** — 300×450 vertical → L format, ~460×360.
HEADER → SETUP (`— STONE —`, StoneId menu) → OPS (MUTE) → FINE two columns: Reference | Trim → FOOTER (Slot/N/pool/Status line, logo).

**dslar** — already structurally conformant (Power/Reset on top, two columns); Tier 1 only.

## Tooling — the enforced rigor

- **`doc/style/ui-style.md`** — the style guide (English), carrying the zone anatomy, casing rules, canonical palette, S/L formats.
- **`plugins/_template/`** — skeleton `.uidesc` with zones traced plus a minimal README; not registered in CMake, inert until copied.
- **`tools/check-uidesc.py`** — pure-python3 (stdlib) lint over all `plugins/*/resource/*.uidesc`:
  - title equals `SEAM ` + uppercased directory name (error);
  - no `TextDim` anywhere, definition included (error);
  - core palette values match the canonical list (error);
  - every text label declares `font-color` with `TextLight` or an allowed accent (error);
  - zone order via y-coordinates of known operational control tags below HEADER and above the first fine control (warning).
  Wired as a **ctest** test, so `ctest` fails when a plugin violates the standard.
- **`tools/README.md`** — documents `check-uidesc.py` and the existing `gen-faust-doc.sh` (tooling-dir README convention).

## Verification

- Lint green on all 15 plugins.
- Affected plugins build (`-G Xcode`, `-DSEAM_VST3SDK_DIR=...`).
- GS visual check in Reaper for the two restructured plugins (ltglide, multipink).

## Sequencing

This work starts **after** the strx thread concludes: GS host-visual check → `calbus` merge to `main` → strx cross-loop accumulation and goniometer zoom land.
The UI branch then forks from `main` and covers all 15 plugins at once.
New strx controls added meanwhile (zoom selector, pass counter) are born conformant to this spec.

## Out of scope

- strx cross-loop accumulation and goniometer zoom (separate briefs).
- Any DSP or parameter change; this spec touches presentation only.
- The shared web "docstyle" (postponed, see memory project_web_docs_strategy); `doc/style/ui-style.md` may later feed it.
