#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one way every tier runs ctest (docs/testing-flow.md): ctest plus a test cache, a flake rerun and a time budget.

  tools/test/run.py --build build [--cache DIR] [--budget 20] [--flaky-log tests/flaky.md] [-- ctest args...]
  tools/test/run.py --self-test

* Nightly tests (label `nightly`) never run here unless the ctest args select them (-L nightly): T3 runs them.
* Test cache (--cache, or $SF_TEST_CACHE): every test gets a fingerprint, the hash of everything it can read: its
  program (the built file), every file or folder its command names, the app's assets and test data for app tests,
  the test's ENVIRONMENT and the SF_/SCULPTFORGE_/SDL_/GL driver variables of this run. Tests that read the whole tree
  (marks, the guide, release notes...) hash every tracked file. A test that passed with the same fingerprint on this
  machine is skipped and reported as cached; a pass on the first try is recorded. Never on CI (nothing persists).
* Flakes: failed tests run once more (ctest --rerun-failed). A pass on the rerun is a flake: logged (`flaky: name`
  lines, appended to --flaky-log), not cached, and does not fail the run. A test labelled `flaky` that fails twice is
  reported but never fails the run either (it never blocks a release; fix it or delete it).
* Budget: a non-nightly test that took longer than --budget seconds (default $SF_TEST_BUDGET or 20; 0 = off) fails
  the run, with the list. Make it wait on events instead of fixed frames, split it, or label it nightly.
Exit code: 0 green, 1 a real failure or a test over budget, 2 ctest itself could not run.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

ENV_PREFIXES = ("SF_", "SCULPTFORGE_", "SDL_", "GALLIUM_", "LIBGL_", "MESA_", "__GLX_")
ENV_IGNORE = {"SDL_APP_ID"}
# Tests that read files their command doesn't name: hashed as globs (repo-relative). "*" = every tracked file.
READS = {
    "marks": ["*"], "marks_self_test": ["tools/check_marks.py"],
    "guide_site": ["*"], "guide_menu_paths": ["*"], "ui_guide_shortcuts": ["docs/guide"],
    "release_notes": ["tools/release"], "release_notes_fragments": ["changes", "tools/release"],
    "release_script_beta": ["*"], "upstream_watch": ["tools/upstream-watch"],
    "brush_gallery_index": ["tools/brush-gallery"], "insider_drops": ["tools/drops"],
    "blender_exchange": ["addons/blender", "tests/blender"], "blender_addon_package": ["addons/blender"],
    "maya_shelf_script": ["integrations/maya", "tests/maya"], "link_worker": ["services/link-worker"],
    "release_smoke": ["tests/release_smoke.py", "tests/ui/release_smoke.sfui"],
}
APP_READS = ["assets", "tests/ui/data", "docs/guide"]  # the app loads these at run time (docs/guide: Help)
UNIT_READS = ["tests/data"]


MEMO = {"file": {}, "path": {}, "tree": {}}


def forget():
    for m in MEMO.values():
        m.clear()


def sha_file(path):
    memo = MEMO["file"]
    try:
        st = os.stat(path)
    except OSError:
        return "missing"
    key = (path, st.st_mtime_ns, st.st_size)
    if key not in memo:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        memo[key] = h.hexdigest()
    return memo[key]


def sha_path(path):
    memo = MEMO["path"]
    if path in memo:
        return memo[path]
    if os.path.isdir(path):
        h = hashlib.sha256()
        for d, dirs, files in sorted(os.walk(path)):
            dirs[:] = sorted(x for x in dirs if x not in (".git", "build", "__pycache__", "node_modules"))
            for f in sorted(files):
                p = os.path.join(d, f)
                h.update(f"{os.path.relpath(p, path)}\0{sha_file(p)}\n".encode())
        memo[path] = h.hexdigest()
    else:
        memo[path] = sha_file(path)
    return memo[path]


def tree_hash(root):
    memo = MEMO["tree"]
    """Every tracked and new file of the work tree (what a whole-tree test can read)."""
    if root not in memo:
        out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=root, capture_output=True)
        h = hashlib.sha256()
        for f in sorted(x for x in out.stdout.decode(errors="replace").split("\0") if x):
            h.update(f"{f}\0{sha_file(os.path.join(root, f))}\n".encode())
        memo[root] = h.hexdigest() if out.returncode == 0 else f"no-git-{time.time()}"  # no git: never cached
    return memo[root]


