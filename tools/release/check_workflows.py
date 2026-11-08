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
  - build once by key: packages.yml filters its pushes by `paths:` (tools/release/build_inputs.txt is the one list of
    build inputs; a filter skipped 0.11.14's staging push), or has no job reuse picking the earlier run with
    tools/release/staged_run.py --before-run (only older runs, so two runs never wait for each other).
  - integrations.yml (the Blender/Maya lanes) runs on anything but tag pushes, has an unpinned `uses:` or shell
    tracing, reads a secret other than UPDATE_SIGNING_KEY (in `environment: release`, by a job that can't write), or
    publishes without re-checking SHA256SUMS.txt.sig against the app's key (openssl pkeyutl -verify).
  - t0.yml (T0 on GitHub's runners, ~/Work/.sf/t0-ci.sh) runs on anything but pushes to main and t0/**, can write,
    reads a secret, has an unpinned `uses:` or shell tracing, saves a cache in a step not limited to main (t0/ runs
    only restore), or can end red (its job must be continue-on-error: the verdict is the summary, a red T0 emails nobody).
  - another workflow that runs on branch pushes would also run for t0/** refs (branches-ignore them).

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
        if re.search(r"^\s+paths(-ignore)?:", pk.split("\njobs:")[0], re.M):
            errs.append("packages.yml: a paths: filter (job reuse skips unchanged build inputs: tools/release/build_inputs.txt)")
        if not re.search(r"staged_run\.py\b.*--before-run", "\n".join(jobs(pk).get("reuse", []))):
            errs.append("packages.yml: no job reuse running tools/release/staged_run.py --before-run")
        for job, lines in jobs(pk).items():
            body = "\n".join(lines)
            if job != "sums" and "upload-artifact" in body and not ATTEST.search(body):
                errs.append(f"packages.yml: job {job} uploads packages without a pinned actions/attest-build-provenance step")
            elif job != "sums" and "upload-artifact" in body and paths(body, "path") != paths(body, "subject-path"):
                errs.append(f"packages.yml: job {job} attests {sorted(paths(body, 'subject-path'))} but uploads {sorted(paths(body, 'path'))}"
                            " (verify refuses an unattested file)")
    it = files.get("integrations.yml")
    if it is not None:
        on = it.split("\njobs:")[0]
        if re.findall(r"secrets\.(\w+)", it) and set(re.findall(r"secrets\.(\w+)", it)) != {"UPDATE_SIGNING_KEY"}:
            errs.append("integrations.yml: reads a secret other than UPDATE_SIGNING_KEY")
        ij = jobs(it)
        for job, lines in ij.items():
            body = "\n".join(lines)
            if SIGNING in body and re.search(r"contents:\s*write", body):
                errs.append(f"integrations.yml: job {job} holds UPDATE_SIGNING_KEY and a GitHub token that can write")
            if "gh release create" in body and "pkeyutl -verify" not in body:
                errs.append(f"integrations.yml: job {job} publishes without re-checking the signature (openssl pkeyutl -verify)")
        if not any(SIGNING in "\n".join(l) for l in ij.values()):
            errs.append("integrations.yml: no job signs SHA256SUMS.txt (UPDATE_SIGNING_KEY in environment release)")
        if not re.search(r"^on:\s*\n\s+push:\s*\n\s+tags:", on, re.M) or re.search(
                r"\b(branches|pull_request|workflow_dispatch|schedule|workflow_run|issue_comment)\b", on):
            errs.append("integrations.yml: must run on tag pushes only")
        for n, line in enumerate(it.splitlines(), 1):
            m = re.match(r"^\s*(?:-\s+)?uses:\s*(\S.*?)\s*$", line)
            if m and not m.group(1).startswith("./") and not PINNED.match(m.group(1)):
                errs.append(f"integrations.yml:{n}: `uses: {m.group(1)}` is not pinned to a full commit SHA")
            if re.search(r"(^|[\s;])set\s+-[a-wyz]*x", line):
                errs.append(f"integrations.yml:{n}: shell tracing (set -x)")
    t0 = files.get("t0.yml")
    if t0 is not None:
        on = t0.split("\njobs:")[0]
        if not re.search(r"^on:\s*\n\s+push:\s*\n\s+branches:\s*\[\s*main,\s*'t0/\*\*'\s*\]\s*$", on, re.M) or re.search(
                r"\b(tags|pull_request|workflow_dispatch|schedule|workflow_run|issue_comment)\b", on):
            errs.append("t0.yml: must run on pushes to main and t0/** only")
        if "secrets." in t0:
            errs.append("t0.yml: reads a secret")
        if re.search(r":\s*write\b", t0) or not re.search(r"^permissions:\s*\n\s+contents:\s*read\s*$", t0, re.M):
            errs.append("t0.yml: permissions must be contents: read only")
        for step in re.split(r"\n(?=      - )", t0):
            if re.search(r"actions/cache(/save)?@", step) and "if: github.ref == 'refs/heads/main'" not in step:
                errs.append("t0.yml: saves a cache in a step not limited to main (if: github.ref == 'refs/heads/main')")
        for job, lines in jobs(t0).items():
            if not any(re.match(r"^    continue-on-error:\s*true\b", l) for l in lines):
                errs.append(f"t0.yml: job {job} isn't continue-on-error: true (a red T0 must not fail the run)")
        for n, line in enumerate(t0.splitlines(), 1):
            m = re.match(r"^\s*(?:-\s+)?uses:\s*(\S.*?)\s*$", line)
            if m and not PINNED.match(m.group(1)):
                errs.append(f"t0.yml:{n}: `uses: {m.group(1)}` is not pinned to a full commit SHA")
            if re.search(r"(^|[\s;])set\s+-[a-wyz]*x", line):
                errs.append(f"t0.yml:{n}: shell tracing (set -x)")
        for name, text in files.items():
            on = text.split("\njobs:")[0]
            if name != "t0.yml" and re.search(r"^  push:\s*$", on, re.M) and not re.search(r"^    (branches|tags):", on, re.M) \
                    and "t0/**" not in on:
                errs.append(f"{name}: runs on every branch push, t0/** too (add branches-ignore: ['t0/**'])")
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
  reuse:
    steps:
      - run: python3 tools/release/staged_run.py --sha "$SHA" --before-run "$RUN" --wait --full
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


INTEGRATIONS = """on:
  push:
    tags: ['blender-v[0-9]*']
permissions:
  contents: read
jobs:
  sign:
    runs-on: ubuntu-24.04
    environment: release
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
      - env:
          UPDATE_SIGNING_KEY: ${{ secrets.UPDATE_SIGNING_KEY }}
        run: tools/release/sign_sums.sh dist
  publish:
    needs: sign
    runs-on: ubuntu-24.04
    permissions:
      contents: write
    steps:
      - run: openssl pkeyutl -verify -pubin -rawin -in dist/SHA256SUMS.txt -sigfile dist/SHA256SUMS.txt.sig
      - env:
          GH_TOKEN: ${{ github.token }}
        run: gh release create x --latest=false
"""


T0 = """on:
  push:
    branches: [main, 't0/**']
permissions:
  contents: read
jobs:
  t0:
    runs-on: ubuntu-24.04
    continue-on-error: true
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
      - uses: actions/cache/restore@caa296126883cff596d87d8935842f9db880ef25 # v5.1.0
      - if: github.ref == 'refs/heads/main'
        uses: actions/cache/save@caa296126883cff596d87d8935842f9db880ef25 # v5.1.0
      - run: bash tools/ci/t0.sh
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
        "packages paths filter": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("[main, staging]\n", "[main, staging]\n    paths: ['**', '!website/**']\n")}, 1),
        "packages paths-ignore": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("[main, staging]\n", "[main, staging]\n    paths-ignore: [website/**]\n")}, 1),
        "packages no reuse": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace("staged_run.py", "x.py")}, 1),
        "packages reuse waits on newer": ({"release.yml": GOOD, "packages.yml": PACKAGES.replace(' --before-run "$RUN"', "")}, 1),
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
        "integrations good": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS}, 0),
        "integrations unpinned": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1", "@v7")}, 1),
        "integrations other secret": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "github.token", "secrets.REPO_SIGNING_KEY")}, 1),
        "integrations key outside env": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "    environment: release\n", "")}, 1),
        "integrations signer can write": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "    environment: release\n", "    environment: release\n    permissions:\n      contents: write\n")}, 1),
        "integrations unverified publish": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "openssl pkeyutl -verify", "true")}, 1),
        "integrations unsigned": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "secrets.UPDATE_SIGNING_KEY", "github.token")}, 1),
        "integrations on a branch": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "    tags: ['blender-v[0-9]*']", "    branches: [main]")}, 1),
        "integrations dispatch": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace(
            "jobs:", "  workflow_dispatch:\njobs:", 1)}, 1),
        "integrations set -x": ({"release.yml": GOOD, "integrations.yml": INTEGRATIONS.replace("run: gh release", "run: set -x; gh release")}, 1),
        "t0 good": ({"release.yml": GOOD, "t0.yml": T0}, 0),
        "t0 on all branches": ({"release.yml": GOOD, "t0.yml": T0.replace("[main, 't0/**']", "['**']")}, 1),
        "t0 dispatch": ({"release.yml": GOOD, "t0.yml": T0.replace("permissions:", "  workflow_dispatch:\npermissions:")}, 1),
        "t0 write": ({"release.yml": GOOD, "t0.yml": T0.replace("contents: read", "contents: write")}, 1),
        "t0 secret": ({"release.yml": GOOD, "t0.yml": T0 + "        env: { K: '${{ secrets.X }}' }\n"}, 1),
        "t0 unpinned": ({"release.yml": GOOD, "t0.yml": T0.replace("@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1", "@v7")}, 1),
        "t0 cache save": ({"release.yml": GOOD, "t0.yml": T0.replace("cache/restore@", "cache/save@")}, 1),
        "t0 cache save any ref": ({"release.yml": GOOD, "t0.yml": T0.replace("      - if: github.ref == 'refs/heads/main'\n        uses", "      - uses")}, 1),
        "t0 can go red": ({"release.yml": GOOD, "t0.yml": T0.replace("    continue-on-error: true\n", "")}, 1),
        "t0 set -x": ({"release.yml": GOOD, "t0.yml": T0.replace("run: bash", "run: set -x; bash")}, 1),
        "t0 other on all branches": ({"release.yml": GOOD, "t0.yml": T0, "a.yml": "on:\n  push:\n    paths: [x]\njobs:\n  a:\n"}, 1),
        "t0 other ignores t0": ({"release.yml": GOOD, "t0.yml": T0,
                                 "a.yml": "on:\n  push:\n    branches-ignore: ['t0/**']\n    paths: [x]\njobs:\n  a:\n"}, 0),
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
