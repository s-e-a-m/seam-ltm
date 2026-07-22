#!/usr/bin/env python3
"""Unit tests for check-uidesc.py, driven by inline fixture XML."""
import contextlib
import importlib.util
import io
import os
import shutil
import tempfile
import unittest
import unittest.mock

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

    def test_duplicate_title_label_fails(self):
        second = TITLE_OK.replace("SEAM DEMO", "SEAM OTHER")
        body = TITLE_OK + "\n" + second
        root = self._root(fixture(body))
        errors = check_uidesc.check_title(root, "demo", "p.uidesc")
        self.assertEqual(len(errors), 1)
        self.assertIn("2 TitleFont labels", errors[0])
        self.assertIn("'SEAM DEMO'", errors[0])
        self.assertIn("'SEAM OTHER'", errors[0])


class TestCheckFile(unittest.TestCase):
    def setUp(self):
        import tempfile
        tmpdir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmpdir)
        # The filename ("anything.uidesc") is deliberately not "demo" —
        # check_file must derive the plugin name from the *directory* two
        # levels up ("demo"), not from the file itself.
        resource_dir = os.path.join(tmpdir, "plugins", "demo", "resource")
        os.makedirs(resource_dir)
        self.path = os.path.join(resource_dir, "anything.uidesc")

    def _write(self, text):
        with open(self.path, "w") as f:
            f.write(text)
        return self.path

    def test_plugin_name_comes_from_directory_matching_title_passes(self):
        path = self._write(fixture(TITLE_OK))
        self.assertEqual(check_uidesc.check_file(path), [])

    def test_plugin_name_comes_from_directory_not_filename_fails(self):
        # If check_file used the filename ("anything") instead of the
        # directory ("demo"), this title would wrongly pass.
        body = TITLE_OK.replace("SEAM DEMO", "SEAM ANYTHING")
        path = self._write(fixture(body))
        errors = check_uidesc.check_file(path)
        self.assertEqual(len(errors), 1)
        self.assertIn("SEAM DEMO", errors[0])

    def test_malformed_document_reports_only_parse_error(self):
        body = TITLE_OK + '\n    <!-- a comment -- with a double dash -->'
        path = self._write(fixture(body))
        errors = check_uidesc.check_file(path)
        self.assertEqual(len(errors), 1)
        self.assertIn("malformed XML", errors[0])


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

    def test_both_violations_warn_without_a_third_overlapping_message(self):
        # SETUP below OPS *and* OPS below FINE together also mean SETUP is
        # below FINE, but that's implied by the two messages already
        # emitted — a third complaint about the same layout would be noise.
        setup_low = self.SETUP.replace('origin="160, 106"', 'origin="160, 606"')
        ops_low = self.OPS.replace('origin="180, 140"', 'origin="180, 300"')
        body = "\n".join([TITLE_OK, setup_low, ops_low, self.FINE])
        messages = check_uidesc.check_zone_order(self._root(body), "p.uidesc")
        self.assertEqual(len(messages), 2)

    def test_setup_below_fine_warns_when_ops_is_absent(self):
        # No OPS zone at all (e.g. a passive converter that still declares
        # a STONE identity): the SETUP-vs-OPS and OPS-vs-FINE comparisons
        # both go silent since ops_y is None, so this is the only check
        # that can catch SETUP sitting below the fine controls.
        setup_low = self.SETUP.replace('origin="160, 106"', 'origin="160, 252"')
        body = "\n".join([TITLE_OK, setup_low, self.FINE])
        messages = check_uidesc.check_zone_order(self._root(body), "p.uidesc")
        self.assertEqual(len(messages), 1)
        self.assertIn("WARN", messages[0])
        self.assertIn("SETUP", messages[0])
        self.assertIn("fine control", messages[0])

    def test_setup_above_fine_with_no_ops_passes(self):
        body = "\n".join([TITLE_OK, self.SETUP, self.FINE])
        self.assertEqual(check_uidesc.check_zone_order(self._root(body), "p.uidesc"), [])

    def test_untitled_checkbox_with_ops_control_tag_is_recognised(self):
        # The actual bug shape (dslar, plugins/_template): POWER is an
        # untitled CCheckBox (title="") carrying control-tag="Power", with
        # its caption drawn by a separate CTextLabel next to it. The OPS
        # fixture above carries a control-tag *and* an all-caps title at
        # once, so it cannot isolate the control-tag branch of
        # _is_ops_view from the title branch — a title-only predicate
        # already matches it. Here the checkbox has no title of its own,
        # so only the control-tag branch can find it, and it sits below
        # the first CSlider so a correct implementation must warn.
        ops_untitled = ('    <view class="CCheckBox" origin="93, 260" size="14, 14"'
                        ' control-tag="Power" title=""/>')
        caption = ('    <view class="CTextLabel" origin="30, 260" size="60, 14"'
                   ' font-color="TextLight" title="POWER"/>')
        body = "\n".join([TITLE_OK, self.SETUP, self.FINE, ops_untitled, caption])
        messages = check_uidesc.check_zone_order(self._root(body), "p.uidesc")
        self.assertEqual(len(messages), 1)
        self.assertIn("WARN", messages[0])
        self.assertIn("OPS", messages[0])


