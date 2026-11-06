#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one Packages run a release may promote for a commit (docs/releasing.md, "Build once, promote").

  staged_run.py --sha SHA [--repo R] [--max-days N] [--actors a,b] [--wait]

Prints the run id and exits 0 when a run of .github/workflows/packages.yml in this repository
  - was triggered by a push to the branch `staging` (scripts/merge-queue.sh staging), for exactly SHA: never a
    pull_request, never a workflow_dispatch, never a fork,
  - was pushed and (re)run by a maintainer (actor and triggering_actor in --actors),
  - finished with success, at most --max-days ago (default 7; artifacts are kept 14),
  - still has every package artifact and the recorded checksums (staged-sums), none expired.
Exits 3 when no Packages run was pushed to staging for SHA at all (release.yml then builds from the tag: the
--rebuild path), 1 when there are such runs but none may be promoted (the reasons on stderr). --wait waits for a run
in progress. scripts/release.sh (steps ci and tag) and release.yml (job locate) both use it.
"""
import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

WORKFLOW = ".github/workflows/packages.yml"
ARTIFACTS = {
    "linux-x86_64", "linux-arm64", "flatpak-x86_64", "flatpak-aarch64", "vfx-vfx2026-x86_64", "vfx-vfx2026-arm64",
    "vfx-vfx2027-x86_64", "vfx-vfx2027-arm64", "macos", "windows", "staged-sums",
}


def ts(s):
    return datetime.fromisoformat(s.replace("Z", "+00:00"))


def candidates(runs, sha, repo):
    """Runs pushed to staging for exactly sha, from this repository's own packages.yml."""
    return [r for r in runs if r.get("path") == WORKFLOW and r.get("head_sha") == sha and r.get("head_branch") == "staging"
            and r.get("event") == "push" and (r.get("head_repository") or {}).get("full_name") == repo
            and (r.get("repository") or {}).get("full_name") == repo]


def choose(runs, artifacts, sha, repo, actors, now, max_days):
    """(run id, 0) | (None, 3, why) | (None, 1, why) | (None, 2, why: in progress). artifacts: {run id: [artifact]}."""
    cs = candidates(runs, sha, repo)
    if not cs:
        return None, 3, f"no Packages run pushed to staging for {sha[:12]}"
    why = []
    for r in sorted(cs, key=lambda r: r["created_at"], reverse=True):
        rid = r["id"]
        if r.get("status") != "completed":
            why.append(f"run {rid} is {r.get('status')}")
            continue
        if r.get("conclusion") != "success":
            why.append(f"run {rid} {r.get('conclusion')}")
            continue
        who = {(r.get("actor") or {}).get("login"), (r.get("triggering_actor") or {}).get("login")}
        if not who <= actors:
            why.append(f"run {rid} was pushed or rerun by {', '.join(sorted(map(str, who - actors)))}, not a maintainer")
            continue
        age = now - ts(r["created_at"])
        if age > timedelta(days=max_days):
            why.append(f"run {rid} is {age.days} days old (at most {max_days})")
            continue
        arts = artifacts.get(rid, [])
        have = {a["name"] for a in arts if not a.get("expired")}
        missing = ARTIFACTS - have
        if missing:
            why.append(f"run {rid} lacks artifacts {', '.join(sorted(missing))} (expired or never built)")
            continue
        return rid, 0, ""
    pending = any(r.get("status") != "completed" for r in cs)
    return None, 2 if pending else 1, "; ".join(why)


def gh(path):
    out = subprocess.run(["gh", "api", path], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def live(sha, repo):
    runs = gh(f"repos/{repo}/actions/workflows/packages.yml/runs?head_sha={sha}&branch=staging&event=push&per_page=20")["workflow_runs"]
    arts = {r["id"]: gh(f"repos/{repo}/actions/runs/{r['id']}/artifacts?per_page=100")["artifacts"]
            for r in candidates(runs, sha, repo) if r.get("conclusion") == "success"}
    return runs, arts


def self_test():
    sha, repo, now = "a" * 40, "o/r", ts("2026-11-05T12:00:00Z")
    me = {"login": "islandmed"}

    def run(**k):
        r = {"id": 1, "path": WORKFLOW, "head_sha": sha, "head_branch": "staging", "event": "push", "status": "completed",
             "conclusion": "success", "head_repository": {"full_name": repo}, "repository": {"full_name": repo},
             "actor": me, "triggering_actor": me, "created_at": "2026-11-04T12:00:00Z"}
        r.update(k)
        return r
    full = {1: [{"name": n} for n in ARTIFACTS]}
    cases = {
        "good": ([run()], full, 0),
        "none": ([], {}, 3),
        "dispatch": ([run(event="workflow_dispatch")], full, 3),
        "pull request": ([run(event="pull_request")], full, 3),
        "fork": ([run(head_repository={"full_name": "evil/r"})], full, 3),
        "other branch": ([run(head_branch="main")], full, 3),
        "other workflow": ([run(path=".github/workflows/ci.yml")], full, 3),
        "other commit": ([run(head_sha="b" * 40)], full, 3),
        "failed": ([run(conclusion="failure")], full, 1),
        "in progress": ([run(status="in_progress", conclusion=None)], full, 2),
        "stranger": ([run(actor={"login": "mallory"})], full, 1),
        "stranger rerun": ([run(triggering_actor={"login": "mallory"})], full, 1),
        "old": ([run(created_at="2026-10-20T12:00:00Z")], full, 1),
        "expired": ([run()], {1: [{"name": n, "expired": n == "macos"} for n in ARTIFACTS]}, 1),
        "no sums": ([run()], {1: [{"name": n} for n in ARTIFACTS - {"staged-sums"}]}, 1),
        "newer failed, older ok": ([run(id=2, conclusion="failure", created_at="2026-11-05T11:00:00Z"), run()], full, 0),
    }
    bad = []
    for name, (runs, arts, want) in cases.items():
        got = choose(runs, arts, sha, repo, {"islandmed"}, now, 7)
        if got[1] != want:
            bad.append(f"{name}: want {want}, got {got}")
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
    ap.add_argument("--wait", action="store_true", help="wait (up to 2 hours) for a staging run in progress")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.sha or len(a.sha) != 40:
        ap.error("--sha takes a full 40-hex commit SHA")
    actors = set(a.actors.split(","))
    end = time.time() + 7200
    while True:
        runs, arts = live(a.sha, a.repo)
        rid, code, why = choose(runs, arts, a.sha, a.repo, actors, datetime.now(timezone.utc), a.max_days)
        if code == 2 and a.wait and time.time() < end:
            print(f"   waiting for the staging Packages run ({why}) ...", file=sys.stderr)
            time.sleep(60)
            continue
        break
    if code == 0:
        print(rid)
        return 0
    print(why, file=sys.stderr)
    return 1 if code == 2 else code


if __name__ == "__main__":
    sys.exit(main())
