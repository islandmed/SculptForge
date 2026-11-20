#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one Packages run a release may promote for a commit (docs/releasing.md, "Build once, promote").

  staged_run.py --sha SHA [--repo R] [--max-days N] [--actors a,b] [--wait [--wait-minutes M]] [--before-run ID] [--full]

Prints the run id (with --full: "ID BUILT_SHA BRANCH") and exits 0 when a run of .github/workflows/packages.yml in this
repository
  - was triggered by a push to `main` or `staging` (scripts/merge-queue.sh staging), for exactly SHA or for a commit
    with the same build-input key (tools/release/build_key.py: every file that goes into the packages byte for byte
    the same, computed here from git, never taken from the run): never a pull_request, never a workflow_dispatch,
    never a fork,
  - was pushed and (re)run by a maintainer (actor and triggering_actor in --actors),
  - finished with success, at most --max-days ago (default 7; artifacts are kept 14). A run that finished failed or
    cancelled still counts when only its Python wheels jobs ("Python wheels ...", "Record wheel checksums", pypi) did
    not succeed and every other job did: release.yml publishes the app without the wheels (a hung wheels job
    cancelled 0.13.10's staging run, and every app package was built),
  - still has every package artifact and the recorded checksums (staged-sums), none expired: a run that reused
    another one (packages.yml job reuse) has none, so the run that built them is the one picked.
A run for exactly SHA wins over one for another commit with the same key, then the newest. Exits 3 when no Packages
run was pushed for SHA or its key at all (release.yml then builds from the tag: the --rebuild path), 1 when there are
such runs but none may be promoted (the reasons on stderr). --wait waits for a run in progress. --before-run ID looks
only at runs older than run ID (packages.yml job reuse: an older run never waits for a newer one, so two never wait for
each other). scripts/release.sh (steps ci and tag), release.yml (job locate) and packages.yml (job reuse) use it.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_key  # noqa: E402

WORKFLOW = ".github/workflows/packages.yml"
BRANCHES = {"main", "staging"}
ARTIFACTS = {
    "linux-x86_64", "linux-arm64", "flatpak-x86_64", "flatpak-aarch64", "vfx-vfx2026-x86_64", "vfx-vfx2026-arm64",
    "vfx-vfx2027-x86_64", "vfx-vfx2027-arm64", "macos", "windows", "staged-sums",
}


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def candidates(runs, repo, before=None):
    """This repository's own packages.yml runs on pushes to main or staging (older than run `before`)."""
    return [r for r in runs if r.get("path") == WORKFLOW and r.get("head_branch") in BRANCHES and r.get("event") == "push"
            and (r.get("head_repository") or {}).get("full_name") == repo and (r.get("repository") or {}).get("full_name") == repo
            and (before is None or r["id"] < before)]


def is_wheels_job(name):
    """The optional Python package jobs of packages.yml (and release.yml's pypi), also as "caller / job" in a reusable workflow."""
    return re.match(r"(python wheels\b|record wheel checksums$|pypi$)", (name or "").rsplit(" / ", 1)[-1].strip(), re.I) is not None


def only_wheels_failed(jobs):
    """None when every job that is not a wheels job succeeded (at least one such job), else the reason."""
    if not jobs:
        return "no jobs"
    bad = sorted(j.get("name") or "?" for j in jobs if not is_wheels_job(j.get("name")) and j.get("conclusion") != "success")
    if bad:
        return f"{', '.join(bad)} did not succeed"
    if all(is_wheels_job(j.get("name")) for j in jobs):
        return "no job but the wheels ran"
    return None


def problem(r, actors, now, max_days, jobs=None):
    """Why run r can't be promoted, judged from the run alone (jobs(run id) -> [job], asked only for a run that ended
    without success); None when it may (artifacts aside)."""
    rid = r["id"]
    if r.get("status") != "completed":
        return f"run {rid} is {r.get('status')}"
    if r.get("conclusion") != "success":
        if jobs is None or r.get("conclusion") not in ("failure", "cancelled", "timed_out"):
            return f"run {rid} {r.get('conclusion')}"
        why = only_wheels_failed(jobs(rid))
        if why:
            return f"run {rid} {r.get('conclusion')}: {why}"
    who = {(r.get("actor") or {}).get("login"), (r.get("triggering_actor") or {}).get("login")}
    if not who <= actors:
        return f"run {rid} was pushed or rerun by {', '.join(sorted(map(str, who - actors)))}, not a maintainer"
    age = now - ts(r["created_at"])
    if age > timedelta(days=max_days):
        return f"run {rid} is {age.days} days old (at most {max_days})"
    return None


def choose(runs, artifacts, sha, repo, actors, now, max_days, keys, before=None, jobs=None):
    """(run, 0, "") | (None, 3, why) | (None, 1, why) | (None, 2, why: in progress).
    artifacts(run id) -> [artifact]; jobs(run id) -> [job] (name, conclusion); keys(shas) -> {sha: build-input key or None (commit unknown)}."""
    cs = candidates(runs, repo, before)
    exact = [r for r in cs if r.get("head_sha") == sha]
    # Another commit's run counts only when it could be promoted (or is still running) and its key is SHA's.
    others = [r for r in cs if r.get("head_sha") != sha and (problem(r, actors, now, max_days, jobs) is None or r.get("status") != "completed")]
    if others:
        k = keys({sha} | {r["head_sha"] for r in others})
        want = k.get(sha)
        others = [r for r in others if want is not None and k.get(r["head_sha"]) == want]
    if not exact and not others:
        return None, 3, f"no Packages run pushed to main or staging for {sha[:12]} or its build inputs"
    why = []
    newest = lambda rs: sorted(rs, key=lambda r: r["created_at"], reverse=True)
    for r in newest(exact) + newest(others):
        rid = r["id"]
        p = problem(r, actors, now, max_days, jobs)
        if p:
            why.append(p)
            continue
        have = {a["name"] for a in artifacts(rid) if not a.get("expired")}
        missing = ARTIFACTS - have
        if missing:
            why.append(f"run {rid} lacks artifacts {', '.join(sorted(missing))} (expired, never built, or it reused another run)")
            continue
        return r, 0, ""
    pending = any(r.get("status") != "completed" for r in exact + others)
    return None, 2 if pending else 1, "; ".join(why)


def gh(path):
    out = subprocess.run(["gh", "api", path], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def git_keys(shas):
    """Build-input keys from this repository's git objects; fetches commits it lacks (trees only in a partial clone)."""
    git = lambda *a: subprocess.run(["git", *a], capture_output=True, text=True)
    shas = {s for s in shas if isinstance(s, str) and re.fullmatch(r"[0-9a-f]{40}", s)}
    missing = [s for s in shas if git("cat-file", "-e", f"{s}^{{commit}}").returncode != 0]
    if missing:
        cmd = ["fetch", "-q", "--no-tags"]
        if git("rev-parse", "--is-shallow-repository").stdout.strip() == "true":
            cmd.append("--depth=1")
        if git("config", "remote.origin.promisor").stdout.strip() == "true":
            cmd.append("--filter=blob:none")
        git(*cmd, "origin", *missing) # a commit it can't fetch gets no key: never matched
    rules = build_key.load_rules()
    return {s: build_key.key(s, rules) for s in shas}


def live(sha, repo):
    base = f"repos/{repo}/actions/workflows/packages.yml/runs?event=push&per_page="
    # (100 per branch: main's website pushes each add a run that reused another, and the builder must stay in view)
    runs = {r["id"]: r for q in (f"30&head_sha={sha}", "100&branch=main", "100&branch=staging")
            for r in gh(base + q)["workflow_runs"]}
    cache = {}

    def artifacts(rid):
        if rid not in cache:
            cache[rid] = gh(f"repos/{repo}/actions/runs/{rid}/artifacts?per_page=100")["artifacts"]
        return cache[rid]
    jcache = {}

    def jobs(rid):
        if rid not in jcache:
            jcache[rid] = gh(f"repos/{repo}/actions/runs/{rid}/jobs?per_page=100")["jobs"]
        return jcache[rid]
    return list(runs.values()), artifacts, jobs


def self_test():
    sha, other, repo, now = "a" * 40, "b" * 40, "o/r", ts("2026-11-05T12:00:00Z")
    me = {"login": "islandmed"}

    def run(**k):
        r = {"id": 1, "path": WORKFLOW, "head_sha": sha, "head_branch": "staging", "event": "push", "status": "completed",
             "conclusion": "success", "head_repository": {"full_name": repo}, "repository": {"full_name": repo},
             "actor": me, "triggering_actor": me, "created_at": "2026-11-04T12:00:00Z"}
        r.update(k)
        return r
    full = {n: [{"name": a} for a in ARTIFACTS] for n in (1, 2, 3)}
    def job(name, conclusion="success"):
        return {"name": name, "conclusion": conclusion}
    app = [job(n) for n in ("Reuse an earlier build of the same inputs", "Linux x86_64 (.deb, AppImage, .tar.gz)", "macOS (universal .dmg)",
                            "Windows x64 (installer, .zip)", "Record checksums")]
    wheels_ok = [job(f"Python wheels {n}") for n in ("Linux x86_64", "Windows x64")] + [job("Record wheel checksums")]
    wheels_cut = [job("Python wheels Linux x86_64"), job("Python wheels macOS universal2", "cancelled"), job("Record wheel checksums", "skipped")]
    same = {sha: "k1", other: "k1"}
    differ = {sha: "k1", other: "k2"}
    cases = { # name: (runs, artifacts, keys, want code, want run id)
        "good": ([run()], full, differ, 0, 1),
        "main push": ([run(head_branch="main")], full, differ, 0, 1),
        "none": ([], {}, differ, 3, None),
        "dispatch": ([run(event="workflow_dispatch")], full, differ, 3, None),
        "pull request": ([run(event="pull_request")], full, differ, 3, None),
        "fork": ([run(head_repository={"full_name": "evil/r"})], full, same, 3, None),
        "other branch": ([run(head_branch="feat/x")], full, differ, 3, None),
        "other workflow": ([run(path=".github/workflows/ci.yml")], full, differ, 3, None),
        "other commit, other key": ([run(head_sha=other)], full, differ, 3, None),
        "other commit, same key": ([run(head_sha=other)], full, same, 0, 1),
        "other commit, same key, main": ([run(head_sha=other, head_branch="main")], full, same, 0, 1),
        "other commit, unknown here": ([run(head_sha=other)], full, {sha: "k1", other: None}, 3, None),
        "this commit unknown here": ([run(head_sha=other)], full, {sha: None, other: None}, 3, None),
        "same key, fork": ([run(head_sha=other, head_repository={"full_name": "evil/r"})], full, same, 3, None),
        "same key, dispatch by another actor": ([run(head_sha=other, event="workflow_dispatch", actor={"login": "mallory"},
                                                     triggering_actor={"login": "mallory"})], full, same, 3, None),
        "same key, pushed by another actor": ([run(head_sha=other, actor={"login": "mallory"})], full, same, 3, None),
        "same key, rerun by another actor": ([run(head_sha=other, triggering_actor={"login": "mallory"})], full, same, 3, None),
        "same key, expired": ([run(head_sha=other)], {1: [{"name": n, "expired": n == "windows"} for n in ARTIFACTS]}, same, 1, None),
        "same key, old": ([run(head_sha=other, created_at="2026-10-20T12:00:00Z")], full, same, 3, None),
        "same key, failed": ([run(head_sha=other, conclusion="failure")], full, same, 3, None),
        "same key, in progress": ([run(head_sha=other, status="in_progress", conclusion=None)], full, same, 2, None),
        "failed": ([run(conclusion="failure")], full, differ, 1, None),
        "in progress": ([run(status="in_progress", conclusion=None)], full, differ, 2, None),
        "stranger": ([run(actor={"login": "mallory"})], full, differ, 1, None),
        "stranger rerun": ([run(triggering_actor={"login": "mallory"})], full, differ, 1, None),
        "old": ([run(created_at="2026-10-20T12:00:00Z")], full, differ, 1, None),
        "expired": ([run()], {1: [{"name": n, "expired": n == "macos"} for n in ARTIFACTS]}, differ, 1, None),
        "no sums": ([run()], {1: [{"name": n} for n in ARTIFACTS - {"staged-sums"}]}, differ, 1, None),
        "newer failed, older ok": ([run(id=2, conclusion="failure", created_at="2026-11-05T11:00:00Z"), run()], full, differ, 0, 1),
        "exact wins over a newer same-key run": ([run(id=2, head_sha=other, created_at="2026-11-05T11:00:00Z"), run()], full, same, 0, 1),
        "reuse run skipped for its builder": ([run(id=2, head_branch="main", created_at="2026-11-05T11:00:00Z"),
                                               run(head_sha=other)], {1: full[1], 2: []}, same, 0, 1),
        "the 0.11.14 case (main built the staged commit)": ([run(id=2, head_branch="main")], {2: full[2]}, differ, 0, 2),
    }
    # name: (runs, jobs of run 1, want code, want run id): the runs that ended without success (the 0.13.10 staging run)
    jcases = {
        "wheels cancelled": ([run(conclusion="cancelled")], app + wheels_cut, 0, 1),
        "wheels failed": ([run(conclusion="failure")], app + wheels_cut, 0, 1),
        "wheels timed out, main": ([run(conclusion="failure", head_branch="main")], app + wheels_cut, 0, 1),
        "macos app job failed": ([run(conclusion="failure")], app[:2] + [job("macOS (universal .dmg)", "failure")] + app[3:] + wheels_cut, 1, None),
        "macos app job cancelled": ([run(conclusion="cancelled")], app[:2] + [job("macOS (universal .dmg)", "cancelled")] + app[3:] + wheels_cut, 1, None),
        "app job skipped": ([run(conclusion="failure")], app[:2] + [job("macOS (universal .dmg)", "skipped")] + app[3:] + wheels_cut, 1, None),
        "app and wheels failed": ([run(conclusion="failure")], app + [job("Windows x64 (installer, .zip)", "failure")] + wheels_cut, 1, None),
        "only wheels ran": ([run(conclusion="cancelled")], wheels_cut, 1, None),
        "no jobs": ([run(conclusion="cancelled")], [], 1, None),
        "wheels still running": ([run(status="in_progress", conclusion=None)], app + wheels_cut, 2, None),
        "skipped run": ([run(conclusion="skipped")], app + wheels_cut, 1, None),
        "success needs no jobs": ([run()], None, 0, 1),
        "wheels cancelled, stranger": ([run(conclusion="cancelled", actor={"login": "mallory"})], app + wheels_cut, 1, None),
        "wheels cancelled, other commit, same key": ([run(head_sha=other, conclusion="cancelled")], app + wheels_cut, 0, 1),
        "wheels cancelled, other commit, other key": ([run(head_sha=other, conclusion="cancelled")], app + wheels_cut, 3, None),
        "wheels cancelled, no artifacts": ([run(conclusion="cancelled")], app + wheels_cut, 1, None),
    }
    bad = []
    for name, (runs, js, want, want_id) in jcases.items():
        arts = {} if "no artifacts" in name else full
        keys = differ if "other key" in name else same
        got = choose(runs, lambda rid: arts.get(rid, []), sha, repo, {"islandmed"}, now, 7, lambda s: {x: keys.get(x) for x in s},
                     jobs=None if js is None else lambda rid: js)
        if got[1] != want or (got[0] or {}).get("id") != want_id:
            bad.append(f"{name}: want {want} {want_id}, got {got[1]} {(got[0] or {}).get('id')} {got[2]}")
    # the same cut run without a jobs source (a caller that can't ask) is still refused
    got = choose([run(conclusion="cancelled")], lambda r: full[1], sha, repo, {"islandmed"}, now, 7, lambda s: {})
    if got[1] != 1:
        bad.append(f"no jobs source: want 1, got {got}")
    for name in ("Python wheels macOS universal2", "Build / Python wheels Linux x86_64", "Record wheel checksums", "pypi", "Publish to PyPI / pypi"):
        if not is_wheels_job(name):
            bad.append(f"is_wheels_job({name!r}) is false")
    for name in ("macOS (universal .dmg)", "Record checksums", "Windows x64 (installer, .zip)", "Wheels of the app", "", None):
        if is_wheels_job(name):
            bad.append(f"is_wheels_job({name!r}) is true")
    for name, (runs, arts, keys, want, want_id) in cases.items():
        got = choose(runs, lambda rid: arts.get(rid, []), sha, repo, {"islandmed"}, now, 7, lambda s: {x: keys.get(x) for x in s})
        if got[1] != want or (got[0] or {}).get("id") != want_id:
            bad.append(f"{name}: want {want} {want_id}, got {got[1]} {(got[0] or {}).get('id')} {got[2]}")
    # --before-run: an older run never waits for a newer one (packages.yml job reuse).
    got = choose([run(id=5, status="in_progress", conclusion=None)], lambda r: [], sha, repo, {"islandmed"}, now, 7, lambda s: {}, before=5)
    if got[1] != 3:
        bad.append(f"before-run: want 3, got {got}")
    for b in bad:
        print("self-test FAIL", b)
    print("self-test", "failed" if bad else "ok")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sha")
    ap.add_argument("--repo", default="islandmed/SculptForge")
    ap.add_argument("--max-days", type=int, default=7)
    ap.add_argument("--actors", default="islandmed")
    ap.add_argument("--wait", action="store_true", help="wait for a matching run in progress")
    ap.add_argument("--wait-minutes", type=int, default=120)
    ap.add_argument("--before-run", type=int, help="only runs older than this run id")
    ap.add_argument("--full", action="store_true", help='print "ID BUILT_SHA BRANCH"')
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.sha or len(a.sha) != 40:
        ap.error("--sha takes a full 40-hex commit SHA")
    actors = set(a.actors.split(","))
    end = time.time() + 60 * a.wait_minutes
    while True:
        runs, arts, jobs = live(a.sha, a.repo)
        r, code, why = choose(runs, arts, a.sha, a.repo, actors, datetime.now(timezone.utc), a.max_days, git_keys, a.before_run, jobs)
        if code == 2 and a.wait and time.time() < end:
            print(f"   waiting for the Packages run in progress ({why}) ...", file=sys.stderr)
            time.sleep(60)
            continue
        break
    if code == 0:
        if r["head_sha"] != a.sha:
            print(f"   run {r['id']} built {r['head_sha'][:12]} ({r['head_branch']}), whose build inputs are {a.sha[:12]}'s", file=sys.stderr)
        print(f"{r['id']} {r['head_sha']} {r['head_branch']}" if a.full else r["id"])
        return 0
    print(why, file=sys.stderr)
    return 1 if code == 2 else code


if __name__ == "__main__":
    sys.exit(main())
