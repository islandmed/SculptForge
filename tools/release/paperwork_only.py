#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Is BASE..HEAD only release paperwork? Then CI's result for BASE stands for HEAD (scripts/release.sh step ci,
.github/workflows/release.yml job locate; docs/testing-flow.md).

  tools/release/paperwork_only.py BASE HEAD     exit 0: paperwork only; 1: something else changed (printed); 2: error
  tools/release/paperwork_only.py --self-test

Paperwork is exactly what scripts/release.sh step bump writes, file by file and line by line: the version lines of
CMakeLists.txt, CITATION.cff, packaging/rez/package.py and the metainfo's <release> lines; CHANGELOG.md; Markdown under
docs/ (the guide's What's New included: release.sh step check runs the guide and What's New tests on the bumped tree);
the generated website (website/, not website/link, which is built into the app); deleted note fragments (changes/).
Anything else, a rename, a mode change or an unreadable diff is not paperwork (fail safe).
"""
import re
import subprocess
import sys

# file (exact or prefix/) -> None (any change) or a regex every changed line must match
LINES = {
    "CMakeLists.txt": r'(project\(SculptForge VERSION [0-9.]+[ )].*|set\(SCULPTFORGE_PRERELEASE "[a-z0-9.]*"\))',
    "CITATION.cff": r"(version|date-released): .*",
    "packaging/rez/package.py": r'version = "[0-9.]+"',
    "packaging/linux/org.sculptforge.SculptForge.metainfo.xml": r'\s*<release version="[0-9.]+" date="[0-9-]+"/>',
    "CHANGELOG.md": None,
}


def allowed(path):
    if path in LINES:
        return True
    if path.startswith("website/") and not path.startswith("website/link/"):
        return True
    if path.startswith("docs/") and path.endswith(".md"):
        return True
    return False


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def check(base, head):
    problems = []
    for line in git("diff", "--no-renames", "--raw", base, head).splitlines():
        meta, path = line.split("\t", 1)  # :old_mode new_mode old_sha new_sha status
        om, nm, _, _, status = meta[1:].split()
        if om != nm and om != "000000" and nm != "000000":
            problems.append(f"{path}: mode change")
            continue
        if path.startswith("changes/") and status == "D":
            continue
        if not allowed(path):
            problems.append(f"{path}: not release paperwork")
            continue
        rule = LINES.get(path)
        if rule:
            diff = git("diff", "--no-renames", "-U0", base, head, "--", path)
            for d in diff.splitlines():
                if d.startswith(("+++", "---")) or not d.startswith(("+", "-")):
                    continue
                if not re.fullmatch(rule, d[1:]):
                    problems.append(f"{path}: {d[:80]}")
    return problems


def self_test():
    import os
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        run = lambda *a: subprocess.run(a, cwd=d, check=True, capture_output=True)
        run("git", "init", "-q")
        os.makedirs(os.path.join(d, "changes"))
        os.makedirs(os.path.join(d, "src"))
        w = lambda p, t: open(os.path.join(d, p), "w").write(t)
        w("CMakeLists.txt", 'project(SculptForge VERSION 1.2.3 LANGUAGES C CXX)\nset(SCULPTFORGE_PRERELEASE "")\nadd_x()\n')
        w("CHANGELOG.md", "## [Unreleased]\n- a\n")
        w("changes/f.md", "x")
        w("src/a.cpp", "int a;")
        run("git", "add", "-A")
        run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "a")
        cwd = os.getcwd()
        os.chdir(d)
        try:
            w("CMakeLists.txt", 'project(SculptForge VERSION 1.3.0 LANGUAGES C CXX)\nset(SCULPTFORGE_PRERELEASE "beta.1")\nadd_x()\n')
            w("CHANGELOG.md", "## [Unreleased]\n\n## [1.3.0]\n- a\n")
            os.remove("changes/f.md")
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "b")
            assert check("HEAD~1", "HEAD") == [], check("HEAD~1", "HEAD")
            w("CMakeLists.txt", 'project(SculptForge VERSION 1.3.0 LANGUAGES C CXX)\nset(SCULPTFORGE_PRERELEASE "beta.1")\nadd_y()\n')
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "c")
            assert check("HEAD~1", "HEAD") == ["CMakeLists.txt: -add_x()", "CMakeLists.txt: +add_y()"], check("HEAD~1", "HEAD")
            w("src/a.cpp", "int b;")
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "d")
            assert check("HEAD~1", "HEAD") == ["src/a.cpp: not release paperwork"]
        finally:
            os.chdir(cwd)
    print("paperwork_only.py self-test passed")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if len(argv) != 2:
        print(__doc__)
        return 2
    try:
        problems = check(*argv)
    except (subprocess.CalledProcessError, ValueError) as e:
        print(f"can't compare {argv[0]} and {argv[1]}: {e}")
        return 2
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