def load_tests(build, ctest_args):
    out = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1", *ctest_args], capture_output=True, text=True)
    if out.returncode != 0:
        return None
    tests = []
    for t in json.loads(out.stdout).get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        tests.append({"name": t["name"], "command": t.get("command", []), "labels": props.get("LABELS", []),
                      "env": props.get("ENVIRONMENT", []), "disabled": bool(props.get("DISABLED")),
                      "cwd": props.get("WORKING_DIRECTORY", build)})
    return tests


def fingerprint(t, root, build, run_env):
    h = hashlib.sha256()
    h.update(json.dumps([t["name"], t["command"], sorted(t["env"]), sorted(run_env.items())]).encode())
    is_app = False
    for arg in t["command"]:
        for a in re.split(r"[=;]", arg):
            if a and (os.path.isabs(a) or "/" in a):
                p = os.path.normpath(a if os.path.isabs(a) else os.path.join(t["cwd"], a))
                if os.path.exists(p):
                    h.update(f"{p}\0{sha_path(p)}\n".encode())
                    is_app |= os.path.basename(p) in ("sculptforge", "sculptforge.exe")
                    # a script's sibling modules
                    if p.endswith(".py") and not os.path.relpath(p, root).startswith(".."):
                        h.update(sha_path(os.path.dirname(p)).encode())
    exe = shutil.which(t["command"][0]) if t["command"] else None
    if exe:
        h.update(sha_file(exe).encode())
    reads = READS.get(t["name"], [])
    if is_app:
        reads = reads + APP_READS
    if t["name"].startswith("sculptforge_tests"):
        reads = reads + UNIT_READS
    if not exe or t["command"][0].endswith(("python", "python3", "python3.exe", "python.exe", "bash", "node", "cmake")) \
            and t["name"] not in READS:
        reads = ["*"]  # a script test nobody listed: it may read anything
    for r in reads:
        h.update((tree_hash(root) if r == "*" else sha_path(os.path.join(root, r))).encode())
    return h.hexdigest()


def parallel_jobs(args):
    """N of ctest's -jN / -j N / --parallel N (1 when absent)."""
    for i, x in enumerate(args):
        m = re.fullmatch(r"-j(\d+)", x)
        if m:
            return int(m.group(1))
        if x in ("-j", "--parallel") and i + 1 < len(args) and args[i + 1].isdigit():
            return int(args[i + 1])
    return 1


def run_env():
    return {k: v for k, v in os.environ.items() if k.startswith(ENV_PREFIXES) and k not in ENV_IGNORE}


