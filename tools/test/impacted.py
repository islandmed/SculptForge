#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""T0 of the test flow (docs/testing-flow.md): which ctest entries a change can break, as a `ctest -R` regex.

  tools/test/impacted.py [--build build] [--base origin/main] [-v]       changed files from git (base...HEAD + work tree)
  tools/test/impacted.py --build build --files a.cpp tests/ui/b.sfui     an explicit file list (pc.sh passes one)
  tools/test/impacted.py --self-test

Prints one line: the regex (`^(ui_bake|marks)$`), or `.` when the change touches the build itself (CMake files) and
everything must run. Nightly tests are never listed (T3 runs them). -v explains every pick on stderr.

How a file maps to tests, most exact first:
  1. the build graph (build.ninja plus `ninja -t deps`, so headers count): a source or header -> the object files that
     include it -> the libraries and programs linking them -> the tests that run those programs (a test's command or
     arguments name the program; sculptforge_tests is split into shards, all of them run);
  2. a test's own files: the script it runs, the files its command names, and their folder (tools/guide/*.py);
  3. UI scripts that name a changed data file (tests/ui/data/x.json -> scripts mentioning x.json);
  4. the hand map below for files no command names (docs, site, release scripts, fragments);
  5. when the app itself changed (src/app, src/render ...): UI tests whose name shares a word with the changed file
     (PanelMicro.cpp -> ui_micro_levels), UI tests changed together with it in the last 600 commits, and SMOKE.
T1 (the full suite on the PC) catches anything this misses; files that map to nothing are listed with -v.
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

# Files no test command names, and the tests that read them. Globs match the repo-relative path.
HAND_MAP = [
    ("CMakeLists.txt", "."), ("cmake/*", "."),
    ("docs/guide/*", "guide_site guide_menu_paths ui_user_guide ui_user_guide_1080 ui_whats_new ui_guide_shortcuts ui_quick_tour"),
    ("docs/guide/whats-new.md", "release_notes ui_whats_new"),
    ("docs/supporters.txt", "ui_support ui_support_card"),
    ("website/*", "guide_site"),
    ("changes/*", "release_notes release_notes_fragments"),
    ("CHANGELOG.md", "release_notes"),
    ("scripts/release.sh", "release_script_beta"),
    ("tests/release_beta_test.sh", "release_script_beta"),
    ("tools/release/*", "release_notes release_notes_fragments release_script_beta"),
    ("packaging/windows/*", "ui_windows_dpi_2560 ui_windows_dpi_2160 ui_ui_scale_900"),
    ("addons/blender/*", "blender_exchange blender_addon_package blender_addon"),
    ("integrations/maya/*", "maya_shelf_script ui_maya_bridge"),
    ("services/link-worker/*", "link_worker ui_phone_link"),
    ("tools/brush-gallery/*", "brush_gallery_index ui_brush_gallery"),
    ("tools/marks_*.txt", "marks"),
    ("tools/test/*", "test_impact"),
    ("tests/ui/data/*", ""),  # step 3 handles these
]
# Public text is checked for third-party marks on every change: cheap, and the one check nobody may skip.
ALWAYS = ["marks"]
# The app's own sanity set, whenever the app program changed.
SMOKE = ["ui_shell_layout", "ui_sculpt_basics", "ui_idle_redraw", "ui_release_smoke"]
# Sources whose change only matters to the app's UI (step 5); a core change reaches every test through the graph.
APP_DIRS = ("src/app/", "src/render/", "src/link/")
STOP_WORDS = {"panel", "tool", "tools", "util", "utils", "test", "impl", "main", "app", "core", "data", "file", "ui"}


def sh(*cmd, cwd=None):
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return ""


def changed_files(root, base):
    mb = sh("git", "merge-base", base, "HEAD", cwd=root).strip() or base
    out = sh("git", "diff", "--name-only", mb, cwd=root) + sh("git", "ls-files", "-o", "--exclude-standard", cwd=root)
    return sorted({f for f in out.split("\n") if f})


