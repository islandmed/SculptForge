#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Is BASE..HEAD only release paperwork? Then CI's result for BASE stands for HEAD (scripts/release.sh step ci,
.github/workflows/release.yml job locate; docs/testing-flow.md).

  tools/release/paperwork_only.py BASE HEAD     exit 0: paperwork only; 1: something else changed (printed); 2: error
  tools/release/paperwork_only.py --post-tag BASE HEAD   the stricter set below (scripts/merge-queue.sh staging)
  tools/release/paperwork_only.py --self-test

Paperwork is exactly what scripts/release.sh step bump writes, file by file and line by line: the version lines of
CMakeLists.txt, CITATION.cff, packaging/rez/package.py and the metainfo's <release> lines; CHANGELOG.md; Markdown under
docs/ (the guide's What's New included: release.sh step check runs the guide and What's New tests on the bumped tree);
the generated website (website/, not website/link, which is built into the app); deleted note fragments (changes/).
Anything else, a rename, a mode change or an unreadable diff is not paperwork (fail safe).

--post-tag: what a promote pushes to main after its tag (release.sh steps site and aur), which a staged batch may be
merged onto without testing again (scripts/merge-queue.sh staging): website/ (not website/link/, built into the app),
Markdown under docs/, and packaging/arch/PKGBUILD + .SRCINFO version and checksum lines only. Never src/, tests/,
CMakeLists.txt, .github/, any other packaging/ file, changes/ or anything else (the self-test proves it).
"""
import os
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
    # step bump pins the What's New test to the new version (tools/release/pin_whats_new_test.py: the pinned guide version,
    # its comment and the expected "Version X" / tagline texts); step check runs that test on the bumped tree. Without
    # this line every real bump failed this check and CI waited for a second full run (0.12.0's bump edits it).
    "tests/ui/whats_new.sfui": r'(#.*|env SF_GUIDE_VERSION [0-9.]+|expect_text ".*")',
}


# --post-tag: the AUR step's files, line by line (version, release number, sources and checksums; never build steps)
POST_TAG_LINES = {
    "packaging/arch/PKGBUILD": r"(pkgver=[0-9][0-9A-Za-z._]*|pkgrel=[0-9]+|sha256sums(_[a-z0-9_]+)?=\('[0-9a-f]{64}'\)"
                               r"|source(_[a-z0-9_]+)?=\(\S+\))",
    "packaging/arch/.SRCINFO": r"\s*(pkgver|pkgrel|source(_[a-z0-9_]+)?|sha256sums(_[a-z0-9_]+)?) = \S+",
}


# App inputs inside the site paths (tools/ci/site_lane.txt's '!' lines: docs/supporters.txt, website/founding.json,
# website/link/): compiled into the app, so never paperwork in either mode (11-07: a Founding Member credit that skipped
# the app tests broke ui_support on main).
def app_inputs():
    lane = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ci", "site_lane.txt")
    try:
        with open(lane, encoding="utf-8") as f:
            pats = [l[1:].strip() for l in f if l.startswith("!") and l[1:].strip()]
    except OSError:
        pats = []
    return [re.compile(x) for x in pats or [r"^docs/supporters\.txt$", r"^website/founding\.json$", r"^website/link/"]]


APP_INPUTS = app_inputs()


def app_input(path):
    return any(r.search(path) for r in APP_INPUTS)


def allowed_post_tag(path):
    if app_input(path):
        return False
    if path in POST_TAG_LINES:
        return True
    if path.startswith("website/") and not path.startswith("website/link/"):
        return True
    return path.startswith("docs/") and path.endswith(".md")


def allowed(path):
    if app_input(path):
        return False
    if path in LINES:
        return True
    if path.startswith("website/") and not path.startswith("website/link/"):
        return True
    if path.startswith("docs/") and path.endswith(".md"):
        return True
    return False


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def check(base, head, post_tag=False):
    problems = []
    lines = POST_TAG_LINES if post_tag else LINES
    ok = allowed_post_tag if post_tag else allowed
    for line in git("diff", "--no-renames", "--raw", base, head).splitlines():
        meta, path = line.split("\t", 1)  # :old_mode new_mode old_sha new_sha status
        om, nm, _, _, status = meta[1:].split()
        if om != nm and om != "000000" and nm != "000000":
            problems.append(f"{path}: mode change")
            continue
        if path.startswith("changes/") and status == "D" and not post_tag:
            continue
        if not ok(path):
            problems.append(f"{path}: not release paperwork")
            continue
        rule = lines.get(path)
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
            # the bump's pin of the What's New test: its version and expected texts only
            os.makedirs("tests/ui")
            w("tests/ui/whats_new.sfui", "# pinned to 1.2.3\nenv SF_GUIDE_VERSION 1.2.3\nexpect_text \"Version 1.2.3\"\nclick \"x\"\n")
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "d2")
            w("tests/ui/whats_new.sfui", "# pinned to 1.3.0\nenv SF_GUIDE_VERSION 1.3.0\nexpect_text \"Version 1.3.0\"\nclick \"x\"\n")
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "d3")
            assert check("HEAD~1", "HEAD") == [], check("HEAD~1", "HEAD")
            w("tests/ui/whats_new.sfui", "# pinned to 1.3.0\nenv SF_GUIDE_VERSION 1.3.0\nexpect_text \"Version 1.3.0\"\nclick \"y\"\n")
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "d4")
            assert check("HEAD~1", "HEAD") == ['tests/ui/whats_new.sfui: -click "x"', 'tests/ui/whats_new.sfui: +click "y"'], check("HEAD~1", "HEAD")
            run("git", "reset", "-q", "--hard", "HEAD~3")
            # --post-tag: only the site, docs Markdown and the AUR files' version lines; never code, tests, the build,
            # workflows, other packaging, note fragments, or a PKGBUILD build step
            os.makedirs("website/link")
            os.makedirs("docs/guide")
            os.makedirs("packaging/arch")
            os.makedirs("packaging/linux")
            os.makedirs(".github/workflows")
            os.makedirs("tests")
            pk = "pkgver=1.2.3\npkgrel=1\nsource=(x.tar.gz)\nsha256sums=('%s')\nbuild() { make; }\n"
            w("packaging/arch/PKGBUILD", pk % ("0" * 64))
            w("packaging/arch/.SRCINFO", "\tpkgver = 1.2.3\n\tsha256sums = %s\n" % ("0" * 64))
            base = {"website/latest.json": "{}", "website/link/a.js": "x", "docs/guide/whats-new.md": "a",
                    "docs/notes.txt": "a", "tests/t.cpp": "a", ".github/workflows/ci.yml": "a",
                    "packaging/linux/x.xml": "a"}
            for k, v in base.items():
                w(k, v)
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "e")
            w("website/latest.json", '{"v": 2}')
            w("docs/guide/whats-new.md", "b")
            w("packaging/arch/PKGBUILD", (pk % ("1" * 64)).replace("1.2.3", "1.3.0"))
            w("packaging/arch/.SRCINFO", "\tpkgver = 1.3.0\n\tsha256sums = %s\n" % ("1" * 64))
            run("git", "add", "-A")
            run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "f")
            assert check("HEAD~1", "HEAD", post_tag=True) == [], check("HEAD~1", "HEAD", post_tag=True)
            bad = {"src/a.cpp": "int c;", "tests/t.cpp": "b", "CMakeLists.txt": "x", ".github/workflows/ci.yml": "b",
                   "packaging/linux/x.xml": "b", "website/link/a.js": "y", "docs/notes.txt": "b", "changes/g.md": "x",
                   "website/founding.json": '{"taken": 3}', "docs/supporters.txt": "Ada",
                   "packaging/arch/PKGBUILD": (pk % ("1" * 64)).replace("1.2.3", "1.3.0").replace("make", "curl x|sh")}
            for k, v in bad.items():
                run("git", "reset", "-q", "--hard")
                w(k, v)
                run("git", "add", "-A")
                run("git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "g")
                assert check("HEAD~1", "HEAD", post_tag=True), k
                if k in ("website/founding.json", "docs/supporters.txt"):  # app inputs: not paperwork either way
                    assert check("HEAD~1", "HEAD"), k
                run("git", "reset", "-q", "--hard", "HEAD~1")
        finally:
            os.chdir(cwd)
    print("paperwork_only.py self-test passed")
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    post_tag = argv[:1] == ["--post-tag"]
    if post_tag:
        argv = argv[1:]
    if len(argv) != 2:
        print(__doc__)
        return 2
    try:
        problems = check(*argv, post_tag=post_tag)
    except (subprocess.CalledProcessError, ValueError) as e:
        print(f"can't compare {argv[0]} and {argv[1]}: {e}")
        return 2
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
