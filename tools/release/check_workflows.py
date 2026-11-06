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
  - any workflow uses pull_request_target, or release.yml turns on shell tracing (set -x) that would log values,
  - build once, promote (docs/releasing.md): a job reads UPDATE_SIGNING_KEY outside the environments `release` and
    `member-release`; release.yml's publish isn't in `release`; locate doesn't pick the packages with
    tools/release/staged_run.py; a publisher doesn't need job verify or re-check the checksums
    (tools/release/verify_staged.py); packages.yml reads any secret, or a job of it uploads packages without a
    pinned actions/attest-build-provenance step.

Plain text parsing (no PyYAML): workflows here keep jobs at two spaces and their keys at four.
"""
import re
import sys
from pathlib import Path

TOKEN = "secrets.INSIDERS_REPO_TOKEN"
MEMBER_ENV = "member-release"
SIGNING = "secrets.UPDATE_SIGNING_KEY"
SIGNING_ENVS = {"release", MEMBER_ENV}
ATTEST = re.compile(r"uses:\s*actions/attest-build-provenance@[0-9a-f]{40}\b")
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
        m = re.match(r"^    environment:\s*([^#\s]+)", line)
        if m:
            return m.group(1).strip("'\"")
    return None


def paths(body, key):
    """The globs of `key:` (path / subject-path) in the job's upload-artifact / attest step: inline or a `|` block."""
    want = "upload-artifact" if key == "path" else "attest-build-provenance"
    out, lines = set(), "\n".join(s for s in re.split(r"\n(?=\s*- )", body) if want in s).splitlines()
    for i, line in enumerate(lines):
        m = re.match(rf"^(\s+){key}:\s*(.*?)\s*$", line)
        if not m:
            continue
        if m.group(2) and m.group(2) != "|":
            out.add(m.group(2))
            continue
        for nxt in lines[i + 1:]:
            if len(nxt) - len(nxt.lstrip()) <= len(m.group(1)) or not nxt.strip():
                break
            out.add(nxt.strip())
    return out


def check(files):
    """files: {name: text}. Returns the list of problems."""
    errs = []
    for name, text in files.items():
        if re.search(r"^\s*pull_request_target\s*:", text, re.M) or re.search(r"\bpull_request_target\b", text.split("jobs:")[0]):
            errs.append(f"{name}: pull_request_target (runs outside code with this repository's secrets)")
        for job, lines in jobs(text).items():
            if any(TOKEN in l for l in lines) and not environment(lines):
                errs.append(f"{name}: job {job} reads INSIDERS_REPO_TOKEN outside a GitHub Environment")
            if any(SIGNING in l for l in lines) and environment(lines) not in SIGNING_ENVS:
                errs.append(f"{name}: job {job} reads UPDATE_SIGNING_KEY outside the environments release / member-release")
    pk = files.get("packages.yml")
    if pk is not None:
        if "secrets." in pk:
            errs.append("packages.yml: reads a secret (staging builds must never see one; signing happens at promotion)")
        for job, lines in jobs(pk).items():
            body = "\n".join(lines)
            if job != "sums" and "upload-artifact" in body and not ATTEST.search(body):
                errs.append(f"packages.yml: job {job} uploads packages without a pinned actions/attest-build-provenance step")
            elif job != "sums" and "upload-artifact" in body and paths(body, "path") != paths(body, "subject-path"):
                errs.append(f"packages.yml: job {job} attests {sorted(paths(body, 'subject-path'))} but uploads {sorted(paths(body, 'path'))}"
                            " (verify refuses an unattested file)")
    rel = files.get("release.yml")
    if rel is None:
        return errs + ["release.yml: missing"]
    for n, line in enumerate(rel.splitlines(), 1):
        m = re.match(r"^\s*(?:-\s+)?uses:\s*(\S.*?)\s*$", line)
        if m and not m.group(1).startswith("./") and not PINNED.match(m.group(1)):
            errs.append(f"release.yml:{n}: `uses: {m.group(1)}` is not pinned to a full commit SHA (pin it, comment the version)")
        if re.search(r"(^|[\s;])set\s+-[a-wyz]*x", line):
            errs.append(f"release.yml:{n}: shell tracing (set -x) would log secret-derived values")
    rj = jobs(rel)
    if "staged_run.py" not in "\n".join(rj.get("locate", [])):
        errs.append("release.yml: job locate doesn't pick the packages with tools/release/staged_run.py")
    if "verify_staged.py" not in "\n".join(rj.get("verify", [])):
        errs.append("release.yml: no job verify running tools/release/verify_staged.py")
    if environment(rj.get("publish", [])) != "release":
        errs.append("release.yml: job publish isn't in `environment: release` (the only holder of UPDATE_SIGNING_KEY)")
    for job in ("publish", "publish-members"):
        body = "\n".join(rj.get(job, []))
        if not re.search(r"needs:.*\bverify\b", body) or "verify_staged.py" not in body:
            errs.append(f"release.yml: job {job} must need verify and re-check the checksums (verify_staged.py --sums-only)")
    holders = {j: l for j, l in rj.items() if any(TOKEN in x for x in l)}
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
  locate:
    steps:
      - run: python3 tools/release/staged_run.py --sha "$sha"
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
      - uses: ./.github/workflows/packages.yml
  verify:
    steps:
      - run: python3 tools/release/verify_staged.py dist sums
  publish:
    needs: [locate, build, verify, smoke]
    environment: release
    steps:
      - run: python3 tools/release/verify_staged.py dist sums --sums-only
      - env:
          UPDATE_SIGNING_KEY: ${{ secrets.UPDATE_SIGNING_KEY }}
        run: tools/release/sign_sums.sh dist
  publish-members:
    needs: [locate, build, verify, smoke]
    if: ${{ contains(needs.locate.outputs.tag, '-') }}
    environment: member-release
    permissions:
      contents: read
    steps:
      - run: python3 tools/release/verify_staged.py dist sums --sums-only
      - name: Publish
        env:
          GH_TOKEN: ${{ secrets.INSIDERS_REPO_TOKEN }}
        run: gh release create
