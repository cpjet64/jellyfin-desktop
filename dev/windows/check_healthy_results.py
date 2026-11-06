"""Require exactly the selected successful CTest cases in the diagnostic report."""
import collections
import sys
import xml.etree.ElementTree as ET


def check(path, expected):
    if not expected or len(expected) != len(set(expected)):
        raise ValueError("empty or duplicate expected diagnostic tests")
    root = ET.parse(path).getroot()
    suites = ([root] if root.tag == "testsuite" else []) + root.findall(".//testsuite")
    if not suites:
        raise ValueError("missing diagnostic test suite")
    for suite in suites:
        try:
            counts = {key: int(suite.attrib[key]) for key in ("tests", "failures")}
            counts.update({key: int(suite.get(key, "0")) for key in ("errors", "disabled", "skipped")})
        except (KeyError, ValueError) as exc:
            raise ValueError("missing or malformed diagnostic suite counts") from exc
        if counts["tests"] != len(suite.findall(".//testcase")) or any(counts[key] != 0 for key in ("failures", "errors", "disabled", "skipped")):
            raise ValueError("inconsistent diagnostic suite counts")
    cases = root.findall(".//testcase")
    if collections.Counter(case.get("name") for case in cases) != collections.Counter(expected):
        raise ValueError("diagnostic test names do not match the allowlist")
    for case in cases:
        if case.get("status") != "run" or any(case.find(tag) is not None for tag in ("failure", "error", "skipped")):
            raise ValueError("diagnostic test did not pass")


if __name__ == "__main__":
    check(sys.argv[1], sys.argv[2:])
