#!/usr/bin/env python3
"""Fails when a third-party sculpting-app trademark appears anywhere in the repository (docs/legal/trademarks.md).

Every git-tracked file and path is scanned, case-insensitively, for the patterns listed in docs/legal/trademarks.md,
the one file allowed to spell them (this script doesn't). No other exceptions. A space in a pattern also matches a
hyphen, underscore, dot or several spaces, and words joined by - _ . are also read squashed ("a-b_c" as "abc"), so
spelling variants hit too. Binary files: zip archives member by member (names and contents, zlib-compressed ones
unpacked first) and the strings in SculptForge's own formats; images, video and fonts are not read.
discord.py (the coordinator's posts) uses marks_in() from here, so the two can't disagree.

  check_marks.py            check the repository (ctest: marks; CI job "Trademarks")
  check_marks.py --count    print matches per pattern and per top-level folder
  check_marks.py --self-test
"""
import io
import re
import subprocess
import sys
import zipfile
import zlib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRADEMARKS = "docs/legal/trademarks.md"
SKIP_PREFIXES = ("third_party/",)  # vendored code, as its authors wrote it
SEPARATOR = r"(?:[ \t]+|[-_.])"  # what a space in a pattern stands for
# Binary files of SculptForge's own formats, read as their printable strings (zip archives are read whatever the name).
OWN_BINARY = (".sfproj", ".sfbrush", ".sfmaterial", ".sfpack", ".sfmacro", ".bridge", ".z", ".bin", ".json")
MIN_STRING = 4  # shortest printable run read from a binary file


def load_marks(text=None):
    """The patterns between <!-- marks:begin --> and <!-- marks:end --> (and labels:begin / labels:end) in
    docs/legal/trademarks.md."""
    if text is None:
        text = (ROOT / TRADEMARKS).read_text(encoding="utf-8")
    marks = []
    for name in ("marks", "labels"):  # the marks, then their interface's exact labels (#117)
        if f"<!-- {name}:begin -->" in text:
            block = text.split(f"<!-- {name}:begin -->", 1)[1].split(f"<!-- {name}:end -->", 1)[0]
            lines = [line.strip() for line in block.splitlines() if line.strip()]
            # A mark's space also stands for - _ . or several spaces; a label's stays a space (ids like draw_size).
            marks += [m.replace(" ", SEPARATOR) for m in lines] if name == "marks" else lines
    if not marks:
        raise SystemExit(f"check_marks: no patterns in {TRADEMARKS}")
    return re.compile("|".join(f"(?:{m})" for m in marks), re.IGNORECASE)


def fnv1a64(word):
    """src/core/LegacyNames.h's hash: FNV-1a 64 of the lower-cased bytes."""
    h = 0xCBF29CE484222325
    for b in word.lower().encode():
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def squash(word):
    """A name as LegacyNames.h and brush lookup compare it: lower case, no spaces, hyphens, underscores or dots."""
    return re.sub(r"[\s_.\-]+", "", word).lower()


def old_words(text=None, hashes=False):
    """(old, today) pairs from the table of old words in docs/legal/trademarks.md ((old, today, hash) with hashes)."""
    if text is None:
        text = (ROOT / TRADEMARKS).read_text(encoding="utf-8")
    rows = re.findall(r"^\| W\d+ \| `([^`]+)` \| `([^`]+)` \| `0x([0-9A-F]+)` \|", text, re.M)
    return [(o, t, int(h, 16)) for o, t, h in rows] if hashes else [(o, t) for o, t, _ in rows]


def tracked_files():
    """Tracked files, or None when git can't list them."""
    # safe.directory: CI containers check the repository out as another user ("dubious ownership").
    r = subprocess.run(["git", "-c", "safe.directory=*", "ls-files", "-z"], cwd=ROOT, capture_output=True)
    if r.returncode != 0:
        return None
    return [p for p in r.stdout.decode("utf-8").split("\0") if p and not p.startswith(SKIP_PREFIXES)]


def is_binary(data):
    return b"\0" in data[:8192]


def binary_strings(data):
    """The printable ASCII runs in binary data, one per line."""
    return "\n".join(m.decode("ascii") for m in re.findall(rb"[\x20-\x7e]{%d,}" % MIN_STRING, data))


def texts_in(name, data, depth=0):
    """(where, text) for what a file holds that a person or program could read as words: the text of a text file;
    for a zip archive each member's name and contents; for a zlib stream what it unpacks to; for SculptForge's
    other binary formats their strings. Media files give nothing."""
    if not is_binary(data):
        return [(name, data.decode("utf-8", errors="replace"))]
    if depth > 2:
        return []
    if data[:4] == b"PK\x03\x04":
        try:
            z = zipfile.ZipFile(io.BytesIO(data))
            out = [(name, "\n".join(z.namelist()))]
            for member in z.namelist():
                out += texts_in(f"{name}!{member}", z.read(member), depth + 1)
            return out
        except (zipfile.BadZipFile, OSError, RuntimeError, NotImplementedError, zlib.error):
            pass
    if data[:1] == b"\x78":
        try:
            return texts_in(name, zlib.decompress(data), depth + 1)
        except zlib.error:
            pass
    if name.lower().endswith(OWN_BINARY):
        return [(name, binary_strings(data))]
    return []


