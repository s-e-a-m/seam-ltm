# SEAM LTM UI Standard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make all 15 seam-ltm plugin GUIs conform to the approved UI standard, and make a lint enforce it so the drift cannot come back.

**Architecture:** The standard lives in three places: a written guide (`doc/style/ui-style.md`), an inert skeleton to copy (`plugins/_template/`), and a stdlib-only Python lint (`tools/check-uidesc.py`) wired as a ctest. The lint is written **first** and becomes the test for every subsequent task: each conformance rule is added test-first, goes red against the current 15 `.uidesc` files, and the fix turns it green. Cosmetic changes (Tier 1) touch colours, titles and casing only; structural changes (Tier 2) re-lay-out `ltglide` and `multipink` into the L format, and rename `multipink`'s MUTE to POWER.

**Tech Stack:** VSTGUI `.uidesc` (XML), C++17 (VST3 SDK), CMake + ctest, Python 3 (stdlib only — `xml.etree.ElementTree`, `unittest`).

**Source spec:** `docs/superpowers/specs/2026-07-21-ui-standard-design.md` (approved).

## Global Constraints

- **Canonical palette (exact rgba, verbatim):** `BgDark #292c2fff`, `TextLight #fcfbfdff`, `SliderTrack #444444ff`, `SliderActive #4a9ec8ff`.
- **Allowed functional accents (exact rgba):** `MeterFill #c8a24aff`, `MeterInv #c04040ff`, `SliderDiv #c8874aff`, `Structure #888888ff`.
- **`TextDim` is forbidden everywhere**, its `<color>` definition included, in `.uidesc` and in C++ `getColor(...)` calls.
- **Every text-bearing view declares `font-color="TextLight"`.** No accent colour may appear in a `font-color` attribute — `Structure` is for graphic structure (frames, axes, circles) only.
- **Title** = `SEAM ` + plugin directory name uppercased. **Subtitles and taglines** are Title Case. **Operational button labels** are ALL CAPS.
- **Zone order**, top to bottom: HEADER → SETUP → OPS → FINE → FOOTER. An absent zone is omitted. Runtime feedback belongs to FOOTER, never SETUP.
- **Window formats:** S = `300, N` single column; L = width ≥ 460, two columns. L is required when the plugin has an OPS zone or more than ~5 fine controls.
- **Python:** standard library only. No `pip install`, no third-party XML library. The lint must run from a bare `python3`.
- **Build command** (Xcode generator is required — the default Makefile generator does not produce a loadable bundle here):
  ```bash
  cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
  cmake --build build --config Release
  ```
- **VST3 symlink ownership:** the last build tree to compile a target owns `~/Library/Audio/Plug-Ins/VST3/<name>.vst3`. Use the `build` tree for anything Giuseppe will open in Reaper; configure throwaway trees with `-DSEAM_BUILD_PLUGINS=OFF`.
- **Working language:** code, comments, commits and `doc/style/ui-style.md` in English.

## Two facts discovered while planning (they simplify the spec)

1. **No editor size constant exists in C++.** Grepping the whole `plugins/*/source` tree for `ViewRect`, `kEditorW`, `kEditorH` or literal sizes returns nothing: geometry lives entirely in the `.uidesc`. Custom views are constructed with a placeholder `CRect(0, 0, w, h)` which VSTGUI's `ViewFactory` overwrites by applying the `origin`/`size` attributes from the XML. **The Tier 2 restructures are therefore pure XML edits** — the spec's warning about "the editor size constant in C++ and the SHOT custom-view position" no longer applies (SHOT was removed in commit `f8fb76e`, and the size was never in C++).
2. **`TextDim` is used for two different jobs.** As `font-color` on text (28 occurrences) and as `boxframe-color` on checkboxes (dslar, ltglide, multipink) plus one C++ `getColor("TextDim")` feeding a custom button's frame colour. Text becomes `TextLight`; the frames become `Structure`, which is the same `#888888ff` — a semantic rename with zero pixel change.

## File Structure

**New files**

| File | Responsibility |
|---|---|
| `tools/check-uidesc.py` | The lint. Parses every `plugins/*/resource/*.uidesc`, applies the rules, prints `ERROR`/`WARN` lines, exits non-zero on any error. |
| `tools/test_check_uidesc.py` | `unittest` suite over the lint's rule functions, driven by inline fixture XML strings. This is where each rule is proven before it is pointed at real plugins. |
| `tools/README.md` | Documents `check-uidesc.py` and the pre-existing `gen-faust-doc.sh` (tooling-dir README convention). |
| `doc/style/ui-style.md` | The written standard: zone anatomy, casing, palette, S/L formats, how to run the lint. |
| `plugins/_template/resource/_template.uidesc` | Inert skeleton with all five zones traced. Not in CMake. |
| `plugins/_template/README.md` | How to copy the skeleton for a new plugin. |

**Modified files**

| File | Change |
|---|---|
| `tests/CMakeLists.txt` | Register two ctests: `uidesc_lint_selftest` and `uidesc_lint`. |
| `plugins/{abmodulex,b2xrot,bamodulex,ddelay,hilbert,lr2xhgr,ltburst,m2xhgr,sdmx,x2uhj,xyprrot}/resource/*.uidesc` | Tier 1 cosmetics. |
| `plugins/hilbert/source/hilbert_processor.cpp`, `plugins/x2uhj/source/x2uhj_processor.cpp` | `getColor("TextDim")` → `getColor("TextLight")` for readout labels. |
| `plugins/dslar/resource/dslar.uidesc`, `plugins/dslar/source/dslar_processor.cpp` | Tier 1 + `Structure` + ALL-CAPS ops labels. |
| `plugins/strx/resource/strx.uidesc` | Subtitle Title Case, drop the unused `TextDim` definition. |
| `plugins/multipink/source/multipink_ids.h`, `multipink_processor.h`, `multipink_processor.cpp` | MUTE → POWER (rename + polarity + clean-break state format). |
| `plugins/multipink/resource/multipink.uidesc` | Tier 1 + L-format restructure + POWER. |
| `plugins/ltglide/resource/ltglide.uidesc` | Tier 1 + L-format restructure. |

---

### Task 1: Lint harness — XML validity and title rule, wired as ctest

The first rule is XML validity, because that is the one born from a real incident: on 2026-07-21 a `--` inside a `.uidesc` comment shipped an **empty editor** in the host. Neither the build nor the VST3 validator parses `.uidesc`, so malformed XML is invisible until a host opens the GUI.

**Files:**
- Create: `tools/check-uidesc.py`
- Create: `tools/test_check_uidesc.py`
- Modify: `tests/CMakeLists.txt` (append at end of file)

**Interfaces:**
- Consumes: nothing.
- Produces, for later tasks to extend:
  - `parse_uidesc(path: str) -> tuple[Element | None, list[str]]` — returns `(root, errors)`; on a parse failure returns `(None, ["<path>: ERROR malformed XML: <msg>"])`.
  - `check_title(root: Element, plugin_name: str, path: str) -> list[str]` — plugin_name is the plugin *directory* name.
  - `check_file(path: str) -> list[str]` — runs every rule for one file; returns all messages.
  - Message convention: each message is one line, `"<path>: ERROR <text>"` or `"<path>: WARN <text>"`. Errors set the exit code; warnings do not.

- [ ] **Step 1: Write the failing tests**

Create `tools/test_check_uidesc.py`:

