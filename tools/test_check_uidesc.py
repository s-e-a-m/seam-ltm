#!/usr/bin/env python3
"""Unit tests for check-uidesc.py, driven by inline fixture XML."""
import importlib.util
import os
import shutil
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


if __name__ == "__main__":
    unittest.main()
