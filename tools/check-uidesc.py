#!/usr/bin/env python3
"""SEAM-LTM UI standard lint.

Checks every plugins/*/resource/*.uidesc against doc/style/ui-style.md, and
sweeps plugins/*/source/ for the one colour name the standard removed.
Standard library only: this must run from a bare python3, with no pip step,
on any machine that can build the suite.

Exit code 0 when no ERROR was reported (WARNs do not fail the run), 1 otherwise.
"""
import glob
import os
import sys
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ERROR = "ERROR"
WARN = "WARN"


class Message(str):
    """One lint finding, carrying its severity as data.

    The printed form is exactly '<path>: <LEVEL> <text>', which is what the
    documentation quotes and what a reader greps for. The severity is also
    kept in .level, so main() decides the exit code by asking the message
    what it is instead of searching its prose for the word ERROR: rewording
    a message must never be able to switch the ctest off.

    Subclassing str keeps every rule function returning something that reads,
    compares and formats as a plain string, so the rules and their tests are
    untouched by the severity becoming explicit.
    """

    def __new__(cls, path, level, text):
        message = super().__new__(cls, "%s: %s %s" % (path, level, text))
        message.level = level
        return message


def error(path, text):
    """An ERROR: it fails the run."""
    return Message(path, ERROR, text)


def warn(path, text):
    """A WARN: it is printed and counted, and the run still passes."""
    return Message(path, WARN, text)


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
        return None, [error(path, "malformed XML: %s" % exc)]
    except OSError as exc:
        return None, [error(path, "cannot read file: %s" % exc)]


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
        return [error(path, "no TitleFont label found (expected title %r)"
                      % expected)]
    if len(labels) > 1:
        titles = ", ".join(repr(label.get("title") or "") for label in labels)
        return [error(path, "%d TitleFont labels found, expected exactly 1 "
                            "(titles: %s)" % (len(labels), titles))]
    actual = labels[0].get("title") or ""
    if actual != expected:
        return [error(path, "title is %r, expected %r" % (actual, expected))]
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
# The one name the standard deleted rather than redefined, so that it cannot
# creep back one plugin at a time. It is banned in the .uidesc and in the C++
# that asks the description for a colour by name.
FORBIDDEN_COLOR = "TextDim"


def check_palette(root, path):
    """Colour definitions match the canonical values; no unknown names."""
    errors = []
    for color in root.iter("color"):
        name, rgba = color.get("name"), color.get("rgba")
        expected = CANONICAL_COLORS.get(name) or ACCENT_COLORS.get(name)
        if expected is None:
            errors.append(error(path, "unknown colour %r (not in the standard "
                                      "palette; see doc/style/ui-style.md)" % name))
        elif rgba != expected:
            errors.append(error(path, "colour %r is %s, expected %s"
                                % (name, rgba, expected)))
    return errors


def check_no_textdim(root, path):
    """TextDim is gone from the standard: definition and every use of it.

    Text is white (TextLight); the grey that used to frame checkboxes is
    graphic structure and is now named Structure. Checking for the absence
    of the definition, not just of its uses, is what stops it creeping back.
    """
    errors = []
    for color in root.iter("color"):
        if color.get("name") == FORBIDDEN_COLOR:
            errors.append(error(path, "TextDim colour definition present — "
                                      "remove it (text is TextLight, frames "
                                      "are Structure)"))
    for view in root.iter("view"):
        for attribute, value in view.attrib.items():
            if value == FORBIDDEN_COLOR:
                errors.append(error(path, "%s=\"TextDim\" on %s — use TextLight "
                                          "for text, Structure for frames"
                                    % (attribute, view.get("class"))))
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
        name = view.get("title") or view.get("control-tag")
        if font_color is None:
            errors.append(error(path, "%s %r declares no font-color"
                                % (klass, name)))
        elif font_color != "TextLight":
            errors.append(error(path, "%s %r uses font-color=%r — all text is "
                                      "TextLight (accents are for graphics, "
                                      "not text)" % (klass, name, font_color)))
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


#  The operational vocabulary (doc/style/ui-style.md, "Operational
# vocabulary"): the control-tag names an OPS view carries regardless of how
# its caption is drawn.
OPS_CONTROL_TAGS = ("Power", "Reset", "Loop")


def _is_ops_view(view):
    """A view belongs to the OPS zone.

    Two independent ways to recognise it, because the zone is drawn two
    different ways across the suite:

    - By control-tag: POWER, RESET and LOOP are the standard's operational
      vocabulary, and the control-tag carries that name even when the
      caption is a separate CTextLabel next to an untitled CCheckBox (dslar,
      and the _template skeleton) — a layout that lets the caption sit in
      its own column. dslar's Reset is a custom view with no control-tag at
      all, so it is invisible either way; its Power box is enough to place
      the zone.
    - By title: a CCheckBox whose own title is non-empty and all-caps
      (ltglide's LOOP) draws its own caption. The title alone is enough,
      whether or not the control-tag names one of the vocabulary words,
      so a plugin that labels a switch outside the vocabulary still has
      its OPS zone placed.

    Keeping both means a plugin that captions its own checkbox is not
    suddenly invisible just because the vocabulary check doesn't name it.
    """
    if view.get("control-tag") in OPS_CONTROL_TAGS:
        return True
    title = view.get("title") or ""
    return view.get("class") == "CCheckBox" and title != "" and title.isupper()


