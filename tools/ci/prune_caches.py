#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keeps the newest GitHub Actions cache of each kind per branch and deletes the older ones.

The repository gets 10 GB of cache. Every push to main saves a compiler cache per job (ccache-ci-<job>.<commit>,
ccache-pkg-<job>.<commit>) and a configure cache per job (cmake-ci-<job>~<image, Python, CMake files>), about 5 GB in
all, so without pruning each push evicts the previous one's and branch runs start cold (8-13 min builds instead of
about 1). Only the newest entry of a kind can be restored by a later run anyway (restore-keys pick the newest).

  tools/ci/prune_caches.py [--dry-run] PREFIX...   e.g. ccache-ci- cmake-ci-

Needs GH_TOKEN with actions: write and GITHUB_REPOSITORY (both set in a workflow job).
"""
import json
import os
import re
import subprocess
import sys


def kind(key):
    """ccache-ci-blender-4.2.0.<40 hex> -> ccache-ci-blender-4.2.0; cmake-ci-windows~win22-... -> cmake-ci-windows."""
    return re.sub(r"(\.[0-9a-f]{40}|~.*)$", "", key)


def stale(caches, prefixes):
    """The caches to delete: all but the newest of each (branch, kind) whose key starts with one of the prefixes."""
    newest = {}
    for c in caches:
        if not c["key"].startswith(tuple(prefixes)):
            continue
        group = (c["ref"], kind(c["key"]))
        if group not in newest or c["created_at"] > newest[group]["created_at"]:
            newest[group] = c
    keep = {c["id"] for c in newest.values()}
    return [c for c in caches if c["key"].startswith(tuple(prefixes)) and c["id"] not in keep]


def main(argv):
    dry = "--dry-run" in argv
    prefixes = [a for a in argv if a != "--dry-run"]
    if not prefixes:
        print(__doc__)
        return 2
    if prefixes == ["--self-test"]:
        return self_test()
    repo = os.environ["GITHUB_REPOSITORY"]
    out = subprocess.run(["gh", "api", "--paginate", f"repos/{repo}/actions/caches?per_page=100",
                          "--jq", ".actions_caches[]"], check=True, capture_output=True, text=True).stdout
    caches = [json.loads(line) for line in out.splitlines() if line.strip()]
    for c in stale(caches, prefixes):
        print(f"delete {c['key']} ({c['ref']}, {c['size_in_bytes'] >> 20} MB, {c['created_at']})")
        if not dry:
            subprocess.run(["gh", "api", "-X", "DELETE", f"repos/{repo}/actions/caches/{c['id']}"], check=False)
    return 0


def self_test():
    sha1, sha2 = "a" * 40, "b" * 40
    caches = [
        {"id": 1, "key": f"ccache-ci-windows.{sha1}", "ref": "refs/heads/main", "created_at": "2026-11-02T15:00:00Z"},
        {"id": 2, "key": f"ccache-ci-windows.{sha2}", "ref": "refs/heads/main", "created_at": "2026-11-02T16:00:00Z"},
        {"id": 3, "key": f"ccache-ci-blender-4.2.0.{sha1}", "ref": "refs/heads/main", "created_at": "2026-11-02T15:00:00Z"},
        {"id": 4, "key": f"ccache-ci-windows.{sha1}", "ref": "refs/heads/feat/x", "created_at": "2026-11-02T15:30:00Z"},
        {"id": 5, "key": "cmake-ci-windows~win22-20261026.1-py3.13.17-abc", "ref": "refs/heads/main", "created_at": "2026-11-01T10:00:00Z"},
        {"id": 6, "key": "cmake-ci-windows~win22-20261026.1-py3.13.17-def", "ref": "refs/heads/main", "created_at": "2026-11-02T10:00:00Z"},
        {"id": 7, "key": f"ccache-pkg-windows.{sha1}", "ref": "refs/heads/main", "created_at": "2026-11-01T10:00:00Z"},
    ]
    got = sorted(c["id"] for c in stale(caches, ["ccache-ci-", "cmake-ci-"]))
    assert got == [1, 5], got  # newest per (branch, kind) kept; other prefixes untouched
    assert kind(f"ccache-ci-blender-4.2.0.{sha1}") == "ccache-ci-blender-4.2.0"
    print("prune_caches self-test ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
