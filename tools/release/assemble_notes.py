#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Release notes from note fragments: one changes/<branch>.md per branch instead of every branch editing CHANGELOG.md
and the guide's What's New (the merge conflicts of every release).

A fragment is front matter only (changes/README.md has the format):

    ---
    whats_new: **Gizmo size**: **Draw > Camera > Gizmo size** sets Small, Medium or Large.
    highlight: Corner axes in three sizes
    added: **Gizmo size** (Draw > Camera): the corner axes come in Small, Medium (as before, the default) and Large,
      on top of the UI scale. Saved in the preferences.
    testers:
      - Windows, 150% scaling: Draw > Camera > Gizmo size Large; click each axis end, the view snaps to that side.
    ---

Keys: whats_new (a bullet for docs/guide/whats-new.md, which the app shows), highlight (a line for the version's
feature block and the update feed), added / changed / fixed / removed / security (a CHANGELOG.md entry in that
section), testers (bullets for the #testers checklist). Every key may repeat; a value continues on lines indented
by two spaces.

  tools/release/assemble_notes.py X.Y.Z             what would change (nothing written)
  tools/release/assemble_notes.py X.Y.Z --apply     write CHANGELOG.md [Unreleased] and What's New X.Y.Z, delete the
                                                    fragments (scripts/release.sh step bump runs this)
  tools/release/assemble_notes.py X.Y.Z --testers   print the #testers checklist (post it with the preview build)
  tools/release/assemble_notes.py X.Y.Z --highlights  the version's highlight lines, What's New's and the fragments'
                                                    (release.sh uses them when HIGHLIGHTS is not set)
  tools/release/assemble_notes.py --check           every fragment parses (ctest release_notes runs --self-test)
"""
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SECTIONS = ["Added", "Changed", "Deprecated", "Removed", "Fixed", "Security"]  # Keep a Changelog's order
KEYS = {"whats_new", "highlight", "testers"} | {s.lower() for s in SECTIONS}


class NoteError(Exception):
    pass


def parse(path):
    """A fragment's front matter as {key: [values]}."""
    text = Path(path).read_text(encoding="utf-8")
    m = re.match(r"---\n(.*?)\n---[ \t]*(\n|$)", text, re.S)
    if not m:
        raise NoteError(f"{path}: no front matter (a '---' line, key: value lines, a '---' line)")
    out, key, item = {}, None, None
    for n, line in enumerate(m.group(1).split("\n"), start=2):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        km = re.match(r"([a-z_]+):[ \t]*(.*)$", line)
        if km and not line[0].isspace():
            key = km.group(1)
            if key not in KEYS:
                raise NoteError(f"{path}:{n}: unknown key '{key}' (known: {', '.join(sorted(KEYS))})")
            out.setdefault(key, [])
            item = None
            if km.group(2).strip():
                out[key].append(km.group(2).strip())
                item = len(out[key]) - 1
            continue
        if key is None or not line.startswith("  "):
            raise NoteError(f"{path}:{n}: expected 'key: value' or a line indented by two spaces")
        body = line.strip()
        if key == "testers" and body.startswith("- "):
            out[key].append(body[2:].strip())
            item = len(out[key]) - 1
        elif item is not None:
            out[key][item] += " " + body
        else:
            out[key].append(body)
            item = len(out[key]) - 1
    if not any(out.get(k) for k in KEYS):
        raise NoteError(f"{path}: empty (give at least a whats_new or a CHANGELOG line)")
    return out


def fragments(root):
    d = Path(root) / "changes"
    files = sorted(p for p in d.glob("*.md") if p.name.lower() != "readme.md") if d.is_dir() else []
    return [(p, parse(p)) for p in files]


def wrap(text, first, rest, width=118):
    """Markdown bullet text wrapped as the hand-written CHANGELOG and guide are (118 columns, 2-space hang)."""
    lines, cur = [], ""
    for w in text.split():
        prefix = rest if lines else first
        if cur and len(prefix) + len(cur) + 1 + len(w) > width:
            lines.append(prefix + cur)
            cur = w
        else:
            cur = f"{cur} {w}" if cur else w
    lines.append((rest if lines else first) + cur)
    return "\n".join(lines) + "\n"


def changelog(text, frags):
    """CHANGELOG.md with every fragment's entries added to [Unreleased], in their sections."""
    m = re.search(r"^## \[Unreleased\][^\n]*\n", text, re.M)
    if not m:
        raise NoteError("CHANGELOG.md has no '## [Unreleased]' section")
    nxt = re.search(r"^## \[", text[m.end():], re.M)
    end = m.end() + nxt.start() if nxt else len(text)
    body = text[m.end():end]
    for section in SECTIONS:
        entries = [e for _, f in frags for e in f.get(section.lower(), [])]
        if not entries:
            continue
        add = "".join(wrap(e, "- ", "  ") for e in entries)
        sm = re.search(rf"^### {section}[ \t]*\n", body, re.M)
        if sm:
            snext = re.search(r"^##", body[sm.end():], re.M)
            at = sm.end() + (snext.start() if snext else len(body) - sm.end())
            before = body[:at].rstrip("\n") + "\n"
            body = before + add + "\n" + body[at:].lstrip("\n")
        else:
            body = body.rstrip("\n") + f"\n\n### {section}\n" + add + "\n"
    body = "\n" + body.lstrip("\n")
    if not body.endswith("\n\n") and nxt:
        body = body.rstrip("\n") + "\n\n"
    return text[:m.end()] + body + text[end:]


def whats_new(text, version, frags):
    """docs/guide/whats-new.md with the fragments' bullets (and highlights) in the version's section, made if new."""
    bullets = [b for _, f in frags for b in f.get("whats_new", [])]
    highlights = [h for _, f in frags for h in f.get("highlight", [])]
    if not bullets and not highlights:
        return text
    ev = re.escape(version)
    m = re.search(rf"^## Version {ev}[ \t]*\n", text, re.M)
    if not m:
        first = re.search(r"^## Version ", text, re.M)
        at = first.start() if first else len(text)
        block = f"## Version {version}\n\n```features\ndate: unreleased\n```\n\n"
        text = text[:at] + block + text[at:]
        m = re.search(rf"^## Version {ev}[ \t]*\n", text, re.M)
    nxt = re.search(r"^## ", text[m.end():], re.M)
    end = m.end() + nxt.start() if nxt else len(text)
    section = text[m.end():end]
    if highlights:
        fm = re.search(r"^```features\n(.*?)^```", section, re.M | re.S)
        if not fm:
            raise NoteError(f"whats-new.md: Version {version} has no ```features block for the highlights")
        lines = fm.group(1).split("\n")
        last = max([i for i, l in enumerate(lines) if l.startswith(("date:", "tagline:", "highlight:"))], default=-1)
        lines[last + 1:last + 1] = [f"highlight: {h}" for h in highlights]
        section = section[:fm.start(1)] + "\n".join(lines) + section[fm.end(1):]
    if bullets:
        # Straight after the list if the section has one, else after a blank line.
        list_end = re.search(r"^- ", section, re.M) and section.rstrip("\n").split("\n")[-1].startswith(("- ", "  "))
        section = section.rstrip("\n") + ("\n" if list_end else "\n\n")
        section += "".join(wrap(b, "- ", "  ") for b in bullets) + "\n"
    return text[:m.end()] + section + text[end:]


def testers(version, frags):
    out = [f"**SculptForge {version} preview: please try** (reply with OS, tablet and what you saw)", ""]
    for path, f in frags:
        for t in f.get("testers", []):
            out.append(f"- [ ] {t} ({path.stem})")
    return "\n".join(out) + "\n"


def run(root, version, mode):
    root = Path(root)
    frags = fragments(root)
    if mode == "--check":
        print(f"{len(frags)} note fragment(s) parse")
        return 0
    if mode == "--testers":
        sys.stdout.write(testers(version, frags))
        return 0
    if mode == "--highlights":  # the version's feature block's highlights, then the fragments'
        wn = (root / "docs" / "guide" / "whats-new.md").read_text(encoding="utf-8")
        m = re.search(rf"^## Version {re.escape(version)}[ \t]*\n+```features\n(.*?)^```", wn, re.M | re.S)
        lines = re.findall(r"^highlight:[ \t]*(.+)$", m.group(1), re.M) if m else []
        lines += [h for _, f in frags for h in f.get("highlight", [])]
        sys.stdout.write("".join(h + "\n" for h in lines))
        return 0
    if not frags:
        print("no note fragments in changes/")
        return 0
    cl, wn = root / "CHANGELOG.md", root / "docs" / "guide" / "whats-new.md"
    new = {cl: changelog(cl.read_text(encoding="utf-8"), frags),
           wn: whats_new(wn.read_text(encoding="utf-8"), version, frags)}
    for p, t in new.items():
        if mode == "--apply":
            p.write_text(t, encoding="utf-8")
        print(f"{p.relative_to(root)}: {'written' if mode == '--apply' else 'would change'}")
    for path, _ in frags:
        if mode == "--apply":
            path.unlink()
        print(f"{path.relative_to(root)}: {'deleted' if mode == '--apply' else 'would be deleted'}")
    return 0


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "changes").mkdir()
        (t / "docs" / "guide").mkdir(parents=True)
        (t / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [Unreleased]\n\n### Fixed\n- Old fix.\n\n## [0.1.0] - 2026-01-01\n\n### Added\n- A.\n")
        (t / "docs/guide/whats-new.md").write_text(
            "# What's new\n\nIntro.\n\n## Version 0.1.0\n\n```features\ndate: 2026-01-01\n```\n\n- Old.\n")
        (t / "changes/README.md").write_text("not a fragment\n")
        (t / "changes/feat-gizmo.md").write_text(
            "---\nwhats_new: **Gizmo size**: Small, Medium or Large.\nhighlight: Corner axes in three sizes\n"
            "added: **Gizmo size** (Draw > Camera): Small,\n  Medium and Large.\n"
            "fixed: Axes no longer clip.\ntesters:\n  - Click each axis end;\n    the view snaps.\n  - Try Large.\n---\n"
            "Free notes for reviewers.\n")
        (t / "changes/fix-pen.md").write_text("---\nfixed: Pen pressure on Windows Ink (#72).\n---\n")
        assert run(t, "0.2.0", "--check") == 0
        hl = Path(tmp) / "hl.txt"
        with open(hl, "w") as out:
            saved, sys.stdout = sys.stdout, out
            try:
                run(t, "0.1.0", "--highlights")
            finally:
                sys.stdout = saved
        assert hl.read_text() == "Corner axes in three sizes\n", hl.read_text()
        assert run(t, "0.2.0", "--apply") == 0
        cl = (t / "CHANGELOG.md").read_text()
        # Entries join their section; a section [Unreleased] lacks is added at its end.
        assert "### Fixed\n- Old fix.\n- Axes no longer clip.\n- Pen pressure on Windows Ink (#72).\n\n### Added\n" in cl, cl
        assert "### Added\n- **Gizmo size** (Draw > Camera): Small, Medium and Large.\n\n## [0.1.0]" in cl, cl
        wn = (t / "docs/guide/whats-new.md").read_text()
        assert wn.index("## Version 0.2.0") < wn.index("## Version 0.1.0"), wn
        assert "```features\ndate: unreleased\nhighlight: Corner axes in three sizes\n```" in wn, wn
        assert "- **Gizmo size**: Small, Medium or Large.\n\n## Version 0.1.0" in wn, wn
        assert not (t / "changes/feat-gizmo.md").exists() and (t / "changes/README.md").exists()
        # A second run with no fragments changes nothing.
        assert run(t, "0.2.0", "--apply") == 0 and (t / "CHANGELOG.md").read_text() == cl
        # #testers checklist
        (t / "changes/a.md").write_text("---\ntesters:\n  - Click each axis end;\n    the view snaps.\n---\n")
        assert testers("0.2.0", fragments(t)).endswith("- [ ] Click each axis end; the view snaps. (a)\n")
        # Errors name the file and line.
        (t / "changes/bad.md").write_text("---\nwhatsnew: typo\n---\n")
        try:
            fragments(t)
            raise AssertionError("unknown key accepted")
        except NoteError as e:
            assert "bad.md:2: unknown key 'whatsnew'" in str(e), e
        # Long entries wrap at 118 columns with a two-space hang.
        w = wrap("word " * 40, "- ", "  ")
        assert all(len(l) <= 118 for l in w.splitlines()) and w.splitlines()[1].startswith("  word"), w
    print("assemble_notes self-test ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv == ["--check"]:
        argv = ["0", "--check"]
    if not argv or not re.fullmatch(r"\d+(\.\d+)*", argv[0]) or len(argv) > 2:
        print(__doc__)
        return 2
    mode = argv[1] if len(argv) == 2 else "--dry-run"
    if mode not in ("--dry-run", "--apply", "--testers", "--highlights", "--check"):
        print(__doc__)
        return 2
    try:
        return run(ROOT, argv[0], mode)
    except NoteError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