def ctest(build, args, junit):
    cmd = ["ctest", "--test-dir", build, "--output-on-failure", "--output-junit", junit,
           "--test-output-size-passed", "1048576", *args]
    print("+ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd).returncode


INNER = re.compile(r"^\[ OK \] (\S+) \((\d+) ms\)$", re.M)


def inner_times(junit):
    """The single TEST cases inside the sculptforge_tests shards: {case: seconds} (their "[ OK ] name (N ms)" lines)."""
    try:
        root = ET.parse(junit).getroot()
    except (OSError, ET.ParseError):
        return {}
    out = {}
    for tc in root.iter("testcase"):
        if (tc.get("name") or "").startswith("sculptforge_tests"):
            for so in tc.iter("system-out"):
                for name, ms in INNER.findall(so.text or ""):
                    out[name] = int(ms) / 1000
    return out


def results(junit):
    try:
        root = ET.parse(junit).getroot()
    except (OSError, ET.ParseError):
        return {}
    out = {}
    for tc in root.iter("testcase"):
        failed = tc.find("failure") is not None or tc.find("error") is not None or tc.get("status") == "fail"
        skipped = tc.find("skipped") is not None or tc.get("status") in ("notrun", "disabled")
        out[tc.get("name")] = ("fail" if failed else "skip" if skipped else "pass", float(tc.get("time") or 0))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--cache", default=os.environ.get("SF_TEST_CACHE", ""))
    ap.add_argument("--budget", type=float, default=float(os.environ.get("SF_TEST_BUDGET", "20")))
    ap.add_argument("--flaky-log", default="")
    ap.add_argument("--times", default="", help="write name<TAB>seconds<TAB>result for every test run here")
    ap.add_argument("--junit", default="", help="also keep the first run's JUnit file here (CI: build/ctest.xml)")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("ctest_args", nargs=argparse.REMAINDER)
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    args = [x for x in a.ctest_args if x != "--"]
    build = os.path.abspath(a.build)
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True).stdout.strip() or os.getcwd()
    if os.environ.get("CI"):
        a.cache = ""
    selects_nightly = any(x in ("-L", "--label-regex") and i + 1 < len(args) and "nightly" in args[i + 1]
                          for i, x in enumerate(args))
    if not selects_nightly:  # one -LE (ctest keeps only the last): fold nightly into the caller's
        le = [i for i, x in enumerate(args) if x in ("-LE", "--label-exclude") and i + 1 < len(args)]
        if le:
            args[le[-1] + 1] = f"({args[le[-1] + 1]})|nightly"
        else:
            args += ["-LE", "nightly"]
    # A parallel run loads the machine like CI: timing checks (timeLimit, tests/TestHarness.h) get CI's slack.
    if parallel_jobs(args) > 1:
        os.environ.setdefault("SF_TIME_LIMIT_SCALE", "4")
    tests = load_tests(build, args)
    if tests is None:
        print(f"run.py: ctest can't list the tests in {build} (configured?)")
        return 2
    labels = {t["name"]: t["labels"] for t in tests}
    t0 = time.time()

    # Cache: skip tests whose fingerprint passed before.
    cached, prints = [], {}
    if a.cache:
        os.makedirs(a.cache, exist_ok=True)
        env = run_env()
        for t in tests:
            if t["disabled"]:
                continue
            fp = prints[t["name"]] = fingerprint(t, root, build, env)
            if os.path.exists(os.path.join(a.cache, fp)):
                cached.append(t["name"])
        print(f"test cache: {len(cached)} of {len(prints)} tests passed before with these exact inputs: skipped "
              f"({time.time() - t0:.1f} s to fingerprint)")
    todo = [t["name"] for t in tests if not t["disabled"] and t["name"] not in cached]
    res, inner = {}, {}
    rc = 0
    with tempfile.TemporaryDirectory() as tmp:
        if todo:
            run_args = list(args)
            if cached:  # only the rest: their names (a subset of what the args selected, so -R gives way)
                run_args = [x for i, x in enumerate(run_args) if x not in ("-R", "--tests-regex")
                            and (i == 0 or run_args[i - 1] not in ("-R", "--tests-regex"))]
                run_args += ["-R", "^(" + "|".join(re.escape(n) for n in todo) + ")$"]
            rc = ctest(build, run_args, os.path.join(tmp, "a.xml"))
            res = results(os.path.join(tmp, "a.xml"))
            inner = inner_times(os.path.join(tmp, "a.xml"))
            if a.junit and os.path.exists(os.path.join(tmp, "a.xml")):
                shutil.copy(os.path.join(tmp, "a.xml"), a.junit)
            if not res and rc != 0:
                print("run.py: ctest failed before running tests")
                return 2
            failed = [n for n, (s, _) in res.items() if s == "fail"]
            flaky = []
            if failed:
                print(f"\nrerunning {len(failed)} failed test(s) once: {' '.join(failed)}", flush=True)
                ctest(build, ["--rerun-failed", *[x for x in args if x.startswith("-j")]], os.path.join(tmp, "b.xml"))
                again = results(os.path.join(tmp, "b.xml"))
                for n in failed:
                    if again.get(n, ("fail",))[0] == "pass":
                        flaky.append(n)
                        res[n] = ("flaky", res[n][1])
            for n in flaky:
                print(f"flaky: {n} (failed, then passed on the rerun)")
            if flaky and a.flaky_log:
                head = "" if os.path.exists(a.flaky_log) else FLAKY_HEADER
                sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
                with open(a.flaky_log, "a", encoding="utf-8") as f:
                    f.write(head + "".join(f"| {time.strftime('%Y-%m-%d')} | {n} | {os.uname().nodename} | {sha} |\n" for n in flaky))
    # Record first-try passes in the cache.
    if a.cache:
        for n, (s, _) in res.items():
            if s == "pass" and n in prints:
                open(os.path.join(a.cache, prints[n]), "w").write(f"{n} {time.strftime('%Y-%m-%dT%H:%M:%S')}\n")
    real = [n for n, (s, _) in res.items() if s == "fail" and "flaky" not in labels.get(n, [])]
    tolerated = [n for n, (s, _) in res.items() if s == "fail" and "flaky" in labels.get(n, [])]
    # The budget is per test: a ctest entry, or one TEST case inside a sculptforge_tests shard (a shard is many tests).
    slow = sorted(((d, n) for n, (s, d) in res.items() if a.budget > 0 and d > a.budget and s != "skip"
                   and "nightly" not in labels.get(n, []) and not n.startswith("sculptforge_tests")), reverse=True)
    slow += sorted(((d, f"sculptforge_tests: {n}") for n, d in inner.items() if a.budget > 0 and d > a.budget
                    and not n.startswith("nightly_")), reverse=True)
    if a.times:
        with open(a.times, "w") as f:
            f.write("".join(f"{n}\t{d:.2f}\t{s}\n" for n, (s, d) in sorted(res.items())))
            f.write("".join(f"{n}\t0\tcached\n" for n in cached))
            f.write("".join(f"case:{n}\t{d:.2f}\tpass\n" for n, d in sorted(inner.items())))
    print(f"\n== {len(res)} run, {len(cached)} cached, {len(real)} failed, {sum(s == 'flaky' for s, _ in res.values())} flaky, "
          f"{len(slow)} over the {a.budget:g} s budget; {time.time() - t0:.0f} s")
    for n in tolerated:
        print(f"failed but labelled flaky (does not block): {n}")
    for d, n in slow:
        print(f"over budget: {n} {d:.1f} s (wait on events, split it, or label it nightly)")
    for n in real:
        print(f"FAILED: {n}")
    return 1 if real or slow else 0


