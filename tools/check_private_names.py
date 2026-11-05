#!/usr/bin/env python3
"""Fails when a private name (the maintainer's own, an employer's) appears in the repository.

The list itself is private, so it is never in the repository: the names come from the environment variable
SF_PRIVATE_NAMES (CI: the repository secret of that name) or, on a developer machine, from the file named by
SF_PRIVATE_NAMES_FILE (default ~/.config/sculptforge/private-names). Names are separated by commas or new lines;
'#' starts a comment. Each is matched case-insensitively as a word, also inside identifiers (name_x, Name2), but
not inside a longer word. Every git-tracked text file and path is scanned (docs/legal/ is allowed), and a hit is
reported by place only: this script never prints a name, so CI logs don't spread them.

  check_private_names.py            check (skips when no names are configured)
  check_private_names.py --require  check, and fail when no names are configured (CI on pushes and dispatches)
  check_private_names.py --self-test
"""
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_marks import read_text, tracked_files  # noqa: E402

ALLOWED_PREFIXES = ("docs/legal/",)
DEFAULT_FILE = Path.home() / ".config" / "sculptforge" / "private-names"


def parse_names(text):
    names = []
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        names += [n.strip() for n in line.split(",") if n.strip()]
    return names


def load_names():
    """The configured names, or [] when there are none."""
    env = os.environ.get("SF_PRIVATE_NAMES", "")
    if env.strip():
        return parse_names(env)
    path = Path(os.environ.get("SF_PRIVATE_NAMES_FILE", DEFAULT_FILE)).expanduser()
    try:
        return parse_names(path.read_text(encoding="utf-8"))
    except OSError:
        return []


def names_regex(names):
    # A word on its own or inside an identifier, never inside a longer word (letters on either side).
    alternatives = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    return re.compile(rf"(?<![A-Za-z])(?:{alternatives})(?![A-Za-z])", re.IGNORECASE)


def lines_with(text, name_re):
    return [n for n, line in enumerate(text.splitlines(), 1) if name_re.search(line)]


def scan(name_re):
    errors = []
    for path in tracked_files():
        if path.startswith(ALLOWED_PREFIXES):
            continue
        if name_re.search(path):
            errors.append(f"{path}: the file name has a private name")
        text = read_text(path)
        if text is None:
            continue
        errors += [f"{path}:{n}: a private name" for n in lines_with(text, name_re)]
    return errors


def self_test():
    names = parse_names("Alpha, Beta Corp # a comment\n\ngamma\n")
    assert names == ["Alpha", "Beta Corp", "gamma"], names
    name_re = names_regex(names)
    for hit in ("alpha", "ALPHA's", "alpha_x", "Alpha2", "x.alpha", "beta corp", "(Gamma)"):
        assert name_re.search(hit), hit
    for miss in ("alphabet", "Alphanumeric", "betacorp", "gammas", "Kalpha"):
        assert not name_re.search(miss), miss
    assert lines_with("a\nb alpha\nc", name_re) == [2]
    print("check_private_names self-test ok")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    names = load_names()
    if not names:
        if "--require" in argv:
            print("check_private_names: no names configured (SF_PRIVATE_NAMES)", file=sys.stderr)
            return 1
        print("check_private_names: skipped, no names configured")
        return 0
    if tracked_files() is None:
        print("check_private_names: skipped, git can't list the tracked files here")
        return 0
    errors = scan(names_regex(names))
    for e in errors:
        print(e)
    if errors:
        print(f"\n{len(errors)} private name(s). Use a role instead (\"the maintainer\"); "
              "only docs/legal/ may name people.", file=sys.stderr)
        return 1
    print(f"check_private_names: none of the {len(names)} private name(s) in the tracked files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