def words(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    parts = re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|[0-9]+", stem)
    return {p.lower() for p in parts if len(p) >= 4 and p.lower() not in STOP_WORDS}


class Graph:
    """The build graph: which outputs (objects, libraries, programs) each repo file reaches."""

    def __init__(self, build, root):
        self.build, self.root = build, root
        self.users = defaultdict(set)  # input path (absolute) -> outputs built from it
        ninja = os.path.join(build, "build.ninja")
        if not os.path.exists(ninja):
            self.ok = self.load_make(build)
            return
        self.ok = True
        text = open(ninja, encoding="utf-8", errors="replace").read().replace("$\n", " ")
        for line in text.split("\n"):
            if not line.startswith("build "):
                continue
            m = re.match(r"build (.+?)(?<!\$): (\S+)(.*)", line)
            if not m or m.group(2) in ("phony", "CUSTOM_COMMAND", "RERUN_CMAKE", "CLEAN", "HELP"):
                continue
            outs = [self.abs(o) for o in self.split(m.group(1)) if o != "|"]
            ins = self.split(m.group(3).split("||")[0])
            for i in ins:
                if i != "|":
                    for o in outs:
                        self.users[self.abs(i)].add(o)
        # Headers: the dependency lists the compiler wrote (valid after a build).
        obj = None
        for line in sh("ninja", "-C", build, "-t", "deps").split("\n"):
            if line and not line.startswith(" "):
                obj = self.abs(line.split(":")[0])
            elif line.strip() and obj:
                self.users[self.abs(line.strip())].add(obj)

    def load_make(self, build):
        """The Makefile generator's graph: CMakeFiles/<target>.dir/link.txt (what links what) and the compiler's
        .o.d dependency files (which headers each object includes). False when there is no build at all."""
        found = False
        for d, dirs, files in os.walk(build):
            dirs[:] = [x for x in dirs if x not in ("_deps", "Testing", "ui-shots", "test-tmp")]
            if not d.endswith(".dir") or os.path.basename(os.path.dirname(d)) != "CMakeFiles":
                continue
            base = os.path.dirname(os.path.dirname(d))  # where the target's outputs and commands live
            try:
                words = open(os.path.join(d, "link.txt"), encoding="utf-8", errors="replace").read().split()
            except OSError:
                words = []
            out = None
            if "-o" in words and words.index("-o") + 1 < len(words):
                out = words[words.index("-o") + 1]
            elif len(words) > 2 and words[0].endswith("ar") and words[1] in ("qc", "cr", "rcs", "qcs"):
                out = words[2]
            for root, _, fs in os.walk(d):
                for f in fs:
                    path = os.path.join(root, f)
                    if f.endswith(".o.d"):
                        found = True
                        text = open(path, encoding="utf-8", errors="replace").read().replace("\\\n", " ")
                        head, _, deps = text.partition(": ")
                        obj = os.path.normpath(os.path.join(base, head.strip()))
                        for dep in deps.split():
                            self.users[os.path.normpath(os.path.join(base, dep))].add(obj)
                    elif f.endswith(".o") and out:
                        self.users[os.path.normpath(path)].add(os.path.normpath(os.path.join(base, out)))
            if out:
                for w in words:
                    if w.endswith((".a", ".so", ".lib")) and not os.path.isabs(w):
                        self.users[os.path.normpath(os.path.join(base, w))].add(os.path.normpath(os.path.join(base, out)))
        return found

    @staticmethod
    def split(s):
        return [p.replace("$ ", " ").replace("$:", ":") for p in re.split(r"(?<!\$) +", s.strip()) if p]

    def abs(self, p):
        return os.path.normpath(p if os.path.isabs(p) else os.path.join(self.build, p))

    def reach(self, path):
        seen, todo = set(), [os.path.normpath(os.path.join(self.root, path))]
        while todo:
            for o in self.users.get(todo.pop(), ()):
                if o not in seen:
                    seen.add(o)
                    todo.append(o)
        return seen


def load_tests(build):
    try:
        out = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], capture_output=True, text=True,
                             check=True).stdout
        data = json.loads(out)
    except (OSError, subprocess.CalledProcessError, ValueError):
        return []
    tests = []
    for t in data.get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if props.get("DISABLED"):
            continue
        tests.append({"name": t["name"], "command": t.get("command", []), "labels": props.get("LABELS", []),
                      "cwd": props.get("WORKING_DIRECTORY", "")})
    return tests


