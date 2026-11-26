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
section), testers (bullets for the #testers checklist), member_request (true: this fragment's What's New bullets end
"*Requested by a member*" and its highlights "(requested by a member)"; generic, never a name), member_credit (the
member's @handle; accepted, not printed: public text names no member). Every key may repeat; a value continues on
lines indented by two spaces.

  tools/release/assemble_notes.py X.Y.Z             what would change (nothing written)
  tools/release/assemble_notes.py X.Y.Z --apply     write CHANGELOG.md [Unreleased] and What's New X.Y.Z, delete the
                                                    fragments (scripts/release.sh step bump runs this)
  tools/release/assemble_notes.py X.Y.Z --testers   print the #testers checklist (post it with the preview build)
  tools/release/assemble_notes.py X.Y.Z --highlights  the version's highlight lines, What's New's and the fragments'
                                                    (release.sh uses them when HIGHLIGHTS is not set)
  tools/release/assemble_notes.py X.Y.Z --member-requests  print how many fragments are member requests (the number
                                                    for latest.json's support.member_requests_this_release; run it
                                                    before --apply, which deletes the fragments)
  tools/release/assemble_notes.py --check           every fragment parses (ctest release_notes runs --self-test)
"""
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SECTIONS = ["Added", "Changed", "Deprecated", "Removed", "Fixed", "Security"]  # Keep a Changelog's order
NOTE_KEYS = {"whats_new", "highlight", "testers"} | {s.lower() for s in SECTIONS}  # keys that carry release text
KEYS = NOTE_KEYS | {"member_request", "member_credit"}
BULLET_TAG = "*Requested by a member*"
HIGHLIGHT_TAG = "(requested by a member)"


class NoteError(Exception):
    pass


def unquote(v):
    """v without the outer double quotes of a YAML double-quoted value, \\" and \\\\ read back; anything else as it is."""
    if len(v) >= 2 and v[0] == v[-1] == '"' and not re.search(r'(?<!\\)"', v[1:-1].replace("\\\\", "")):
        return v[1:-1].replace('\\"', '"').replace("\\\\", "\\")
    return v


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
    # A value written as a YAML double-quoted string ("...", \" inside) is its text: 0.14.1's CHANGELOG kept the quotes.
    out = {k: [unquote(v) for v in vs] for k, vs in out.items()}
    if not any(out.get(k) for k in NOTE_KEYS):
        raise NoteError(f"{path}: empty (give at least a whats_new or a CHANGELOG line)")
    if any(v.lower() not in ("true", "false") for v in out.get("member_request", [])):
        raise NoteError(f"{path}: member_request must be true or false, got '{' '.join(out['member_request'])}'")
    return out


def member_request(f):
    """True if the fragment is a member's request (any member_request line says true)."""
    return any(v.lower() == "true" for v in f.get("member_request", []))


def tagged(f, key):
    """The fragment's whats_new / highlight lines, with the generic member-request tag when it is one."""
    tag = {"whats_new": BULLET_TAG, "highlight": HIGHLIGHT_TAG}[key]
    return [f"{v} {tag}" if member_request(f) else v for v in f.get(key, [])]


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
    bullets = [b for _, f in frags for b in tagged(f, "whats_new")]
    highlights = [h for _, f in frags for h in tagged(f, "highlight")]
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
        have = {l[len("highlight:"):].strip() for l in lines if l.startswith("highlight:")}
        # A hand-written line that starts with the note's text (plus a credit) is the same highlight (0.12.2/0.12.3 doubled).
        lines[last + 1:last + 1] = [f"highlight: {h}" for h in dict.fromkeys(highlights)
                                    if not any(x == h or x.startswith(h + " (") for x in have)]
        section = section[:fm.start(1)] + "\n".join(lines) + section[fm.end(1):]
    # A bullet whose bold title the section already has was written by hand (0.12.2/0.12.3 shipped it twice).
    titles = set(re.findall(r"^- (\*\*[^*]+\*\*)", section, re.M))
    bullets = [b for b in bullets if not (re.match(r"\*\*[^*]+\*\*", b) and re.match(r"\*\*[^*]+\*\*", b).group(0) in titles)]
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
        # whats_new and highlight text goes into the in-app What's New, drawn with the app's Latin UI font: the same
        # rule as the guide (tools/guide/build_site.py, ctest guide_site), here and not only after the bump assembles
        # the notes (0.11.10: "繁體中文 and 简体中文" in a whats_new line stopped the stage).
        bad = []
        for path, f in frags:
            for key in ("whats_new", "highlight"):
                for text in f.get(key, []) if isinstance(f.get(key), list) else [f.get(key) or ""]:
                    chars = sorted({ch for ch in text if ord(ch) > 0xFF or (ord(ch) < 0x20 and ch not in "\t\n")})
                    if chars:
                        bad.append(f"{path.name}: {key}: characters the app's font can't show: "
                                   + " ".join(f"U+{ord(ch):04X}" for ch in chars))
        for b in bad:
            print(b)
        print(f"{len(frags)} note fragment(s) parse")
        return 1 if bad else 0
    if mode == "--testers":
        sys.stdout.write(testers(version, frags))
        return 0
    if mode == "--member-requests":  # for latest.json's support.member_requests_this_release (before --apply)
        print(sum(1 for _, f in frags if member_request(f)))
        return 0
    if mode == "--highlights":  # the version's feature block's highlights, then the fragments'
        wn = (root / "docs" / "guide" / "whats-new.md").read_text(encoding="utf-8")
        m = re.search(rf"^## Version {re.escape(version)}[ \t]*\n+```features\n(.*?)^```", wn, re.M | re.S)
        lines = re.findall(r"^highlight:[ \t]*(.+)$", m.group(1), re.M) if m else []
        lines += [h for _, f in frags for h in tagged(f, "highlight")]
        lines = list(dict.fromkeys(lines))  # a fragment's highlight already in the block counts once
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
        # member_request: tags the fragment's own bullets and highlights, is counted, never prints member_credit.
        (t / "changes/feat-req.md").write_text(
            "---\nwhats_new: **Soft clay**: a softer clay brush.\nhighlight: Soft clay brush\n"
            "added: **Soft clay** brush.\nmember_request: true\nmember_credit: @someone\n---\n")
        assert (t / "changes/feat-req.md").read_text().count("true") == 1
        assert dict(fragments(t))[t / "changes/feat-req.md"]["member_request"] == ["true"]
        # A YAML double-quoted value is its text (0.14.1's CHANGELOG kept the quotes); a quote inside plain text stays.
        q = t / "changes/fix-quoted.md"
        q.write_text('---\nfixed: "**AI tools**: answered \\"Unknown tool\\";\n  now fixed."\n'
                     'testers:\n  - "Try it."\n  - The "Try" button.\n---\n')
        assert parse(q) == {"fixed": ['**AI tools**: answered "Unknown tool"; now fixed.'],
                            "testers": ["Try it.", 'The "Try" button.']}, parse(q)
        q.unlink()
        cjk = t / "changes/fix-lang.md"  # (a language named in its own script: the app's font can't draw it)
        cjk.write_text("---\nwhats_new: **Español, 繁體中文** labels.\n---\n", encoding="utf-8")
        assert run(t, "0.2.0", "--check") == 1
        cjk.unlink()
        hl = Path(tmp) / "hl.txt"
        with open(hl, "w") as out:
            saved, sys.stdout = sys.stdout, out
            try:
                run(t, "0.1.0", "--highlights")
            finally:
                sys.stdout = saved
        assert hl.read_text() == "Corner axes in three sizes\nSoft clay brush (requested by a member)\n", hl.read_text()
        mr = Path(tmp) / "mr.txt"
        with open(mr, "w") as out:
            saved, sys.stdout = sys.stdout, out
            try:
                run(t, "0.2.0", "--member-requests")
                run(t, "0.2.0", "--highlights")
            finally:
                sys.stdout = saved
        assert mr.read_text() == "1\nCorner axes in three sizes\nSoft clay brush (requested by a member)\n", mr.read_text()
        assert run(t, "0.2.0", "--apply") == 0
        cl = (t / "CHANGELOG.md").read_text()
        # Entries join their section; a section [Unreleased] lacks is added at its end.
        assert "### Fixed\n- Old fix.\n- Axes no longer clip.\n- Pen pressure on Windows Ink (#72).\n\n### Added\n" in cl, cl
        assert "### Added\n- **Gizmo size** (Draw > Camera): Small, Medium and Large.\n- **Soft clay** brush.\n\n## [0.1.0]" in cl, cl
        wn = (t / "docs/guide/whats-new.md").read_text()
        assert wn.index("## Version 0.2.0") < wn.index("## Version 0.1.0"), wn
        assert "```features\ndate: unreleased\nhighlight: Corner axes in three sizes\nhighlight: Soft clay brush (requested by a member)\n```" in wn, wn
        assert "- **Soft clay**: a softer clay brush. *Requested by a member*\n" in wn, wn
        assert "@someone" not in wn and "equested by" not in cl, (wn, cl)
        assert "- **Gizmo size**: Small, Medium or Large.\n- " in wn and "Large. *Requested" not in wn, wn
        assert not (t / "changes/feat-gizmo.md").exists() and (t / "changes/README.md").exists()
        # A second run with no fragments changes nothing.
        assert run(t, "0.2.0", "--apply") == 0 and (t / "CHANGELOG.md").read_text() == cl
        # #testers checklist
        (t / "changes/a.md").write_text("---\ntesters:\n  - Click each axis end;\n    the view snaps.\n---\n")
        assert testers("0.2.0", fragments(t)).endswith("- [ ] Click each axis end; the view snaps. (a)\n")
        # member_request takes true or false only; on its own it is not a note.
        (t / "changes/bad.md").write_text("---\nfixed: x.\nmember_request: yes\n---\n")
        try:
            fragments(t)
            raise AssertionError("member_request: yes accepted")
        except NoteError as e:
            assert "member_request must be true or false" in str(e), e
        (t / "changes/bad.md").write_text("---\nmember_request: true\n---\n")
        try:
            fragments(t)
            raise AssertionError("member_request alone accepted")
        except NoteError as e:
            assert "empty" in str(e), e
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
    if mode not in ("--dry-run", "--apply", "--testers", "--highlights", "--member-requests", "--check"):
        print(__doc__)
        return 2
    try:
        return run(ROOT, argv[0], mode)
    except NoteError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
