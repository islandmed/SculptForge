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
import importlib.util
import json
import os
import re
import subprocess
import sys

CORE_FAN_IN = 30  # a src/core header included by this many files under src/ and tests/ is core
AREA_CAP = 40     # T0 runs a whole area only up to this many tests; a broader one rides impacted.py's picks + smoke + its unit cases
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
            stem = os.path.splitext(os.path.basename(path))[0]
            if path.startswith("tests/e2e/"):  # tests/e2e/x.sfui is ui_e2e_x (quit_*.sfui belong to e2e_quit_clean)
                return self.test_area("ui_e2e_" + stem) or self.test_area("ui_e2e_x")
            return self.test_area("ui_" + stem) or self.test_area("ui_x")
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


def affected(root, files, amap, tests=None, fan_in=None, cmake_regs=None):
    """-> {"full", "areas", "regex", "unit_areas", "why"} for the changed files. tests: {ctest name: labels}.
    cmake_regs: the test names a CMakeLists.txt diff only registers or edits (impacted.test_registrations), None when it
    changes anything else: a branch that adds a UI test is its area's change, not a build change."""
    fan_in = fan_in or (lambda h: includers(root, h))
    why, areas, full = [], set(), False
    for f in files:
        if f == "CMakeLists.txt" and cmake_regs is not None:
            for n in sorted(cmake_regs):
                a = amap.test_area(n)
                areas.add(a or "site")
                why.append(f"CMakeLists.txt registers {n}: {a}")
            continue
        if amap.is_core_path(f):
            full = True
            why.append(f"{f}: core path, the full suite")
            continue
        if f.endswith((".h", ".hpp")) and f.startswith(("src/core/", "tests/")):  # the data model; app/render headers are impacted.py's build graph
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
    res["t0_regex"] = regex_for(areas, amap, tests, full, AREA_CAP)  # what T0 runs (merge)
    return res


def tests_of(areas, amap, tests, cap=None):
    """Names of the non-nightly tests of these areas, plus the smoke set; the sculptforge_tests shards when any area has units.
    cap: leave out the tests of an area with more than that many (smoke and shards stay)."""
    out = set()
    size = {}
    for name, labels in (tests or {}).items():
        if "nightly" not in labels:
            a = amap.test_area(name)
            size[a] = size.get(a, 0) + 1
    for name, labels in (tests or {}).items():
        if "nightly" in labels:
            continue
        a = amap.test_area(name)
        if (a in areas and not (cap and size.get(a, 0) > cap)) or name in amap.sections.get("smoke", []) or a == "unit":
            out.add(name)
    if "marks" in (tests or {}):
        out.add("marks")
    return out


def regex_for(areas, amap, tests, full, cap=None):
    if full:
        return "."
    names = tests_of(areas, amap, tests, cap)
    return "^(" + "|".join(sorted(names)) + ")$" if names else ""


def merge(res, impacted_regex):
    """The area tests plus the direct picks of impacted.py (a script's own tests, a UI test sharing a word with the file ...):
    and `.` from either side stays the full suite. A broad area (over AREA_CAP tests) adds only its unit cases and the smoke
    set here: its UI tests are impacted.py's picks, and `affected.py --area X` / `ctest -L area_X` run them all."""
    if res["full"] or impacted_regex == "." or res["regex"] == ".":
        return "."
    picks = set(re.findall(r"[A-Za-z0-9_]+", impacted_regex))
    mine = set(re.findall(r"[A-Za-z0-9_]+", res.get("t0_regex", res["regex"])))
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
    assert merge(a, "^(ui_tube)$") == "^(marks|sculptforge_tests_0of2|sculptforge_tests_1of2|ui_cloth_brush|ui_cloth_drape|ui_shell|ui_tube)$"
    big = {f"ui_n{i}": ["ui"] for i in range(AREA_CAP + 1)}
    big.update(tests)
    b = affected("/nonexistent", ["src/app/Shell.cpp"], amap, big, fan)
    assert "ui_n3" in b["regex"] and "ui_n3" not in merge(b, "^(ui_tube)$") and "ui_tube" in merge(b, "^(ui_tube)$"), "a broad area is not added whole to T0"
    assert r("src/core/Mesh.h")["regex"] == "." and r("src/core/Mesh.h")["full"]
    assert r("src/core/Small.h")["regex"] != "."
    assert r("CMakeLists.txt")["regex"] == "." and r("tests/areas.map")["full"]
    c = affected("/nonexistent", ["CMakeLists.txt", "tests/ui/tube.sfui"], amap, tests, fan, cmake_regs={"ui_cloth_drape"})
    assert not c["full"] and c["areas"] == ["cloth", "ui"], c  # a CMake edit that only registers a test is that test's area
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
    ap.add_argument("--cmake-diff", help="git diff -U0 of CMakeLists.txt against the base (a tree without .git)")
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
        regs = None
        if "CMakeLists.txt" in files:
            diff = open(a.cmake_diff, encoding="utf-8").read() if a.cmake_diff else sh(
                "git", "diff", "-U0", sh("git", "merge-base", a.base, "HEAD", cwd=root).strip() or a.base, "--", "CMakeLists.txt", cwd=root)
            try:
                spec = importlib.util.spec_from_file_location("impacted", os.path.join(os.path.dirname(os.path.abspath(__file__)), "impacted.py"))
                imp = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(imp)
                regs = imp.test_registrations(open(os.path.join(root, "CMakeLists.txt"), encoding="utf-8").read(), diff) if diff else None
            except (OSError, ImportError, AttributeError):
                regs = None
        res = affected(root, files, amap, tests, cmake_regs=regs)
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
