#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draft issue replies for a release (release.sh step replies): one per issue the CHANGELOG section names.

Each draft quotes every entry that names the issue whole (an entry is its "- " line and the indented lines under
it; 0.11.8's drafts cut them at the first line), thanks the reporter by @handle (from GitHub, not the entry text),
and says "Fixed in" for an entry under ### Fixed, "Shipped in" otherwise. When most of the issue title's words are
missing from the entries, the draft gets a REVIEW line naming them, to say "Part of this shipped" and keep the
issue open if anything is left: 0.11.8 told #144 (OCIO config) "Fixed" when only its Preferences part shipped.
An issue that already has our reply for this version (an islandmed comment saying "Fixed in", "Shipped in" or
"Part of this shipped in" and the version) gets no draft: 0.11.8's replies went out twice. sfmonitor posts them.
Nothing is posted here.

    python3 reply_drafts.py VERSION SECTION_FILE REPO TAG > release-replies-VERSION.md
    python3 reply_drafts.py --self-test
Standard library only; reads issues with the gh CLI.
"""

import json
import re
import subprocess
import sys

OURS = {"islandmed", "github-actions[bot]", "dependabot[bot]"}


def entries(section):
    """[(category, text)] for each "- " entry, its indented continuation lines joined."""
    out, category = [], ""
    for line in section.splitlines():
        if line.startswith("### "):
            category = line[4:].strip()
        elif line.startswith("- "):
            out.append([category, line[2:].strip()])
        elif out and line.startswith("  ") and line.strip():
            out[-1][1] += " " + line.strip()
    return [(c, t) for c, t in out]


def issues_named(section):
    return sorted({int(n) for n in re.findall(r"#(\d+)\b", section)})


STOP = {"about", "after", "already", "also", "being", "doesn", "from", "have", "into", "make", "more", "never",
        "only", "should", "some", "that", "then", "there", "this", "very", "when", "will", "with", "without", "would"}


def missing_words(title, text):
    """The issue title's words (4+ letters, compared on their first 5) that no entry mentions, if most are."""
    words = [w.lower() for w in re.findall(r"[A-Za-z]{3,}", title or "")]
    words = [w for w in dict.fromkeys(words) if w not in STOP and (len(w) >= 4 or w.isupper() or w in ("pen", "uvs", "vdm"))]
    have = text.lower()
    gone = [w for w in words if w[:5] not in have]
    return gone if words and len(gone) / len(words) > 0.6 else []


def draft(n, issue, named, version, repo, tag):
    fixed = all(c == "Fixed" for c, _ in named)
    gone = missing_words(issue["title"], " ".join(t for _, t in named))
    head = f"{'Fixed' if fixed else 'Shipped'} in SculptForge {version}, out now:"
    lines = [f"## #{n} {issue['title']} ({issue['state']})", ""]
    if gone:
        lines += [f"> REVIEW before posting: the entry doesn't mention {', '.join(gone)} from the issue title. If part of",
                  "> the issue is still open, say \"Part of this shipped in\" and keep it open. Delete this line.", ""]
    lines += [f"{head} https://github.com/{repo}/releases/tag/{tag}",
              "(Help > Check for updates in the app, or https://sculptforge.app/#download)", ""]
    lines += [f"- {text}" for _, text in named]
    author = (issue.get("author") or {}).get("login", "")
    lines += ["", f"Thanks @{author} for reporting it." if author and author not in OURS else "Thanks for reporting it.", ""]
    return "\n".join(lines)


def replied(issue, version):
    """Our reply for this version is already on the issue."""
    for c in issue.get("comments") or []:
        body = c.get("body") or ""
        if (c.get("author") or {}).get("login") in OURS and version in body and \
                re.search(r"\b(Fixed in|Shipped in|Part of this shipped in)\b", body):
            return True
    return False


def gh_issue(n, repo):
    try:
        out = subprocess.run(["gh", "issue", "view", str(n), "-R", repo, "--json", "state,title,author,body,comments"],
                             capture_output=True, text=True, timeout=60, check=True).stdout
        return json.loads(out)
    except (OSError, subprocess.SubprocessError, ValueError):
        return None


def main(argv):
    if argv[:1] == ["--self-test"]:
        sec = ("### Changed\n- **Library**: a single click uses a brush.\n  Double-click still works (#142, thanks @x).\n"
               "### Fixed\n- The guide's **Edit > Preferences** paths (#144).\n- Stamp keeps strength (#146,\n  Huion).\n")
        e = entries(sec)
        assert e[0] == ("Changed", "**Library**: a single click uses a brush. Double-click still works (#142, thanks @x).")
        assert e[2] == ("Fixed", "Stamp keeps strength (#146, Huion).")
        assert issues_named(sec) == [142, 144, 146]
        assert missing_words("Options: OCIO config file (UI scale already in Edit > Preferences)", e[1][1])
        assert not missing_words("UVs being corrupted on Reconstruct Subdiv", "**Reconstruct Subdiv** keeps UVs")
        d = draft(146, {"title": "Drag stamp weak", "state": "OPEN", "author": {"login": "pen"}, "body": ""},
                  [e[2]], "9.0.0", "o/r", "v9.0.0")
        assert "Fixed in SculptForge 9.0.0" in d and "Thanks @pen " in d and "Huion)." in d
        d = draft(144, {"title": "OCIO config file", "state": "OPEN", "author": {"login": "nic"}, "body": ""},
                  [e[1]], "9.0.0", "o/r", "v9.0.0")
        assert "REVIEW" in d and "ocio" in d.lower() and "Thanks @nic " in d
        posted = {"comments": [{"author": {"login": "islandmed"}, "body": "Fixed in SculptForge 9.0.0, out now"}]}
        assert replied(posted, "9.0.0") and not replied(posted, "9.0.1")
        assert not replied({"comments": [{"author": {"login": "someone"}, "body": "Fixed in SculptForge 9.0.0?"}]}, "9.0.0")
        print("reply_drafts self-test ok")
        return 0
    version, section_file, repo, tag = argv
    section = open(section_file, encoding="utf-8").read()
    all_entries = entries(section)
    print(f"# Draft replies for SculptForge {version} (read, edit, then post by hand; nothing here was posted)\n")
    count = 0
    for n in issues_named(section):
        issue = gh_issue(n, repo)
        if not issue or replied(issue, version):
            continue
        named = [(c, t) for c, t in all_entries if re.search(rf"#{n}\b", t)]
        print(draft(n, issue, named, version, repo, tag))
        count += 1
    print(f"{count} drafts", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
