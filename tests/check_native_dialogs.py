#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Every system dialog and URL goes through src/app/NativeDialog.cpp, so none opens on a desktop during UI tests, on
the offscreen driver or with SF_NO_DIALOGS=1 (an agent's test run once opened real file dialogs on a desktop through
xdg-desktop-portal). Fails, naming file:line, on any direct SDL_Show*Dialog / SDL_ShowFileDialogWithProperties /
SDL_Show*MessageBox / SDL_OpenURL call elsewhere under src/.

  tests/check_native_dialogs.py [repo root]      tests/check_native_dialogs.py --self-test
"""
import os
import re
import sys

CALL = re.compile(r"\bSDL_(Show\w*Dialog\w*|ShowFileDialogWithProperties|Show\w*MessageBox|OpenURL)\s*\(")
ALLOWED = {os.path.join("src", "app", "NativeDialog.cpp")}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def offenders(root):
    out = []
    for d, _, files in os.walk(os.path.join(root, "src")):
        for f in files:
            if not f.endswith((".cpp", ".h", ".mm", ".m", ".c", ".hpp")):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, root)
            if rel in ALLOWED:
                continue
            with open(path, encoding="utf-8", errors="replace") as fh:
                for n, line in enumerate(strip_comments(fh.read()).split("\n"), 1):
                    if CALL.search(line):
                        out.append(f"{rel}:{n}: {line.strip()}")
    return out


def self_test():
    assert CALL.search("SDL_ShowOpenFileDialog(cb, this")
    assert CALL.search("  if (!SDL_OpenURL (url))")
    assert CALL.search("SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR")
    assert not CALL.search("sf::dialogs::openFile(cb")
    assert not CALL.search(strip_comments("// for SDL_OpenURL (\"\" without a log)"))
    print("check_native_dialogs self-test ok")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()
    root = next((a for a in sys.argv[1:] if not a.startswith("-")), os.path.join(os.path.dirname(__file__), ".."))
    bad = offenders(os.path.abspath(root))
    for b in bad:
        print(f"{b}\n    call sf::dialogs:: (src/app/NativeDialog.h) instead: it never opens a dialog in tests")
    if bad:
        print(f"{len(bad)} direct system dialog / URL call(s)")
        return 1
    print("check_native_dialogs: every dialog and URL goes through NativeDialog")
    return 0


if __name__ == "__main__":
    sys.exit(main())
