#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rewrite ZBrush-style palette paths in the guide to the SculptForge layout's task menus.

"Preferences > Input" becomes "Edit > Preferences > Input", "Material > Load Lit Sphere..." becomes
"Render > Materials > Load Lit Sphere...", and so on (src/app/Palettes.cpp, taskMenus). Tool paths stay: the Tool
palette is in the right tray in both layouts. Run it after merging a chapter written with the old paths:

    python3 tools/guide/menu_paths.py            # rewrite docs/guide/*.md in place
    python3 tools/guide/menu_paths.py --check    # exit 1 if any old path is left (ctest guide_menu_paths)
"""
import argparse
import pathlib
import re
import sys

GUIDE = pathlib.Path(__file__).resolve().parents[2] / "docs" / "guide"

# Palette -> its place in the task menus. A menu with one section (Paint, View, Help) has no section name.
MENUS = {
    "Preferences": "Edit > Preferences",
    "Brush": "Sculpt > Brush",
    "Draw": "Sculpt > Draw & Tablet",
    "Stroke": "Sculpt > Stroke",
    "Alpha": "Sculpt > Alpha",
    "Transform": "Sculpt > Symmetry & Gizmo",
    "Color": "Paint",
    "Generate": "Mesh > Generate 3D",
    "Render": "View",
    "Material": "Render > Materials",
    "Splats": "Render > Gaussian Splats",
    "Script": "File > Script",
}
# Pages that keep the old names on purpose: release history, and the chapter that explains both layouts.
SKIP = {"whats-new.md"}

# "Preferences > X" anywhere (bold, tables, plain text), but not when already behind a menu ("Edit > Preferences"),
# the new Render menu, Blender's own "Preferences > Add-ons", or ZBrush's "Preferences > Mem".
NAMES = "|".join(n + "(?! > (?:Materials|Gaussian Splats))" if n == "Render" else n for n in MENUS)  # new Render menu
PATH = re.compile(r"(?<!> )\b(" + NAMES + r") > (?=[A-Z0-9+])(?!Add-ons|Mem\b)")
# "the Color palette", "**Brush palette**": the menu (and section) that holds it.
PHRASE = re.compile(r"(\*\*)?\b([Tt]he )?(" + "|".join(MENUS) + r") palette\b(\*\*)?")


def phrase(m: re.Match) -> str:
    where = MENUS[m.group(3)]
    if ">" in where:
        return "**" + where + "**"  # "in the Draw palette" -> "in **Sculpt > Draw & Tablet**"
    return (m.group(2) or "") + "**" + where + "** menu"


BOLD_NAME = re.compile(r"\*\*(" + "|".join(MENUS) + r")\*\* palette\b")  # "the **Draw** palette"


def convert(text: str) -> str:
    out = []
    zbrush_note = generated = False
    for line in text.split("\n"):
        # Tables generated from the app (update_shortcuts.py) quote its own help text; the app owns those words.
        if line.startswith("<!-- BEGIN GENERATED"):
            generated = True
        elif line.startswith("<!-- END GENERATED"):
            generated = False
        if generated:
            out.append(line)
            continue
        # "> **ZBrush-style layout:** ..." notes name the palettes of that layout on purpose.
        zbrush_note = line.startswith("> **ZBrush-style layout") or (zbrush_note and line.startswith(">"))
        if zbrush_note:
            out.append(line)
            continue
        line = BOLD_NAME.sub(r"\1 palette", line)
        if line.startswith("| ") and line.count("|") > 2:
            # Tables: leave the first cell (other apps' names in the glossary and the ZBrush chapter).
            first, rest = line[2:].split("|", 1)
            line = "| " + first + "|" + PHRASE.sub(phrase, PATH.sub(lambda m: MENUS[m.group(1)] + " > ", rest))
        elif line.startswith("!["):
            # Image captions name what the picture shows: a palette in a tray keeps its palette name.
            line = PATH.sub(lambda m: MENUS[m.group(1)] + " > ", line)
        else:
            line = PHRASE.sub(phrase, PATH.sub(lambda m: MENUS[m.group(1)] + " > ", line))
        out.append(line)  # one pass each: a rewritten "Render > Materials" is not read again as "Render >"
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report old paths and exit 1, change nothing")
    args = ap.parse_args()
    stale = 0
    for md in sorted(GUIDE.glob("*.md")):
        if md.name in SKIP:
            continue
        text = md.read_text(encoding="latin-1")
        new = convert(text)
        if new == text:
            continue
        if args.check:
            for n, (a, b) in enumerate(zip(text.split("\n"), new.split("\n")), 1):
                if a != b:
                    print(f"{md.name}:{n}: {a.strip()[:100]}")
                    stale += 1
        else:
            md.write_text(new, encoding="latin-1")
    if args.check and stale:
        print(f"{stale} line(s) use ZBrush-style palette paths; run tools/guide/menu_paths.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
