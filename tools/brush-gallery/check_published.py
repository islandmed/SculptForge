#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails when the public gallery repository (the app's default community gallery) differs from repo/ here.

The app and brushes.html read https://github.com/islandmed/sculptforge-brushes; repo/ is its source. A rename here
that never reached it left a third-party mark live for weeks, so release.sh runs this. Compares git blob hashes of
every file (no clone), then runs check_marks' patterns over repo/'s file names and text.

    python3 check_published.py            (network; release.sh preflight)
    python3 check_published.py --self-test
Standard library only.
"""

import hashlib
import json
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE / "repo"
PUBLIC = "islandmed/sculptforge-brushes"
sys.path.insert(0, str(HERE.parent))
import check_marks  # noqa: E402


def blob_sha(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def local_tree():
    return {p.relative_to(REPO).as_posix(): blob_sha(p.read_bytes()) for p in sorted(REPO.rglob("*")) if p.is_file()}


def public_tree():
    url = f"https://api.github.com/repos/{PUBLIC}/git/trees/main?recursive=1"
    with urllib.request.urlopen(urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"}), timeout=30) as r:
        tree = json.load(r)["tree"]
    return {t["path"]: t["sha"] for t in tree if t["type"] == "blob"}


def differences(local, public):
    out = [f"{p}: only on {PUBLIC}" for p in sorted(public.keys() - local.keys())]
    out += [f"{p}: not published to {PUBLIC}" for p in sorted(local.keys() - public.keys())]
    out += [f"{p}: differs from {PUBLIC}" for p in sorted(local.keys() & public.keys()) if local[p] != public[p]]
    return out


def marks(mark_re):
    out = []
    for p in sorted(REPO.rglob("*")):
        rel = p.relative_to(REPO).as_posix()
        if mark_re.search(rel):
            out.append(f"{rel}: the file name has a mark")
        data = p.read_bytes() if p.is_file() else b""
        if data and b"\0" not in data[:8192] and mark_re.search(data.decode("utf-8", "replace")):
            out.append(f"{rel}: has a mark")
    return out


def main(argv):
    if "--self-test" in argv:
        assert blob_sha(b"") == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"  # git hash-object /dev/null
        assert differences({"a": "1", "b": "2"}, {"a": "1", "b": "3", "c": "4"}) == [
            f"c: only on {PUBLIC}", f"b: differs from {PUBLIC}"]
        assert not marks(check_marks.load_marks())
        print("check_published self-test ok")
        return 0
    problems = marks(check_marks.load_marks()) + differences(local_tree(), public_tree())
    for p in problems:
        print(p)
    if problems:
        print(f"\nthe public gallery differs from tools/brush-gallery/repo: copy repo/ into a clone of {PUBLIC}, "
              "commit, push its main", file=sys.stderr)
        return 1
    print(f"check_published: {PUBLIC} matches tools/brush-gallery/repo, no marks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
