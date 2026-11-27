#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Splits a T0 run's tests over runners by measured duration (t0.yml: one build job, N test jobs, one verdict job).

  tools/ci/shard.py plan  --build build --regex RX --out plan.json [--costs tools/ci/test_costs.json] [--target 300] [--max 8]
  tools/ci/shard.py regex plan.json K          the ctest -R regex of shard K (the plan's own regex when there is one shard)
  tools/ci/shard.py costdata plan.json K FILE  FILE = build/Testing/Temporary/CTestCostData.txt: ctest starts the slowest first
  tools/ci/shard.py costs --old tools/ci/test_costs.json TIMES.tsv...   the new costs file (stdout) from run.py --times files
  tools/ci/shard.py --self-test

The tests are the ones tools/test/run.py would run for `-R RX -E ^marks$` (nightly left out, fixtures come along with -R).
Costs are seconds of one ctest entry on a CI runner with 4 tests at a time (tools/ci/test_costs.json, refreshed from the
`times.tsv` a T0 run leaves in its t0-result artifact); a test without a cost counts DEFAULT_S. The shard count is the
estimated total over TARGET_S x 4 slots (at most --max), and the tests go longest first onto the least loaded shard.
"""
import argparse
import importlib.util
import json
import math
import os
import re
import sys

DEFAULT_S = 10.0   # a test nobody measured yet
SLOTS = 4          # ctest -j on a 4 vCPU runner
MAX_NAMES = 700    # per -R regex (cmsys' regex compiler has a size limit)


def load_run():
    spec = importlib.util.spec_from_file_location("sf_run", os.path.join(os.path.dirname(__file__), "..", "test", "run.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_costs(path):
    try:
        return {k: float(v) for k, v in json.load(open(path)).items()}
    except (OSError, ValueError):
        return {}


def split(names, costs, target_s=420.0, max_shards=8):
    """-> (shards, estimates): the names in n lists, balanced by cost (longest first onto the least loaded)."""
    cost = {n: costs.get(n, DEFAULT_S) for n in names}
    total = sum(cost.values())
    n = max(1, min(max_shards, math.ceil(total / (target_s * SLOTS)), len(names) or 1))
    n = max(n, min(max_shards, math.ceil(len(names) / MAX_NAMES)))
    shards, load = [[] for _ in range(n)], [0.0] * n
    for name in sorted(names, key=lambda x: (-cost[x], x)):
        i = load.index(min(load))
        shards[i].append(name)
        load[i] += cost[name]
    return shards, [round(x, 1) for x in load]


def plan(a):
    run = load_run()
    args = run.without_nightly(["-R", a.regex, "-E", "^marks$"])
    tests = run.load_tests(os.path.abspath(a.build), args)
    if tests is None:
        print("shard.py: ctest can't list the tests", file=sys.stderr)
        return 2
    names = sorted(t["name"] for t in tests if not t["disabled"])
    shards, est = split(names, read_costs(a.costs), a.target, a.max)
    json.dump({"regex": a.regex, "tests": len(names), "n": len(shards), "estimate_s": est, "shards": shards},
              open(a.out, "w"), indent=1)
    print(f"shard.py: {len(names)} tests -> {len(shards)} shard(s), estimated {', '.join(str(int(x // SLOTS)) for x in est)} s each")
    return 0


def shard_regex(p, k):
    if p["n"] == 1:
        return p["regex"]
    return "^(" + "|".join(re.escape(n) for n in sorted(p["shards"][k])) + ")$"


def costs(a):
    """The old costs, with every test the TIMES files measured replaced by its mean over them."""
    out, seen = read_costs(a.old), {}
    for path in a.times:
        for line in open(path):
            f = line.rstrip("\n").split("\t")
            if len(f) >= 3 and f[2] in ("pass", "flaky") and not f[0].startswith("case:"):  # (case: lines are single TEST cases inside a unit shard)
                try:
                    seen.setdefault(f[0], []).append(float(f[1]))
                except ValueError:
                    pass
    out.update({n: round(sum(v) / len(v), 1) for n, v in seen.items()})
    print(json.dumps(dict(sorted(out.items())), indent=0))
    return 0


def self_test():
    names = [f"t{i}" for i in range(40)]
    c = {"t0": 600, "t1": 300, "t2": 300}
    shards, est = split(names, c, target_s=100, max_shards=4)
    assert sorted(sum(shards, [])) == sorted(names) and len(shards) == 4, shards
    assert max(est) - min(est) <= 600, est   # the 600 s test alone is a shard's longest piece
    assert split(names[:5], {}, 420, 8)[0] == [sorted(names[:5])], "few tests: one shard"
    assert len(split([f"x{i}" for i in range(1500)], {}, 1e9, 8)[0]) == 3, "name cap per regex"
    p = {"regex": "a|b", "n": 1, "shards": [["a", "b"]]}
    assert shard_regex(p, 0) == "a|b"
    p = {"regex": ".", "n": 2, "shards": [["a.b", "c"], ["d"]]}
    assert shard_regex(p, 0) == r"^(a\.b|c)$" and shard_regex(p, 1) == "^(d)$"
    print("shard.py self-test ok")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--build", default="build")
    p.add_argument("--regex", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--costs", default=os.path.join(os.path.dirname(__file__), "test_costs.json"))
    p.add_argument("--target", type=float, default=240.0)
    p.add_argument("--max", type=int, default=8)
    p = sub.add_parser("regex")
    p.add_argument("plan")
    p.add_argument("k", type=int)
    p = sub.add_parser("costdata")
    p.add_argument("plan")
    p.add_argument("k", type=int)
    p.add_argument("file")
    p.add_argument("--costs", default=os.path.join(os.path.dirname(__file__), "test_costs.json"))
    p = sub.add_parser("costs")
    p.add_argument("--old", default=os.path.join(os.path.dirname(__file__), "test_costs.json"))
    p.add_argument("times", nargs="*")
    a = ap.parse_args()
    if a.cmd == "plan":
        return plan(a)
    if a.cmd == "costs":
        return costs(a)
    pl = json.load(open(a.plan))
    if a.cmd == "regex":
        print(shard_regex(pl, a.k))
        return 0
    c = read_costs(a.costs)  # CTestCostData.txt: "name runs cost", then "---": ctest orders the longest first with it
    os.makedirs(os.path.dirname(a.file), exist_ok=True)
    with open(a.file, "w") as f:
        for n in pl["shards"][a.k]:
            f.write(f"{n} 1 {c.get(n, DEFAULT_S)}\n")
        f.write("---\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