def read_texts(path):
    try:
        data = (ROOT / path).read_bytes()
    except OSError:
        return []  # deleted in the working tree
    return texts_in(path, data)


# Words joined by hyphens, underscores or dots, read squashed as well ("a-b_c" as "abc").
JOINED = re.compile(r"[A-Za-z0-9]+(?:[-_.][A-Za-z0-9]+)+")


def has_mark(line, mark_re):
    return bool(mark_re.search(line)) or any(mark_re.search(squash(m.group(0))) for m in JOINED.finditer(line))


# A string literal that goes on in the next one ("...Store "\n    "Config..."): C, C++ and Python join them.
LITERAL_BREAK = re.compile(r'"[ \t]*\r?\n[ \t]*"')


def problems_in(text, mark_re):
    """(line, snippet) for every line with a mark, and for a mark split across adjacent string literals."""
    lines = text.splitlines()
    found = [(n, line.strip()) for n, line in enumerate(lines, 1) if has_mark(line, mark_re)]
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


def marks_in(text, mark_re=None):
    """(line, snippet) for every line of `text` with a mark: what this check and discord.py refuse."""
    return problems_in(text, mark_re or load_marks())


def scan(mark_re):
    errors = []
    for path in tracked_files():
        if path == TRADEMARKS:
            continue
        if has_mark(path, mark_re):
            errors.append(f"{path}: the file name has a mark")
        for where, text in read_texts(path):
            for n, snippet in problems_in(text, mark_re):
                errors.append(f"{where}:{n}: {snippet[:160]}")
    return errors


def count(mark_re):
    per_mark, per_dir = Counter(), Counter()
    for path in tracked_files():
        if path == TRADEMARKS:
            continue
        for m in (m for _, text in read_texts(path) for m in mark_re.finditer(text)):
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
    for old, today, h in old_words(hashes=True):
        assert mark_re.search(old) and mark_re.search("x." + old.upper() + " count"), old
        assert not has_mark(today, mark_re) and not has_mark(today.title(), mark_re), today
        assert fnv1a64(squash(old)) == h, f"{old}: the hash in the table isn't LegacyNames.h's"
    # Spelling variants of every multi-word mark: spaced, hyphenated, underscored, dotted, camelCase, upper case.
    # (pairs, as the labels below, because this file is scanned too)
    for a, b in (("cl", "ay buildup"), ("da", "m standard"), ("po", "ly paint"), ("su", "b tool"), ("li", "ght box"),
                 ("ma", "t cap"), ("z", " brush"), ("z", " remesher"), ("dy", "na mesh"), ("po", "ly group"),
                 ("z", " sphere"), ("fi", "ber mesh")):
        a, b = (a + b).split(" ")
        for v in (f"{a} {b}", f"{a}  {b}", f"{a}-{b}", f"{a}_{b}", f"{a}.{b}", a.title() + b.title(),
                  a + b.title(), f"{a.title()} {b.title()}", f"{a}-{b}".upper(), f"the {a.title()}-{b.title()} brush"):
            assert has_mark(v, mark_re), v
            assert marks_in("ok\n" + v, mark_re) == [(2, v)], v
    for a, b in (("pi", "xo-logic"), ("Scu", "lpt_Ris"), ("mu", "d.box"), ("na", "no-mesh"), ("spo", "t-light")):
        assert has_mark(a + b, mark_re), a + b  # one-word marks split anywhere
    # Squashing doesn't invent marks out of ordinary text.
    assert not has_mark("set max on the slider; go zone; lit-sphere, clay-block, sharp_crease, vertex.paint", mark_re)
    # Binary files: zip members (names and contents), zlib streams, strings of our formats; not media.
    zbuf = io.BytesIO()
    with zipfile.ZipFile(zbuf, "w") as z:
        z.writestr("manifest.json", '{"name": "Clay ' + 'Buildup"}')
        z.writestr("parts/0/positions.bin", b"\0\1\2")
    zipped = zbuf.getvalue()
    hits = lambda name, data: [w for w, t in texts_in(name, data) if problems_in(t, mark_re)]
    assert hits("a.sfproj", zipped) == ["a.sfproj!manifest.json"]
    assert hits("a.sfproj.z", zlib.compress(zipped)) == ["a.sfproj.z!manifest.json"]
    assert hits("a.Bridge", b"\0\0" + b"dam_" + b"standard\0\1") == ["a.Bridge"]
    assert hits("a.png", b"\0\0" + b"dam_" + b"standard\0\1") == []
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
