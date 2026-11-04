#!/usr/bin/env python3
"""Fails when a third-party sculpting-app trademark appears anywhere it may not (docs/legal/trademarks.md).

Every git-tracked text file and every tracked path is scanned, case-insensitively, for the marks in MARKS.
- tools/marks_allow.txt: the permanent exceptions (path globs), each with its reason.
- tools/marks_pending.txt: files not fully renamed yet, as "<glob> <mode>". It only shrinks: a pending file
  with no marks left is reported so it is taken off.
    strings  code: only string literals that read as text (capital letters or spaces) are checked; a line
             marked "// marks-pending" (a value name scripts use) waits for the rename with an alias
    prose    docs: text outside `code spans` and <code> is checked (command names follow the code rename)
    skip     another branch owns the file and rewrites it

  check_marks.py            check the repository (ctest: marks)
  check_marks.py --count    print matches per mark and per top-level folder, for before/after counts
  check_marks.py --self-test
"""
import fnmatch
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The marks, as case-insensitive patterns. Order matters only for --count labels.
MARKS = [
    r"z ?brush", r"pixologic", r"maxon", r"\bgoz(?!o)", r"gozbrush", r"dyna ?mesh", r"z ?remesher", r"sculptris",
    r"subtools?|\bsub tools?\b", r"poly ?paint", r"z ?modeler", r"lightbox|\blight box\b", r"z ?spheres?", r"matcaps?|\bmat caps?\b", r"fiber ?mesh",
    r"lightcap|\blight cap\b", r"\bpixols?\b", r"z ?script", r"z ?plug(?:in)?s?\b", r"\bspotlight\b", r"shadow ?box",
    r"nano ?mesh", r"array ?mesh", r"live ?boolean", r"\bz ?(?:add|sub)\b", r"insert ?multi ?mesh", r"\bimm\b",
    r"poly ?groups?(?!s)", r"projection ?master", r"hd ?geometry", r"decimation ?master", r"uv ?master",
    r"clip ?curve", r"trim ?curve", r"poly ?group ?it", r"panel ?loops", r"dam_? ?standard", r"clay ?buildup",
    r"\bh ?polish\b", r"trim ?dynamic", r"move ?topological", r"\bztools?\b", r"\.ztl\b",
    # Other sculpting apps, named only for comparison in old copy: none of that in ours either.
    r"mudbox", r"\bnomad\b", r"sculptgl", r"3d-?coat",
]
MARK_RE = re.compile("|".join(f"(?:{m})" for m in MARKS), re.IGNORECASE)
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
TEXTY_RE = re.compile(r"^[A-Z]| ")  # reads as text: starts with a capital or has a space
SKIP_PREFIXES = ("third_party/",)


CODE_SPAN_RE = re.compile(r"`[^`\n]*`|<code>.*?</code>")


def load_globs(name):
    """{glob: mode} from a list file; the mode is the second word (empty for the allowlist)."""
    globs = {}
    for line in (ROOT / "tools" / name).read_text(encoding="utf-8").splitlines():
        words = line.split("#", 1)[0].split()
        if words:
            globs[words[0]] = words[1] if len(words) > 1 else ""
    return globs


def glob_match(path, g):
    return fnmatch.fnmatchcase(path, g) or g.endswith("/**") and path.startswith(g[:-2])


def matches(path, globs):
    """The mode of the first glob that matches, or None."""
    return next((mode for g, mode in globs.items() if glob_match(path, g)), None)


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p and not p.startswith(SKIP_PREFIXES)]


def read_text(path):
    try:
        data = (ROOT / path).read_bytes()
    except OSError:
        return None  # deleted in the working tree
    if b"\0" in data[:8192]:
        return None  # binary
    return data.decode("utf-8", errors="replace")


