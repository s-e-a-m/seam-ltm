# SEAM-LTM UI style

Every plugin in the suite shares one window grammar, so that a user who
learns one GUI has learned all fifteen.
This document is the standard; `tools/check-uidesc.py` enforces the
machine-checkable part of it.

## Window anatomy

Zones appear in this vertical order.
An absent zone is omitted, never moved.

| Zone | Content |
|---|---|
| HEADER | title, subtitle, tagline |
| SETUP | user-entered station identity (STONE id; later, room coordinates) |
| OPS | operational buttons (POWER, RESET, LOOP) |
| FINE | fine controls: sliders and menus, two columns in L format |
| FOOTER | runtime readouts, status line, logo |

SETUP precedes OPS because identity is declared once, before working.
Runtime feedback — slot badges, pool status, meters — is FOOTER content.
It is not SETUP: SETUP is what the user tells the plugin, FOOTER is what
the plugin tells the user.

## Formats

**S — `300, N`, single column.** Passive converters and rotators: up to
about four fine controls, no OPS or SETUP zone.

**L — width 460 or more, two columns.** Required as soon as a plugin has an
OPS zone or more than about five fine controls.
Working and calibration plugins (dslar, ltglide, multipink, strx) are all L.

The window shape therefore tells the user the plugin's role before they
read a single label.

Two-column geometry (the dslar reference): columns 180 px wide at x=30 and
x=250, a 40 px gutter between them.
Within a column a control block is label (14 px), slider (18 px), value
(16 px), and blocks repeat every 58 px.
A menu block is the same 14 px label followed by a 20 px `COptionMenu` and
no value row, since the menu already reads out its own selection; both
rebuilt windows use it (multipink's Reference, ltglide's Sweep and Timing).
`KnobLabelFont` is 12 and `ValueFont` 11 in every two-column window, so a
plugin copied from the skeleton matches the ones already built.

## Typography and casing

- Title: `SEAM <NAME>`, where `<NAME>` is the plugin directory name
  uppercased.
  Drawn in `TitleFont`.
- Subtitle: Title Case.
  It names the plugin in plain language: "Multichannel Pink Noise",
  "Wideband Quadrature Transformer".
- Tagline: the lowercase technical gloss under the subtitle, naming the
  mechanism in the plugin's own vocabulary rather than describing it again.
  `N=5 grains · swept · looped` (ltglide), `64-slot shared pool ·
  RMS-calibrated` (multipink), `c = 331.4 m/s · nextprime · 4ch sync`
  (ddelay), `quadrature ±90° · mono → I/Q` (hilbert).
  Facts are separated by a middle dot, and no casing rule is imposed on it:
  symbols, units, channel names and proper names keep the capitalisation the
  domain gives them (`AmbiX`, `LFU RFD RBU LBD`, `dBFS`, dslar's
  "Homeostatic Loop"), which is what makes the line read as notation instead
  of a second title.
- Zone and column headers: `— NAME —`, centred.
- Operational button labels: ALL CAPS.

## Colour — all text is white

Every text-bearing view declares `font-color="TextLight"` explicitly.
There is no dim text tier: `TextDim` was removed from the standard, its
definition included, so that it cannot creep back one plugin at a time.

| Name | Value | Use |
|---|---|---|
| `BgDark` | `#292c2fff` | window background |
| `TextLight` | `#fcfbfdff` | all text |
| `SliderTrack` | `#444444ff` | slider backs and frames |
| `SliderActive` | `#4a9ec8ff` | slider fill, checkmarks |

Functional accents, used only where the plugin needs them, and never as a
`font-color`:

| Name | Value | Use |
|---|---|---|
| `MeterFill` | `#c8a24aff` | meter fill |
| `MeterInv` | `#c04040ff` | inverted or out-of-phase indication |
| `SliderDiv` | `#c8874aff` | divergence sliders |
| `Structure` | `#888888ff` | graphic structure: frames, axes, circles |

`Structure` is the grey that used to be `TextDim`.
It carries the same value and the same pixels, and a different meaning: it
draws shapes, not words.

The ban on `TextDim` covers the C++ as well as the XML.
Five plugins — `strx`, `dslar`, `hilbert`, `x2uhj`, `ltglide` — draw text
from custom views, which ask the description for a colour by name at draw
time, so a grey label can exist in a plugin whose `.uidesc` is spotless.
That is where the greys survived the previous tidy-up, and the lint now
scans `plugins/*/source/` for the name for exactly that reason.

## Operational vocabulary

| Label | Meaning |
|---|---|
| POWER | the standard on/off for emitters and processors; on means sounding |
| RESET | clear state, return to defaults |
| LOOP | repeat versus single pass (ltglide's only transport toggle) |

MUTE is not part of the vocabulary: an inverted switch reads backwards next
to a POWER on its neighbour's window.

## Running the lint

```bash
python3 tools/check-uidesc.py                       # all plugins
python3 tools/check-uidesc.py plugins/dslar/resource/dslar.uidesc
ctest --test-dir build -C Release -R uidesc         # as part of the suite
```

The `-C Release` is not optional: the suite is generated with Xcode, a
multi-config generator, and without a named configuration `ctest` reports
both tests `Not Run` and exits 8.

Errors fail the run; warnings (zone order) do not.
The whole-plugin run also sweeps `plugins/*/source/` for `TextDim`; naming
a single `.uidesc` checks that document alone.
The first rule the lint applies is XML validity, because nothing else in
the build parses a `.uidesc`: on 2026-07-21 a `--` inside a comment shipped
an empty editor that the compiler and the VST3 validator both accepted in
silence.

The zone-order check does not compare all five zones against each other.
It compares SETUP against OPS, OPS against FINE, and — only when a plugin
has no OPS zone — SETUP against FINE directly.
HEADER and FOOTER positions are never compared against anything, so a
title drawn below the logo would lint clean.
The check recognises SETUP by the `StoneId` control-tag and FINE by the
first `CSlider`.
It recognises OPS two ways.
A control-tag naming the operational vocabulary — POWER, RESET or LOOP —
catches the view regardless of how its caption is drawn, including an
untitled `CCheckBox` whose caption is a separate label next to it (dslar,
`_template`).
A `CCheckBox` whose own title is non-empty and all-caps also counts, and
that branch accepts any such title, not only the three vocabulary words.
No plugin in the suite currently exercises it on its own: every checkbox
that draws an all-caps title of its own also carries a vocabulary
control-tag (multipink's POWER, ltglide's LOOP), so the control-tag branch
catches it first.
Knowing where this net has gaps is itself useful: watch HEADER and FOOTER
placement by eye, because the lint does not.
Nor does it see a colour a plugin composes rather than names — a `CColor`
built from literal components, an alpha applied to a palette entry, a shade
derived from another — since only a name can be checked against the palette.
The C++ sweep catches the one removed name, and everything else a custom
view invents for itself is on the reviewer.
Casing beyond the title is unchecked too: the subtitle rule and the tagline
description above are read by people, not by the lint.

## Starting a new plugin

Copy `plugins/_template/resource/_template.uidesc`, rename it, and run the
lint before writing any C++.
The skeleton traces all five zones and passes the standard as-is.