def co_changed(root, files, ui_names, limit=600, top=6, log_file=None):
    """UI tests whose script changed in the same commits as these files (the last `limit` commits). log_file: that
    `git log --no-merges --name-only --format=@@` output, for a copy of the tree without .git (pc.sh)."""
    if log_file:
        log = open(log_file, encoding="utf-8", errors="replace").read()
    else:
        log = sh("git", "log", f"-{limit}", "--no-merges", "--name-only", "--format=@@", cwd=root)
    pairs = defaultdict(Counter)
    want = set(files)
    for commit in log.split("@@"):
        names = {f for f in commit.split("\n") if f}
        hit = names & want
        if not hit:
            continue
        scripts = {"ui_" + os.path.splitext(os.path.basename(f))[0] for f in names
                   if f.startswith("tests/ui/") and f.endswith(".sfui")}
        for f in hit:
            for s in scripts & ui_names:
                pairs[f][s] += 1
    out = {}
    for f, c in pairs.items():
        out[f] = [s for s, n in c.most_common(top) if n >= 2]
    return out


def is_app(out):
    return os.path.basename(out) in ("sculptforge", "sculptforge.exe")


def impacted(root, build, files, verbose=False, graph=None, tests=None, history=True, log_file=None):
    log = (lambda *a: print(*a, file=sys.stderr)) if verbose else (lambda *a: None)
    tests = load_tests(build) if tests is None else tests
    graph = Graph(build, root) if graph is None else graph
    names = {t["name"] for t in tests}
    ui_names = {n for n in names if n.startswith("ui_")}
    nightly = {t["name"] for t in tests if "nightly" in t["labels"]}
    pick = {}

    def add(name, why):
        if name in names and name not in nightly and name not in pick:
            pick[name] = why

    # Every file a test's command names, and that file's folder (a script's sibling modules).
    by_file, by_dir, by_output = defaultdict(set), defaultdict(set), defaultdict(set)
    for t in tests:
        for arg in t["command"]:
            for a in re.split(r"[=;]", arg):
                if not a or not (os.path.isabs(a) or "/" in a):
                    continue
                p = os.path.normpath(a if os.path.isabs(a) else os.path.join(t["cwd"] or build, a))
                by_output[p].add(t["name"])
                rel = os.path.relpath(p, root)
                if not rel.startswith(".."):
                    by_file[rel].add(t["name"])
                    if os.path.isfile(p) and not rel.startswith("tests/ui/"):
                        by_dir[os.path.dirname(rel)].add(t["name"])
                    elif os.path.isdir(p):
                        by_dir[rel].add(t["name"])

    everything, app_files, unmapped = False, [], []
    for f in files:
        before = len(pick)
        for glob, targets in HAND_MAP:
            if fnmatch.fnmatch(f, glob):
                if targets == ".":
                    everything = True
                for n in targets.split():
                    add(n, f"hand map {glob}")
        for n in by_file.get(f, ()):
            add(n, f"runs {f}")
        d = os.path.dirname(f)
        while d:
            for n in by_dir.get(d, ()):
                add(n, f"same folder as its script ({d})")
            if d.startswith("tools/") and d.count("/") > 1:
                d = os.path.dirname(d)
            else:
                break
        if f.startswith("tests/ui/data/"):
            base = os.path.basename(f)
            for t in tests:
                for arg in t["command"]:
                    if arg.endswith(".sfui") and os.path.isfile(arg) and base in open(arg, encoding="utf-8").read():
                        add(t["name"], f"script names {base}")
        if graph.ok:
            for out in graph.reach(f):
                if is_app(out):  # every UI test runs the app: step 5 picks the ones that matter
                    app_files.append(f)
                    continue
                for n in by_output.get(out, ()):
                    add(n, f"build graph: {f} -> {os.path.relpath(out, build)}")
        elif f.startswith(APP_DIRS):
            app_files.append(f)
        if len(pick) == before and not everything and f not in app_files:
            unmapped.append(f)

    if not graph.ok and any(f.startswith("src/") and not f.startswith(APP_DIRS) for f in files):
        # No build graph: a core source change could reach anything.
        log("no build.ninja: every non-nightly test")
        everything = True
    app_files = sorted(set(app_files))
    if app_files:
        for f in app_files:
            w = words(f)
            for n in ui_names:
                if w & set(n[3:].split("_")):
                    add(n, f"name shares a word with {f}")
        if history:
            for f, ns in co_changed(root, app_files, ui_names, log_file=log_file).items():
                for n in ns:
                    add(n, f"changed together with {f}")
        for n in SMOKE:
            add(n, "smoke (the app changed)")
    if files:
        for n in ALWAYS:
            add(n, "always")
    for n, why in sorted(pick.items()):
        log(f"  {n:40} {why}")
    for f in unmapped:
        log(f"  (no test reads {f}; T1 still runs everything)")
    if everything:
        return "."
    return "^(" + "|".join(sorted(pick)) + ")$" if pick else ""


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as root:
        b = os.path.join(root, "build")
        os.makedirs(os.path.join(root, "tools/guide"))
        os.makedirs(os.path.join(root, "tests/ui/data"))
        os.makedirs(b)
        for p in ("tools/guide/build_site.py", "tools/guide/lib.py", "tests/ui/micro_levels.sfui", "tests/ui/bake.sfui"):
            open(os.path.join(root, p), "w").write("env F $SCRIPT_DIR/data/feed.json\n" if "bake" in p else "")
        open(os.path.join(root, "tests/ui/data/feed.json"), "w").write("{}")
        open(os.path.join(b, "build.ninja"), "w").write(
            "build CMakeFiles/core.dir/src/core/Mesh.cpp.o: CXX_COMPILER__core ../src/core/Mesh.cpp\n"
            "build libcore.a: CXX_STATIC_LIBRARY_LINKER__core CMakeFiles/core.dir/src/core/Mesh.cpp.o\n"
            "build CMakeFiles/app.dir/src/app/PanelMicro.cpp.o: CXX_COMPILER__app ../src/app/PanelMicro.cpp\n"
            "build sculptforge: CXX_EXECUTABLE_LINKER__app CMakeFiles/app.dir/src/app/PanelMicro.cpp.o | $\n"
            "    libcore.a\n"
            "build sculptforge_tests: CXX_EXECUTABLE_LINKER__t libcore.a\n")
        g = Graph(b, root)
        g.users[g.abs("../src/core/Mesh.h")].add(g.abs("CMakeFiles/core.dir/src/core/Mesh.cpp.o"))  # a -t deps line
        T = lambda n, *c, labels=(): {"name": n, "command": list(c), "labels": list(labels), "cwd": ""}
        app, ui = os.path.join(b, "sculptforge"), os.path.join(root, "tests/ui")
        tests = [T("sculptforge_tests_0of2", os.path.join(b, "sculptforge_tests"), "--shard", "0/2"),
                 T("sculptforge_tests_1of2", os.path.join(b, "sculptforge_tests"), "--shard", "1/2"),
                 T("ui_micro_levels", app, "--ui-test", ui + "/micro_levels.sfui"),
                 T("ui_bake", app, "--ui-test", ui + "/bake.sfui"),
                 T("ui_bench", app, "--bench", labels=["ui", "nightly"]),
                 T("ui_shell_layout", app), T("marks", "python3", root + "/tools/check_marks.py"),
                 T("guide_site", "python3", root + "/tools/guide/build_site.py", "--check"),
                 T("release_notes_fragments", "python3", "x.py")]
        r = lambda *f: impacted(root, b, list(f), graph=g, tests=tests, history=False)
        assert r("src/core/Mesh.h") == "^(marks|sculptforge_tests_0of2|sculptforge_tests_1of2|ui_shell_layout)$", r("src/core/Mesh.h")
        assert r("src/app/PanelMicro.cpp") == "^(marks|ui_micro_levels|ui_shell_layout)$", r("src/app/PanelMicro.cpp")
        assert r("tests/ui/bake.sfui") == "^(marks|ui_bake)$", r("tests/ui/bake.sfui")
        assert r("tests/ui/data/feed.json") == "^(marks|ui_bake)$"
        assert r("tools/guide/lib.py") == "^(guide_site|marks)$", r("tools/guide/lib.py")
        assert r("changes/x.md") == "^(marks|release_notes_fragments)$"
        assert r("CMakeLists.txt") == "."
        assert r() == ""
        assert words("src/app/PanelMicroLevels.cpp") == {"micro", "levels"}
    print("impacted.py self-test passed")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--git-log", help="git log --no-merges --name-only --format=@@ output (a tree without .git)")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    root = sh("git", "rev-parse", "--show-toplevel").strip() or os.getcwd()
    build = os.path.abspath(a.build)
    files = a.files if a.files is not None else changed_files(root, a.base)
    if a.verbose:
        print(f"{len(files)} changed file(s)", file=sys.stderr)
    print(impacted(root, build, files, a.verbose, log_file=a.git_log))
    return 0


if __name__ == "__main__":
    sys.exit(main())
