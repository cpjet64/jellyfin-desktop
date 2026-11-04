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
        self.check("PASS   : Example::initTestCase()\nPASS   : Example::test()\nPASS   : Example::cleanupTestCase()\n"
                   "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n********* Finished testing of Example *********\n")

    def test_rejects_totals_pass_count_mismatch(self):
        text = ("PASS   : Example::initTestCase()\nPASS   : Example::test()\nPASS   : Example::cleanupTestCase()\n"
                "Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted\n********* Finished testing of Example *********\n")
        with self.assertRaisesRegex(ValueError, "Incomplete or unsuccessful QTest"):
            self.check(text)

    def test_failed_skipped_empty_and_short_reports_fail(self):
        for text in ("", "Totals: 2 passed, 0 failed, 0 skipped",
                     "Totals: 3 passed, 1 failed, 0 skipped",
                     "Totals: 3 passed, 0 failed, 1 skipped"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.check(text)

    def test_rejects_fail_skip_multiple_totals_and_missing_footer(self):
        footer = "********* Finished testing of Example *********"
        good = "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted"
        for text in (good, f"FAIL!  : Example::case()\n{good}\n{footer}",
                     f"SKIP   : Example::case()\n{good}\n{footer}",
                     f"{good}\nTotals: 3 passed, 1 failed, 0 skipped\n{footer}",
                     f"{good}\n{footer}\nextra"):
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

    def test_every_required_case_must_be_present(self):
        xml = '<testsuite><testcase name="test_fault_stop"/><testcase name="test_webclient_transport"/></testsuite>'
        self.check(xml, ["test_fault_stop", "test_webclient_transport"])
        with self.assertRaisesRegex(ValueError, "test_mpvqt_event_ownership"):
            self.check(xml, ["test_fault_stop", "test_webclient_transport", "test_mpvqt_event_ownership"])


if __name__ == "__main__":
    unittest.main()
