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


if __name__ == "__main__":
    unittest.main()