def check_zone_order(root, path):
    """SETUP above OPS above FINE.

    A warning, not an error: zones are recognised by convention (the StoneId
    menu is SETUP, an operational view is OPS, the first slider opens FINE)
    and a future plugin may legitimately not fit that shape. It is here to
    catch the accidental reordering, not to legislate layout.

    OPS is optional (a passive converter may declare a STONE identity with
    no operational toggle at all), so the SETUP-OPS and OPS-FINE checks below
    both go silent when ops_y is None. Without a direct SETUP-vs-FINE check,
    a plugin missing OPS could put SETUP below its fine controls and this
    rule would never notice — exactly the mistake it exists to catch. That
    comparison only runs when OPS is absent: when OPS is present, a SETUP
    below FINE violation is already implied by (and reported through) the
    SETUP-vs-OPS and OPS-vs-FINE messages, so adding a third overlapping
    complaint about the same layout would just be noise.
    """
    setup_y = _first_y(root, lambda v: v.get("control-tag") == "StoneId")
    ops_y = _first_y(root, _is_ops_view)
    fine_y = _first_y(root, lambda v: v.get("class") == "CSlider")

    messages = []
    if setup_y is not None and ops_y is not None and setup_y > ops_y:
        messages.append(warn(path, "SETUP (StoneId, y=%g) sits below OPS (y=%g) — "
                                   "identity is set before working"
                             % (setup_y, ops_y)))
    if ops_y is not None and fine_y is not None and ops_y > fine_y:
        messages.append(warn(path, "OPS (y=%g) sits below the first fine control "
                                   "(y=%g)" % (ops_y, fine_y)))
    if (ops_y is None and setup_y is not None and fine_y is not None
            and setup_y > fine_y):
        messages.append(warn(path, "SETUP (StoneId, y=%g) sits below the first "
                                   "fine control (y=%g) — identity is set before "
                                   "fine tuning, and no OPS zone was found to "
                                   "separate them" % (setup_y, fine_y)))
    return messages


# C++ that draws its own text asks the description for a colour by name, so
# the rules above — which read .uidesc files — never see it.
SOURCE_SUFFIXES = (".h", ".cpp")


def check_source_colors(plugins_dir):
    """The forbidden colour name in the C++ that the .uidesc rules cannot see.

    Five plugins (strx, dslar, hilbert, x2uhj, ltglide) draw text from custom
    views, naming their colours in C++ rather than in the XML. That is exactly
    where TextDim survived the previous suite-wide tidy-up: the greys came
    back through getColor("TextDim", ...) calls no .uidesc rule could reach.

    This is a name scan over the source text, not a parse of it, so a comment
    that merely mentions TextDim is reported too. That is deliberate: the
    standard removed the name itself, and a comment still explaining a colour
    tier that no longer exists is drift in the prose students read.
    """
    messages = []
    for path in sorted(glob.glob(os.path.join(plugins_dir, "*", "source", "*"))):
        if not path.endswith(SOURCE_SUFFIXES):
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                lines = f.read().splitlines()
        except OSError as exc:
            messages.append(error(path, "cannot read file: %s" % exc))
            continue
        for number, line in enumerate(lines, 1):
            if FORBIDDEN_COLOR in line:
                messages.append(error(path, "line %d names TextDim — text is "
                                            "TextLight, graphic structure is "
                                            "Structure" % number))
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
    """Lint the named .uidesc files, or the whole suite when given none.

    The C++ sweep runs in whole-suite mode only: asking about one .uidesc is
    asking about that document, and answering with findings from a source
    tree the caller never named would be surprising.

    The count in the summary line is .uidesc documents; the error and warning
    counts are messages, and one defect can raise several of them.
    """
    plugins_dir = os.path.join(REPO_ROOT, "plugins")
    whole_suite = not argv[1:]
    paths = argv[1:] or sorted(
        glob.glob(os.path.join(plugins_dir, "*", "resource", "*.uidesc")))
    if not paths:
        print("no .uidesc files found under plugins/*/resource/", file=sys.stderr)
        return 1
    messages = []
    for path in paths:
        messages += check_file(path)
    if whole_suite:
        messages += check_source_colors(plugins_dir)
    for message in messages:
        print(message)
    failed = [m for m in messages if m.level == ERROR]
    print("checked %d file(s): %d error(s), %d warning(s)"
          % (len(paths), len(failed), len(messages) - len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
