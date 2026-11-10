#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Areas of the test flow (docs/testing-flow.md "Areas"): which part of the app a change is in, and so which tests to run.

  tools/test/affected.py [--base origin/main] [--build build] [-v] [--json]   changed files from git (merge base + work tree)
  tools/test/affected.py --files a.cpp b.h ...                                 an explicit file list
  tools/test/affected.py --area cloth,ui                                       the tests of named areas (no git)
  tools/test/affected.py --self-test

The map is tests/areas.map: [paths] (a source file -> area), [tests] (a ctest name -> area; CMake labels every entry
area_<name>, cmake/TestAreas.cmake), [units] (tests/test_*.cpp -> area; sculptforge_tests --area), [smoke], [core].

Prints one line, like impacted.py: a `ctest -R` regex of the area tests plus smoke, or `.` for the full suite:
  - a path no [paths] line covers (the map has a catch-all per folder, so this is a new kind of file),
  - a [core] path (CMake, the test harness, the map itself) or a header included by 30+ files under src/ and tests/
    (Document.h, Mesh.h, Json.h ...): anything can break.
With --json: {"full": bool, "areas": [...], "regex": "...", "unit_areas": "cloth,ui", "why": [...]} for t0.sh / tools/ci/t0.sh
(unit_areas is SF_TEST_AREA for the sculptforge_tests shards: they run only those areas' TEST cases).
Nightly tests are never listed. The tests are the ctest list of --build (ctest --show-only=json), each given its area by
the [tests] rules.
"""
import argparse
import json
import os
import re
import subprocess
import sys

CORE_FAN_IN = 30  # a header included by this many files under src/ and tests/ is core
SOURCE_EXT = (".h", ".hpp", ".cpp", ".mm", ".inc")


def sh(*cmd, cwd=None):
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return ""


def glob_match(glob, s):
    """`*` matches anything, '/' included; every other character is itself (tests/areas.map, cmake/TestAreas.cmake)."""
    return re.fullmatch(".*".join(re.escape(p) for p in glob.split("*")), s) is not None


class AreaMap:
    def __init__(self, text):
        self.sections = {}
        self.areas = {}  # name -> what it owns
        section = ""
        for raw in text.split("\n"):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("["):
                section = line.strip("[]")
                self.sections[section] = []
            elif section == "areas":
                name, _, what = line.partition("|")
                self.areas[name.strip()] = what.strip()
            elif section in ("tests", "units", "paths"):
                parts = line.split()
                if len(parts) == 2:
                    self.sections[section].append((parts[0], parts[1]))
            elif section in ("core", "smoke"):
                self.sections[section].append(line)

    @classmethod
    def load(cls, root):
        return cls(open(os.path.join(root, "tests", "areas.map"), encoding="utf-8").read())

    def first(self, section, key):
        for glob, area in self.sections.get(section, []):
            if glob_match(glob, key):
                return area
        return None

    def test_area(self, name):
        return self.first("tests", name)

    def unit_area(self, stem):
        return self.first("units", stem)

    def path_area(self, path):
        """Area of a repo path; tests/ui/x.sfui and tests/test_x.cpp are the area of the test they belong to."""
        a = self.first("paths", path)
        if a == "@test":
            return self.test_area("ui_" + os.path.splitext(os.path.basename(path))[0]) or self.test_area("ui_x")
        if a == "@unit":
            return self.unit_area(os.path.splitext(os.path.basename(path))[0])
        return a

    def is_core_path(self, path):
        return any(glob_match(g, path) for g in self.sections.get("core", []))


def includers(root, header):
    """How many files under src/ and tests/ include this header (by its file name: `#include "x/Name.h"`)."""
    base = os.path.basename(header)
    pat = re.compile(r'^\s*#\s*include\s*"(?:[^"]*/)?' + re.escape(base) + '"', re.M)
    n = 0
    for top in ("src", "tests"):
        for d, _, files in os.walk(os.path.join(root, top)):
            for f in files:
                if f.endswith(SOURCE_EXT):
                    try:
                        if pat.search(open(os.path.join(d, f), encoding="utf-8", errors="replace").read()):
                            n += 1
                    except OSError:
                        pass
    return n


def load_tests(build):
    out = sh("ctest", "--test-dir", build, "--show-only=json-v1")
    try:
        data = json.loads(out)
    except ValueError:
        return {}
    tests = {}
    for t in data.get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if not props.get("DISABLED"):
            tests[t["name"]] = props.get("LABELS", [])
    return tests


def affected(root, files, amap, tests=None, fan_in=None):
    """-> {"full", "areas", "regex", "unit_areas", "why"} for the changed files. tests: {ctest name: labels}."""
    fan_in = fan_in or (lambda h: includers(root, h))
    why, areas, full = [], set(), False
    for f in files:
        if amap.is_core_path(f):
            full = True
            why.append(f"{f}: core path, the full suite")
            continue
        if f.endswith((".h", ".hpp")) and f.startswith(("src/", "tests/")):
            n = fan_in(f)
            if n >= CORE_FAN_IN:
                full = True
                why.append(f"{f}: core header ({n} files include it), the full suite")
                continue
        a = amap.path_area(f)
        if a is None:
            full = True
            why.append(f"{f}: no area (add it to tests/areas.map [paths]), the full suite")
        else:
            areas.add(a)
            why.append(f"{f}: {a}")
    res = {"full": full, "areas": sorted(areas), "why": why}
    # The unit cases (SF_TEST_AREA): the areas themselves; a file in no [units] area never filters (runs always).
    res["unit_areas"] = ",".join(sorted(areas)) if not full else ""
    res["regex"] = regex_for(areas, amap, tests, full)
    return res


def tests_of(areas, amap, tests):
    """Names of the non-nightly tests of these areas, plus the smoke set; the sculptforge_tests shards when any area has units."""
    out = set()
    for name, labels in (tests or {}).items():
        if "nightly" in labels:
            continue
        a = amap.test_area(name)
        if a in areas or name in amap.sections.get("smoke", []) or a == "unit":
            out.add(name)
    if "marks" in (tests or {}):
        out.add("marks")
    return out


def regex_for(areas, amap, tests, full):
    if full:
        return "."
    names = tests_of(areas, amap, tests)
    return "^(" + "|".join(sorted(names)) + ")$" if names else ""


def merge(res, impacted_regex):
    """The area tests plus the direct picks of impacted.py (a script's own tests, a UI test sharing a word with the file ...):
    never fewer tests than the area says, and `.` from either side stays the full suite."""
    if res["full"] or impacted_regex == "." or res["regex"] == ".":
        return "."
    picks = set(re.findall(r"[A-Za-z0-9_]+", impacted_regex))
    mine = set(re.findall(r"[A-Za-z0-9_]+", res["regex"]))
    names = mine | picks
    return "^(" + "|".join(sorted(names)) + ")$" if names else ""


def changed_files(root, base):
    mb = sh("git", "merge-base", base, "HEAD", cwd=root).strip() or base
    out = sh("git", "diff", "--name-only", mb, cwd=root) + sh("git", "ls-files", "-o", "--exclude-standard", cwd=root)
    return sorted({f for f in out.split("\n") if f})


def self_test():
    import tempfile
    amap = AreaMap("""
[areas]
cloth | c
ui | u
unit | shards
site | s
[tests]
ui_cloth*  cloth
ui_*       ui
sculptforge_tests_*  unit
*          site
[units]
test_cloth  cloth
test_*      ui
[paths]
src/core/Cloth*  cloth
src/app/*        ui
src/core/*       ui
tests/ui/*       @test
tests/test_*.cpp @unit
docs/*           site
[core]
CMakeLists.txt
tests/areas.map
[smoke]
ui_shell
""")
    tests = {"ui_cloth_drape": ["ui"], "ui_cloth_brush": ["ui"], "ui_shell": ["ui"], "ui_tube": ["ui"], "ui_bench": ["ui", "nightly"],
             "sculptforge_tests_0of2": [], "sculptforge_tests_1of2": [], "marks": [], "guide_site": []}
    fan = lambda h: 40 if h.endswith("Mesh.h") else 3
    r = lambda *f: affected("/nonexistent", list(f), amap, tests, fan)
    a = r("src/core/Cloth.cpp")
    assert a["areas"] == ["cloth"] and a["unit_areas"] == "cloth" and not a["full"], a
    assert a["regex"] == "^(marks|sculptforge_tests_0of2|sculptforge_tests_1of2|ui_cloth_brush|ui_cloth_drape|ui_shell)$", a["regex"]
    assert r("src/core/Cloth.cpp", "src/app/Shell.cpp")["unit_areas"] == "cloth,ui"
    assert r("src/core/Mesh.h")["regex"] == "." and r("src/core/Mesh.h")["full"]
    assert r("src/core/Small.h")["regex"] != "."
    assert r("CMakeLists.txt")["regex"] == "." and r("tests/areas.map")["full"]
    assert r("weird/new.thing")["regex"] == ".", "an unknown path is the full suite"
    assert r("docs/x.md")["areas"] == ["site"]
    assert r("tests/ui/cloth_drape.sfui")["areas"] == ["cloth"] and r("tests/ui/tube.sfui")["areas"] == ["ui"]
    assert r("tests/test_cloth.cpp")["areas"] == ["cloth"] and r("tests/test_other.cpp")["areas"] == ["ui"]
    assert glob_match("ui_*", "ui_x") and not glob_match("ui_*", "xui_x") and glob_match("a?b*", "a?bc")
    # includers() on a real tree
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, "src/core"))
        os.makedirs(os.path.join(root, "tests"))
        open(os.path.join(root, "src/core/A.h"), "w").write("#pragma once\n")
        open(os.path.join(root, "src/core/B.cpp"), "w").write('#include "core/A.h"\n')
        open(os.path.join(root, "tests/t.cpp"), "w").write('#include "A.h"\n#include "NotA.h"\n')
        assert includers(root, "src/core/A.h") == 2
    print("affected.py self-test passed")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--area", help="comma list of areas: their tests, no git")
    ap.add_argument("--merge", help="impacted.py's regex: its picks join the area tests (its '.' stays '.')")
    ap.add_argument("--print", default="regex", choices=("regex", "unit_areas"), help="what to print (default: the regex)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    root = sh("git", "rev-parse", "--show-toplevel").strip() or os.getcwd()
    amap = AreaMap.load(root)
    tests = load_tests(os.path.abspath(a.build))
    if a.area:
        names = [x for x in a.area.split(",") if x]
        bad = [x for x in names if x not in amap.areas]
        if bad:
            print(f"unknown area {' '.join(bad)}; areas: {' '.join(amap.areas)}", file=sys.stderr)
            return 2
        res = {"full": False, "areas": names, "unit_areas": ",".join(names), "why": [],
               "regex": regex_for(set(names), amap, tests, False)}
    else:
        files = a.files if a.files is not None else changed_files(root, a.base)
        res = affected(root, files, amap, tests)
    if a.merge is not None:
        res["regex"] = merge(res, a.merge)
    if a.verbose:
        print(f"areas: {' '.join(res['areas']) or '-'}", file=sys.stderr)
        for w in res["why"]:
            print("  " + w, file=sys.stderr)
    print(json.dumps(res) if a.json else res[a.print])
    return 0


if __name__ == "__main__":
    sys.exit(main())
