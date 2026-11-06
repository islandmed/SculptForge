#!/usr/bin/env python3
"""Fails when a third-party sculpting-app trademark appears anywhere in the repository (docs/legal/trademarks.md).

Every git-tracked text file and every tracked path is scanned, case-insensitively, for the patterns listed in
docs/legal/trademarks.md, the one file allowed to spell them (this script doesn't). No other exceptions.

  check_marks.py            check the repository (ctest: marks; CI job "Trademarks")
  check_marks.py --count    print matches per pattern and per top-level folder
  check_marks.py --self-test
"""
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRADEMARKS = "docs/legal/trademarks.md"
SKIP_PREFIXES = ("third_party/",)  # vendored code, as its authors wrote it


def load_marks(text=None):
    """The patterns between <!-- marks:begin --> and <!-- marks:end --> (and labels:begin / labels:end) in
    docs/legal/trademarks.md."""
    if text is None:
        text = (ROOT / TRADEMARKS).read_text(encoding="utf-8")
    marks = []
    for name in ("marks", "labels"):  # the marks, then their interface's exact labels (#117)
        if f"<!-- {name}:begin -->" in text:
            block = text.split(f"<!-- {name}:begin -->", 1)[1].split(f"<!-- {name}:end -->", 1)[0]
            marks += [line.strip() for line in block.splitlines() if line.strip()]
    if not marks:
        raise SystemExit(f"check_marks: no patterns in {TRADEMARKS}")
    return re.compile("|".join(f"(?:{m})" for m in marks), re.IGNORECASE)


def old_words(text=None):
    """(old, today) pairs from the table of old words in docs/legal/trademarks.md."""
    if text is None:
        text = (ROOT / TRADEMARKS).read_text(encoding="utf-8")
    return re.findall(r"^\| W\d+ \| `([^`]+)` \| `([^`]+)` \|", text, re.M)


def tracked_files():
    """Tracked files, or None when git can't list them."""
    # safe.directory: CI containers check the repository out as another user ("dubious ownership").
    r = subprocess.run(["git", "-c", "safe.directory=*", "ls-files", "-z"], cwd=ROOT, capture_output=True)
    if r.returncode != 0:
        return None
    return [p for p in r.stdout.decode("utf-8").split("\0") if p and not p.startswith(SKIP_PREFIXES)]


def read_text(path):
    try:
        data = (ROOT / path).read_bytes()
    except OSError:
        return None  # deleted in the working tree
    if b"\0" in data[:8192]:
        return None  # binary
    return data.decode("utf-8", errors="replace")


# A string literal that goes on in the next one ("...Store "\n    "Config..."): C, C++ and Python join them.
LITERAL_BREAK = re.compile(r'"[ \t]*\r?\n[ \t]*"')


def problems_in(text, mark_re):
    """(line, snippet) for every line with a mark, and for a mark split across adjacent string literals."""
    lines = text.splitlines()
    found = [(n, line.strip()) for n, line in enumerate(lines, 1) if mark_re.search(line)]
    joined, starts, pos = [], [], 0  # the text with the breaks taken out, and where each piece began
    for m in LITERAL_BREAK.finditer(text):
        joined.append(text[pos:m.start()])
        starts.append(pos)
        pos = m.end()
    if not starts:
        return found
    joined.append(text[pos:])
    starts.append(pos)
    flat, seen = "".join(joined), {n for n, _ in found}
    offsets = [0]
    for piece in joined[:-1]:
        offsets.append(offsets[-1] + len(piece))
    for m in mark_re.finditer(flat):
        k = max(i for i, o in enumerate(offsets) if o <= m.start())
        n = text.count("\n", 0, starts[k] + m.start() - offsets[k]) + 1
        if n not in seen:
            seen.add(n)
            found.append((n, lines[n - 1].strip()))
    return sorted(found)


def scan(mark_re):
    errors = []
    for path in tracked_files():
        if path == TRADEMARKS:
            continue
        if mark_re.search(path):
            errors.append(f"{path}: the file name has a mark")
        text = read_text(path)
        if text is None:
            continue
        for n, snippet in problems_in(text, mark_re):
            errors.append(f"{path}:{n}: {snippet[:160]}")
    return errors


def count(mark_re):
    per_mark, per_dir = Counter(), Counter()
    for path in tracked_files():
        text = read_text(path)
        if text is None or path == TRADEMARKS:
            continue
        for m in mark_re.finditer(text):
            per_mark[m.group(0).lower().replace(" ", "")] += 1
            per_dir[path.split("/", 1)[0] if "/" in path else "(root)"] += 1
    print(f"total {sum(per_mark.values())}")
    for k, v in per_dir.most_common():
        print(f"  {v:6} {k}/")
    for k, v in per_mark.most_common():
        print(f"  {v:6} {k}")


def self_test():
    mark_re = load_marks()
    words = old_words()
    assert len(words) >= 12, "the table of old words in docs/legal/trademarks.md is missing"
    for old, today in words:
        assert mark_re.search(old) and mark_re.search("x." + old.upper() + " count"), old
        assert not mark_re.search(today), today
    assert not mark_re.search("Lit Sphere, Part, Face group, Library, Vertex Paint, Bridge, Forge Spheres, Classic")
    assert not mark_re.search("immediate dimmed trimmed Gozo, format capability, parts_list, bridge_send")
    # their labels (#117), split here because this file is scanned too
    assert all(mark_re.search(a + b) for a, b in zip(("press Store", "the Focal", "Lower"), (" Config", " Shift slider", " Res")))
    assert not mark_re.search("drawSize focalShift_ RgbIntensity config.store subdiv_lower projectAll lower resolution")
    assert not mark_re.search("Save Layout, Size, Intensity, Colour intensity, Falloff, Lower Level, Lasso Mask")
    assert problems_in("a\n" + words[0][0] + " b\nc", mark_re) == [(2, words[0][0] + " b")]
    assert problems_in('x\n  "press Store "\n  "Config keeps it"', mark_re) == [(2, '"press Store "')]
    assert load_marks("x <!-- marks:begin -->\n  ab?c\n<!-- marks:end -->").search("AC")
    print("check_marks self-test ok")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    mark_re = load_marks()
    if tracked_files() is None:
        print("check_marks: skipped, git can't list the tracked files here")
        return 0
    if "--count" in argv:
        count(mark_re)
        return 0
    errors = scan(mark_re)
    for e in errors:
        print(e)
    if errors:
        print(f"\n{len(errors)} trademark problem(s). Rename by function (docs/legal/trademarks.md); "
              "there are no exceptions outside that file.", file=sys.stderr)
        return 1
    print("check_marks: no marks outside docs/legal/trademarks.md")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
