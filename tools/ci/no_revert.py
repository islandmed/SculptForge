#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refuses a merge or squash that quietly undoes what landed on the other side (scripts/merge-queue.sh runs it).

    tools/ci/no_revert.py BASE..TIP            every merge commit in the range
    tools/ci/no_revert.py --merge COMMIT       one merge commit
    tools/ci/no_revert.py --squash RESULT BRANCH   RESULT (one commit on main) squashes BRANCH
    tools/ci/no_revert.py --self-test

A merge X of A and B (merge base M) must keep both sides' changes: no file A changed since M may come out of X as it
was at M or as B has it (the same for B). Conflict resolutions that take one side's file wholesale, or a
squash made from a branch that started before main moved, undo the other side instead (0.11.1: the quick-remesh
merge nearly reverted main's newer micro-renderer fix). Five or more changed lines thrown away that way (branch notes
and the generated guide don't count) fail the check, listed per file. A deliberate revert passes with SF_ALLOW_REVERT=<its commit>.
Exit 0 ok, 1 refused, 2 usage or git error.
"""
import os
import re
import subprocess
import sys
import tempfile

LIMIT = 5
# Branch notes (merge-batch.sh removes them on purpose) and generated files.
SKIP = re.compile(r"^(REPORT|BRIEF|REPLY|REPLIES|STRIPE|TESTERS)[^/]*$|^website/guide/")


def git(*args, cwd=None):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def tree(commit, cwd=None):
    """{path: blob} of a commit."""
    out = {}
    for line in git("ls-tree", "-r", "-z", commit, cwd=cwd).split("\0"):
        if line:
            meta, path = line.split("\t", 1)
            out[path] = meta.split()[2]
    return out


def size(a, b, path, cwd=None):
    """Lines changed in path from a to b (numstat; a binary file counts as 1000)."""
    out = git("diff", "--numstat", "--no-renames", a, b, "--", path, cwd=cwd).split()
    return 1000 if out[:1] == ["-"] else int(out[0]) + int(out[1]) if out else 0


def undone(bases, side, other, result, cwd=None):
    """{path: lines}: files where side has commits of its own (not in other) whose change result throws away whole:
    result has other's file, or the file as it was at a merge base, or none. A conflict resolved by taking one side's
    file, or a squash of a branch's files onto a main that moved, looks like this; a resolution that combines the
    two doesn't. Files renamed between side and result are left out (their changes went with them)."""
    ts, to, tr = (tree(c, cwd) for c in (side, other, result))
    old = [tree(b, cwd) for b in bases]
    renamed = {l.split("\t")[1] for l in git("diff", "-M", "--name-status", side, result, cwd=cwd).splitlines()
               if l.startswith("R")}
    found = {}
    for p in set(ts) | set().union(*old):
        if SKIP.search(p) or p in renamed or tr.get(p) == ts.get(p):
            continue
        if tr.get(p) != to.get(p) and all(tr.get(p) != t.get(p) for t in old):
            continue  # result has a version of its own: a combined resolution
        # side's own commits on the file (not in other) whose change result lacks: the patch applies to result's file
        # forward and not in reverse (a rebased or cherry-picked copy of it on the other side is no loss)
        n = sum(lost(c, p, result, cwd) for c in git("rev-list", "--no-merges", f"{other}..{side}", "--", p, cwd=cwd).split())
        if n:
            found[p] = n
    return found


def lost(commit, path, result, cwd=None):
    """Lines of commit's change to path that result doesn't have (0 when result has it, or it can't be told)."""
    patch = git("diff", "--no-color", "--no-renames", "--full-index", "--binary", commit + "^", commit, "--", path, cwd=cwd)
    if not patch:
        return 0
    with tempfile.TemporaryDirectory() as d:
        env = dict(os.environ, GIT_INDEX_FILE=os.path.join(d, "index"))
        subprocess.run(["git", "read-tree", result], cwd=cwd, env=env, check=True, capture_output=True)
        check = lambda *a: subprocess.run(["git", "apply", "--cached", "--check", *a], cwd=cwd, env=env, input=patch,
                                          capture_output=True, text=True).returncode == 0
        if check("-R") or not check():
            return 0
    return size(commit + "^", commit, path, cwd)


def report(what, found):
    n = sum(found.values())
    if n < LIMIT:
        return 0
    print(f"{what}: throws away {n} changed lines in {len(found)} file(s) (the result has the other side's or the old file):")
    for p, k in sorted(found.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  {p}: {k}")
    return n


def check_merge(commit, cwd=None):
    sha = git("rev-parse", commit, cwd=cwd).strip()
    parents = git("rev-list", "--parents", "-n1", sha, cwd=cwd).split()[1:]
    if len(parents) < 2:
        return 0
    bad = 0
    bases = git("merge-base", "--all", parents[0], parents[1], cwd=cwd).split()
    subject = git("log", "-1", "--format=%h %s", sha, cwd=cwd).strip()
    for i, (side, other) in enumerate(((parents[0], parents[1]), (parents[1], parents[0]))):
        name = git("log", "-1", "--format=%h", side, cwd=cwd).strip()
        bad += report(f"merge {subject[:70]}: parent {i + 1} ({name})", undone(bases, side, other, sha, cwd))
    return bad and sha


def check_squash(result, branch, cwd=None):
    sha = git("rev-parse", result, cwd=cwd).strip()
    parent = git("rev-parse", sha + "~1", cwd=cwd).strip()
    bases = git("merge-base", "--all", parent, branch, cwd=cwd).split()
    subject = git("log", "-1", "--format=%h %s", sha, cwd=cwd).strip()
    return report(f"squash {subject[:70]} of {branch}: main", undone(bases, parent, branch, sha, cwd)) and sha


def allowed(sha):
    ok = os.environ.get("SF_ALLOW_REVERT", "")
    return bool(ok) and sha.startswith(ok) and len(ok) >= 7


def run(argv, cwd=None):
    if len(argv) == 1 and ".." in argv[0]:
        refused = []
        for c in git("rev-list", "--merges", argv[0], cwd=cwd).split():
            sha = check_merge(c, cwd)
            if sha and not allowed(sha):
                refused.append(sha)
    elif len(argv) == 2 and argv[0] == "--merge":
        sha = check_merge(argv[1], cwd)
        refused = [sha] if sha and not allowed(sha) else []
    elif len(argv) == 3 and argv[0] == "--squash":
        sha = check_squash(argv[1], argv[2], cwd)
        refused = [sha] if sha and not allowed(sha) else []
    else:
        print(__doc__)
        return 2
    for sha in refused:
        print(f"refused: {sha[:10]}. Redo it keeping both sides; a deliberate revert: SF_ALLOW_REVERT={sha[:10]}")
    return 1 if refused else 0


def self_test():
    with tempfile.TemporaryDirectory() as d:
        g = lambda *a: git("-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgsign=false", *a, cwd=d)
        g("init", "-q", "-b", "main")
        g("config", "core.autocrlf", "false")  # Windows' git would convert the files written below

        def write(name, lines):
            with open(os.path.join(d, name), "w") as f:
                f.write("".join(l + "\n" for l in lines))

        old = [f"int old_{i} = {i};" for i in range(10)]
        write("a.cpp", old)
        write("b.cpp", ["int b = 1;"])
        g("add", "-A"); g("commit", "-qm", "base")
        g("branch", "feat")
        # main moves on: a fix of 8 lines in a.cpp
        fix = old[:2] + [f"int fixed_{i} = {i};" for i in range(8)]
        write("a.cpp", fix)
        g("commit", "-qam", "main: the fix")
        # the branch: its own feature in b.cpp, plus an unrelated small edit in a.cpp
        g("checkout", "-q", "feat")
        write("b.cpp", ["int b = 1;", "int feature_one = 1;", "int feature_two = 2;"])
        write("REPORT.md", ["READY abc1234 notes for the coordinator"])
        g("add", "-A"); g("commit", "-qm", "feat")
        feat = g("rev-parse", "HEAD").strip()
        # 1. a clean merge keeps both: passes, even though the notes go afterwards
        g("checkout", "-q", "main")
        g("merge", "-q", "--no-ff", "-m", "Merge feat", "feat")
        g("rm", "-q", "REPORT.md"); g("commit", "-qm", "Remove branch notes")
        assert run(["HEAD~3..HEAD"], d) == 0, "a clean merge was refused"
        # 2. a merge whose "resolution" took the branch's a.cpp: refused, and passes when allowed by its sha
        g("reset", "-q", "--hard", "HEAD~2")
        g("merge", "-q", "--no-ff", "--no-commit", "feat")
        write("a.cpp", old)
        g("add", "-A"); g("commit", "-qm", "Merge feat (bad resolution)")
        bad = g("rev-parse", "HEAD").strip()
        assert run(["--merge", "HEAD"], d) == 1, "a merge that reverted main's fix passed"
        assert run(["HEAD~2..HEAD"], d) == 1, "the range check missed the bad merge"
        os.environ["SF_ALLOW_REVERT"] = bad[:10]
        try:
            assert run(["HEAD~2..HEAD"], d) == 0, "SF_ALLOW_REVERT didn't allow that merge"
        finally:
            del os.environ["SF_ALLOW_REVERT"]
        # 3. a squash made from the branch's whole tree on top of the moved main: refused; one from its diff: passes
        g("reset", "-q", "--hard", "HEAD~1")
        g("checkout", "-q", feat, "--", ".")
        g("commit", "-qm", "Squash feat (from the branch's files)")
        assert run(["--squash", "HEAD", "feat"], d) == 1, "a squash that reverted main's fix passed"
        g("reset", "-q", "--hard", "HEAD~1")
        g("merge", "-q", "--squash", "feat"); g("commit", "-qm", "Squash feat")
        assert run(["--squash", "HEAD", "feat"], d) == 0, "a clean squash was refused"
        # 4. main deleted lines; a merge that brings them back is refused too
        g("checkout", "-q", "-b", "feat2", "HEAD~1")
        write("c.cpp", ["int c = 3;"]); g("add", "-A"); g("commit", "-qm", "feat2")
        g("checkout", "-q", "main")
        write("a.cpp", fix[:2]); g("commit", "-qam", "main deletes the fixed lines")
        g("merge", "-q", "--no-ff", "--no-commit", "feat2")
        write("a.cpp", fix); g("add", "-A"); g("commit", "-qm", "Merge feat2 (brings deleted lines back)")
        assert run(["--merge", "HEAD"], d) == 1, "a merge that restored main's deleted lines passed"
    print("no_revert self-test: ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        import contextlib, io
        with contextlib.redirect_stdout(io.StringIO()) as quiet:
            try:
                self_test()
            except AssertionError:
                sys.stdout = sys.__stdout__
                print(quiet.getvalue())
                raise
        print("no_revert self-test: ok")
        sys.exit(0)
    try:
        sys.exit(run(sys.argv[1:]))
    except subprocess.CalledProcessError as e:
        print(f"git {' '.join(e.cmd[1:])}: {e.stderr.strip()}")
        sys.exit(2)
