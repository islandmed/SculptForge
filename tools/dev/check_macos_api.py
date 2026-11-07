#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails on library calls the macOS package can't use: it builds for macOS 11 (packages.yml,
CMAKE_OSX_DEPLOYMENT_TARGET=11.0), and libc++ marks some of them unavailable below a later macOS, a build error that
only the Packages macOS job shows (Linux, Windows and CI's macOS job build without that target). 0.11.10: Units.cpp's
std::to_chars(float) stopped the .dmg.

    python3 check_macos_api.py            (ctest macos_api)
    python3 check_macos_api.py --self-test
Standard library only.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
# std::to_chars / std::from_chars on float or double need macOS 13.3. A line can't show its argument's type (0.11.10:
# `std::to_chars(buf, buf + sizeof buf, v)` with a float v), so every call is flagged unless the line says it is an
# integer one with a `// macos-api: integer` comment.
FLOAT_CHARCONV = re.compile(r"std::(to_chars|from_chars)\b")
INTEGER_OK = "macos-api: integer"


def problems(files):
    out = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            code = line.split("//", 1)[0]
            if FLOAT_CHARCONV.search(code) and INTEGER_OK not in line:
                out.append(f"{path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}:{lineno}: "
                           f"std::to_chars/from_chars: the float overloads need macOS 13.3 (the package targets 11): "
                           f"use snprintf/strtof, or mark an integer call `// macos-api: integer`")
    return out


def main(argv):
    if "--self-test" in argv:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "bad.cpp"
            bad.write_text("void f(float v) {\n  auto r = std::to_chars(b, b + 32, v);\n}\n")
            ok = Path(tmp) / "ok.cpp"
            ok.write_text("void g(int n) {\n  auto r = std::to_chars(b, b + 32, n); // macos-api: integer\n"
                          "  // std::to_chars(float) note\n}\n")
            assert len(problems([bad])) == 1 and not problems([ok])
        print("check_macos_api self-test ok")
        return 0
    files = sorted(p for p in (ROOT / "src").rglob("*") if p.suffix in (".cpp", ".h", ".hpp", ".mm"))
    found = problems(files)
    for p in found:
        print(p)
    if found:
        return 1
    print(f"check_macos_api: {len(files)} files, no macOS-13.3-only float charconv")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
