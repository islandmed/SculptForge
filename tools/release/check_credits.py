#!/usr/bin/env python3
"""Every @handle credited in the release notes is a GitHub account (release.sh preflight).

Credits read "thanks @handle" and point at GitHub; a Discord-only requester is credited without the @ ("thanks name on
Discord"), so a handle that isn't a GitHub account is a typo or a Discord name written as a GitHub one (0.11.13:
@foirgairwolf). Reads changes/*.md and the HIGHLIGHTS file; asks `gh api users/<handle>` once per handle.
The names in tools/release/discord_only.txt are never written "@name" in those, the guide (docs/guide) or the site
(website, CHANGELOG.md), even when GitHub has an account by that name (someone else's): checked offline, first.
  python3 tools/release/check_credits.py [FILE...]    exit 1: a handle isn't on GitHub or a Discord name has an @,
                                                      2: GitHub can't be asked
  python3 tools/release/check_credits.py --highlights FILE [--version X.Y.Z] [NOTE...]
      every credit in the highlights ("thanks @handle", "thanks name on Discord", "#123") is written the same way in a
      note of the batch (default changes/*.md, plus CHANGELOG.md's X.Y.Z section, where step bump moves the notes):
      credits are copied from the branch's own note, never from memory
      (0.12.0's draft credited the wrong people twice). Offline; exit 1 names each credit no note has.
  python3 tools/release/check_credits.py --self-test   (offline: what counts as a handle, how credits are read)
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DISCORD_ONLY = os.path.join(ROOT, "tools", "release", "discord_only.txt")
SITE_TEXT = (".md", ".html", ".json", ".txt", ".xml")

# @handle as GitHub spells them (1-39 letters, digits, single hyphens), not inside an email address or a word.
HANDLE = re.compile(r'(?<![\w.@/`])@([A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38})(?![\w@-])')


def handles(text):
    return {m.group(1) for m in HANDLE.finditer(text)}


def discord_names(path=DISCORD_ONLY):
    with open(path, encoding="utf-8") as fh:
        return [n for n in (line.split("#")[0].strip() for line in fh) if n]


def discord_ats(text, names):
    """The Discord-only names written as @name (any case; @fenixpoints is not @fenix)."""
    return sorted({n for n in names
                   if re.search(r'(?<![\w.@/`])@' + re.escape(n) + r'(?![\w@-])', text, re.IGNORECASE)})


def published_files():
    """The guide and the site: where a credit is read by people."""
    out = [os.path.join(ROOT, "CHANGELOG.md")]
    for top in ("docs/guide", "website"):
        for d, _, fs in os.walk(os.path.join(ROOT, top)):
            out += [os.path.join(d, f) for f in fs if f.endswith(SITE_TEXT)]
    return [f for f in out if os.path.isfile(f)]


# "(thanks @a on GitHub, #108, and b on Discord)": the people (and issues) one highlight credits.
THANKS = re.compile(r'\bthanks\s+([^)]*)', re.IGNORECASE)


def credits(line):
    """The credits in one highlight line, each as the text a note must contain: "@handle", "#123", "name on Place"."""
    out = []
    for m in THANKS.finditer(line):
        for part in re.split(r',\s*|\s+and\s+', m.group(1)):
            part = re.sub(r'^(?:and|to)\s+', '', part.strip()).rstrip('.;')
            if not part:
                continue
            h = HANDLE.match(part)
            out.append("@" + h.group(1) if h else part)  # "@x on GitHub" is credited as @x; else "#123" or "name on Place"
    return out


def flat(text):
    return " ".join(text.split()).lower()  # notes wrap lines; compare with whitespace collapsed, any case


def missing_credits(highlights, notes):
    corpus = flat(notes)
    bad = []
    for line in highlights.splitlines():
        for c in credits(line):
            key = flat(c)
            if c.startswith("@"):
                hit = re.search(r'(?<![\w.@/`])' + re.escape(key) + r'(?![\w@-])', corpus)
            elif c.startswith("#"):
                hit = re.search(re.escape(key) + r'(?!\d)', corpus)
            else:
                hit = key in corpus
            if not hit:
                bad.append(c)
    return bad


def changelog_section(version, path=os.path.join(ROOT, "CHANGELOG.md")):
    """CHANGELOG.md's "## [X.Y.Z]" section (the notes after step bump moved them there), or ""."""
    if not version or not os.path.isfile(path):
        return ""
    with open(path, encoding="utf-8") as fh:
        m = re.search(r'^## \[' + re.escape(version) + r'\][^\n]*\n(.*?)(?=^## |\Z)', fh.read(), re.M | re.S)
    return m.group(1) if m else ""


def check_highlights(path, notes, version=""):
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    corpus = changelog_section(version)
    for n in notes:
        if os.path.basename(n) != "README.md":
            with open(n, encoding="utf-8", errors="replace") as fh:
                corpus += fh.read() + "\n"
    bad = missing_credits(text, corpus)
    if bad:
        print(f"credits in {path} that no note of the batch writes ({len(notes)} notes): " + "; ".join(bad) +
              " (copy each credit from that branch's own changes/ note)")
        return 1
    print(f"check_credits: every credit in {os.path.basename(path)} is in a note of the batch")
    return 0


