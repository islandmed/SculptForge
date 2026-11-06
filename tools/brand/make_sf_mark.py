#!/usr/bin/env python3
"""RETIRED (Tony 11-06: the cube is the app icon again, docs/brand.md); kept for history, nothing shipped uses it.
The SF mark, the app icon and the site's monogram (Tony 11-05): on the forge-orange tile (#f28c3c, rx 13 of 56), a dark "S" (#141416) and a
white "F" in Archivo Black, 28 of 56, set apart (S centred at x 17, F at x 39, baseline 39). The letters are paths,
so the header and favicons look the same whatever fonts load.

    uv run --no-project --with fonttools python -I tools/brand/make_sf_mark.py <ArchivoBlack-Regular.ttf> tools/brand
    (github.com/google/fonts ofl/archivoblack, SIL OFL 1.1; THIRD_PARTY_NOTICES.md)

Writes sf-mark.svg (the mark), sf-mark-small.svg (16-32 px), sf-mark-square.svg (full bleed: apple-touch-icon,
Discord avatar) and sf-mark-macos(-small).svg (Apple's icon grid). tools/brand/make_icons.py renders the app icon set and the
site's favicons from them (rsvg-convert); the site header has the sf-mark.svg <rect> and paths inline (class="sf-mark"
in the pages and tools/guide/build_site.py).
"""
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

# (size, baseline, S centre, F centre) in the 56-unit tile.
REGULAR = (28.0, 39.0, 17.0, 39.0)
SMALL = (33.0, 41.5, 16.0, 40.5)  # 16-32 px: bigger letters, the tile edge to edge, so they stay crisp
TILE = "#f28c3c"


def letter_paths(font_path, spec):
    size, baseline, s_x, f_x = spec
    f = TTFont(font_path)
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    k = size / f["head"].unitsPerEm
    out = []
    for ch, cx, fill in (("S", s_x, "#141416"), ("F", f_x, "#ffffff")):
        g = gs[cmap[ord(ch)]]
        pen = SVGPathPen(gs, ntos=lambda v: ("%.2f" % v).rstrip("0").rstrip("."))
        # Centred on its advance at cx (as text-anchor="middle"), y up to y down at the baseline.
        g.draw(TransformPen(pen, (k, 0, 0, -k, cx - g.width * k / 2, baseline)))
        out.append(f'<path fill="{fill}" d="{pen.getCommands()}"/>')
    return "".join(out)


def write(path, body):
    with open(path, "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56">{body}</svg>\n')


def main():
    font, out = sys.argv[1], sys.argv[2]
    letters = letter_paths(font, REGULAR)
    # The mark (site header, favicons from 48 px, the app icon from 48 px).
    write(out + "/sf-mark.svg", f'<rect x="1" y="1" width="54" height="54" rx="13" fill="{TILE}"/>' + letters)
    # 16-32 px (favicons, the app icon's small sizes, the title bar).
    write(out + "/sf-mark-small.svg",
          f'<rect width="56" height="56" rx="11" fill="{TILE}"/>' + letter_paths(font, SMALL))
    # Full bleed, square: for masks that round or circle it themselves (apple-touch-icon, Discord avatars).
    write(out + "/sf-mark-square.svg", f'<rect width="56" height="56" fill="{TILE}"/>' + letters)
    # macOS: Apple's icon grid (an 824 px tile centred in 1024, corner 185), so it sits with the other Dock icons;
    # -small with the small cut's letters (the .icns's 16 and 32 px).
    g, r = 56 * 824 / 1024, 56 * 185 / 1024
    o = (56 - g) / 2
    for name, spec in (("sf-mark-macos", REGULAR), ("sf-mark-macos-small", SMALL)):
        write(f"{out}/{name}.svg",
              f'<rect x="{o:.3f}" y="{o:.3f}" width="{g:.3f}" height="{g:.3f}" rx="{r:.3f}" fill="{TILE}"/>'
              f'<g transform="translate({o:.3f} {o:.3f}) scale({g / 56:.5f})">{letter_paths(font, spec)}</g>')


if __name__ == "__main__":
    main()
