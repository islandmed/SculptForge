#!/usr/bin/env python3
"""Every @handle credited in the release notes is a GitHub account (release.sh preflight).

Credits read "thanks @handle" and point at GitHub; a Discord-only requester is credited without the @ ("thanks name on
Discord"), so a handle that isn't a GitHub account is a typo or a Discord name written as a GitHub one (0.11.13:
@foirgairwolf). Reads changes/*.md and the HIGHLIGHTS file; asks `gh api users/<handle>` once per handle.
  python3 tools/release/check_credits.py [FILE...]    exit 1: a handle isn't on GitHub, 2: GitHub can't be asked
  python3 tools/release/check_credits.py --self-test   (offline: what counts as a handle)
"""
import glob
import os
import re
import subprocess
import sys

# @handle as GitHub spells them (1-39 letters, digits, single hyphens), not inside an email address or a word.
HANDLE = re.compile(r'(?<![\w.@/`])@([A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38})(?![\w@-])')


def handles(text):
    return {m.group(1) for m in HANDLE.finditer(text)}


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
    print("check_credits self-test: ok")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    files = argv or sorted(glob.glob("changes/*.md")) + [f for f in [os.environ.get("HIGHLIGHTS", "")] if f]
    found = {}
    for f in files:
        if os.path.basename(f) == "README.md":
            continue
        with open(f, encoding="utf-8") as fh:
            for h in handles(fh.read()):
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