class TestSourceColors(unittest.TestCase):
    """The C++ sweep: text drawn from source names its colours in C++, where
    no .uidesc rule can see them, and that is where the greys came back."""

    def setUp(self):
        self.plugins = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.plugins)

    def _source(self, name, text, plugin="demo"):
        directory = os.path.join(self.plugins, plugin, "source")
        os.makedirs(directory, exist_ok=True)
        path = os.path.join(directory, name)
        with open(path, "w") as f:
            f.write(text)
        return path

    def test_conforming_source_passes(self):
        self._source("demo_processor.cpp",
                     'description->getColor("TextLight", text);\n')
        self.assertEqual(check_uidesc.check_source_colors(self.plugins), [])

    def test_getcolor_call_fails(self):
        # hilbert and x2uhj shipped exactly this line until this branch.
        self._source("demo_processor.cpp",
                     'void draw() {\n'
                     '    description->getColor("TextDim", label);\n'
                     '}\n')
        messages = check_uidesc.check_source_colors(self.plugins)
        self.assertEqual(len(messages), 1)
        self.assertIn("ERROR", messages[0])
        self.assertIn("line 2", messages[0])
        self.assertIn("demo_processor.cpp", messages[0])

    def test_comment_naming_the_removed_colour_fails(self):
        # Two view headers carried a comment about the TextDim tier long
        # after the colour itself was gone; the name is what is banned.
        self._source("demo_view.h", '// label drawn in TextDim\n')
        messages = check_uidesc.check_source_colors(self.plugins)
        self.assertEqual(len(messages), 1)
        self.assertIn("line 1", messages[0])

    def test_only_h_and_cpp_are_read(self):
        self._source("notes.txt", 'TextDim\n')
        self.assertEqual(check_uidesc.check_source_colors(self.plugins), [])

    def test_every_occurrence_is_reported_across_plugins(self):
        self._source("a_processor.cpp", 'TextDim\nTextLight\nTextDim\n',
                     plugin="alpha")
        self._source("b_view.h", 'TextDim\n', plugin="beta")
        messages = check_uidesc.check_source_colors(self.plugins)
        self.assertEqual(len(messages), 3)

    def test_the_real_suite_is_clean(self):
        plugins = os.path.join(os.path.dirname(_HERE), "plugins")
        self.assertEqual(check_uidesc.check_source_colors(plugins), [])


class TestMessageSeverity(unittest.TestCase):
    """A message knows its own level; the printed form is unchanged."""

    def test_error_prints_and_reports_its_level(self):
        message = check_uidesc.error("p.uidesc", "something is wrong")
        self.assertEqual(message, "p.uidesc: ERROR something is wrong")
        self.assertEqual(message.level, check_uidesc.ERROR)

    def test_warn_prints_and_reports_its_level(self):
        message = check_uidesc.warn("p.uidesc", "something is odd")
        self.assertEqual(message, "p.uidesc: WARN something is odd")
        self.assertEqual(message.level, check_uidesc.WARN)

    def test_severity_survives_rewording(self):
        # The point of carrying the level as data: a message whose text never
        # contains the word ERROR is still an error.
        message = check_uidesc.error("p.uidesc", "title mismatch")
        self.assertEqual(message.level, check_uidesc.ERROR)


