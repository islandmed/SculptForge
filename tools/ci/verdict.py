#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The T0 verdict files (tools/ci/t0.sh, .github/workflows/t0.yml): result.json, summary.md, output.txt, failed.txt.

  tools/ci/verdict.py finish OUT RESULT STAGE REGEX BUILD_S TEST_S TOTAL_S BASE FAILED TIER [TESTS_RUN SHARDS]
      one run's result.json + summary.md in OUT (the build stage failing, or a single-runner run)
  tools/ci/verdict.py merge OUT PLAN_DIR SHARD_DIR...
      the verdict of a sharded run: PLAN_DIR is the build job's folder (plan.json, and result.json when the build stage
      already ended the run), each SHARD_DIR a test job's (shard.json, ctest.log, failed.txt, output.txt, times.tsv);
      writes OUT/{result.json,summary.md,output.txt,failed.txt,times.tsv,ui-fails/}. A shard that left nothing writes
      no result at all (the caller reruns the failed jobs once, as for any runner that died).
  tools/ci/verdict.py --self-test

result.json keeps the fields ~/Work/.sf/t0-ci.sh and ready.sh read ({result, stage, sha, base, regex, failed, build_s,
test_s, total_s, tier}); tests_run and shards are new. test_s is the slowest shard's, total_s the run from the build job's
start to this verdict.
"""
import json
import os
import shutil
import sys
import time


def finish(out, result, stage, regex, build_s, test_s, total_s, base, failed, tier, tests_run=0, shards=1, extra=()):
    os.makedirs(out, exist_ok=True)
    r = {"result": result, "stage": stage, "sha": os.environ.get("GITHUB_SHA", ""), "base": base, "regex": regex,
         "failed": failed.split(), "build_s": int(build_s), "test_s": int(test_s), "total_s": int(total_s), "tier": tier,
         "tests_run": int(tests_run), "shards": int(shards)}
    json.dump(r, open(f"{out}/result.json", "w"), indent=1)
    # (a push-tier green is the per-push smoke, never the READY verdict: its title says so)
    lines = [f"## T0 {result}" + ("" if result == "green" else f" ({stage})") + (" (push tier: not a READY verdict)" if tier == "push" else ""), "",
             f"- commit `{r['sha'][:8]}`, base `{base[:8]}`; build {build_s} s, tests {test_s} s, {total_s} s in all",
             f"- tests: `{regex or 'none'}`" + (f", {tests_run} run in {shards} runner(s)" if tests_run else "")]
    if r["failed"]:
        lines += ["- failed: " + " ".join(f"`{n}`" for n in r["failed"])]
    lines += list(extra)
    for name, title in (("failed.txt", "LastTestsFailed + FAILED lines"), ("output.txt", "output on failure (last 300 lines)")):
        p = f"{out}/{name}"
        if os.path.exists(p) and os.path.getsize(p):
            lines += ["", f"### {title}", "", "```", open(p, errors="replace").read()[-60000:].rstrip(), "```"]
    open(f"{out}/summary.md", "w").write("\n".join(lines) + "\n")
    return r


def read_json(path):
    try:
        return json.load(open(path))
    except (OSError, ValueError):
        return None


def read_text(path):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return ""


def merge(out, plan_dir, shard_dirs):
    plan = read_json(f"{plan_dir}/plan.json")
    if plan is None:
        print("verdict.py: the build job left no plan.json: no verdict")
        return 0
    os.makedirs(out, exist_ok=True)
    elapsed = max(0, int(time.time() - plan.get("started", time.time())))
    if plan.get("early"):  # marks, gallery, configure, build or iwyu ended the run: the build job's own verdict
        r = read_json(f"{plan_dir}/result.json")
        if r is None:
            print("verdict.py: the plan says the run ended early but has no result.json: no verdict")
            return 0
        for name in os.listdir(plan_dir):
            if name not in ("plan.json",) and os.path.isfile(f"{plan_dir}/{name}"):
                shutil.copy(f"{plan_dir}/{name}", f"{out}/{name}")
        r["total_s"] = elapsed
        json.dump(r, open(f"{out}/result.json", "w"), indent=1)
        return 0
    shards, iw, iw_dir = {}, None, ""
    for d in shard_dirs:
        s = read_json(f"{d}/shard.json")
        if s is not None:
            shards[s["shard"]] = (d, s)
        elif read_json(f"{d}/iwyu.json") is not None:
            iw, iw_dir = read_json(f"{d}/iwyu.json"), d
    n = plan.get("n", 0)
    missing = [k for k in range(n) if k not in shards]
    if missing or (plan.get("iwyu") and iw is None):
        print(f"verdict.py: no result from test job(s) {missing}{' or the iwyu job' if plan.get('iwyu') and iw is None else ''}: no verdict")
        return 0
    failed, notes, out_txt, failed_txt, times, rows, ran = [], [], [], [], [], [], 0
    ui = f"{out}/ui-fails"
    for k in range(n):
        d, s = shards[k]
        failed += s.get("failed", [])
        ran += s.get("ran", 0)
        rows.append((k, s.get("tests", 0), s.get("test_s", 0), s.get("result", "red")))
        if s.get("result") != "green":
            for name, acc in (("output.txt", out_txt), ("failed.txt", failed_txt)):
                t = read_text(f"{d}/{name}").strip()
                if t:
                    acc.append(f"== shard {k}\n{t}")
            if not s.get("failed"):
                failed.append(f"shard{k}_no_result")
        times.append(read_text(f"{d}/times.tsv"))
        if os.path.isdir(f"{d}/ui-fails"):
            os.makedirs(ui, exist_ok=True)
            for f in os.listdir(f"{d}/ui-fails"):
                shutil.copy(f"{d}/ui-fails/{f}", f"{ui}/s{k}-{f}")
    stage = "test"
    if iw is not None and iw.get("result") != "green":
        failed.insert(0, "iwyu_lite")
        stage = "iwyu"
        out_txt.insert(0, read_text(f"{iw_dir}/output.txt").strip())
    failed = list(dict.fromkeys(failed))
    open(f"{out}/output.txt", "w").write("\n".join(x for x in out_txt if x) + ("\n" if out_txt else ""))
    open(f"{out}/failed.txt", "w").write("\n".join(failed_txt) + ("\n" if failed_txt else ""))
    allt = [l for t in times for l in t.splitlines() if l.strip()]
    open(f"{out}/times.tsv", "w").write("\n".join(allt) + ("\n" if allt else ""))
    slow = sorted(((float(l.split("\t")[1]), l.split("\t")[0]) for l in allt if l.count("\t") >= 2), reverse=True)[:10]
    extra = ["", "| test job | tests | s | result |", "|---|---|---|---|"] + [f"| {k} | {t} | {s} | {r} |" for k, t, s, r in rows]
    if slow:
        extra += ["", "| slowest tests | s |", "|---|---|"] + [f"| {name} | {sec:.0f} |" for sec, name in slow]
    green = not failed and all(r == "green" for *_, r in rows)
    finish(out, "green" if green else "red", "test" if green else stage, plan.get("regex", ""), plan.get("build_s", 0),
           max([s.get("test_s", 0) for _, s in shards.values()] or [0]), elapsed, plan.get("base", ""), " ".join(failed),
           plan.get("tier", "ready"), ran, n, extra)
    return 0


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        os.environ["GITHUB_SHA"] = "a" * 40
        pl, o = f"{d}/plan", f"{d}/out"
        os.makedirs(pl)
        json.dump({"n": 2, "regex": "x", "build_s": 5, "base": "b" * 40, "tier": "ready", "started": time.time() - 90}, open(f"{pl}/plan.json", "w"))
        for k, res, failed in ((0, "green", []), (1, "red", ["t9"])):
            os.makedirs(f"{d}/s{k}")
            json.dump({"shard": k, "result": res, "failed": failed, "ran": 3, "tests": 3, "test_s": 10 + k}, open(f"{d}/s{k}/shard.json", "w"))
            open(f"{d}/s{k}/times.tsv", "w").write(f"t{k}\t{20 + k}.5\tpass\n")
        open(f"{d}/s1/output.txt", "w").write("boom\n")
        merge(o, pl, [f"{d}/s0", f"{d}/s1"])
        r = json.load(open(f"{o}/result.json"))
        assert r["result"] == "red" and r["failed"] == ["t9"] and r["tests_run"] == 6 and r["test_s"] == 11 and r["shards"] == 2, r
        assert "== shard 1" in open(f"{o}/output.txt").read() and "t1" in open(f"{o}/summary.md").read()
        json.dump({"shard": 1, "result": "green", "failed": [], "ran": 3, "tests": 3, "test_s": 9}, open(f"{d}/s1/shard.json", "w"))
        merge(o, pl, [f"{d}/s0", f"{d}/s1"])
        r = json.load(open(f"{o}/result.json"))
        assert r["result"] == "green" and r["total_s"] >= 90, r
        os.makedirs(f"{d}/iw")
        json.dump({"result": "red"}, open(f"{d}/iw/iwyu.json", "w"))
        open(f"{d}/iw/output.txt", "w").write("missing <cstring>\n")
        json.dump({"n": 2, "iwyu": True, "regex": "x", "started": time.time()}, open(f"{pl}/plan.json", "w"))
        merge(o, pl, [f"{d}/s0", f"{d}/s1", f"{d}/iw"])
        r = json.load(open(f"{o}/result.json"))
        assert r["result"] == "red" and r["stage"] == "iwyu" and r["failed"] == ["iwyu_lite"], r
        shutil.rmtree(o)
        merge(o, pl, [f"{d}/s0", f"{d}/s1"])  # the iwyu job left nothing: no verdict
        assert not os.path.exists(f"{o}/result.json")
        json.dump({"n": 2, "regex": "x", "started": time.time()}, open(f"{pl}/plan.json", "w"))
        merge(o, pl, [f"{d}/s0"])  # a test job left nothing: no verdict
        assert not os.path.exists(f"{o}/result.json")
        json.dump({"early": True, "started": time.time()}, open(f"{pl}/plan.json", "w"))
        finish(pl, "red", "build", "x", 5, 0, 5, "b" * 40, "", "ready")
        merge(o, pl, [])
        assert json.load(open(f"{o}/result.json"))["stage"] == "build"
    print("verdict.py self-test ok")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if argv[:1] == ["finish"] and len(argv) >= 11:
        finish(*argv[1:])
        return 0
    if argv[:1] == ["merge"] and len(argv) >= 3:
        return merge(argv[1], argv[2], argv[3:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
