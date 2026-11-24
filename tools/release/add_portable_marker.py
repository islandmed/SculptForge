#!/usr/bin/env python3
"""Puts portable.txt next to sculptforge.exe in the Windows portable .zip (and only there).

A file portable.txt beside the program makes SculptForge keep all its data in a data/ folder beside it
(src/core/DataDir.h), so the unzipped folder, on a USB stick or anywhere, carries the workbench. The installer
and the AppImage/.dmg/.deb never get it: they keep per-user settings.

    python3 add_portable_marker.py SculptForge-X.Y.Z-windows-x86_64.zip
"""
import sys
import zipfile

TEXT = (
    "This folder is a portable SculptForge.\r\n"
    "\r\n"
    "While this file (portable.txt) sits next to sculptforge.exe, everything SculptForge saves - settings, brushes,\r\n"
    "layouts, shelves, hotkeys, My Panel, recent files, logs and caches - goes into the data folder next to it,\r\n"
    "so you can move or copy this whole folder (a USB stick, another PC) and your workbench comes along.\r\n"
    "Projects save wherever you choose.\r\n"
    "\r\n"
    "Delete this file to use this computer's own settings instead.\r\n"
)


def add_marker(path):
    with zipfile.ZipFile(path) as z:
        exes = [n for n in z.namelist() if n.lower().endswith("bin/sculptforge.exe")]
        names = set(z.namelist())
    if len(exes) != 1:
        raise SystemExit(f"{path}: expected one bin/sculptforge.exe, found {len(exes)}")
    marker = exes[0][: -len("sculptforge.exe")] + "portable.txt"
    if marker in names:
        return marker
    with zipfile.ZipFile(path, "a", zipfile.ZIP_DEFLATED) as z:
        z.writestr(marker, TEXT)
    return marker


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    print("added", add_marker(sys.argv[1]))
