#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pins tests/ui/whats_new.sfui's guide check to the newest What's New section (release.sh step bump runs it).

    tools/release/pin_whats_new_test.py X.Y.Z [--check]   write (or only check) the pin
    tools/release/pin_whats_new_test.py --self-test

The guide chapter shows the last three versions in full, so the test expects the newest three '## Version' lines of
docs/guide/whats-new.md and each one's tagline, with SF_GUIDE_VERSION at X.Y.Z. The release never stops at
"whats_new.sfui doesn't check Version X" again: the bump writes it.
"""
import re
import sys

WHATS_NEW = "docs/guide/whats-new.md"
TEST = "tests/ui/whats_new.sfui"


def sections(md):
    """[(version, tagline or None)], newest first."""
    out = []
    for m in re.finditer(r"^## Version (\S+)\n(.*?)(?=^## |\Z)", md, re.M | re.S):
        t = re.search(r"^tagline: (.+)$", m.group(2), re.M)
        out.append((m.group(1), t.group(1).strip() if t else None))
    return out


def visible(text):
    """The part of a tagline the test can quote: up to the first quote or Markdown mark (sfui words have no escapes)."""
    text = re.split(r'["*_`\[]', text)[0].strip()
    return text if len(text) >= 12 else None


def pin(test, md, version):
    secs = sections(md)[:3]
    if not secs or secs[0][0] != version:
        raise ValueError(f"{WHATS_NEW}: the newest section is not '## Version {version}'")
    lines = []
    for v, tag in secs:
        lines.append(f'expect_text "Version {v}"')
        if tag and visible(tag):
            lines.append(f'expect_text "{visible(tag)}"')
    test = re.sub(r"^env SF_GUIDE_VERSION .*$", f"env SF_GUIDE_VERSION {version}", test, count=1, flags=re.M)
    test = re.sub(r"pinned to [0-9][0-9.a-z-]* \(SF_GUIDE_VERSION\), so it checks [0-9][0-9.a-z-]*'s",
                  f"pinned to {version} (SF_GUIDE_VERSION), so it checks {version}'s", test, count=1)
    m = re.search(r"^expect_guide whats-new\.md\n((?:expect_text .*\n)*)", test, re.M)
    if not m:
        raise ValueError(f"{TEST}: no 'expect_guide whats-new.md' followed by its expect_text lines")
    return test[:m.start(1)] + "".join(l + "\n" for l in lines) + test[m.end(1):]


def self_test():
    md = ("# What's new\n\n## Version 2.0.0\n\n```features\ndate: unreleased\ntagline: Faster \"things\" here, now.\n```\n"
          "- x\n\n## Version 1.9.0\n\n```features\ntagline: Plain words for the old one.\n```\n\n## Version 1.8.0\n\n"
          "- no block\n\n## Version 1.7.0\n\ntagline: too old\n")
    test = ("# chapter pinned to 1.9.0 (SF_GUIDE_VERSION), so it checks 1.9.0's section.\nenv SF_GUIDE_VERSION 1.9.0\n"
            "expect_guide whats-new.md\nexpect_text \"Version 1.9.0\"\nexpect_text \"old\"\nexpect_value n == 6\n")
    out = pin(test, md, "2.0.0")
    assert "env SF_GUIDE_VERSION 2.0.0\n" in out and "checks 2.0.0's" in out, out
    assert 'expect_text "Version 2.0.0"\n' in out and 'expect_text "Version 1.8.0"\n' in out, out
    assert "Version 1.7.0" not in out and '"old"' not in out, out
    assert 'expect_text "Plain words for the old one."\n' in out, out
    assert 'expect_text "Faster' not in out, out  # "Faster " is too short to be a useful check
    assert out.endswith("expect_value n == 6\n"), out
    assert pin(out, md, "2.0.0") == out  # idempotent
    try:
        pin(test, md, "2.1.0")
        raise AssertionError("pinned to a version that has no section")
    except ValueError:
        pass
    print("pin_whats_new_test self-test: ok")


def main(argv):
    if argv[1:] == ["--self-test"]:
        self_test()
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    version, check = argv[1], "--check" in argv[2:]
    test = open(TEST, encoding="utf-8").read()
    try:
        new = pin(test, open(WHATS_NEW, encoding="utf-8").read(), version)
    except ValueError as e:
        print(e)
        return 1
    if new == test:
        return 0
    if check:
        print(f"{TEST} is not pinned to {version}: tools/release/pin_whats_new_test.py {version}")
        return 1
    open(TEST, "w", encoding="utf-8").write(new)
    print(TEST)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
