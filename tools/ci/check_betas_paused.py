#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Member betas are PAUSED: member-facing copy must not promise them.

    tools/ci/check_betas_paused.py              exit 1 if "beta"/"betas" (a whole word, any case) is in member-facing copy
                                                and not allowed by tools/ci/betas_allow.txt, or an allow line matches nothing
    tools/ci/check_betas_paused.py --self-test

Member-facing copy: the string literals in src/app/*.cpp and *.h (interface text, tooltips and every translation table),
README.md, docs/guide/*.md and website/**/*.html (website/guide is generated from docs/guide at release). Not scanned (history or internal): CHANGELOG.md, docs/guide/whats-new.md
(release history), changes/ (notes of shipped versions), docs/design/, docs/legal/, docs/releasing.md, code comments, tests/.

What members get instead is worded exactly "Early builds, like SculptForge for Android today". When betas come back, empty
tools/ci/betas_allow.txt's reasons that no longer hold and delete this test (CMakeLists.txt: betas_paused; tools/test/impacted.py).
An allow line is `path<TAB>text that must be on the line<TAB>reason`; a line that matches nothing fails (the list only shrinks).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORD = re.compile(r"\bbetas?\b", re.IGNORECASE)
STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def src_hits(text):
    """(line number, line) for each C++ line with the word inside a string literal (comments are not copy)."""
    for n, line in enumerate(text.splitlines(), 1):
        code = line.split("//", 1)[0] if '"' not in line.split("//", 1)[0][-1:] else line
        if any(WORD.search(m.group(0)) for m in STRING.finditer(code)):
            yield n, line.strip()


def text_hits(text):
    for n, line in enumerate(text.splitlines(), 1):
        if WORD.search(line):
            yield n, line.strip()


def files(root):
    for p in sorted((root / "src/app").glob("*.cpp")) + sorted((root / "src/app").glob("*.h")):
        yield p, src_hits
    for p in [root / "README.md"] + sorted((root / "docs/guide").glob("*.md")):
        if p.name != "whats-new.md" and p.exists():
            yield p, text_hits
    for p in sorted((root / "website").rglob("*.html")):
        if "guide" not in p.relative_to(root / "website").parts[:1]:  # website/guide is written from docs/guide at release
            yield p, text_hits


def load_allow(root):
    out = []
    path = root / "tools/ci/betas_allow.txt"
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1) if path.exists() else []:
        if line.strip() and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 3 or not all(x.strip() for x in parts):
                sys.exit(f"{path.name}:{n}: want path<TAB>text<TAB>reason")
            out.append((parts[0].strip(), parts[1], parts[2].strip(), n))
    return out


def check(root):
    allow = load_allow(root)
    used = set()
    bad = []
    for p, finder in files(root):
        rel = p.relative_to(root).as_posix()
        for n, line in finder(p.read_text(encoding="utf-8", errors="replace")):
            ok = [i for i, (ap, at, _, _) in enumerate(allow) if ap == rel and at in line]
            used.update(ok)
            if not ok:
                bad.append(f"{rel}:{n}: {line[:160]}")
    stale = [f"tools/ci/betas_allow.txt:{a[3]}: matches nothing now ({a[0]}: {a[1][:60]})" for i, a in enumerate(allow) if i not in used]
    return bad, stale


def main():
    if "--self-test" in sys.argv:
        s = list(src_hits('x = tr("Members get betas early.");\n// a beta in a comment\nint beta = 3;\nauto a = "alphabet";\n'
                          'f("Beta 1"); // "beta" trailing\n'))
        assert [n for n, _ in s] == [1, 5], s
        assert not list(text_hits("Early builds, like SculptForge for Android today.\nbetamax\n"))
        assert [n for n, _ in text_hits("a\nPreview for the Betas.\n")] == [2]
        print("check_betas_paused self-test ok")
        return 0
    bad, stale = check(ROOT)
    for b in bad:
        print(b)
    for s in stale:
        print(s)
    if bad or stale:
        print("\nMember betas are PAUSED: member-facing copy must not promise them. Word it \"Early builds, like SculptForge for Android\n"
              "today\", or (history, a feature name that is not a promise) add a line with a reason to tools/ci/betas_allow.txt.\n"
              "Where to change the rule: tools/ci/check_betas_paused.py (this test), tools/ci/betas_allow.txt (exceptions).")
        return 1
    print("betas paused: no member-facing copy promises betas")
    return 0


if __name__ == "__main__":
    sys.exit(main())
