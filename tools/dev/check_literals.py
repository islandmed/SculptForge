#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails when a C++ raw string literal in src/ is longer than MSVC accepts in one piece.

MSVC stops at about 16,380 characters a literal (error C2026, "string too big"); Linux and macOS compilers don't, so
a long shader only breaks the Windows build. Split it into adjacent literals, R"(...)" R"(...)": the compiler joins
them (up to 64K) and the text stays the same.

    tools/dev/check_literals.py           exit 1 if any literal is over LIMIT (ctest raw_literal_size)
    tools/dev/check_literals.py --list N  every literal over N characters, largest first
"""
import re
import sys
from pathlib import Path

LIMIT = 16000  # (MSVC's ~16,380, with room)
ROOT = Path(__file__).resolve().parents[2]
RAW = re.compile(r'R"([^()\\\s]{0,16})\((.*?)\)\1"', re.S)


def literals():
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in {".cpp", ".h", ".hpp", ".cc", ".inl"} or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in RAW.finditer(text):
            yield path.relative_to(ROOT), text.count("\n", 0, m.start()) + 1, len(m.group(2))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--list":
        for path, line, n in sorted(literals(), key=lambda x: -x[2]):
            if n > int(sys.argv[2]):
                print(f"{path}:{line}: {n} characters")
        return 0
    over = [(p, l, n) for p, l, n in literals() if n > LIMIT]
    for path, line, n in over:
        print(f"{path}:{line}: raw string literal of {n} characters, over {LIMIT} (MSVC C2026): split it, R\"(...)\" R\"(...)\"")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
