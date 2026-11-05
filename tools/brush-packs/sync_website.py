#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the built-in brush packs (src/core/BrushPacks.cpp) into website/brushes.html.

    python3 sync_website.py                 # rewrite the #packs cards between the packs markers
    python3 sync_website.py --thumbs DIR    # also copy the previews: DIR/recipe-<Brush Name>.png, as the app
                                            # writes them with SCULPTFORGE_THUMBNAIL_CACHE=DIR (needs Pillow)
    python3 sync_website.py --check         # exit 1 if the page, its previews or the guide links are out of date

One section per pack, in the app's order; the brushes A-Z inside it, like the app's default sort. Each card is
the app's own brush thumbnail (192 px, shown at 96), the name and its one-line description.
"""

import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "core" / "BrushPacks.cpp"
PAGE = ROOT / "website" / "brushes.html"
IMAGES = ROOT / "website" / "img" / "brush-packs"
GUIDE = ROOT / "website" / "guide" / "brush-packs.html"
START, END = "<!-- packs:start -->", "<!-- packs:end -->"
PACK_FUNCTIONS = ["clayAndForm", "hardSurface", "organicAndSkin", "clothAndFolds", "damageAndWear", "ornament"]


def read_packs():
    """[(pack, help, [(brush, description), ...]), ...] in the app's pack order."""
    text = SOURCE.read_text(encoding="utf-8")
    table = text.split("kPacks[] = {", 1)[1].split("};", 1)[0]
    packs = re.findall(r'\{"([^"]+)", "([^"]+)"\}', table)
    order = re.findall(r"(\w+)\(\{out, kPacks\[(\d)\]\.name\}\)", text)
    if [f for f, _ in order] != PACK_FUNCTIONS or len(packs) != len(PACK_FUNCTIONS):
        sys.exit("sync_website.py: BrushPacks.cpp changed shape; update PACK_FUNCTIONS")
    out = []
    for func, index in order:
        body = text.split(f"void {func}(PackBuilder b) {{", 1)[1].split("\n}\n", 1)[0]
        brushes = re.findall(r'\bb\("([^"]+)",\s*"([^"]+)",', body)
        name, help_ = packs[int(index)]
        out.append((name, help_, sorted(brushes, key=lambda b: natural_key(b[0]))))
    return out


def natural_key(s):
    """Case-insensitive, numbers by value: the app's naturalLess (core/ListSort.h)."""
    return [(0, int(t), "") if t.isdigit() else (1, 0, t) for t in re.split(r"(\d+)", s.lower()) if t]


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower().replace("'", "")).strip("-")


def guide_anchor(name):
    """The guide's heading ids (tools/guide/build_site.py slugify): "Clay & Form" -> "clay--form"."""
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", name.lower()).strip())


def section(packs):
    e = lambda s: html.escape(s, quote=True)
    lines = [START]
    for name, help_, brushes in packs:
        lines.append(f'        <div class="pack" id="pack-{slug(name)}">')
        lines.append(f'          <div class="pack-head"><h3>{e(name)}</h3><p>{e(help_)} '
                     f'<a href="guide/brush-packs.html#{guide_anchor(name)}">In the guide</a></p></div>')
        lines.append('          <ul class="pack-grid">')
        for brush, desc in brushes:
            lines.append(f'            <li class="pack-brush"><img src="img/brush-packs/{slug(brush)}.webp" alt="{e(brush)} '
                         f'brush preview" width="96" height="96" loading="lazy"><div><h4>{e(brush)}</h4>'
                         f'<p>{e(desc)}</p></div></li>')
        lines.append("          </ul>")
        lines.append("        </div>")
    lines.append(f"        {END}")
    return "\n".join(lines)


def rewrite(page, packs):
    a, b = page.index(START), page.index(END) + len(END)
    page = page[:a] + section(packs) + page[b:]
    total = sum(len(p[2]) for p in packs)
    return re.sub(r"<!-- packs:count -->.*?<!-- /packs:count -->",
                  f"<!-- packs:count -->{total} brushes in {len(packs)} packs<!-- /packs:count -->", page)


def copy_thumbs(folder, packs):
    from PIL import Image  # only to make the previews
    IMAGES.mkdir(parents=True, exist_ok=True)
    for _, _, brushes in packs:
        for brush, _ in brushes:
            src = Path(folder) / f"recipe-{brush}.png"
            if not src.exists():
                sys.exit(f"sync_website.py: no preview {src}")
            Image.open(src).convert("RGBA").save(IMAGES / f"{slug(brush)}.webp", quality=82, method=6)


def main():
    args = sys.argv[1:]
    packs = read_packs()
    page = PAGE.read_text(encoding="utf-8")
    if "--thumbs" in args:
        copy_thumbs(args[args.index("--thumbs") + 1], packs)
    new = rewrite(page, packs)
    if "--check" in args:
        problems = []
        if new != page:
            problems.append("website/brushes.html is out of date with BrushPacks.cpp")
        wanted = {f"{slug(b)}.webp" for _, _, bs in packs for b, _ in bs}
        have = {p.name for p in IMAGES.glob("*.webp")}
        problems += [f"missing preview img/brush-packs/{n}" for n in sorted(wanted - have)]
        problems += [f"stale preview img/brush-packs/{n}" for n in sorted(have - wanted)]
        guide = GUIDE.read_text(encoding="utf-8")
        problems += [f"guide has no #{guide_anchor(n)}" for n, _, _ in packs if f'id="{guide_anchor(n)}"' not in guide]
        for p in problems:
            print("sync_website.py:", p)
        if problems:
            print("Fix: render the previews (SCULPTFORGE_THUMBNAIL_CACHE=DIR, open Sculpt > Brush), then "
                  "python3 tools/brush-packs/sync_website.py --thumbs DIR")
        return 1 if problems else 0
    PAGE.write_text(new, encoding="utf-8")
    print(f"brushes.html: {sum(len(p[2]) for p in packs)} pack brushes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