```python
#!/usr/bin/env python3
"""Unit tests for check-uidesc.py, driven by inline fixture XML."""
import importlib.util
import os
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "check_uidesc", os.path.join(_HERE, "check-uidesc.py"))
check_uidesc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_uidesc)


def fixture(body, colors='<color name="TextLight" rgba="#fcfbfdff"/>'):
    """Wrap a template body in a minimal but complete uidesc document."""
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<vstgui-ui-description version="1">\n'
        '  <fonts><font font-name="Source Code Pro Light" name="TitleFont" size="20"/></fonts>\n'
        f'  <colors>{colors}</colors>\n'
        '  <template name="view" class="CViewContainer" origin="0, 0" size="300, 200">\n'
        f'{body}\n'
        '  </template>\n'
        '</vstgui-ui-description>\n')


TITLE_OK = ('    <view class="CTextLabel" origin="0, 18" size="300, 26" font="TitleFont"'
            ' font-color="TextLight" title="SEAM DEMO" transparent="true"/>')


class TestParse(unittest.TestCase):
    def _write(self, text):
        import tempfile
        fd, path = tempfile.mkstemp(suffix=".uidesc")
        with os.fdopen(fd, "w") as f:
            f.write(text)
        self.addCleanup(os.unlink, path)
        return path

    def test_valid_xml_parses(self):
        root, errors = check_uidesc.parse_uidesc(self._write(fixture(TITLE_OK)))
        self.assertIsNotNone(root)
        self.assertEqual(errors, [])

    def test_double_dash_in_comment_is_an_error(self):
        # The exact shape that shipped an empty editor on 2026-07-21.
        body = TITLE_OK + '\n    <!-- a comment -- with a double dash -->'
        root, errors = check_uidesc.parse_uidesc(self._write(fixture(body)))
        self.assertIsNone(root)
        self.assertEqual(len(errors), 1)
        self.assertIn("malformed XML", errors[0])

    def test_unclosed_tag_is_an_error(self):
        broken = fixture(TITLE_OK).replace("</template>", "")
        root, errors = check_uidesc.parse_uidesc(self._write(broken))
        self.assertIsNone(root)
        self.assertIn("malformed XML", errors[0])


class TestTitle(unittest.TestCase):
    def _root(self, text):
        import xml.etree.ElementTree as ET
        return ET.fromstring(text)

    def test_matching_title_passes(self):
        root = self._root(fixture(TITLE_OK))
        self.assertEqual(check_uidesc.check_title(root, "demo", "p.uidesc"), [])

    def test_lowercase_title_fails(self):
        body = TITLE_OK.replace("SEAM DEMO", "SEAM Demo")
        root = self._root(fixture(body))
        errors = check_uidesc.check_title(root, "demo", "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("SEAM DEMO", errors[0])

    def test_missing_title_label_fails(self):
        root = self._root(fixture("    <!-- no title label -->"))
        errors = check_uidesc.check_title(root, "demo", "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("no TitleFont label", errors[0])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 tools/test_check_uidesc.py -v`
Expected: FAIL — `FileNotFoundError` / `ModuleNotFoundError` because `tools/check-uidesc.py` does not exist yet.

- [ ] **Step 3: Write the lint**

Create `tools/check-uidesc.py`:

```python
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


def _title_label(root):
    """The header title label: the first CTextLabel drawn in TitleFont."""
    for view in root.iter("view"):
        if view.get("class") == "CTextLabel" and view.get("font") == "TitleFont":
            return view
    return None


def check_title(root, plugin_name, path):
    """Title must read 'SEAM ' + the plugin directory name, uppercased."""
    expected = "SEAM " + plugin_name.upper()
    label = _title_label(root)
    if label is None:
        return ["%s: ERROR no TitleFont label found (expected title %r)"
                % (path, expected)]
    actual = label.get("title") or ""
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 tools/test_check_uidesc.py -v`
Expected: PASS — 6 tests OK.

- [ ] **Step 5: Run the lint against the real plugins**

Run: `python3 tools/check-uidesc.py`
Expected: exit code 1, with exactly these two errors (the titles the spec calls out) and nothing else:
```
.../b2xrot/resource/b2xrot.uidesc: ERROR title is 'SEAM B2Xrot', expected 'SEAM B2XROT'
.../xyprrot/resource/xyprrot.uidesc: ERROR title is 'SEAM XYPRrot', expected 'SEAM XYPRROT'
checked 15 file(s): 2 error(s), 0 warning(s)
```
This is the lint proving itself against reality before any file is touched. Do **not** fix the titles yet — Task 4 does that.

- [ ] **Step 6: Wire both as ctests**

Append to `tests/CMakeLists.txt`:

```cmake
# ─── UI standard lint (see doc/style/ui-style.md) ────────────────────────────
# Two tests: the lint's own rule tests, then the lint over all plugins.
# Python3 is optional — on a machine without it the tests are skipped rather
# than failing the build, since no plugin needs Python to compile.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME uidesc_lint_selftest
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_check_uidesc.py)
    add_test(NAME uidesc_lint
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-uidesc.py)
else()
    message(STATUS "Python3 not found — uidesc lint tests will not be registered")
endif()
```

- [ ] **Step 7: Verify the ctests are registered and behave**

Run:
```bash
cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
ctest --test-dir build -R uidesc -V
```
Expected: `uidesc_lint_selftest` **passes**; `uidesc_lint` **fails** with the two title errors above. A red `uidesc_lint` here is the correct state — it is the failing test that Task 4 makes green.

- [ ] **Step 8: Commit**

```bash
git add tools/check-uidesc.py tools/test_check_uidesc.py tests/CMakeLists.txt
git commit -m "test(ui): uidesc lint — XML validity and title rule, wired as ctest"
```

---

### Task 2: Lint rules — palette, TextDim absence, font-color, zone order

**Files:**
- Modify: `tools/check-uidesc.py` (add rule functions, call them from `check_file`)
- Modify: `tools/test_check_uidesc.py` (add test classes)

**Interfaces:**
- Consumes: `parse_uidesc`, `check_title`, `check_file`, the message convention from Task 1.
- Produces:
  - `check_palette(root, path) -> list[str]`
  - `check_no_textdim(root, path) -> list[str]`
  - `check_font_colors(root, path) -> list[str]`
  - `check_zone_order(root, path) -> list[str]` (emits `WARN`, never `ERROR`)
  - Module constants `CANONICAL_COLORS: dict[str, str]` and `ACCENT_COLORS: dict[str, str]`.

- [ ] **Step 1: Write the failing tests**

Append to `tools/test_check_uidesc.py`, before the `if __name__` block:

