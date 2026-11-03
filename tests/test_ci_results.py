"""Acceptance checks for the CI report gate, using no build or user files."""

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ci_results", ROOT / "dev/ci/check_results.py")
RESULTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESULTS)


class TestQtReports(unittest.TestCase):
    def check(self, text):
        with patch.object(Path, "read_text", return_value=text):
            RESULTS.check_qtest("unused")

    def test_valid_report_passes(self):
        self.check("Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted")

    def test_failed_skipped_empty_and_short_reports_fail(self):
        for text in ("", "Totals: 2 passed, 0 failed, 0 skipped",
                     "Totals: 3 passed, 1 failed, 0 skipped",
                     "Totals: 3 passed, 0 failed, 1 skipped"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.check(text)


class TestCTestReports(unittest.TestCase):
    def check(self, xml, required=()):
        with patch.object(RESULTS.ET, "parse", return_value=ET.ElementTree(ET.fromstring(xml))):
            RESULTS.check_junit("unused", required)

    def test_required_passed_case(self):
        self.check('<testsuite><testcase name="test_mpris_fault"/></testsuite>', ["test_mpris_fault"])

    def test_missing_empty_failed_and_skipped_cases_fail(self):
        cases = ('<testsuite/>', '<testsuite><testcase name="other"/></testsuite>',
                 '<testsuite><testcase name="test_mpris_fault"><failure/></testcase></testsuite>',
                 '<testsuite><testcase name="test_mpris_fault"><skipped/></testcase></testsuite>')
        for xml in cases:
            with self.subTest(xml=xml), self.assertRaises(ValueError):
                self.check(xml, ["test_mpris_fault"])


if __name__ == "__main__":
    unittest.main()
