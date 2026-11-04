"""Reject empty, skipped, or unsuccessful CI test reports."""

import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def check_qtest(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    lines = [line for line in text.splitlines() if line.strip()]
    totals = re.findall(r"(?m)^Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped\b[^\r\n]*$", text)
    footer = r"\*{9} Finished testing of [A-Za-z_][A-Za-z_0-9]* \*{9}"
    failure = re.search(r"(?m)^(?:FAIL!|SKIP\s*:)", text)
    if (len(lines) < 2 or len(totals) != 1 or not lines[-2].startswith("Totals:") or
            not re.fullmatch(footer, lines[-1]) or failure or int(totals[0][0]) < 3 or
            int(totals[0][1]) or int(totals[0][2])):
        raise ValueError(f"Incomplete or unsuccessful QTest report: {path}")


def check_junit(path, required=()):
    root = ET.parse(path).getroot()
    tests = root.findall(".//testcase")
    if not tests or any(test.find(name) is not None for test in tests for name in ("failure", "error", "skipped")):
        raise ValueError(f"Empty, failed, or skipped CTest report: {path}")
    missing = set(required) - {test.get("name") for test in tests}
    if missing:
        raise ValueError(f"Missing CTest cases: {', '.join(sorted(missing))}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("qtest", "junit"))
    parser.add_argument("path")
    parser.add_argument("--require", action="append", default=[])
    args = parser.parse_args()
    if args.kind == "qtest":
        check_qtest(args.path)
    else:
        check_junit(args.path, args.require)
    print(f"Verified {args.kind} report: {args.path}")


if __name__ == "__main__":
    main()
