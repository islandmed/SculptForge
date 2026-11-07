#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The build-input key of a commit: one SHA-256 of exactly what goes into its release packages (docs/releasing.md,
"Build once, promote").

  build_key.py [SHA ...]      prints "KEY SHA" per commit (default HEAD); exit 1 if a commit isn't in the repository
  build_key.py --self-test

The key hashes `git ls-tree -r` of the commit restricted to the build inputs (tools/release/build_inputs.txt): each
input file's mode, object id and path, so two commits get the same key exactly when every input file is byte for
byte the same (and none was added, removed or renamed). It reads git objects only, never the working tree.
packages.yml (job reuse) and tools/release/staged_run.py use it to promote one green Packages run for every commit
with the same key: a change to the website or the release paperwork never builds the packages again.
"""
import hashlib
import os
import re
import subprocess
import sys

RULES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build_inputs.txt")
SCHEMA = b"sculptforge build inputs v1\n"


def load_rules(path=RULES):
    """[(is_input, regex)] in file order; a missing or empty file means every file is an input (fail safe)."""
    with open(path, encoding="utf-8") as f:
        lines = [l.strip() for l in f if l.strip() and not l.lstrip().startswith("#")]
    return [(l.startswith("!"), re.compile(l[1:] if l.startswith("!") else l)) for l in lines]


def is_input(path, rules):
    keep = True
    for again, rx in rules:
        if rx.search(path):
            keep = again
    return keep


def key_of_tree(entries, rules):
    """entries: [(mode, type, object id, path)] as git ls-tree -r lists them (sorted by path)."""
    h = hashlib.sha256(SCHEMA)
    for mode, kind, oid, path in entries:
        if is_input(path, rules):
            h.update(f"{mode} {kind} {oid}\t{path}\0".encode())
    return h.hexdigest()


def ls_tree(sha, cwd=None):
    """The commit's files, or None when the commit isn't here."""
    p = subprocess.run(["git", "ls-tree", "-r", "-z", "--full-tree", f"{sha}^{{commit}}"], cwd=cwd, capture_output=True)
    if p.returncode != 0:
        return None
    out = []
    for rec in p.stdout.decode("utf-8", "surrogateescape").split("\0"):
        if rec:
            meta, path = rec.split("\t", 1)
            mode, kind, oid = meta.split(" ")
            out.append((mode, kind, oid, path))
    return out


def key(sha, rules=None, cwd=None):
    entries = ls_tree(sha, cwd)
    return None if entries is None else key_of_tree(entries, rules if rules is not None else load_rules())


def self_test():
    import tempfile
    bad = []
    rules = load_rules()
    expect = {"src/app/Main.cpp": True, "CMakeLists.txt": True, "website/queue.html": False, "website/link/index.html": True,
              "website/founding.json": True, "website/founding.json.bak": False, "packaging/arch/PKGBUILD": False,
              "packaging/flatpak/x.yml": True, "scripts/release.sh": False, "scripts/merge-queue.sh": True,
              ".github/workflows/packages.yml": True, ".github/workflows/pages.yml": False, "docs/releasing.md": False,
              "docs/guide/index.md": True, "docs/supporters.txt": True, "tools/release/build_inputs.txt": True}
    bad += [f"is_input {p}: want {w}" for p, w in expect.items() if is_input(p, rules) != w]
    base = [("100644", "blob", "a" * 40, "CMakeLists.txt"), ("100644", "blob", "b" * 40, "website/queue.html")]
    k = key_of_tree(base, rules)
    variants = {
        "site edit": ([base[0], ("100644", "blob", "c" * 40, "website/queue.html")], True),
        "site file added": (base + [("100644", "blob", "c" * 40, "website/x.html")], True),
        "input edit": ([("100644", "blob", "c" * 40, "CMakeLists.txt"), base[1]], False),
        "input mode": ([("100755", "blob", "a" * 40, "CMakeLists.txt"), base[1]], False),
        "input added": (base + [("100644", "blob", "c" * 40, "src/x.cpp")], False),
        "input removed": (base[1:], False),
        "input renamed": ([("100644", "blob", "a" * 40, "CMakeList.txt"), base[1]], False),
        "app input in site": (base + [("100644", "blob", "c" * 40, "website/founding.json")], False),
    }
    bad += [f"key {n}: same={key_of_tree(e, rules) == k}" for n, (e, same) in variants.items() if (key_of_tree(e, rules) == k) != same]
    if load_rules(os.devnull) != [] or not is_input("website/x", []):
        bad.append("no rules: every file must be an input")
    with tempfile.TemporaryDirectory() as d:
        env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
               "GIT_COMMITTER_EMAIL": "t@t", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
        g = lambda *a: subprocess.run(["git", *a], cwd=d, env=env, check=True, capture_output=True, text=True).stdout.strip()
        g("init", "-q")
        os.makedirs(os.path.join(d, "website"))
        for name, text in {"CMakeLists.txt": "v1", "website/queue.html": "q1", "a b.txt": "x"}.items():
            with open(os.path.join(d, name), "w") as f:
                f.write(text)
        g("add", "-A")
        g("commit", "-qm", "one")
        one = g("rev-parse", "HEAD")
        with open(os.path.join(d, "website/queue.html"), "w") as f:
            f.write("q2")
        g("commit", "-qam", "site")
        two = g("rev-parse", "HEAD")
        with open(os.path.join(d, "CMakeLists.txt"), "w") as f:
            f.write("v2")
        g("commit", "-qam", "app")
        three = g("rev-parse", "HEAD")
        k1, k2, k3 = (key(s, rules, d) for s in (one, two, three))
        if not (k1 and k1 == k2 and k2 != k3):
            bad.append(f"git: site-only commit must keep the key, an app commit change it ({k1} {k2} {k3})")
        if key("f" * 40, rules, d) is not None:
            bad.append("git: a missing commit must give None")
    for b in bad:
        print("self-test FAIL", b)
    print("self-test", "failed" if bad else "ok")
    return 1 if bad else 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    rules, rc = load_rules(), 0
    for sha in sys.argv[1:] or ["HEAD"]:
        k = key(sha, rules)
        if k is None:
            print(f"build_key.py: {sha}: no such commit here", file=sys.stderr)
            rc = 1
        else:
            print(k, subprocess.run(["git", "rev-parse", f"{sha}^{{commit}}"], capture_output=True, text=True).stdout.strip())
    return rc


if __name__ == "__main__":
    sys.exit(main())