def exists(handle):
    """True / False, or None when GitHub can't be asked (network, auth)."""
    r = subprocess.run(["gh", "api", f"users/{handle}", "--silent"], capture_output=True, text=True)
    if r.returncode == 0:
        return True
    return False if "404" in r.stderr or "Not Found" in r.stderr else None


def self_test():
    cases = {
        "thanks @fenixpoints, #115": {"fenixpoints"},
        "(thanks @kenttumanda123-netizen, #155)": {"kenttumanda123-netizen"},
        "write to hello@sculptforge.app": set(),
        "thanks foirgairwolf on Discord": set(),
        "`@media` in CSS, a decorator `@property`": set(),
        "@a and @b-c, not @-x or @x-": {"a", "b-c"},
    }
    for text, want in cases.items():
        got = handles(text)
        if got != want:
            print(f"check_credits self-test: {text!r} -> {sorted(got)}, expected {sorted(want)}")
            return 1
    names = ["foirgairwolf", "fenix", "Cytrick", "Oslen", "_tfx", "Hameron"]
    ats = {
        "requested by @foirgairwolf": ["foirgairwolf"],
        "Thanks @FoirgairWolf!": ["foirgairwolf"],
        "thanks @cytrick and @fenix (#120)": ["Cytrick", "fenix"],
        "thanks foirgairwolf on Discord": [],
        "thanks @fenixpoints, #115": [],
        "fenix@example.com, `@fenix`": [],
        "thanks @_tfx and @Hameron": ["Hameron", "_tfx"],
        "thanks _tfx on Discord, @oslen": ["Oslen"],
    }
    for text, want in ats.items():
        got = discord_ats(text, names)
        if got != want:
            print(f"check_credits self-test: Discord-only in {text!r} -> {got}, expected {want}")
            return 1
    split = {
        "Edge creases (thanks @NicTanghe on GitHub, #108, and _tfx on Discord)":
            ["@NicTanghe", "#108", "_tfx on Discord"],
        "Plugins (thanks @bribblej-web, #20)": ["@bribblej-web", "#20"],
        "Linux pens (thanks Huguillon on YouTube)": ["Huguillon on YouTube"],
        "Gizmo All (thanks fenix on Discord, @fenixpoints)": ["fenix on Discord", "@fenixpoints"],
        "Clean cut caps": [],
    }
    for text, want in split.items():
        got = credits(text)
        if got != want:
            print(f"check_credits self-test: credits({text!r}) -> {got}, expected {want}")
            return 1
    notes = "whats_new: ... (thanks @NicTanghe, #108). Thanks _tfx on\n  Discord for asking.\nfixed: ... Huguillon on YouTube"
    found = {
        "Edges (thanks @NicTanghe on GitHub, #108, and _tfx on Discord)": [],
        "Pens (thanks Huguillon on YouTube)": [],
        "Pens (thanks GABO on Discord)": ["GABO on Discord"],
        "Edges (thanks @NicTang, #10)": ["@NicTang", "#10"],
    }
    for text, want in found.items():
        got = missing_credits(text, notes)
        if got != want:
            print(f"check_credits self-test: missing in {text!r} -> {got}, expected {want}")
            return 1
    listed = discord_names()
    if not {"foirgairwolf", "fenix", "Cytrick", "Oslen", "_tfx", "Hameron"} <= set(listed):
        print(f"check_credits self-test: {DISCORD_ONLY} lists {listed}")
        return 1
    print("check_credits self-test: ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv[:1] == ["--highlights"]:
        if len(argv) < 2:
            print("usage: check_credits.py --highlights FILE [NOTE...]")
            return 2
        rest, version = argv[2:], ""
        if rest[:1] == ["--version"] and len(rest) > 1:
            version, rest = rest[1], rest[2:]
        return check_highlights(argv[1], rest or sorted(glob.glob(os.path.join(ROOT, "changes", "*.md"))), version)
    files = argv or sorted(glob.glob("changes/*.md")) + [f for f in [os.environ.get("HIGHLIGHTS", "")] if f]
    names, bad = discord_names(), []
    for f in files + ([] if argv else published_files()):
        with open(f, encoding="utf-8", errors="replace") as fh:
            for n in discord_ats(fh.read(), names):
                bad.append(f"@{n} in {os.path.relpath(f)}: write '{n} on Discord'")
    if bad:
        print("Discord-only names written as GitHub handles (tools/release/discord_only.txt): " + "; ".join(bad))
        return 1
    discord = {n.lower() for n in names}
    found = {}
    for f in files:
        if os.path.basename(f) == "README.md":
            continue
        with open(f, encoding="utf-8") as fh:
            for h in handles(fh.read()):
                if h.lower() not in discord:
                    found.setdefault(h, []).append(f)
    missing, unknown = [], []
    for h in sorted(found, key=str.lower):
        ok = exists(h)
        if ok is None:
            unknown.append(h)
        elif not ok:
            missing.append(f"@{h} ({', '.join(found[h])})")
    if missing:
        print("not GitHub accounts: " + "; ".join(missing))
        return 1
    if unknown:
        print("couldn't ask GitHub about: " + ", ".join("@" + h for h in unknown))
        return 2
    print(f"check_credits: {len(found)} handle(s), all on GitHub")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
