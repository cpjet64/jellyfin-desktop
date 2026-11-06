import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import check_healthy_results as report


class ReportTests(unittest.TestCase):
    def check_xml(self, xml, expected=("first", "second")):
        if '<testsuite>' in xml:
            count = xml.count('<testcase ')
            xml = xml.replace('<testsuite>', f'<testsuite tests="{count}" failures="0" errors="0" disabled="0">')
        with patch.object(report.ET, "parse", return_value=ET.ElementTree(ET.fromstring(xml))):
            report.check("unused", expected)

    def test_exact_passing_cases(self):
        self.check_xml('<testsuite><testcase name="first" status="run"/><testcase name="second" status="run"/></testsuite>')
        self.check_xml('<testsuite tests="2" failures="0"><testcase name="first" status="run"/><testcase name="second" status="run"/></testsuite>')

    def test_missing_extra_duplicate_and_nonpassing(self):
        cases = [
            '', '<testcase name="first" status="run"/>',
            '<testcase name="first" status="run"/><testcase name="first" status="run"/>',
            '<testcase name="first" status="run"/><testcase name="second" status="run"/><testcase name="extra" status="run"/>',
            '<testcase name="first" status="run"/><testcase name="second" status="notrun"/>',
            '<testcase name="first" status="run"/><testcase name="second" status="run"><failure/></testcase>',
            '<testcase name="first" status="run"/><testcase name="second" status="run"><skipped/></testcase>',
        ]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.check_xml('<testsuite>' + value + '</testsuite>')

    def test_missing_and_malformed_report(self):
        for error in (FileNotFoundError(), ET.ParseError()):
            with self.subTest(error=error), patch.object(report.ET, "parse", side_effect=error), self.assertRaises(type(error)):
                report.check("unused", ["first"])

    def test_bad_suite_counts(self):
        cases = '<testcase name="first" status="run"/><testcase name="second" status="run"/>'
        for attrs in ('tests="3" failures="0" errors="0" disabled="0"',
                      'tests="2" failures="1" errors="0" disabled="0"',
                      'tests="2" failures="0" errors="bad" disabled="0"',
                      'tests="2" errors="0" disabled="0"'):
            with self.subTest(attrs=attrs), self.assertRaises(ValueError):
                self.check_xml(f'<testsuite {attrs}>{cases}</testsuite>')


if __name__ == "__main__":
    unittest.main()
