#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay past T0 runs through the test selection, before and after a change to it (docs/testing-flow.md).

  tools/test/replay_selection.py RESULTS.tsv [--old-ref origin/main] [--ctest-logs 'DIR/*/ctest.log']

RESULTS.tsv: t0-ci.sh's log (date, branch, head, t0sha, result, failed tests, run). For every run whose commit is here,
the changed files (merge base with origin/main .. t0sha) go through affected.py with the CMake rule of --old-ref's
impacted.py and of this tree's. Prints the runs that leave the full suite, their estimated minutes, and every failed
test of a red run that the new area selection doesn't name (impacted.py's build-graph picks join it on the real run,
so such a line is a lead to check by hand, not a proven miss). Times: the median seconds per test in the ctest logs.
"""
import argparse
import glob
import importlib.util
import os
import re
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def git(*a):
    return subprocess.run(["git", *a], capture_output=True, text=True).stdout


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def times(pattern):
    per = {}
    for f in glob.glob(pattern):
        for line in open(f, encoding="utf-8", errors="replace"):
            m = re.search(r"Test +#\d+: (\S+) \.+.*?(Passed|Failed|\*\*\*\S+).*?([\d.]+) sec", line)
            if m:
                per.setdefault(m.group(1), []).append(float(m.group(3)))
    return {n: statistics.median(v) for n, v in per.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("results")
    ap.add_argument("--old-ref", default="origin/main")
    ap.add_argument("--ctest-logs", default=os.path.expanduser("~/Work/.sf/t0-ci/*/ctest.log"))
    ap.add_argument("--jobs", type=int, default=4, help="ctest -j of the runs (Actions: 4)")
    a = ap.parse_args()
    root = git("rev-parse", "--show-toplevel").strip()
    aff = load("affected", os.path.join(HERE, "affected.py"))
    new = load("impacted_new", os.path.join(HERE, "impacted.py"))
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as t:
        t.write(git("show", f"{a.old_ref}:tools/test/impacted.py"))
    old = load("impacted_old", t.name)
    os.unlink(t.name)
    amap = aff.AreaMap(open(os.path.join(root, "tests/areas.map"), encoding="utf-8").read())
    secs = times(a.ctest_logs)
    tests = {n: [] for n in secs}
    full_s = sum(secs.values()) / a.jobs
    rows, seen = [], set()
    for line in open(a.results, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        c = line.rstrip("\n").split("\t")
        if len(c) < 5 or not git("cat-file", "-t", c[3]).strip():
            continue
        rows.append(c)
    fan = {}
    changed_full, misses, n = [], [], 0
    for c in rows:
        sha, failed = c[3], (c[5].split() if len(c) > 5 else [])
        base = git("merge-base", sha, "origin/main").strip()
        files = [f for f in git("diff", "--name-only", base, sha).split("\n") if f]
        if not files:
            continue
        n += 1
        text = git("show", f"{sha}:CMakeLists.txt")
        diff = git("diff", "-U0", base, sha, "--", "CMakeLists.txt") if "CMakeLists.txt" in files else ""
        fan_in = lambda h: fan.setdefault(h, aff.includers(root, h))
        ro = old.test_registrations(text, diff) if diff else None
        rn = new.test_registrations(text, diff, files) if diff else None
        before = aff.affected(root, files, amap, tests, fan_in, ro)
        after = aff.affected(root, files, amap, tests, fan_in, rn)
        if before["full"] and not after["full"]:
            pick = re.findall(r"[\w.+-]+", after["t0_regex"].strip("^()$")) + list(new.ALWAYS)
            for f in files:  # impacted.py's graph-free picks (the build graph's need a build)
                for g, targets in new.HAND_MAP:
                    if targets != "." and new.fnmatch.fnmatch(f, g):
                        pick += targets.split()
            est = sum(secs.get(p, 0) for p in pick) / a.jobs
            changed_full.append((c[0], c[1], sha[:8], c[4], len(pick), est))
            if c[4] == "red":
                named = set(pick)  # (an area regex already holds the tests named by name)
                for f in failed:
                    if f not in named:
                        misses.append((c[1], sha[:8], f))
    print(f"{n} runs replayed; {len(changed_full)} leave the full suite (full suite ~{full_s / 60:.1f} min of tests at -j{a.jobs})")
    saved = 0
    for d, br, sha, res, k, est in changed_full:
        saved += full_s - est
        print(f"  {d} {br} {sha} {res}: {k} tests, ~{est / 60:.1f} min")
    print(f"test minutes saved: ~{saved / 60:.0f}")
    print(f"failed tests of red runs not named by the areas (check the build graph picks): {len(misses)}")
    for m in misses:
        print("  " + " ".join(m))
    return 0


if __name__ == "__main__":
    sys.exit(main())
