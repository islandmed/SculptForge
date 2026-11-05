#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The members' publishing path stays locked down (docs/releasing.md, "Secrets"). scripts/release.sh check and ctest
(release_workflows) run this; it fails when

  - release.yml has a `uses:` that isn't a local workflow or pinned to a full 40-hex commit SHA,
  - release.yml has no job in `environment: member-release` holding INSIDERS_REPO_TOKEN, or that job can write
    (contents: write) or isn't limited to betas,
  - any workflow's job reads INSIDERS_REPO_TOKEN outside a GitHub Environment (environment secrets are invisible
    there anyway, so such a job is a sign someone put the token back at repository level),
  - any workflow uses pull_request_target, or release.yml turns on shell tracing (set -x) that would log values.

Plain text parsing (no PyYAML): workflows here keep jobs at two spaces and their keys at four.
"""
import re
import sys
from pathlib import Path

TOKEN = "secrets.INSIDERS_REPO_TOKEN"
MEMBER_ENV = "member-release"
PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}(\s+#\s*\S.*)?$")


def jobs(text):
    """{job id: its lines} for the top-level `jobs:` block."""
    out, cur, inside = {}, None, False
    for line in text.splitlines():
        if re.match(r"^jobs:\s*$", line):
            inside = True
            continue
        if inside and re.match(r"^\S", line):
            inside = False
        if not inside:
            continue
        m = re.match(r"^  ([\w-]+):\s*$", line)
        if m:
            cur = m.group(1)
            out[cur] = []
        elif cur:
            out[cur].append(line)
    return out


def environment(lines):
    for line in lines:
        m = re.match(r"^    environment:\s*(\S.*?)\s*$", line)
        if m:
            return m.group(1).strip("'\"")
    return None


def check(files):
    """files: {name: text}. Returns the list of problems."""
    errs = []
    for name, text in files.items():
        if re.search(r"^\s*pull_request_target\s*:", text, re.M) or re.search(r"\bpull_request_target\b", text.split("jobs:")[0]):
            errs.append(f"{name}: pull_request_target (runs outside code with this repository's secrets)")
        for job, lines in jobs(text).items():
            if any(TOKEN in l for l in lines) and not environment(lines):
                errs.append(f"{name}: job {job} reads INSIDERS_REPO_TOKEN outside a GitHub Environment")
    rel = files.get("release.yml")
    if rel is None:
        return errs + ["release.yml: missing"]
    for n, line in enumerate(rel.splitlines(), 1):
        m = re.match(r"^\s*(?:-\s+)?uses:\s*(\S.*?)\s*$", line)
        if m and not m.group(1).startswith("./") and not PINNED.match(m.group(1)):
            errs.append(f"release.yml:{n}: `uses: {m.group(1)}` is not pinned to a full commit SHA (pin it, comment the version)")
        if re.search(r"(^|[\s;])set\s+-[a-wyz]*x", line):
            errs.append(f"release.yml:{n}: shell tracing (set -x) would log secret-derived values")
    holders = {j: l for j, l in jobs(rel).items() if any(TOKEN in x for x in l)}
    if not holders:
        errs.append("release.yml: no job publishes to the insiders repository with INSIDERS_REPO_TOKEN")
    for job, lines in holders.items():
        body = "\n".join(lines)
        if environment(lines) != MEMBER_ENV:
            errs.append(f"release.yml: job {job} holds INSIDERS_REPO_TOKEN but isn't in `environment: {MEMBER_ENV}`")
        if re.search(r"contents:\s*write", body):
            errs.append(f"release.yml: job {job} holds INSIDERS_REPO_TOKEN and a GitHub token that can write")
        if "contains(needs.locate.outputs.tag, '-')" not in body:
            errs.append(f"release.yml: job {job} isn't limited to betas (if: ... contains(needs.locate.outputs.tag, '-'))")
        if sum(TOKEN in x for x in lines) != 1:
            errs.append(f"release.yml: job {job} should pass INSIDERS_REPO_TOKEN to exactly one step")
    return errs


GOOD = """on:
  push:
    tags: ['v*']
permissions:
  contents: read
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
      - uses: ./.github/workflows/packages.yml
  publish-members:
    if: ${{ contains(needs.locate.outputs.tag, '-') }}
    environment: member-release
    permissions:
      contents: read
    steps:
      - name: Publish
        env:
          GH_TOKEN: ${{ secrets.INSIDERS_REPO_TOKEN }}
        run: gh release create
"""


def self_test():
    cases = {
        "good": ({"release.yml": GOOD}, 0),
        "unpinned": ({"release.yml": GOOD.replace("checkout@3d3c42e5aac5ba805825da76410c181273ba90b1", "checkout@v7")}, 1),
        "short sha": ({"release.yml": GOOD.replace("3d3c42e5aac5ba805825da76410c181273ba90b1", "3d3c42e")}, 1),
        "no environment": ({"release.yml": GOOD.replace("    environment: member-release\n", "")}, 1),
        "other environment": ({"release.yml": GOOD.replace("member-release", "production")}, 1),
        "write token": ({"release.yml": GOOD.replace("      contents: read\n    steps", "      contents: write\n    steps")}, 1),
        "not beta-only": ({"release.yml": GOOD.replace("contains(needs.locate.outputs.tag, '-')", "true")}, 1),
        "set -x": ({"release.yml": GOOD.replace("run: gh release create", "run: set -ex; gh release create")}, 1),
        "no publisher": ({"release.yml": GOOD.replace(TOKEN, "github.token")}, 1),
        "prt": ({"release.yml": GOOD, "x.yml": "on:\n  pull_request_target:\njobs:\n  a:\n    steps: []\n"}, 1),
        "repo-level reader": ({"release.yml": GOOD, "x.yml": f"jobs:\n  a:\n    env:\n      T: ${{{{ {TOKEN} }}}}\n"}, 1),
        "env reader ok": ({"release.yml": GOOD, "x.yml": f"jobs:\n  a:\n    environment: other-env\n    env:\n      T: ${{{{ {TOKEN} }}}}\n"}, 0),
    }
    bad = [f"{k}: {check(f)}" for k, (f, n) in cases.items() if bool(check(f)) != bool(n)]
    for b in bad:
        print("self-test FAIL", b)
    print("self-test", "failed" if bad else "ok")
    return 1 if bad else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    root = Path(__file__).resolve().parents[2] / ".github" / "workflows"
    files = {p.name: p.read_text(encoding="utf-8") for p in sorted(root.glob("*.yml"))}
    errs = check(files)
    for e in errs:
        print(e)
    if not errs:
        print(f"workflows ok: release.yml pinned, INSIDERS_REPO_TOKEN only in environments ({len(files)} workflows)")
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