class TestMainExitCode(unittest.TestCase):
    """main() decides whether the ctest passes. Nothing else in the suite
    pins that decision, so a rule can keep reporting while the run goes
    green — which is how a lint gets switched off without anyone noticing."""

    def _run(self, *paths):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = check_uidesc.main(["check-uidesc.py"] + list(paths))
        return code, out.getvalue()

    def _plugin_file(self, body):
        tmpdir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmpdir)
        resource = os.path.join(tmpdir, "plugins", "demo", "resource")
        os.makedirs(resource)
        path = os.path.join(resource, "demo.uidesc")
        with open(path, "w") as f:
            f.write(body)
        return path

    def test_conforming_file_returns_zero(self):
        # The skeleton is the conforming case by construction: a new plugin
        # starts from it and must lint clean before any C++ is written.
        template = os.path.join(os.path.dirname(_HERE), "plugins", "_template",
                                "resource", "_template.uidesc")
        code, output = self._run(template)
        self.assertEqual(code, 0)
        self.assertIn("0 error(s)", output)

    def test_file_with_an_error_returns_one(self):
        path = self._plugin_file(fixture(TITLE_OK.replace("SEAM DEMO", "SEAM WRONG")))
        code, output = self._run(path)
        self.assertEqual(code, 1)
        self.assertIn("ERROR", output)
        self.assertIn("1 error(s)", output)

    def test_warning_only_file_returns_zero(self):
        # Warnings are printed and counted and do not fail the run: the other
        # half of the contract, and the half a "failed = messages" slip breaks.
        setup_low = ('    <view class="COptionMenu" origin="160, 252" size="140, 20"'
                     ' control-tag="StoneId" font-color="TextLight"/>')
        fine = ('    <view class="CSlider" origin="30, 228" size="180, 18"'
                ' control-tag="Trim"/>')
        path = self._plugin_file(fixture("\n".join([TITLE_OK, setup_low, fine])))
        code, output = self._run(path)
        self.assertEqual(code, 0)
        self.assertIn("WARN", output)
        self.assertIn("0 error(s), 1 warning(s)", output)


class TestMainWholeSuite(unittest.TestCase):
    """With no arguments the lint sweeps the whole suite, C++ included."""

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root)
        resource = os.path.join(self.root, "plugins", "demo", "resource")
        self.source = os.path.join(self.root, "plugins", "demo", "source")
        os.makedirs(resource)
        os.makedirs(self.source)
        self.uidesc = os.path.join(resource, "demo.uidesc")
        with open(self.uidesc, "w") as f:
            f.write(fixture(TITLE_OK))

    def _run(self, *paths):
        out = io.StringIO()
        with unittest.mock.patch.object(check_uidesc, "REPO_ROOT", self.root):
            with contextlib.redirect_stdout(out):
                code = check_uidesc.main(["check-uidesc.py"] + list(paths))
        return code, out.getvalue()

    def _grey_source(self):
        with open(os.path.join(self.source, "demo_processor.cpp"), "w") as f:
            f.write('description->getColor("TextDim", label);\n')

    def test_clean_suite_passes_and_counts_only_uidesc_documents(self):
        # No C++ written at all: the sweep has nothing to find.
        code, output = self._run()
        self.assertEqual(code, 0)
        self.assertIn("checked 1 file(s): 0 error(s), 0 warning(s)", output)

    def test_grey_in_cpp_fails_the_run(self):
        self._grey_source()
        code, output = self._run()
        self.assertEqual(code, 1)
        self.assertIn("names TextDim", output)
        # The count is documents, the errors are messages: the .uidesc is
        # still the only file counted, and the C++ defect is still an error.
        self.assertIn("checked 1 file(s): 1 error(s), 0 warning(s)", output)

    def test_naming_one_uidesc_does_not_sweep_the_source_tree(self):
        self._grey_source()
        code, output = self._run(self.uidesc)
        self.assertEqual(code, 0)
        self.assertNotIn("names TextDim", output)


if __name__ == "__main__":
    unittest.main()