```python
class TestPalette(unittest.TestCase):
    def _root(self, colors):
        import xml.etree.ElementTree as ET
        return ET.fromstring(fixture(TITLE_OK, colors=colors))

    def test_canonical_palette_passes(self):
        root = self._root(
            '<color name="BgDark" rgba="#292c2fff"/>'
            '<color name="TextLight" rgba="#fcfbfdff"/>'
            '<color name="SliderTrack" rgba="#444444ff"/>'
            '<color name="SliderActive" rgba="#4a9ec8ff"/>')
        self.assertEqual(check_uidesc.check_palette(root, "p.uidesc"), [])

    def test_allowed_accent_passes(self):
        root = self._root('<color name="TextLight" rgba="#fcfbfdff"/>'
                          '<color name="MeterFill" rgba="#c8a24aff"/>')
        self.assertEqual(check_uidesc.check_palette(root, "p.uidesc"), [])

    def test_wrong_rgba_for_known_name_fails(self):
        root = self._root('<color name="TextLight" rgba="#ffffffff"/>')
        errors = check_uidesc.check_palette(root, "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("#fcfbfdff", errors[0])

    def test_unknown_color_name_fails(self):
        root = self._root('<color name="TextLight" rgba="#fcfbfdff"/>'
                          '<color name="HotPink" rgba="#ff69b4ff"/>')
        errors = check_uidesc.check_palette(root, "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("HotPink", errors[0])


class TestNoTextDim(unittest.TestCase):
    def _root(self, body, colors='<color name="TextLight" rgba="#fcfbfdff"/>'):
        import xml.etree.ElementTree as ET
        return ET.fromstring(fixture(body, colors=colors))

    def test_clean_file_passes(self):
        self.assertEqual(
            check_uidesc.check_no_textdim(self._root(TITLE_OK), "p.uidesc"), [])

    def test_definition_alone_fails(self):
        root = self._root(TITLE_OK,
                          colors='<color name="TextLight" rgba="#fcfbfdff"/>'
                                 '<color name="TextDim" rgba="#888888ff"/>')
        errors = check_uidesc.check_no_textdim(root, "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("definition", errors[0])

    def test_usage_as_boxframe_fails(self):
        body = TITLE_OK + ('\n    <view class="CCheckBox" origin="0, 40" size="80, 20"'
                           ' title="LOOP" font-color="TextLight"'
                           ' boxframe-color="TextDim"/>')
        errors = check_uidesc.check_no_textdim(self._root(body), "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("boxframe-color", errors[0])


class TestFontColors(unittest.TestCase):
    def _root(self, body):
        import xml.etree.ElementTree as ET
        return ET.fromstring(fixture(body))

    def test_textlight_passes(self):
        self.assertEqual(
            check_uidesc.check_font_colors(self._root(TITLE_OK), "p.uidesc"), [])

    def test_missing_font_color_fails(self):
        body = ('    <view class="CTextLabel" origin="0, 18" size="300, 26"'
                ' font="TitleFont" title="SEAM DEMO"/>')
        errors = check_uidesc.check_font_colors(self._root(body), "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("declares no font-color", errors[0])

    def test_accent_on_text_fails(self):
        body = TITLE_OK + ('\n    <view class="CTextLabel" origin="0, 60" size="300, 14"'
                           ' font="TitleFont" font-color="Structure" title="x"/>')
        errors = check_uidesc.check_font_colors(self._root(body), "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("Structure", errors[0])

    def test_untitled_checkbox_is_exempt(self):
        # dslar's Power box draws no text of its own — its caption is a
        # separate CTextLabel — so it has no font-color to declare.
        body = TITLE_OK + ('\n    <view class="CCheckBox" origin="93, 88" size="14, 14"'
                           ' control-tag="Power" title=""/>')
        self.assertEqual(
            check_uidesc.check_font_colors(self._root(body), "p.uidesc"), [])


class TestZoneOrder(unittest.TestCase):
    def _root(self, body):
        import xml.etree.ElementTree as ET
        return ET.fromstring(fixture(body))

    SETUP = ('    <view class="COptionMenu" origin="160, 106" size="140, 20"'
             ' control-tag="StoneId" font-color="TextLight"/>')
    OPS = ('    <view class="CCheckBox" origin="180, 140" size="100, 20"'
           ' control-tag="Power" title="POWER" font-color="TextLight"/>')
    FINE = ('    <view class="CSlider" origin="30, 228" size="180, 18"'
            ' control-tag="Trim"/>')

    def test_correct_order_passes(self):
        body = "\n".join([TITLE_OK, self.SETUP, self.OPS, self.FINE])
        self.assertEqual(check_uidesc.check_zone_order(self._root(body), "p.uidesc"), [])

    def test_setup_below_ops_warns(self):
        setup_low = self.SETUP.replace('origin="160, 106"', 'origin="160, 252"')
        body = "\n".join([TITLE_OK, setup_low, self.OPS, self.FINE])
        messages = check_uidesc.check_zone_order(self._root(body), "p.uidesc")
        self.assertEqual(len(messages), 1)
        self.assertIn("WARN", messages[0])
        self.assertIn("SETUP", messages[0])

    def test_ops_below_fine_warns(self):
        ops_low = self.OPS.replace('origin="180, 140"', 'origin="180, 606"')
        body = "\n".join([TITLE_OK, self.SETUP, ops_low, self.FINE])
        messages = check_uidesc.check_zone_order(self._root(body), "p.uidesc")
        self.assertEqual(len(messages), 1)
        self.assertIn("WARN", messages[0])
        self.assertIn("OPS", messages[0])
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 tools/test_check_uidesc.py -v`
Expected: FAIL — `AttributeError: module 'check_uidesc' has no attribute 'check_palette'` (and the same for the other three rules).

- [ ] **Step 3: Implement the rules**

In `tools/check-uidesc.py`, insert after `check_title` and before `check_file`:

```python
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
```

Then extend `check_file` so the new rules run:

```python
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 tools/test_check_uidesc.py -v`
Expected: PASS — 20 tests OK.

- [ ] **Step 5: Confirm the lint is now red across the suite**

Run: `python3 tools/check-uidesc.py | tail -5`
Expected: a large error count (every plugin still defines `TextDim`) ending in a summary line of the form `checked 15 file(s): N error(s), M warning(s)` with N ≥ 45. Record the exact number in the commit message — Tasks 4 to 9 drive it to zero.

- [ ] **Step 6: Commit**

```bash
git add tools/check-uidesc.py tools/test_check_uidesc.py
git commit -m "test(ui): uidesc lint — palette, TextDim absence, font-color, zone order"
```

---

### Task 3: Style guide, template skeleton, tools README

**Files:**
- Create: `doc/style/ui-style.md`
- Create: `plugins/_template/resource/_template.uidesc`
- Create: `plugins/_template/README.md`
- Create: `tools/README.md`

**Interfaces:**
- Consumes: `tools/check-uidesc.py`, the palette constants from Task 2.
- Produces: `plugins/_template/resource/_template.uidesc` — an L-format skeleton (`460, 420`) that passes the lint with plugin name `_template`, so its title reads `SEAM _TEMPLATE`.

- [ ] **Step 1: Write the style guide**

Create `doc/style/ui-style.md`:

```markdown
# SEAM-LTM UI style

Every plugin in the suite shares one window grammar, so that a user who
learns one GUI has learned all fifteen. This document is the standard;
`tools/check-uidesc.py` enforces the machine-checkable part of it.

## Window anatomy

Zones appear in this vertical order. An absent zone is omitted, never moved.

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
OPS zone or more than about five fine controls. Working and calibration
plugins (dslar, ltglide, multipink, strx) are all L.

The window shape therefore tells the user the plugin's role before they
read a single label.

Two-column geometry (the dslar reference): columns 180 px wide at x=30 and
x=250, a 40 px gutter between them. Within a column a control block is
label (14 px), slider (18 px), value (16 px), and blocks repeat every 58 px.

## Typography and casing

- Title: `SEAM <NAME>`, where `<NAME>` is the plugin directory name
  uppercased. Drawn in `TitleFont`.
- Subtitle and tagline: Title Case.
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

`Structure` is the grey that used to be `TextDim`. It carries the same
value and the same pixels, and a different meaning: it draws shapes, not
words.

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
ctest --test-dir build -R uidesc                    # as part of the suite
```

Errors fail the run; warnings (zone order) do not. The first rule the lint
applies is XML validity, because nothing else in the build parses a
`.uidesc`: on 2026-07-21 a `--` inside a comment shipped an empty editor
that the compiler and the VST3 validator both accepted in silence.

## Starting a new plugin

