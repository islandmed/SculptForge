#!/usr/bin/env python3
"""Writes assets/fonts/NotoSansTC-UI.otf: Noto Sans CJK TC (SIL OFL) cut down to the characters the Traditional Chinese
table uses (src/app/I18nTraditionalChinese.cpp) plus CJK punctuation, so the binary carries a few hundred KB, not 16 MB.

Run it after changing the table; the test i18n_chinese_table_fits_the_font lists what the font is missing.

  subset_cjk_font.py [--source NotoSansCJK-Regular.ttc]   (default: the Linux noto-fonts-cjk package's file)
Needs fontTools (pip install fonttools).
"""
import argparse
import re
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTCollection

ROOT = Path(__file__).resolve().parent.parent.parent
TABLE = ROOT / "src/app/I18nTraditionalChinese.cpp"
OUT = ROOT / "assets/fonts/NotoSansTC-UI.otf"
TC_FACE = "Noto Sans CJK TC"
# Always kept: CJK and full-width punctuation the translations (and future edits) reach for.
PUNCTUATION = "，。、：；！？（）「」『』《》〈〉【】－～…·—％／＋"


def table_characters():
    text = TABLE.read_text(encoding="utf-8")
    chars = set()
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
        chars.update(c for c in literal if ord(c) > 0xFF)
    return chars | set(PUNCTUATION)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default="/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc")
    args = ap.parse_args()
    collection = TTCollection(args.source)
    font = next((f for f in collection.fonts if f["name"].getDebugName(4) == TC_FACE), None)
    if font is None:
        raise SystemExit(f"{args.source}: no '{TC_FACE}' face")
    chars = sorted(table_characters())
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
    font.save(OUT)
    print(f"{OUT.relative_to(ROOT)}: {len(chars)} characters, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
