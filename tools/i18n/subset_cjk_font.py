#!/usr/bin/env python3
"""Writes the two Chinese UI fonts, Noto Sans CJK (SIL OFL) cut down to the characters each table uses plus CJK
punctuation, so the binary carries a few hundred KB, not 16 MB:
  assets/fonts/NotoSansTC-UI.otf  Noto Sans CJK TC, for src/app/I18nTraditionalChinese.cpp (Taiwan glyph shapes)
  assets/fonts/NotoSansSC-UI.otf  Noto Sans CJK SC, for src/app/I18nSimplifiedChinese.cpp (mainland glyph shapes)

Run it after changing a table; the test i18n_chinese_table_fits_the_font lists what a font is missing.

  subset_cjk_font.py [--source NotoSansCJK-Regular.ttc]   (default: the Linux noto-fonts-cjk package's file)
Needs fontTools (pip install fonttools).
"""
import argparse
import re
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTCollection

ROOT = Path(__file__).resolve().parent.parent.parent
# (face in the collection, table, output, the language's own name: the Preferences button, I18n.cpp languageName)
FONTS = [
    ("Noto Sans CJK TC", "src/app/I18nTraditionalChinese.cpp", "assets/fonts/NotoSansTC-UI.otf", "繁體中文"),
    ("Noto Sans CJK SC", "src/app/I18nSimplifiedChinese.cpp", "assets/fonts/NotoSansSC-UI.otf", "简体中文"),
]
# Always kept: CJK and full-width punctuation the translations (and future edits) reach for.
PUNCTUATION = "，。、：；！？（）「」『』《》〈〉【】－～…·—％／＋"


def table_characters(table, name):
    text = (ROOT / table).read_text(encoding="utf-8")
    chars = set()
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
        chars.update(c for c in literal if ord(c) > 0xFF)
    return chars | set(PUNCTUATION) | set(name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default="/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc")
    args = ap.parse_args()
    for face, table, out, name in FONTS:
        write_subset(args.source, face, table_characters(table, name), ROOT / out)


def write_subset(source, face, chars, out):
    collection = TTCollection(source)  # read again per face: subsetting changes the shared tables
    font = next((f for f in collection.fonts if f["name"].getDebugName(4) == face), None)
    if font is None:
        raise SystemExit(f"{source}: no '{face}' face")
    chars = sorted(chars)
    cmap = font.getBestCmap()
    missing = [c for c in chars if ord(c) not in cmap]
    if missing:
        raise SystemExit("not in the source font: " + "".join(missing))
    options = subset.Options()
    options.layout_features = []  # ImGui draws glyph by glyph: no shaping tables
    options.hinting = False
    options.name_IDs = ["*"]  # keep the copyright and OFL notice
    options.notdef_outline = True
    sub = subset.Subsetter(options)
    sub.populate(unicodes=[ord(c) for c in chars])
    sub.subset(font)
    font.flavor = None
    font.save(out)
    print(f"{out.relative_to(ROOT)}: {len(chars)} characters, {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