Copy `plugins/_template/resource/_template.uidesc`, rename it, and run the
lint before writing any C++. The skeleton traces all five zones and passes
the standard as-is.
```

- [ ] **Step 2: Write the template skeleton**

Create `plugins/_template/resource/_template.uidesc`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- SEAM-LTM plugin skeleton, L format. Copy, rename, and delete the zones
     the plugin does not have; never reorder them. Not registered in CMake:
     this file is inert until it is copied. See doc/style/ui-style.md. -->
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="KnobLabelFont" size="12"/>
        <font font-name="Source Code Pro Light" name="ValueFont" size="11"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="11"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
        <color name="Structure" rgba="#888888ff"/>
    </colors>

    <template name="view" class="CViewContainer" origin="0, 0" size="460, 420"
              minSize="460, 420" maxSize="460, 420"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM _TEMPLATE" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Subtitle In Title Case" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="tagline" transparent="true"/>

        <!-- ── SETUP — what the user tells the plugin ─────────────────── -->
        <view class="CTextLabel" origin="0, 88" size="460, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="— STONE —" transparent="true"/>
        <view class="COptionMenu" origin="160, 106" size="140, 20" control-tag="StoneId"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- ── OPS — ALL CAPS labels ──────────────────────────────────── -->
        <view class="CCheckBox" origin="196, 140" size="14, 14" control-tag="Power"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>
        <view class="CTextLabel" origin="214, 138" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="POWER" transparent="true"/>

        <!-- ── FINE — two 180 px columns at x=30 and x=250, 58 px stride ─ -->
        <view class="CTextLabel" origin="30, 172" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="— LEFT —" transparent="true"/>
        <view class="CTextLabel" origin="250, 172" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="— RIGHT —" transparent="true"/>

        <view class="CTextLabel" origin="30, 194" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Control" transparent="true"/>
        <view class="CSlider" origin="30, 210" size="180, 18" control-tag="Control"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"/>
        <view class="CTextEdit" origin="30, 230" size="180, 16" font="ValueFont" control-tag="Control"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <!-- ── FOOTER — what the plugin tells the user, then the logo ──── -->
        <view class="CTextLabel" origin="30, 268" size="400, 16" font="InfoFont"
              font-color="TextLight" text-alignment="left" title="Status:" transparent="true"/>
        <view class="CView" origin="110, 300" size="240, 77" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags>
        <control-tag name="StoneId" tag="100"/>
        <control-tag name="Power"   tag="101"/>
        <control-tag name="Control" tag="102"/>
    </control-tags>
</vstgui-ui-description>
```

- [ ] **Step 3: Verify the skeleton passes its own standard**

Run: `python3 tools/check-uidesc.py plugins/_template/resource/_template.uidesc`
Expected:
```
checked 1 file(s): 0 error(s), 0 warning(s)
```
A skeleton that fails the lint would teach the wrong thing on day one.

- [ ] **Step 4: Write the two READMEs**

Create `plugins/_template/README.md`:

```markdown
# Plugin skeleton

`resource/_template.uidesc` is an L-format starting point that already
conforms to `doc/style/ui-style.md`: all five zones traced, canonical
palette, all-white text, two-column FINE geometry.

This directory is **not** registered in the root `CMakeLists.txt`. Nothing
here builds; it exists to be copied.

To start a plugin:

1. `cp -r plugins/_template plugins/<name>` and rename the `.uidesc`.
2. Change the title to `SEAM <NAME>` with `<NAME>` uppercased — the lint
   checks it against the directory name.
3. Delete the zones the plugin does not have. Do not reorder the rest.
4. Run `python3 tools/check-uidesc.py plugins/<name>/resource/<name>.uidesc`
   before writing any C++.
5. Add `add_subdirectory(plugins/<name>)` to the root `CMakeLists.txt`.

An S-format plugin (a passive converter, up to about four controls) starts
from the same file: narrow the template to `300, N`, drop the SETUP and OPS
zones, and keep one column.
```

Create `tools/README.md`:

