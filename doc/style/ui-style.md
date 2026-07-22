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

The control count decides the format.
A SETUP selector and an operational switch are one menu and one box: they
occupy a row each, they are not fine controls, and they crowd nothing.
Reading the OPS zone as the threshold sent multipink to 460 px to draw four
controls in a grid that was half empty, which is how the rule was found to
be wrong.

**S — `300, N`, single column.** Up to about five fine controls, whatever the
plugin does with them.
A STONE selector above a POWER switch above two controls still reads as one
column: that is multipink.

**L — width 460 or more, two columns.** From about six fine controls, where a
single column runs the window taller than it is readable.
dslar (seven fine controls) and ltglide (seven) are L on that count; strx is
L for the width its four measurement views need, having no controls at all;
multipink is S.

The window shape therefore tells the user how much there is to set before
they read a single label.

Two-column geometry (the dslar reference): columns 180 px wide at x=30 and
x=250, a 40 px gutter between them.
Within a column a control block is label (14 px), slider (18 px), value
(16 px), and blocks repeat every 58 px.
A menu block is the same 14 px label followed by a 20 px `COptionMenu` and
no value row, since the menu already reads out its own selection, and it
repeats every 42 px.
It belongs to both formats: ltglide uses it twice in its left column (Sweep
and Timing), multipink once in its single column (Reference).
`KnobLabelFont` is 12 and `ValueFont` 11 in every two-column window, so a
plugin copied from the skeleton matches the ones already built; the 300 px
windows carry the wider scale they have always used, `KnobLabelFont` 13 and
`ValueFont` 12.

A control that belongs to neither column spans the full width above both:
dslar's Output and ltglide's Level are the plugin's output level, not a
member of either group, and a full-width block (label across the window,
300 px slider and value centred) says so without a label having to.

## Typography and casing

- Title: `SEAM <NAME>`, where `<NAME>` is the plugin directory name
  uppercased.
  Drawn in `TitleFont`.
- Host name: the display name registered with the VST3 factory, argument 4 of
  the `DEF_CLASS2` block at the bottom of
  `plugins/<name>/source/<name>_processor.cpp`.
  It reads `SEAM <NAME>` by the same rule as the title, because it is the
  string the host draws for itself: Reaper prints it in the plugin browser and
  above the window as `VST3: SEAM DDELAY`.
  The window title and the host title bar are two different strings in two
  different files, so they drift apart in silence — `SEAM B2Xrot`,
  `SEAM hilbert` and `SEAM XYPRrot` sat above windows already reading
  `SEAM B2XROT`, `SEAM HILBERT` and `SEAM XYPRROT` — and the lint now reads
  both.
- Build metadata names: two more files carry `SEAM <NAME>` by the same
  rule, naming only the plugin, never the subtitle after it.
  `plugins/<name>/CMakeLists.txt`'s `DESCRIPTION "SEAM <NAME> – ..."` names
  the CMake target for build tooling and packagers; `version.h`'s
  `#define stringFileDescription "SEAM <NAME> – ..."` is the file-version
  resource a file manager reads for "Get Info" or the Details tab, and some
  plugins `#define` it twice — once inside a 64-bit conditional.
  A plugin's name is therefore written by hand in four files: the window,
  the host title bar, the CMake description, and the file-version string.
  Checking only the window is why a stale name can survive three release
  cycles in the other three without a single build failing, which is what
  happened here — the lint now reads all four.
  The dash separating `<NAME>` from its subtitle is drawn as an en dash
  (`–`) everywhere except `ltglide`'s `stringFileDescription`, which uses
  an em dash (`—`); the rule reads only the name and does not care which.
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
The whole-plugin run also sweeps `plugins/*/source/` and each plugin's
`CMakeLists.txt`, three times over: once over every `.h` and `.cpp` for
`TextDim`, once over `*_processor.cpp` for the factory display name, and once
over `CMakeLists.txt` and `source/version.h` for the same `SEAM <NAME>`
prefix.
Naming a single `.uidesc` checks that document alone.
The factory rule reads the `DEF_CLASS2` argument list rather than a line of
text, so both of the formattings used in the suite are understood, and it
checks the audio effect registration only.
A processor file that registers no class at all is skipped rather than
flagged, since nothing obliges a source file to carry a factory block; a
display name hidden behind a macro instead of a literal is reported, because
a name the lint cannot read is a rule that only appears to hold.
The metadata rule reads `CMakeLists.txt`'s `DESCRIPTION` and every
`stringFileDescription` line in `version.h` — both, when a plugin `#define`s
it twice for the 64-bit build — for the same `SEAM <NAME>` prefix, and
compares only the name, never the subtitle after the dash: the subtitle is
prose, and CMake's description and the file-version string are allowed to
say it differently.
A file that carries no `SEAM <NAME>` prefix at all, or does not exist, is
skipped for the same reason an absent factory block is skipped: nothing
requires either file to declare one on a particular line.
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
description above are read by people, not by the lint, and that includes the
subtitles in `CMakeLists.txt`'s `DESCRIPTION` and `version.h`'s
`stringFileDescription` — the metadata rule reads the `<NAME>` token in front
of the dash and stops there.

## Starting a new plugin

Copy `plugins/_template/resource/_template.uidesc`, rename it, and run the
lint before writing any C++.
The skeleton traces all five zones and passes the standard as-is.
It is an L skeleton because two columns are the harder shape to lay out from
scratch; a plugin with five fine controls or fewer narrows it to S, which is
a smaller edit than widening the other way, and `plugins/_template/README.md`
says how.
The display name in the `DEF_CLASS2` block follows when the C++ arrives, and
the lint checks it against the same directory name.
