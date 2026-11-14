#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that what a release says up front names only what the release carries (release.sh steps stage and check).

    tools/release/notes_names.py X.Y.Z [--launch FILE] [--highlights FILE]
    tools/release/notes_names.py --self-test

The release's own notes are the truth: its CHANGELOG.md section and its docs/guide/whats-new.md section. The launch
headline (state/launch/X.Y.Z.json "line": the GitHub release's first line), the README's "New in X.Y.Z" line and the
HIGHLIGHTS lines (update feed, Discord) may only name an issue (#123) or a capitalised product word (Voxel Remesh, Tube,
PBR) that those notes name too. 0.13.1: a fix pulled after the headline was written still led the release and the
README. Exit 1 lists each one, so a pulled fix stops the release instead of being announced.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# Capitalised words any line may use: the product, sentence starts the regex can't see, and plain English.
ALLOWED = {"SculptForge", "I", "Members", "Android", "Windows", "Linux", "macOS"}


def section(text, start, stop):
    """The lines from START (a regex for the section heading) to the next STOP heading."""
    m = re.search(start, text, re.M)
    if not m:
        return ""
    rest = text[m.end():]
    n = re.search(stop, rest, re.M)
    return rest[: n.start()] if n else rest


def truth(version, root=ROOT):
    v = re.escape(version)
    cl = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    wn = (root / "docs/guide/whats-new.md").read_text(encoding="utf-8")
    return section(cl, rf"^## \[{v}\]", r"^## \[") + "\n" + section(wn, rf"^## Version {v}\s*$", r"^## Version ")


def readme_line(version, root=ROOT):
    p = root / "README.md"
    if not p.exists():
        return ""
    m = re.search(rf"New in {re.escape(version)}:</strong>(.*)", p.read_text(encoding="utf-8"))
    return re.sub(r"<[^>]+>|\(.*?\)", " ", m.group(1)) if m else ""


def names(text):
    """Issue refs and capitalised words that don't start the text or a sentence/clause."""
    refs = set(re.findall(r"#\d+", text))
    words = set()
    for m in re.finditer(r"(?<![A-Za-z0-9])[A-Z][A-Za-z0-9+]*(?:[- ][A-Z][A-Za-z0-9+]*)*", text):
        before = text[: m.start()].rstrip()
        if not before or before[-1] in ".:!?*\"(":
            continue
        for w in re.split(r"[- ]", m.group(0)):
            if w and w not in ALLOWED:
                words.add(w)
    return refs, words


def problems(version, texts, root=ROOT):
    t = truth(version, root)
    if not t.strip():
        return []  # no notes yet (before the bump): nothing to hold the lines to
    out = []
    for where, text in texts:
        refs, words = names(text)
        for r in sorted(refs):
            if not re.search(re.escape(r) + r"\b", t):
                out.append(f"{where} names {r}, which the {version} notes don't")
        for w in sorted(words):
            if not re.search(rf"\b{re.escape(w)}\b", t):
                out.append(f"{where} names \"{w}\", which the {version} notes don't")
    return out


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    version = argv[1]
    launch = highlights = None
    if "--launch" in argv:
        launch = argv[argv.index("--launch") + 1]
    if "--highlights" in argv:
        highlights = argv[argv.index("--highlights") + 1]
    texts = []
    if launch and Path(launch).is_file():
        try:
            texts.append(("the launch headline", str(json.loads(Path(launch).read_text())["line"])))
        except (ValueError, KeyError, TypeError):
            pass
    r = readme_line(version)
    if r.strip():
        texts.append(("README's New in line", r))
    if highlights and Path(highlights).is_file():
        for i, line in enumerate(l for l in Path(highlights).read_text(encoding="utf-8").splitlines() if l.strip()):
            texts.append((f"HIGHLIGHTS line {i + 1}", line))
    out = problems(version, texts)
    for p in out:
        print(p)
    return 1 if out else 0


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "docs/guide").mkdir(parents=True)
        (root / "CHANGELOG.md").write_text(
            "## [Unreleased]\n\n## [9.0.1] - 2026-01-01\n\n### Fixed\n- Moving Tube points (Fixes #215) closed the app.\n\n"
            "## [9.0.0] - 2025-12-01\n\n- **Voxel Remesh** keeps Clay smooth (#193).\n")
        (root / "docs/guide/whats-new.md").write_text(
            "## Version 9.0.1\n\ntagline: Long Tubes edit without crashing.\n\n## Version 9.0.0\n\n- Voxel Remesh.\n")
        # the 0.13.1 case: a pulled fix in the headline
        bad = problems("9.0.1", [("the launch headline",
                                  "Moving the points of a long Tube no longer closes the app, and Clay strokes stay smooth after a Voxel Remesh")], root)
        assert any('"Voxel"' in p for p in bad) and any('"Remesh"' in p for p in bad) and any('"Clay"' in p for p in bad), bad
        assert not any('"Tube"' in p for p in bad), bad
        assert problems("9.0.1", [("x", "Fixes #193 and the Tube crash")], root), "an issue of another version passed"
        assert not problems("9.0.1", [("x", "Moving the points of a long Tube no longer closes the app (#215)")], root)
        # sentence starts and the allowed words are not names
        assert not problems("9.0.1", [("x", "Long Tubes edit. Windows and SculptForge: Moving points")], root)
        assert not problems("9.0.1", [("x", "Recommended 3D models and the Tube")], root), "a capital inside 3D counted"
        # no notes for the version yet: no verdict
        assert problems("9.9.9", [("x", "Voxel Remesh")], root) == []
    print("notes_names: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
