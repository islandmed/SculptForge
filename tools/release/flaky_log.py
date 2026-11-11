#!/usr/bin/env python3
"""The release stage's flaky log (scripts/release.sh --stage): a test that failed, then passed on the stage's one rerun.

  tools/release/flaky_log.py add --log ~/Work/.sf/logs/flaky.md --test NAME --where "PC suite" [--sha S] [--version V]
  tools/release/flaky_log.py --self-test

One table row per rerun. A test with two rows in 7 days goes to the top of the file ("Fix owner needed"), which is
recomputed from the table on every add. Prints `repeat: NAME (n in 7 days)` for such a test, else nothing.
"""
import argparse
import datetime
import os
import sys
import tempfile

HEAD = "# Flaky tests that passed on the stage rerun (scripts/release.sh --stage)\n"
TOP = "## Fix owner needed (passed on rerun twice in 7 days)\n"
TABLE = "## Log\n\n| date | test | where | sha | version |\n|---|---|---|---|---|\n"
WINDOW_DAYS = 7


def rows(text):
    out = []
    for line in text.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("| 20") and len(cells) >= 5:
            out.append(cells[:5])
    return out


def repeats(table, today):
    """{test: count} for tests with 2+ rows in the last WINDOW_DAYS days."""
    since = today - datetime.timedelta(days=WINDOW_DAYS)
    count = {}
    for d, test, *_ in table:
        try:
            when = datetime.date.fromisoformat(d)
        except ValueError:
            continue
        if since <= when <= today:
            count[test] = count.get(test, 0) + 1
    return {t: n for t, n in count.items() if n >= 2}


def render(table, today):
    rep = repeats(table, today)
    top = "".join(f"- **{t}**: {n} reruns in {WINDOW_DAYS} days, needs a fix owner\n" for t, n in sorted(rep.items(), key=lambda x: -x[1]))
    body = "".join(f"| {' | '.join(r)} |\n" for r in table)
    return HEAD + "\n" + TOP + (top or "- (none)\n") + "\n" + TABLE + body


def add(path, test, where, sha, version, today):
    table = rows(open(path, encoding="utf-8").read()) if os.path.exists(path) else []
    table.append([today.isoformat(), test, where.replace("|", "/"), sha, version])
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(render(table, today))
    os.replace(tmp, path)
    return repeats(table, today).get(test, 0)


def self_test():
    d = datetime.date(2026, 11, 11)
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "sub", "flaky.md")
        assert add(p, "ui_a", "PC suite", "abc", "0.12.7", d) == 0
        assert add(p, "ui_b", "CI macOS", "abc", "0.12.7", d) == 0
        assert add(p, "ui_a", "CI Windows", "def", "0.12.8", d + datetime.timedelta(days=3)) == 2
        text = open(p, encoding="utf-8").read()
        assert text.index("**ui_a**") < text.index("| date |"), "a repeat goes above the table"
        assert "ui_b**" not in text.split("## Log")[0], "a single rerun is not a repeat"
        assert len(rows(text)) == 3
        # outside the window: not a repeat
        assert add(p, "ui_b", "PC suite", "x", "0.12.9", d + datetime.timedelta(days=9)) == 0
        assert add(p, "p|ipe", "w", "x", "v", d + datetime.timedelta(days=9)) == 0 and len(rows(open(p, encoding="utf-8").read())) == 5
    print("flaky_log self-test ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    a = sub.add_parser("add")
    a.add_argument("--log", required=True)
    a.add_argument("--test", required=True)
    a.add_argument("--where", required=True)
    a.add_argument("--sha", default="")
    a.add_argument("--version", default="")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.cmd != "add":
        ap.error("add or --self-test")
    n = add(os.path.expanduser(args.log), args.test, args.where, args.sha, args.version, datetime.date.today())
    if n:
        print(f"repeat: {args.test} ({n} in {WINDOW_DAYS} days)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
