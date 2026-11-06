#!/usr/bin/env python3
"""The site's SF monogram (Tony 11-05): on the forge-orange tile (#f28c3c, rx 13 of 56), a dark "S" (#141416) and a
white "F" in Archivo Black, 28 of 56, set apart (S centred at x 17, F at x 39, baseline 39). The letters are paths,
so the header and favicons look the same whatever fonts load.

    uv run --no-project --with fonttools python -I tools/brand/make_sf_mark.py <ArchivoBlack-Regular.ttf> tools/brand
    (github.com/google/fonts ofl/archivoblack, SIL OFL 1.1; THIRD_PARTY_NOTICES.md)

Writes sf-mark.svg. Then the favicons: rsvg-convert -w N sf-mark.svg for 16, 32, 48 and 64; the 180 px
apple-touch-icon full-bleed (without the rx: iOS rounds it); website/favicon.ico = 48/32/16; and the same <rect> and
paths inline in the site pages' header and tools/guide/build_site.py (class="sf-mark").
"""
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

SIZE, BASELINE = 28.0, 39.0
LETTERS = (("S", 17.0, "#141416"), ("F", 39.0, "#ffffff"))


def letter_paths(font_path):
    f = TTFont(font_path)
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    k = SIZE / f["head"].unitsPerEm
    out = []
    for ch, cx, fill in LETTERS:
        g = gs[cmap[ord(ch)]]
        pen = SVGPathPen(gs, ntos=lambda v: ("%.2f" % v).rstrip("0").rstrip("."))
        # Centred on its advance at cx (as text-anchor="middle"), y up to y down at the baseline.
        g.draw(TransformPen(pen, (k, 0, 0, -k, cx - g.width * k / 2, BASELINE)))
        out.append(f'<path fill="{fill}" d="{pen.getCommands()}"/>')
    return "".join(out)


def main():
    font, out = sys.argv[1], sys.argv[2]
    body = '<rect x="1" y="1" width="54" height="54" rx="13" fill="#f28c3c"/>' + letter_paths(font)
    with open(out + "/sf-mark.svg", "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56">{body}</svg>\n')
    print(len(body))


if __name__ == "__main__":
    main()
