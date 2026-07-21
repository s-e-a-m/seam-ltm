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


def check_file(path):
    """Run every rule over one .uidesc and return all messages."""
    plugin_name = os.path.basename(os.path.dirname(os.path.dirname(path)))
    root, errors = parse_uidesc(path)
    if root is None:
        return errors                      # nothing else is meaningful
    errors += check_title(root, plugin_name, path)
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
