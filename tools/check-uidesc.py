#!/usr/bin/env python3
"""SEAM-LTM UI standard lint.

Checks every plugins/*/resource/*.uidesc against doc/style/ui-style.md.
Standard library only: this must run from a bare python3, with no pip step,
on any machine that can build the suite.

Exit code 0 when no ERROR was reported (WARNs do not fail the run), 1 otherwise.
"""
import glob
import os
import sys
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse_uidesc(path):
    """Parse one .uidesc.

    Returns (root, errors). XML validity is checked first and on its own:
    a .uidesc is parsed by nobody in the build (not the compiler, not the
    VST3 validator), so a malformed file ships an EMPTY editor and the
    breakage only surfaces when a host opens the GUI. A '--' inside a
    comment did exactly that on 2026-07-21.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            return ET.fromstring(f.read()), []
    except ET.ParseError as exc:
        return None, ["%s: ERROR malformed XML: %s" % (path, exc)]
    except OSError as exc:
        return None, ["%s: ERROR cannot read file: %s" % (path, exc)]


def _title_labels(root):
    """Every CTextLabel view drawn in TitleFont.

    The window title is unique by definition in the UI standard, so this
    returns *all* matches rather than the first: check_title uses the full
    list to flag a second TitleFont label as a violation instead of silently
    checking one of the two and ignoring the other.
    """
    return [view for view in root.iter("view")
            if view.get("class") == "CTextLabel" and view.get("font") == "TitleFont"]


def check_title(root, plugin_name, path):
    """Title must read 'SEAM ' + the plugin directory name, uppercased.

    There must be exactly one TitleFont label: zero is a missing title,
    more than one means the title is ambiguous (which one is *the* title?)
    and is itself a standard violation, not just a lint inconvenience.
    """
    expected = "SEAM " + plugin_name.upper()
    labels = _title_labels(root)
    if not labels:
        return ["%s: ERROR no TitleFont label found (expected title %r)"
                % (path, expected)]
    if len(labels) > 1:
        titles = ", ".join(repr(label.get("title") or "") for label in labels)
        return ["%s: ERROR %d TitleFont labels found, expected exactly 1 (titles: %s)"
                % (path, len(labels), titles)]
    actual = labels[0].get("title") or ""
    if actual != expected:
        return ["%s: ERROR title is %r, expected %r" % (path, actual, expected)]
    return []


# The standard's palette. A name that appears here must carry exactly this
# rgba; a colour name absent from both tables is a drift and an error.
CANONICAL_COLORS = {
    "BgDark":       "#292c2fff",
    "TextLight":    "#fcfbfdff",
    "SliderTrack":  "#444444ff",
    "SliderActive": "#4a9ec8ff",
}
# Functional accents: allowed, but only where a plugin genuinely needs them.
# Structure is graphic structure (frames, axes, circles) and never text.
ACCENT_COLORS = {
    "MeterFill": "#c8a24aff",
    "MeterInv":  "#c04040ff",
    "SliderDiv": "#c8874aff",
    "Structure": "#888888ff",
}
# Views that render text and must therefore declare an explicit font-color.
TEXT_CLASSES = ("CTextLabel", "CTextEdit", "CParamDisplay",
                "COptionMenu", "CCheckBox")


def check_palette(root, path):
    """Colour definitions match the canonical values; no unknown names."""
    errors = []
    for color in root.iter("color"):
        name, rgba = color.get("name"), color.get("rgba")
        expected = CANONICAL_COLORS.get(name) or ACCENT_COLORS.get(name)
        if expected is None:
            errors.append("%s: ERROR unknown colour %r (not in the standard "
                          "palette; see doc/style/ui-style.md)" % (path, name))
        elif rgba != expected:
            errors.append("%s: ERROR colour %r is %s, expected %s"
                          % (path, name, rgba, expected))
    return errors


def check_no_textdim(root, path):
    """TextDim is gone from the standard: definition and every use of it.

    Text is white (TextLight); the grey that used to frame checkboxes is
    graphic structure and is now named Structure. Checking for the absence
    of the definition, not just of its uses, is what stops it creeping back.
    """
    errors = []
    for color in root.iter("color"):
        if color.get("name") == "TextDim":
            errors.append("%s: ERROR TextDim colour definition present — "
                          "remove it (text is TextLight, frames are Structure)"
                          % path)
    for view in root.iter("view"):
        for attribute, value in view.attrib.items():
            if value == "TextDim":
                errors.append("%s: ERROR %s=\"TextDim\" on %s — use TextLight "
                              "for text, Structure for frames"
                              % (path, attribute, view.get("class")))
    return errors


def check_font_colors(root, path):
    """Every text-bearing view declares font-color, and it is TextLight.

    A CCheckBox with no title draws no text of its own (its caption is a
    separate label), so it is exempt.
    """
    errors = []
    for view in root.iter("view"):
        klass = view.get("class")
        if klass not in TEXT_CLASSES:
            continue
        if klass == "CCheckBox" and not (view.get("title") or ""):
            continue
        font_color = view.get("font-color")
        if font_color is None:
            errors.append("%s: ERROR %s %r declares no font-color"
                          % (path, klass, view.get("title") or view.get("control-tag")))
        elif font_color != "TextLight":
            errors.append("%s: ERROR %s %r uses font-color=%r — all text is "
                          "TextLight (accents are for graphics, not text)"
                          % (path, klass, view.get("title") or view.get("control-tag"),
                             font_color))
    return errors


def _first_y(root, predicate):
    """Smallest y origin among views matching predicate, or None."""
    ys = []
    for view in root.iter("view"):
        if not predicate(view):
            continue
        origin = (view.get("origin") or "").split(",")
        if len(origin) == 2:
            try:
                ys.append(float(origin[1]))
            except ValueError:
                pass
    return min(ys) if ys else None


def check_zone_order(root, path):
    """SETUP above OPS above FINE.

    A warning, not an error: zones are recognised by convention (the StoneId
    menu is SETUP, an operational toggle is OPS, the first slider opens FINE)
    and a future plugin may legitimately not fit that shape. It is here to
    catch the accidental reordering, not to legislate layout.
    """
    setup_y = _first_y(root, lambda v: v.get("control-tag") == "StoneId")
    ops_y = _first_y(root, lambda v: (v.get("title") or "").isupper()
                     and (v.get("title") or "") != ""
                     and v.get("class") == "CCheckBox")
    fine_y = _first_y(root, lambda v: v.get("class") == "CSlider")

    messages = []
    if setup_y is not None and ops_y is not None and setup_y > ops_y:
        messages.append("%s: WARN SETUP (StoneId, y=%g) sits below OPS (y=%g) — "
                        "identity is set before working" % (path, setup_y, ops_y))
    if ops_y is not None and fine_y is not None and ops_y > fine_y:
        messages.append("%s: WARN OPS (y=%g) sits below the first fine control "
                        "(y=%g)" % (path, ops_y, fine_y))
    return messages


def check_file(path):
    """Run every rule over one .uidesc and return all messages."""
    plugin_name = os.path.basename(os.path.dirname(os.path.dirname(path)))
    root, errors = parse_uidesc(path)
    if root is None:
        return errors                      # nothing else is meaningful
    errors += check_title(root, plugin_name, path)
    errors += check_palette(root, path)
    errors += check_no_textdim(root, path)
    errors += check_font_colors(root, path)
    errors += check_zone_order(root, path)
    return errors


def main(argv):
    paths = argv[1:] or sorted(
        glob.glob(os.path.join(REPO_ROOT, "plugins", "*", "resource", "*.uidesc")))
    if not paths:
        print("no .uidesc files found under plugins/*/resource/", file=sys.stderr)
        return 1
    messages = []
    for path in paths:
        messages += check_file(path)
    for message in messages:
        print(message)
    failed = [m for m in messages if ": ERROR " in m]
    print("checked %d file(s): %d error(s), %d warning(s)"
          % (len(paths), len(failed), len(messages) - len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