"""
PACKAGES = """on:
  push:
    branches: [main, staging]
jobs:
  linux:
    permissions:
      id-token: write
    steps:
      - uses: actions/upload-artifact@v7
        with:
          path: |
            build/*.deb
            build/*.mcpb
      - uses: actions/attest-build-provenance@4d101475d8b20a2381f78447822ac1eab6504dd8 # v4.2.2
        with:
          subject-path: |
            build/*.deb
            build/*.mcpb
  sums:
    steps:
      - uses: actions/upload-artifact@v7
"""


def self_test():
    cases = {
        "good": ({"release.yml": GOOD}, 0),
        "unpinned": ({"release.yml": GOOD.replace("checkout@3d3c42e5aac5ba805825da76410c181273ba90b1", "checkout@v7")}, 1),
        "short sha": ({"release.yml": GOOD.replace("3d3c42e5aac5ba805825da76410c181273ba90b1", "3d3c42e")}, 1),
        "no environment": ({"release.yml": GOOD.replace("    environment: member-release\n", "")}, 1),
        "packages good": ({"release.yml": GOOD, "packages.yml": PACKAGES}, 0),
        "packages unattested": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("attest-build-provenance", "x")}, 1),
        "packages attest unpinned": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("@4d101475d8b20a2381f78447822ac1eab6504dd8", "@v4")}, 1),
        "packages attests less": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("            build/*.mcpb\n  sums:", "  sums:")}, 1),
        "packages secret": ({"release.yml": GOOD, "packages.yml": PACKAGES + "        env: { K: '${{ secrets.X }}' }\n"}, 1),
        "signing key outside env": ({"release.yml": GOOD.replace("    environment: release\n", "")}, 1),
        "signing key at repo level elsewhere": ({"release.yml": GOOD, "x.yml": f"jobs:\n  a:\n    env:\n      K: ${{{{ {SIGNING} }}}}\n"}, 1),
        "no staged_run": ({"release.yml": GOOD.replace("staged_run.py", "x.py")}, 1),
        "no verify job": ({"release.yml": GOOD.replace("  verify:\n", "  check:\n")}, 1),
        "publish without verify": ({"release.yml": GOOD.replace("needs: [locate, build, verify, smoke]\n    environment: release", "needs: [locate, build, smoke]\n    environment: release")}, 1),
        "members without re-check": ({"release.yml": GOOD.replace("      - run: python3 tools/release/verify_staged.py dist sums --sums-only\n      - name: Publish", "      - name: Publish")}, 1),
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