```markdown
# tools

Repository-level tooling. Nothing here is required to build the suite;
each script is a check or a generator run on demand.

## `check-uidesc.py` — UI standard lint

Checks every `plugins/*/resource/*.uidesc` against `doc/style/ui-style.md`:
XML validity first, then title, palette, absence of `TextDim`, explicit
white `font-color` on text, and zone order.

```bash
python3 tools/check-uidesc.py                 # every plugin
python3 tools/check-uidesc.py path/to.uidesc  # one file
```

Errors exit non-zero; zone-order findings are warnings and do not.
Standard library only — no virtualenv, no pip step.

Wired into the build as two ctests:

```bash
ctest --test-dir build -R uidesc
```

`uidesc_lint_selftest` runs `test_check_uidesc.py`, which proves each rule
against inline fixture XML; `uidesc_lint` runs the lint over the real
plugins. The self-test exists because a lint that is only ever run against
files it already passes is a lint nobody has tested.

XML validity leads for a reason: no part of the build parses a `.uidesc`.
On 2026-07-21 a `--` inside a comment produced an empty editor in the host
while the compiler and the VST3 validator both reported success.

## `gen-faust-doc.sh` — Faust reference documentation

Regenerates, per plugin, the `-svg/` block diagrams and the mathdoc PDF
from the `.dsp` files that point at the SEAM Faust libraries.

```bash
tools/gen-faust-doc.sh
```

Requires the `faust` binary and `svg2pdf` on the PATH. The output is
committed documentation, so run it when the Faust specification changes,
not on every build.
```

- [ ] **Step 5: Verify the lint still ignores the template in the default sweep**

Run: `python3 tools/check-uidesc.py | grep -c _template`
Expected: `0` — the default glob is `plugins/*/resource/*.uidesc`, which **does** match `_template`. If this prints `1` or more, that is fine only if the template passes; confirm with `python3 tools/check-uidesc.py | grep _template` that no error line mentions it.

- [ ] **Step 6: Commit**

```bash
git add doc/style/ui-style.md plugins/_template tools/README.md
git commit -m "docs(ui): style guide, plugin skeleton, tools README"
```

---

### Task 4: Tier 1 cosmetics — the eleven S-format plugins

`abmodulex`, `b2xrot`, `bamodulex`, `ddelay`, `hilbert`, `lr2xhgr`, `ltburst`, `m2xhgr`, `sdmx`, `x2uhj`, `xyprrot`. All are passive converters or generators with no OPS zone; they stay S format. Only colours, titles and casing change.

**Files:**
- Modify: the eleven `plugins/<name>/resource/<name>.uidesc`
- Modify: `plugins/hilbert/source/hilbert_processor.cpp:161`
- Modify: `plugins/hilbert/source/hilbert_readout_view.h:18` (comment)
- Modify: `plugins/x2uhj/source/x2uhj_processor.cpp:122`
- Modify: `plugins/x2uhj/source/x2uhj_readout_view.h:16` (comment)

**Interfaces:**
- Consumes: the lint from Tasks 1–2 as the test.
- Produces: nothing other tasks depend on.

- [ ] **Step 1: Confirm the failing state**

Run:
```bash
python3 tools/check-uidesc.py $(ls plugins/{abmodulex,b2xrot,bamodulex,ddelay,hilbert,lr2xhgr,ltburst,m2xhgr,sdmx,x2uhj,xyprrot}/resource/*.uidesc)
```
Expected: FAIL — 11 `TextDim colour definition present` errors, 20 `font-color="TextDim"` errors, and the two title errors for `b2xrot` and `xyprrot`.

- [ ] **Step 2: Apply the colour change across all eleven**

In each of the eleven `.uidesc` files:
- delete the line `<color name="TextDim" rgba="#888888ff"/>`;
- replace every `font-color="TextDim"` with `font-color="TextLight"`.

```bash
cd /Users/giuseppe/Documents/github/seam/librerie/seam-ltm
for p in abmodulex b2xrot bamodulex ddelay hilbert lr2xhgr ltburst m2xhgr sdmx x2uhj xyprrot; do
  f=plugins/$p/resource/$p.uidesc
  perl -0pi -e 's{^\s*<color name="TextDim" rgba="\#888888ff"/>\n}{}m; s{font-color="TextDim"}{font-color="TextLight"}g' "$f"
done
```

None of these eleven use `TextDim` for anything but text — verify with:
```bash
grep -rn 'TextDim' plugins/{abmodulex,b2xrot,bamodulex,ddelay,hilbert,lr2xhgr,ltburst,m2xhgr,sdmx,x2uhj,xyprrot}/resource/
```
Expected: no output.

- [ ] **Step 3: Fix the two titles and the one subtitle**

In `plugins/b2xrot/resource/b2xrot.uidesc`, line 24: `title="SEAM B2Xrot"` → `title="SEAM B2XROT"`.

In `plugins/xyprrot/resource/xyprrot.uidesc`, line 24: `title="SEAM XYPRrot"` → `title="SEAM XYPRROT"`.

In `plugins/hilbert/resource/hilbert.uidesc`, line 26: subtitle
`title="wideband quadrature transformer"` → `title="Wideband Quadrature Transformer"`.

- [ ] **Step 4: Update the two C++ readout views**

In `plugins/hilbert/source/hilbert_processor.cpp`, replace lines 156–163:

```cpp
        // Match the suite's text scheme: all text is TextLight, in the same
        // monospace font as the title block, resolved from the uidesc.
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor label = VSTGUI::kWhiteCColor, value = VSTGUI::kWhiteCColor;
        if (description) {
            description->getColor("TextLight", label);
            description->getColor("TextLight", value);
        }
```

In `plugins/x2uhj/source/x2uhj_processor.cpp`, replace lines 117–124 with the identical block (same three lines of comment, same four lines of code).

In `plugins/hilbert/source/hilbert_readout_view.h:18`, change
`// (TextDim labels, TextLight data, Source Code Pro Light).` to
`// (all-white TextLight text, Source Code Pro Light).`
Make the same edit at `plugins/x2uhj/source/x2uhj_readout_view.h:16`.

- [ ] **Step 5: Verify the lint is green for these eleven**

Run:
```bash
python3 tools/check-uidesc.py $(ls plugins/{abmodulex,b2xrot,bamodulex,ddelay,hilbert,lr2xhgr,ltburst,m2xhgr,sdmx,x2uhj,xyprrot}/resource/*.uidesc)
```
Expected: `checked 11 file(s): 0 error(s), 0 warning(s)`

- [ ] **Step 6: Verify the two touched plugins still build**

Run:
```bash
cmake --build build --config Release --target hilbert x2uhj 2>&1 | tail -5
```
Expected: build succeeded, no warnings about `getColor`.

- [ ] **Step 7: Commit**

```bash
git add plugins/{abmodulex,b2xrot,bamodulex,ddelay,hilbert,lr2xhgr,ltburst,m2xhgr,sdmx,x2uhj,xyprrot}
git commit -m "style(ui): all-white text and canonical titles across the S-format plugins"
```

---

### Task 5: Tier 1 cosmetics — dslar

dslar is already structurally conformant (POWER and RESET on top, two columns, L format). It changes colour, casing, and the semantic split between text grey and structural grey.

**Files:**
- Modify: `plugins/dslar/resource/dslar.uidesc`
- Modify: `plugins/dslar/source/dslar_processor.cpp:186`

**Interfaces:**
- Consumes: the lint; `ACCENT_COLORS["Structure"] == "#888888ff"` from Task 2.
- Produces: the `TextDim` → `Structure` precedent that Tasks 8 and 9 follow.

- [ ] **Step 1: Confirm the failing state**

Run: `python3 tools/check-uidesc.py plugins/dslar/resource/dslar.uidesc`
Expected: FAIL — 1 definition error, 6 `font-color="TextDim"` errors, 1 `boxframe-color="TextDim"` error.

- [ ] **Step 2: Swap the palette entry**

In `plugins/dslar/resource/dslar.uidesc`, replace
`        <color name="TextDim" rgba="#888888ff"/>` with
`        <color name="Structure" rgba="#888888ff"/>`.

The value is identical: this is a rename, not a recolour. Nothing on screen moves or changes shade.

- [ ] **Step 3: Split text from structure**

Replace every `font-color="TextDim"` with `font-color="TextLight"` (6 occurrences: the subtitle at line 27, the tagline at 29, the two column headers at 57 and 59, the two meter labels at 119 and 125).

Replace the single `boxframe-color="TextDim"` (line 35, the Power checkbox) with `boxframe-color="Structure"`.

```bash
perl -0pi -e 's{boxframe-color="TextDim"}{boxframe-color="Structure"}g;
              s{font-color="TextDim"}{font-color="TextLight"}g;
              s{<color name="TextDim" rgba="\#888888ff"/>}{<color name="Structure" rgba="\#888888ff"/>}' \
  plugins/dslar/resource/dslar.uidesc
```

- [ ] **Step 4: Title-case the tagline, ALL-CAPS the ops labels**

In `plugins/dslar/resource/dslar.uidesc`:
- line 29: `title="homeostatic loop"` → `title="Homeostatic Loop"`
- line 41: `title="Power"` → `title="POWER"`
- line 47: `title="Reset"` → `title="RESET"`

The two ops labels are 46 px wide at `KnobLabelFont` size 12; `POWER` and `RESET` are five characters and fit where `Power`/`Reset` did.

- [ ] **Step 5: Update the reset button's frame colour in C++**

In `plugins/dslar/source/dslar_processor.cpp`, line 186, change
`            description->getColor("TextDim", frame);` to
`            description->getColor("Structure", frame);`

A button frame is graphic structure, which is exactly what `Structure` names. The `CColor frame = VSTGUI::kGreyCColor;` fallback two lines above stays as it is.

- [ ] **Step 6: Verify lint and build**

Run:
```bash
python3 tools/check-uidesc.py plugins/dslar/resource/dslar.uidesc
cmake --build build --config Release --target dslar 2>&1 | tail -3
```
Expected: `checked 1 file(s): 0 error(s), 0 warning(s)` and a successful build.

- [ ] **Step 7: Commit**

```bash
git add plugins/dslar
git commit -m "style(ui): dslar — all-white text, Structure for frames, ALL-CAPS ops labels"
```

---

### Task 6: Tier 1 cosmetics — strx

strx is the newest GUI and is already all-white: it carries a `TextDim` definition that nothing uses, plus a subtitle that shouts one word.

**Files:**
- Modify: `plugins/strx/resource/strx.uidesc`

**Interfaces:**
- Consumes: the lint.
- Produces: nothing.

- [ ] **Step 1: Confirm the failing state**

Run: `python3 tools/check-uidesc.py plugins/strx/resource/strx.uidesc`
Expected: FAIL — exactly one error, `TextDim colour definition present`. This is the rule from Task 2 earning its keep: strx never *uses* `TextDim`, so a usage-only check would have called this file clean and left the definition to be picked up by the next plugin copied from it.

- [ ] **Step 2: Remove the unused definition and fix the subtitle**

In `plugins/strx/resource/strx.uidesc`:
- delete line 13, `        <color name="TextDim" rgba="#888888ff"/>` (the `Structure` entry on the next line stays: the goniometer's circle and axes use it);
- line 30: `title="STEREO M/S Analyser"` → `title="Stereo M/S Analyser"`.

- [ ] **Step 3: Verify**

Run: `python3 tools/check-uidesc.py plugins/strx/resource/strx.uidesc`
Expected: `checked 1 file(s): 0 error(s), 0 warning(s)`

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/resource/strx.uidesc
git commit -m "style(ui): strx — drop the unused TextDim definition, Title Case subtitle"
```

---

### Task 7: multipink — MUTE becomes POWER

The OPS vocabulary makes POWER the standard on/off for emitters. multipink is the only plugin still carrying MUTE, and an inverted switch reads backwards sitting next to dslar's POWER.

This is a parameter change, which the spec's "out of scope" section excludes and the later OPS-vocabulary decision (2026-07-21) explicitly requires. The later decision governs.

**Decision taken with GS (2026-07-21): clean break on the state format.** The stream writes `power` directly, with 1 meaning on. Sessions saved before this change re-open with POWER **off** — multipink is silent until the user clicks it once. The alternative (keeping mute polarity on the wire and inverting at the boundary) was rejected as a permanent piece of misdirection in the serialisation code for a plugin whose presets are cheap to re-make.

**Files:**
- Modify: `plugins/multipink/source/multipink_ids.h:16`
- Modify: `plugins/multipink/source/multipink_processor.h:96`
- Modify: `plugins/multipink/source/multipink_processor.cpp` (parameter registration ~line 46, `getState`/`setState` ~185–247, `computeGainLin` ~290, `readParameterChanges` ~323, the bus record at ~140)
- Modify: `plugins/multipink/resource/multipink.uidesc` (the checkbox and its control-tag)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces, for Task 8's layout:
  - `kParamPower = 102` (the tag value is unchanged — only the name and the polarity change)
  - control-tag name in the uidesc: `Power`
  - `MULTIPINKProcessor::paramPower_` — `std::atomic<int>`, `1` means sounding, default `1`.

**On testing:** multipink has no unit test and cannot easily get one here. `computeGainLin()` is a member of an `SingleComponentEffect` subclass, so exercising it means constructing the whole SDK object; the suite's other tests all target SDK-free cores (`ltglide_dsp.h`, `seam_meter.h`, …) and there is no such core to extract for a gain path that is four lines long. Verification for this task is therefore the VST3 validator plus a host check, and this is stated rather than papered over with a test that would only assert the compiler works.

- [ ] **Step 1: Rename the id**

In `plugins/multipink/source/multipink_ids.h`, line 16:

```cpp
    kParamPower       = 102,   // 0 = silent / 1 = sounding      (bool)
```

- [ ] **Step 2: Rename the member**

In `plugins/multipink/source/multipink_processor.h`, line 96:

```cpp
    std::atomic<int>    paramPower_{1};          // 1 = sounding (POWER on)
```

Note the default flips with the polarity: an emitter whose default was "not muted" is an emitter whose default is "powered".

- [ ] **Step 3: Update the parameter registration**

In `plugins/multipink/source/multipink_processor.cpp`, replace the `Mute` registration (around line 45):

```cpp
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 1.0, 1,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
```

The default argument moves from `0.0` to `1.0`: the plugin loads sounding.

- [ ] **Step 4: Update every read of the flag**

Bus record, around line 140:

```cpp
    r.active  = (claimedStart_ >= 0 && paramPower_.load() != 0) ? 1u : 0u;
```

`computeGainLin()`, around line 290:

```cpp
    if (!paramPower_.load()) return 0.0;
```

`readParameterChanges`, around line 323:

```cpp
            case kParamPower:
                paramPower_.store(v >= 0.5 ? 1 : 0);
```

- [ ] **Step 5: Update the state format (clean break)**

In `setState`, around lines 198–228:

```cpp
    int32 refIdx = 0; double trim = 0.0; int32 power = 0; int32 prefStart = -1;
    if (!s.readInt32(refIdx))    return kResultFalse;
    if (!s.readDouble(trim))     return kResultFalse;
    if (!s.readInt32(power))     return kResultFalse;
    if (!s.readInt32(prefStart)) return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    // Third field is POWER (1 = sounding). It held MUTE, with the opposite
    // meaning, until the 2026-07 UI revision: a session saved before that
    // re-opens with POWER off and stays silent until the user clicks it.
    // A deliberate one-off break rather than an inverted wire format kept
    // forever in a plugin whose presets take seconds to re-make.
    paramPower_.store(power ? 1 : 0);
```

and the mirror-back, around line 226:

```cpp
    if (auto* p = parameters.getParameter(kParamPower))
        p->setNormalized(paramPower_.load() ? 1.0 : 0.0);
```

In `getState`, around lines 236–243:

```cpp
    int32 power  = paramPower_.load();
    ...
    if (!s.writeInt32(power))      return kResultFalse;
```

- [ ] **Step 6: Update the uidesc**

In `plugins/multipink/resource/multipink.uidesc`, the checkbox (lines 49–53) and the control-tag list:

```xml
        <view class="CCheckBox" origin="100, 222" size="100, 20" control-tag="Power"
              title="POWER" font="ValueFont" font-color="TextLight"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              frame-width="1" transparent="true" autosize-to-fit="false"/>
```

```xml
        <control-tag name="Power"      tag="102"/>
```

- [ ] **Step 7: Verify no stale name survives**

Run: `grep -rn "Mute\|mute" plugins/multipink/ --include=*.h --include=*.cpp --include=*.uidesc`
Expected: only `multipink_pool.cpp`'s `std::mutex` / `g_mutex` / `lock_guard` lines. Any hit on `paramMute_`, `kParamMute` or a `"Mute"` string means a rename was missed.

- [ ] **Step 8: Build and validate**

Run:
```bash
cmake --build build --config Release --target multipink 2>&1 | tail -3
/Users/giuseppe/Documents/github/seam/sdk/vst3sdk/build/bin/Release/validator \
  ~/Library/Audio/Plug-Ins/VST3/multipink.vst3 2>&1 | tail -5
```
Expected: build succeeded; validator reports 0 failures. If the validator binary sits elsewhere, find it with `find /Users/giuseppe/Documents/github/seam/sdk/vst3sdk -name validator -type f -perm +111`.

- [ ] **Step 9: Commit**

```bash
git add plugins/multipink
git commit -m "feat(multipink): MUTE becomes POWER (on = sounding)

The suite's OPS vocabulary makes POWER the standard on/off for emitters;
an inverted switch read backwards next to dslar's POWER.

The state format takes a clean break: the third stream field is now POWER
(1 = sounding) where it was MUTE. Sessions saved before this re-open with
POWER off and stay silent until clicked once."
```

---

### Task 8: multipink — restructure to L format

Current: 300×450 vertical, with the STONE selector *below* the MUTE button (SETUP under OPS) and the pool readouts stacked at the bottom.

Target: 460×390 two columns, zones in order.

The spec says "~460×360"; 390 is what the arithmetic gives once the 77 px logo and the two-line footer are placed on the 58 px column stride. The deviation is deliberate and small.

**Files:**
- Modify: `plugins/multipink/resource/multipink.uidesc`

**Interfaces:**
- Consumes: `control-tag="Power"` from Task 7; the two-column geometry from `doc/style/ui-style.md`.
- Produces: nothing.

- [ ] **Step 1: Confirm the zone-order warning fires today**

Run: `python3 tools/check-uidesc.py plugins/multipink/resource/multipink.uidesc`
Expected: a `WARN` line reading `SETUP (StoneId, y=272) sits below OPS (y=222)`. That warning is the thing this task removes.

- [ ] **Step 2: Replace the template block**

In `plugins/multipink/resource/multipink.uidesc`, replace everything from `<template …>` to `</template>` with:

```xml
    <template name="view" class="CViewContainer" origin="0, 0" size="460, 390"
              minSize="460, 390" maxSize="460, 390"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM MULTIPINK" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Multichannel Pink Noise" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center"
              title="64-slot shared pool &#xB7; RMS-calibrated" transparent="true"/>

        <!-- ── SETUP ──────────────────────────────────────────────────────
             STONE identity for the calibration bus. Declared by hand, never
             inferred from the pool slot: with four STONEs in the room, an
             instance that guesses is an instance that calibrates the wrong
             power amp. Default "?" = undeclared, which strx renders "STONE ?".
             It sits above OPS because identity is set once, before working. -->
        <view class="CTextLabel" origin="0, 88" size="460, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; STONE &#x2014;" transparent="true"/>
        <view class="COptionMenu" origin="160, 106" size="140, 20" control-tag="StoneId"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- ── OPS ───────────────────────────────────────────────────────
             POWER on means sounding, matching dslar and the rest of the
             suite; the box is centred on the window, its caption to the
             right, exactly as dslar draws Power over its left column. -->
        <view class="CCheckBox" origin="196, 140" size="14, 14" control-tag="Power"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>
        <view class="CTextLabel" origin="214, 138" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="POWER" transparent="true"/>

        <!-- ── FINE — two 180 px columns at x=30 and x=250 ─────────────── -->
        <view class="CTextLabel" origin="30, 172" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; REFERENCE &#x2014;" transparent="true"/>
        <view class="CTextLabel" origin="250, 172" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; TRIM &#x2014;" transparent="true"/>

        <view class="CTextLabel" origin="30, 194" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Level (dBFS RMS)" transparent="true"/>
        <view class="COptionMenu" origin="50, 210" size="140, 20" control-tag="Reference"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <view class="CTextLabel" origin="250, 194" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Offset (dB)" transparent="true"/>
        <view class="CSlider" origin="250, 210" size="180, 18" control-tag="Trim" default-value="0.5"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="250, 230" size="180, 16" font="ValueFont" control-tag="Trim"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <!-- ── FOOTER — runtime feedback, then the logo ─────────────────
             Slot, pool occupancy and status are what the plugin reports back;
             they are FOOTER content and never SETUP. -->
        <view class="CTextLabel" origin="30, 262" size="40, 16" font="InfoFont"
              font-color="TextLight" text-alignment="left" title="Slot:" transparent="true"/>
        <view class="CParamDisplay" origin="66, 262" size="40, 16" control-tag="SlotStart"
              font="InfoFont" font-color="TextLight" text-alignment="left" transparent="true" value-precision="0"/>
        <view class="CTextLabel" origin="112, 262" size="24, 16" font="InfoFont"
              font-color="TextLight" text-alignment="left" title="N:" transparent="true"/>
        <view class="CParamDisplay" origin="138, 262" size="40, 16" control-tag="SlotCount"
              font="InfoFont" font-color="TextLight" text-alignment="left" transparent="true" value-precision="0"/>
        <view class="CTextLabel" origin="250, 262" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="right" title="of 64 pool" transparent="true"/>

        <view class="CTextLabel" origin="30, 282" size="56, 16" font="InfoFont"
              font-color="TextLight" text-alignment="left" title="Status:" transparent="true"/>
        <view class="CParamDisplay" origin="90, 282" size="340, 16" control-tag="PoolStatus"
              font="InfoFont" font-color="TextLight" text-alignment="left" transparent="true" value-precision="0"/>

        <!-- SEAM logo (native 240x77 — CView does not scale bitmaps) -->
        <view class="CView" origin="110, 304" size="240, 77" bitmap="logo"/>
    </template>
```

Also delete the now-unused `<color name="TextDim" .../>` line from the `<colors>` block and add `<color name="Structure" rgba="#888888ff"/>` in its place — the POWER checkbox frame needs it.

The status `CParamDisplay` gains 140 px of width in the move (200 → 340).
This is headroom, not a fix: `PoolStatus` produces only four strings, the longest being `"OK (preferred)"` at 14 characters, which already fitted the old 200 px field at the footer font.
The wider field is consistent with the wider L-format layout.

- [ ] **Step 3: Verify the lint is fully green**

Run: `python3 tools/check-uidesc.py plugins/multipink/resource/multipink.uidesc`
Expected: `checked 1 file(s): 0 error(s), 0 warning(s)` — the zone-order warning from Step 1 is gone.

- [ ] **Step 4: Build and open**

Run:
```bash
cmake --build build --config Release --target multipink 2>&1 | tail -3
```
Expected: build succeeded. The GUI cannot be verified by the build — a malformed `.uidesc` compiles happily and shows an empty window — which is why Step 3 runs first and why Task 10 ends in a host check.

- [ ] **Step 5: Commit**

```bash
git add plugins/multipink/resource/multipink.uidesc
git commit -m "style(multipink): L format, zones in order, wider status line"
```

---

### Task 9: ltglide — restructure to L format

Current: 300×800, a single column so tall the window barely fits a laptop screen, with STONE (SETUP) *below* every fine control and LOOP (OPS) below those.

Target: 460×510, two columns, zones in order.

The spec says "~460×480"; 510 is what the nine controls plus the 77 px logo need at the standard 58 px stride. The window loses 290 px of height and gains a shape that says "working plugin" at a glance.

**This is a pure XML edit.** The `LtglideFuseLabel` custom view is constructed in `ltglide_processor.cpp:320` with a placeholder `CRect(0, 0, 260, 14)` that VSTGUI overwrites from the `origin`/`size` attributes in this file, and there is no editor-size constant anywhere in the C++.

**Files:**
- Modify: `plugins/ltglide/resource/ltglide.uidesc`

**Interfaces:**
- Consumes: `custom-view-name="LtglideFuseLabel"` (created by `LTGLIDEProcessor::createCustomView`); control tags 100–109 unchanged.
- Produces: nothing.

- [ ] **Step 1: Confirm the failing state**

Run: `python3 tools/check-uidesc.py plugins/ltglide/resource/ltglide.uidesc`
Expected: FAIL — the `TextDim` definition, 2 `font-color="TextDim"`, 1 `boxframe-color="TextDim"`, plus a zone-order `WARN` (`SETUP (StoneId, y=660) sits below OPS (y=606)`).

- [ ] **Step 2: Fix the palette block**

Replace `        <color name="TextDim" rgba="#888888ff"/>` with
`        <color name="Structure" rgba="#888888ff"/>`.

- [ ] **Step 3: Replace the template block**

Replace everything from `<template …>` to `</template>` with:

```xml
    <template name="view" class="CViewContainer" origin="0, 0" size="460, 510"
              minSize="460, 510" maxSize="460, 510"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM LTGLIDE" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Linkwitz Glissando Tone-Burst" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center"
              title="N=5 grains &#xB7; swept &#xB7; looped" transparent="true"/>

        <!-- ── SETUP ──────────────────────────────────────────────────────
             STONE identity for the calibration bus. ltglide has no slot to be
             inferred from and the receiver cannot read the host's routing, so
             this is declared by hand. Default "?" = undeclared. -->
        <view class="CTextLabel" origin="0, 88" size="460, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; STONE &#x2014;" transparent="true"/>
        <view class="COptionMenu" origin="160, 106" size="140, 20" control-tag="StoneId"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- ── OPS ───────────────────────────────────────────────────────
             The host transport is the sole sounding switch (play = sound,
             stop = silent); LOOP means "repeat while playing", not "start
             sounding". It is the only transport control left after SHOT was
             removed. -->
        <view class="CCheckBox" origin="180, 140" size="100, 20" control-tag="Loop"
              title="LOOP" font="ValueFont" font-color="TextLight"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              frame-width="1" transparent="true" autosize-to-fit="false"/>

        <!-- ── FINE — two 180 px columns at x=30 and x=250, 58 px stride ─
             Left: what the sweep traverses. Right: how it is paced and how
             loud it plays. -->
        <view class="CTextLabel" origin="30, 176" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; SWEEP &#x2014;" transparent="true"/>
        <view class="CTextLabel" origin="250, 176" size="180, 16" font="InfoFont"
              font-color="TextLight" text-alignment="center" title="&#x2014; TIMING &#x2014;" transparent="true"/>

        <!-- Left column: F0 / F1 / Sweep Time / Sweep shape -->
        <view class="CTextLabel" origin="30, 198" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="F0 (Hz)" transparent="true"/>
        <view class="CSlider" origin="30, 214" size="180, 18" control-tag="F0" default-value="1.0"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 234" size="180, 16" font="ValueFont" control-tag="F0"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 256" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="F1 (Hz)" transparent="true"/>
        <view class="CSlider" origin="30, 272" size="180, 18" control-tag="F1" default-value="0.0"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 292" size="180, 16" font="ValueFont" control-tag="F1"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 314" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Sweep Time (s)" transparent="true"/>
        <view class="CSlider" origin="30, 330" size="180, 18" control-tag="Time" default-value="0.153"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 350" size="180, 16" font="ValueFont" control-tag="Time"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="1" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 372" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Sweep" transparent="true"/>
        <view class="COptionMenu" origin="50, 388" size="140, 20" control-tag="Sweep"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- Right column: Level / Delta / Timing mode, then the fuse label -->
        <view class="CTextLabel" origin="250, 198" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Level (dBFS)" transparent="true"/>
        <view class="CSlider" origin="250, 214" size="180, 18" control-tag="Level" default-value="0.667"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="250, 234" size="180, 16" font="ValueFont" control-tag="Level"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="1" style-no-frame="true"/>

        <view class="CTextLabel" origin="250, 256" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Delta (s)" transparent="true"/>
        <view class="CSlider" origin="250, 272" size="180, 18" control-tag="Delta" default-value="0.141"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="250, 292" size="180, 16" font="ValueFont" control-tag="Delta"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="2" style-no-frame="true"/>

        <view class="CTextLabel" origin="250, 314" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="Timing" transparent="true"/>
        <view class="COptionMenu" origin="270, 330" size="140, 20" control-tag="Timing"
              font="ValueFont" font-color="TextLight" back-color="SliderTrack"
              frame-color="SliderTrack" frame-width="1" style-no-frame="false"/>

        <!-- Step-fuse threshold: below f* = 5/delta Hz the step-mode grain
             train fuses into a continuous micro-stepped glissando (see
             ltglide_fuse_label.h and doc/ltglide-validation.md). Blank in
             gap mode, which has no such floor. Sits directly under the
             Timing selector it depends on. -->
        <view class="CView" origin="250, 356" size="180, 14" custom-view-name="LtglideFuseLabel"
              transparent="true"
              tooltip="Step mode only: below this frequency the grain train fuses into a continuous micro-stepped glissando."/>

        <!-- ── FOOTER ─────────────────────────────────────────────────── -->
        <view class="CView" origin="110, 420" size="240, 77" bitmap="logo"/>
    </template>
```

- [ ] **Step 4: Verify the lint is fully green**

Run: `python3 tools/check-uidesc.py plugins/ltglide/resource/ltglide.uidesc`
Expected: `checked 1 file(s): 0 error(s), 0 warning(s)`

- [ ] **Step 5: Check the fuse label still fits its text**

The label narrows from 260 px to 180 px. `LtglideFuseLabel::draw()` formats its string with `snprintf` in `ltglide_fuse_label.h` — read the format string and confirm the longest rendering (worst case `f* = 250 Hz` style text) fits 180 px at `InfoFont` size 11 (Source Code Pro Light is monospace: roughly 6.6 px per character, so about 27 characters).

Run: `grep -n "snprintf\|drawString" plugins/ltglide/source/ltglide_fuse_label.h`
If the longest string exceeds ~27 characters, widen the view to span both columns instead: `origin="30, 356" size="400, 14"` with `text-alignment` handled inside `draw()`. Record which of the two you chose in the commit message.

- [ ] **Step 6: Build**

Run: `cmake --build build --config Release --target ltglide 2>&1 | tail -3`
Expected: build succeeded.

- [ ] **Step 7: Commit**

```bash
git add plugins/ltglide/resource/ltglide.uidesc
git commit -m "style(ltglide): 300x800 strip becomes a 460x510 two-column window"
```

---

### Task 10: Suite-wide verification and close-out

**Files:**
- Modify: `docs/superpowers/specs/2026-07-21-ui-standard-design.md` (status line, and the two notes where reality diverged from the spec)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Full lint, green**

Run: `python3 tools/check-uidesc.py`
Expected:
```
checked 16 file(s): 0 error(s), 0 warning(s)
```
(16, not 15: `plugins/_template` is matched by the same glob and must pass like any other.)

- [ ] **Step 2: Full ctest**

Run: `ctest --test-dir build -C Release`
Expected: every test passes, including `uidesc_lint_selftest` and `uidesc_lint`.

- [ ] **Step 3: Prove the lint can still fail**

A lint that has only ever been seen green is a lint nobody has tested. Break one file deliberately and confirm red:

```bash
perl -pi -e 's{font-color="TextLight"}{font-color="Structure"} if $. == 26' plugins/dslar/resource/dslar.uidesc
ctest --test-dir build -C Release -R uidesc_lint
git checkout plugins/dslar/resource/dslar.uidesc
```
Expected: the middle command FAILS with an error mentioning `Structure`, and the last restores the file. Then re-run `ctest --test-dir build -C Release -R uidesc_lint` and confirm it is green again.

- [ ] **Step 4: Full build and validator sweep**

Run:
```bash
cmake --build build --config Release 2>&1 | tail -5
```
Expected: all 15 plugins build.

Then run the VST3 validator over the three plugins whose window or parameters changed:
```bash
VALIDATOR=$(find /Users/giuseppe/Documents/github/seam/sdk/vst3sdk -name validator -type f -perm +111 | head -1)
for p in multipink ltglide dslar; do
  echo "── $p"; "$VALIDATOR" ~/Library/Audio/Plug-Ins/VST3/$p.vst3 2>&1 | tail -3
done
```
Expected: 0 failures for each.

- [ ] **Step 5: Host check with GS**

The build and the validator both accept a `.uidesc` that renders an empty window; only a host proves the GUI. Ask Giuseppe to open in Reaper and confirm:

- **multipink** — 460×390, POWER on by default and sounding, STONE menu above POWER, wider status line (headroom, not a truncation fix — the old field already fitted the longest status string);
- **ltglide** — 460×510, LOOP toggling, the fuse label appearing in step mode and blank in gap mode, all nine controls responding;
- **dslar** — unchanged except POWER/RESET now in caps and the subtitle white;
- **strx** — unchanged except the subtitle;
- one S-format plugin of his choice — subtitle now white rather than grey.

- [ ] **Step 6: Close the spec**

In `docs/superpowers/specs/2026-07-21-ui-standard-design.md`:
- change `Status: approved (brainstorm with GS)` to `Status: implemented (2026-07-22, plan docs/superpowers/plans/2026-07-22-ui-standard.md)`;
- in the Tier 2 section, strike the sentence "The editor size constant in C++ and the SHOT custom-view position change with it." and replace it with: "No editor size constant exists in C++ — geometry lives entirely in the .uidesc, and SHOT was removed in f8fb76e. The restructure is a pure XML edit."
- record the two final window sizes (`multipink` 460×390, `ltglide` 460×510) against the spec's approximations;
- in "Out of scope", note that the multipink MUTE → POWER rename *was* carried out under this work, per the OPS-vocabulary decision that post-dates the spec.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-07-21-ui-standard-design.md
git commit -m "docs(ui): close the UI standard spec — implemented across all 15 plugins"
```

---

## Self-review against the spec

| Spec requirement | Task |
|---|---|
| Zone order HEADER→SETUP→OPS→FINE→FOOTER | 2 (lint warning), 8, 9 (applied) |
| Title = `SEAM <DIRNAME>` uppercased; b2xrot, xyprrot fixed | 1 (rule), 4 (fix) |
| Subtitles Title Case (hilbert, dslar tagline, strx) | 4, 5, 6 |
| Dividers `— NAME —` | 3 (guide, template), 8, 9 |
| Ops labels ALL CAPS | 5 (dslar Power/Reset), 7 (multipink), 9 (ltglide LOOP already caps) |
| All text `TextLight`; `TextDim` removed, definition included | 2 (rule), 4, 5, 6, 8, 9 |
| Canonical palette values enforced | 2 |
| Accents allowed, never on text | 2 (rule), 5 (Structure introduced) |
| S / L formats; working plugins are L | 3 (guide), 8, 9 (dslar and strx already L) |
| `doc/style/ui-style.md` | 3 |
| `plugins/_template/` skeleton, not in CMake | 3 |
| `tools/check-uidesc.py`, full XML validation first | 1 |
| Lint wired as ctest | 1 |
| `tools/README.md` | 3 |
| Lint green on all plugins | 10 |
| Affected plugins build | 4, 5, 7, 8, 9, 10 |
| GS visual check in Reaper | 10 |
| MUTE → POWER (OPS vocabulary decision, post-dates the spec) | 7 |

**Deliberate divergences from the spec, all recorded in Task 10 Step 6:**
- `multipink` is 460×390 and `ltglide` 460×510, against the spec's "~360" and "~480": the 77 px logo and the 58 px column stride need the extra room.
- Tier 2 involves no C++ geometry change — there is no size constant, and SHOT no longer exists.
- `TextDim` becomes `Structure` (same `#888888ff`) wherever it framed a shape rather than coloured text; the spec only anticipated the text case.