def problems_in(path, text, pending):
    """(line, snippet) for every mark this file may not have; pending is None or a marks_pending.txt mode."""
    found = []
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if pending == "prose" and line.lstrip().startswith("```"):
            fenced = not fenced
        opens_pre = pending == "prose" and "<pre" in line and "</pre>" not in line
        in_pre = fenced or opens_pre
        if pending == "prose" and "</pre>" in line and "<pre" not in line:
            fenced, in_pre = False, True
        elif opens_pre:
            fenced = True
        if not MARK_RE.search(line):
            continue
        if pending == "prose":
            if not in_pre and MARK_RE.search(CODE_SPAN_RE.sub("", line)):
                found.append((n, line.strip()))
        elif pending == "strings":
            if "marks-pending" in line:
                continue  # a script-facing value name, renamed with an alias in phase 2
            for s in STRING_RE.finditer(line):
                body = s.group(1).split("##", 1)[0]  # an ImGui id after ## is never shown
                if MARK_RE.search(body) and TEXTY_RE.search(body) and not re.fullmatch(r"[\w./<>-]+\.(h|hpp|cpp)", body):
                    found.append((n, body))
        else:
            found.append((n, line.strip()))
    return found


def scan():
    allow, pending = load_globs("marks_allow.txt"), load_globs("marks_pending.txt")
    errors, stale = [], []
    for path in tracked_files():
        if matches(path, allow) is not None:
            continue
        is_pending = matches(path, pending)
        if is_pending == "skip":
            continue
        if MARK_RE.search(path) and is_pending is None:
            errors.append(f"{path}: the file name has a mark")
        text = read_text(path)
        if text is None:
            continue
        for n, snippet in problems_in(path, text, is_pending):
            errors.append(f"{path}:{n}: {snippet[:160]}")
        if path in pending and not MARK_RE.search(text) and not MARK_RE.search(path):
            stale.append(path)
    for path in stale:
        errors.append(f"{path}: no marks left, take it off tools/marks_pending.txt")
    return errors


def count():
    per_mark, per_dir = Counter(), Counter()
    for path in tracked_files():
        text = read_text(path)
        if text is None:
            continue
        for m in MARK_RE.finditer(text):
            per_mark[m.group(0).lower().replace(" ", "")] += 1
            per_dir[path.split("/", 1)[0] if "/" in path else "(root)"] += 1
    print(f"total {sum(per_mark.values())}")
    for k, v in per_dir.most_common():
        print(f"  {v:6} {k}/")
    for k, v in per_mark.most_common():
        print(f"  {v:6} {k}")


def self_test():
    assert MARK_RE.search("Send to ZBrush") and MARK_RE.search("subtoolCount") and MARK_RE.search("use a MatCap")
    assert MARK_RE.search("Polygroups") and MARK_RE.search("DamStandard") and MARK_RE.search("Dragon.ZTL")
    assert not MARK_RE.search("Lit Sphere, Part, Face group, Library, Vertex Paint, Bridge, Forge Spheres")
    assert not MARK_RE.search("immediate dimmed trimmed Gozo, format capability")
    assert problems_in("a.cpp", 'int subtoolCount = run("subtool_append");', "strings") == []
    assert problems_in("a.cpp", 'label("Send to ZBrush");', "strings") == [(1, "Send to ZBrush")]
    assert problems_in("a.cpp", 'j.get("lightboxOpen"); button("Remove##matcap");', "strings") == []
    assert problems_in("a.md", "Coming from ZBrush", None) == [(1, "Coming from ZBrush")]
    assert problems_in("a.md", "Run `subtool_append`.", "prose") == []
    assert problems_in("a.md", "Run `x` on a SubTool.", "prose") != []
    assert problems_in("a.html", "<pre><code>zsphere_new\nzsphere_add</code></pre>\nok", "prose") == []
    assert matches("integrations/bridge-plugin/x.txt", {"integrations/bridge-plugin/**": ""}) == ""
    assert matches("src/a.cpp", {"src/b.cpp": "strings"}) is None
    print("check_marks self-test ok")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    if "--count" in argv:
        count()
        return 0
    errors = scan()
    for e in errors:
        print(e)
    if errors:
        print(f"\n{len(errors)} trademark problem(s). Rename by function (docs/legal/trademarks.md); "
              "don't widen tools/marks_allow.txt.", file=sys.stderr)
        return 1
    print("check_marks: no marks outside the allowlist")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