FLAKY_HEADER = """# Flaky tests

Written by tools/test/run.py (docs/testing-flow.md): a test that failed, then passed on its one automatic rerun.
A test here twice gets the `flaky` label in CMakeLists.txt (it then never blocks a release) and an owner who fixes or
deletes it; remove its lines once fixed.

| date | test | machine | commit |
|---|---|---|---|
"""


def self_test():
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "src")
        os.makedirs(os.path.join(src, "assets"))
        subprocess.run(["git", "init", "-q", src], check=True)
        open(os.path.join(src, "assets", "a.txt"), "w").write("1")
        script = os.path.join(src, "s.sfui")
        open(script, "w").write("x")
        app = os.path.join(d, "sculptforge")
        open(app, "w").write("bin")
        t = {"name": "ui_s", "command": [app, "--ui-test", script], "labels": ["ui"], "env": [], "cwd": d}
        f1 = fingerprint(t, src, d, {})
        assert f1 == fingerprint(t, src, d, {})
        open(script, "w").write("y")
        forget()
        f2 = fingerprint(t, src, d, {})
        assert f2 != f1, "the script is an input"
        open(os.path.join(src, "assets", "a.txt"), "w").write("2")
        forget()
        f3 = fingerprint(t, src, d, {})
        assert f3 != f2, "the app's assets are inputs"
        assert fingerprint(t, src, d, {"SF_X": "1"}) != f3, "the environment is an input"
        py = {"name": "unknown_script", "command": [sys.executable, os.path.join(src, "t.py")], "labels": [], "env": [], "cwd": d}
        open(os.path.join(src, "t.py"), "w").write("")
        g1 = fingerprint(py, src, d, {})
        open(os.path.join(src, "other.md"), "w").write("new")  # an unlisted script test may read any file
        forget()
        assert fingerprint(py, src, d, {}) != g1, "unlisted script tests hash the whole tree"
        junit = os.path.join(d, "j.xml")
        open(junit, "w").write('<testsuite><testcase name="a" time="1.5" status="run"/>'
                               '<testcase name="b" time="2" status="fail"><failure message="x"/></testcase></testsuite>')
        assert results(junit) == {"a": ("pass", 1.5), "b": ("fail", 2.0)}
        open(junit, "w").write('<testsuite><testcase name="sculptforge_tests_0of2" time="9" status="run">'
                               '<system-out>[ RUN] x\n[ OK ] mesh_io (1200 ms)\n[FAIL] y (3 ms)\n</system-out></testcase></testsuite>')
        assert inner_times(junit) == {"mesh_io": 1.2}
    assert parallel_jobs(["-j10"]) == 10 and parallel_jobs(["-C", "Release", "-j", "4"]) == 4 and parallel_jobs([]) == 1
    print("run.py self-test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
