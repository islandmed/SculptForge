#!/usr/bin/env python3
"""Every @handle credited in the release notes is a GitHub account (release.sh preflight).

Credits read "thanks @handle" and point at GitHub; a Discord-only requester is credited without the @ ("thanks name on
Discord"), so a handle that isn't a GitHub account is a typo or a Discord name written as a GitHub one (0.11.13:
@foirgairwolf). Reads changes/*.md and the HIGHLIGHTS file; asks `gh api users/<handle>` once per handle.
The names in tools/release/discord_only.txt are never written "@name" in those, the guide (docs/guide) or the site
(website, CHANGELOG.md), even when GitHub has an account by that name (someone else's): checked offline, first.
  python3 tools/release/check_credits.py [FILE...]    exit 1: a handle isn't on GitHub or a Discord name has an @,
                                                      2: GitHub can't be asked
  python3 tools/release/check_credits.py --self-test   (offline: what counts as a handle)
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
    listed = discord_names()
    if not {"foirgairwolf", "fenix", "Cytrick", "Oslen", "_tfx", "Hameron"} <= set(listed):
        print(f"check_credits self-test: {DISCORD_ONLY} lists {listed}")
        return 1
    print("check_credits self-test: ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
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
