#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes tests/shard_times.inc: the sculptforge_tests cases that take MIN seconds or more, with their median seconds,
which tests/test_main.cpp uses to balance the --shard K/N shards by time (TestHarness.h, inShard).

    tools/test/shard_times.py /tmp/sf-pc-*-times.tsv [--override /tmp/sf-pc-<branch>-times.tsv --match REGEX] [--min 0.5]

Reads the per-test times tools/test/run.py writes (`case:<name>\\t<seconds>\\tpass` rows; ~/Work/.sf/pc.sh copies them
to /tmp/sf-pc-<worktree>-times.tsv). --override: these files' times replace the others' for the cases --match finds
(a branch that changed those tests' length; one run's times of the other tests are noisier than the median). Re-run it when a test gets much faster or slower; a missing or stale line only unbalances the shards.
"""
import argparse
import os
import re
import statistics
import sys


def read_cases(paths):
    times = {}
    for path in paths:
        try:
            with open(path, encoding="utf-8") as f:
                for line in f:
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) >= 3 and parts[0].startswith("case:") and parts[2] == "pass":
                        times.setdefault(parts[0][5:], []).append(float(parts[1]))
        except (OSError, ValueError) as e:
            print(f"shard_times.py: skipping {path}: {e}", file=sys.stderr)
    return {name: statistics.median(v) for name, v in times.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tsv", nargs="+")
    ap.add_argument("--override", action="append", default=[])
    ap.add_argument("--match", default="", help="regex: the cases --override replaces (default: all of its cases)")
    ap.add_argument("--min", type=float, default=0.5, help="seconds a case takes to be listed (default 0.5)")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument("--out", default=os.path.join(root, "tests", "shard_times.inc"))
    a = ap.parse_args()
    times = read_cases(a.tsv)
    times.update({n: t for n, t in read_cases(a.override).items() if re.search(a.match, n)})
    rows = sorted(((s, n) for n, s in times.items() if s >= a.min and not n.startswith("nightly_")), key=lambda r: (-r[0], r[1]))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(f"// The sculptforge_tests cases that take {a.min:g} s or more: median seconds on the PC (RTX 3060, WSL, ctest -j5\n"
                "// per slot). Written by tools/test/shard_times.py; tests/test_main.cpp balances the shards with it.\n")
        f.writelines(f'{{"{n}", {s:.2f}}},\n' for s, n in rows)
    print(f"shard_times.py: {len(rows)} cases, {sum(s for s, _ in rows):.0f} s -> {a.out}")


if __name__ == "__main__":
    main()
