#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest entry test_areas: every test belongs to exactly one area (tests/areas.map, docs/testing-flow.md "Areas").

  tools/test/check_areas.py --map tests/areas.map --tsv build/test-areas.tsv [--units build/sculptforge_tests]

Fails when a ctest entry (the tsv cmake/TestAreas.cmake wrote), a tests/test_*.cpp file (sculptforge_tests --list-areas) or
a [smoke] name has no area / no test, or a rule names an area that [areas] doesn't list. Fix: add a line to tests/areas.map.
Rules that match nothing are listed, not failed (an old glob harms nobody).
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import affected  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--tsv", required=True)
    ap.add_argument("--units")
    a = ap.parse_args()
    amap = affected.AreaMap(open(a.map, encoding="utf-8").read())
    problems = []
    names = {}
    for line in open(a.tsv, encoding="utf-8"):
        name, area, _smoke = line.rstrip("\n").split("\t")
        names[name] = area
        if area == "-":
            problems.append(f"ctest entry {name} has no area: add a line to tests/areas.map [tests]")
        elif area not in amap.areas:
            problems.append(f"ctest entry {name}: area {area} is not in [areas]")
    for section in ("tests", "units", "paths"):
        for glob, area in amap.sections.get(section, []):
            if area not in amap.areas and not area.startswith("@"):
                problems.append(f"[{section}] {glob}: area {area} is not in [areas]")
    for n in amap.sections.get("smoke", []):
        if n not in names:
            problems.append(f"[smoke] {n}: no such ctest entry")
    used = set()
    for n in names:
        for glob, _ in amap.sections["tests"]:
            if affected.glob_match(glob, n):
                used.add(glob)
                break
    if a.units:
        out = subprocess.run([a.units, "--list-areas"], capture_output=True, text=True, check=True,
                             env={**os.environ, "SF_TEST_AREAS_MAP": a.map}).stdout
        for line in out.splitlines():
            f, area = line.split("\t")
            if area == "-":
                problems.append(f"unit test file {os.path.basename(f)} has no area: add a line to tests/areas.map [units]")
            elif area not in amap.areas:
                problems.append(f"unit test file {os.path.basename(f)}: area {area} is not in [areas]")
    per = {}
    for area in names.values():
        per[area] = per.get(area, 0) + 1
    print("areas: " + ", ".join(f"{k} {v}" for k, v in sorted(per.items())))
    dead = [g for g, _ in amap.sections["tests"] if g not in used]
    if dead:
        print("[tests] rules matching no entry here (fine when the build leaves tests out): " + " ".join(dead))
    for p in problems:
        print("FAILED: " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
