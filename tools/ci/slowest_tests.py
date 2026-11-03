#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The slowest tests of a ctest run, for the CI job summary: whole ctest entries, and the single tests inside the
sculptforge_tests shards (their "[ OK ] name (N ms)" lines). Tells which test to split or move when a CI shard
becomes the long pole.

  tools/ci/slowest_tests.py build/ctest.xml [N]     (ctest --output-junit ctest.xml writes it)
"""
import re
import sys

try:
    import xml.etree.ElementTree as ET
    ET.fromstring("<a/>")
except ImportError:  # some system Pythons (Rocky 8's 3.12) ship a pyexpat that doesn't load
    ET = None


def main(argv):
    if ET is None:
        print("no XML parser in this Python: slowest-tests table skipped")
        return 0
    if not argv or argv[0] == "--self-test":
        return self_test()
    try:
        root = ET.parse(argv[0]).getroot()
    except (OSError, ET.ParseError) as e:
        print(f"no ctest results: {e}")
        return 0
    n = int(argv[1]) if len(argv) > 1 else 12
    entries, inner = report(root)
    print(f"| slowest ctest entries | s |\n|---|---|")
    for t, name in entries[:n]:
        print(f"| {name} | {t:.1f} |")
    print(f"\n| slowest tests inside sculptforge_tests | s |\n|---|---|")
    for t, name in inner[:n]:
        print(f"| {name} | {t:.1f} |")
    return 0


def report(root):
    entries, inner = [], []
    for case in root.iter("testcase"):
        entries.append((float(case.get("time") or 0), case.get("name")))
        out = "".join(e.text or "" for e in case.iter("system-out"))
        for name, ms in re.findall(r"^\[ OK \] (\S+) \((\d+) ms\)", out, re.M):
            inner.append((int(ms) / 1000, name))
    return sorted(entries, reverse=True), sorted(inner, reverse=True)


def self_test():
    xml = ('<testsuite><testcase name="sculptforge_tests_3of8" time="282.1"><system-out>[ RUN] a\n[ OK ] a (120500 ms)\n'
           '[ OK ] b (12 ms)\n</system-out></testcase><testcase name="ui_bake" time="51"/></testsuite>')
    entries, inner = report(ET.fromstring(xml))
    assert entries == [(282.1, "sculptforge_tests_3of8"), (51.0, "ui_bake")], entries
    assert inner == [(120.5, "a"), (0.012, "b")], inner
    print("slowest_tests self-test ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
