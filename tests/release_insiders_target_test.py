#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest release_insiders_target: the members' beta step of .github/workflows/release.yml, run for real against a stand-in gh.

The runner's checkout has the tag, the insiders repository does not, so `gh release create TAG` alone fails with "tag exists locally
but has not been pushed" (v0.12.11-beta.1). The step must read the insiders repository's default branch and create the release with
--target <that branch>, keep the tag name, upload the packages, and stop (without creating anything) when the branch can't be read."""
import os
import pathlib
import subprocess
import sys
import tempfile
import textwrap

ROOT = pathlib.Path(__file__).resolve().parent.parent
YML = (ROOT / ".github" / "workflows" / "release.yml").read_text(encoding="utf-8")
STEP = "- name: Pre-release in sculptforge/sculptforge-insiders"


def fail(msg):
    sys.exit("FAIL: " + msg)


def step_script():
    lines = YML.split("\n")
    start = next((i for i, l in enumerate(lines) if l.strip() == STEP), None)
    if start is None:
        fail("release.yml has no step " + STEP)
    run = next(i for i in range(start, len(lines)) if lines[i].strip() == "run: |")
    indent = len(lines[run + 1]) - len(lines[run + 1].lstrip())
    body = []
    for l in lines[run + 1:]:
        if l.strip() and len(l) - len(l.lstrip()) < indent:
            break
        body.append(l)
    return textwrap.dedent("\n".join(body))


GH = """#!/bin/sh
echo "$*" >> "$GH_LOG"
case "$1 $2" in
  "api repos/"*) [ -n "$GH_NO_BRANCH" ] && exit 1; echo "$GH_BRANCH" ;;
  "release view") exit 1 ;;
esac
exit 0
"""


def run(branch="trunk", no_branch=False):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "bin").mkdir()
        (tmp / "bin" / "gh").write_text(GH)
        (tmp / "bin" / "gh").chmod(0o755)
        (tmp / "dist").mkdir()
        (tmp / "dist" / "pkg.zip").write_text("x")
        (tmp / "notes.md").write_text("notes\n")
        env = dict(os.environ, PATH=f"{tmp / 'bin'}:{os.environ['PATH']}", GH_LOG=str(tmp / "log"), GH_BRANCH=branch,
                   GH_NO_BRANCH="1" if no_branch else "", TAG="v9.9.9-beta.1", GH_TOKEN="t", GH_REPO="org/insiders")
        r = subprocess.run(["bash", "-c", step_script()], cwd=tmp, env=env, capture_output=True, text=True)
        log = (tmp / "log").read_text().splitlines() if (tmp / "log").exists() else []
        return r, log


r, log = run()
if r.returncode:
    fail(f"the step failed: {r.stderr}")
create = [l for l in log if l.startswith("release create")]
if len(create) != 1:
    fail(f"want one release create, got {log}")
if "--target trunk" not in create[0] or not create[0].startswith("release create v9.9.9-beta.1 "):
    fail("release create lacks --target <the insiders default branch> or changed the tag name: " + create[0])
if "--prerelease" not in create[0] or "--latest=false" not in create[0]:
    fail("release create lost --prerelease/--latest=false: " + create[0])
if not any(l.startswith("api repos/org/insiders") for l in log):
    fail("the default branch was not read from the insiders repository: " + repr(log))
if not any(l.startswith("release upload v9.9.9-beta.1 dist/pkg.zip") for l in log):
    fail("the packages were not uploaded: " + repr(log))

# the branch can't be read: no release is created, the step fails with a message
r, log = run(no_branch=True)
if r.returncode == 0 or any(l.startswith("release create") for l in log):
    fail("an unreadable default branch must stop the step before creating the release: " + repr(log))
if "cannot read the default branch" not in r.stdout + r.stderr:
    fail("no message when the branch can't be read")
print("release_insiders_target ok")
