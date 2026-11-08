#!/usr/bin/env python3
"""The filled cube: the alternative app icon (the shipped one is the outline cube in tools/brand/cube-outline/, owner
11-06 21:1x; docs/brand.md). An orange cube seen corner on (three faces: light top, forge-orange left, dark right, split
by dark seams) on a dark tile. Our own drawing: six points of a regular hexagon and its centre, nothing traced or taken
from an icon set.

    python -I tools/brand/make_cube_mark.py tools/brand

Writes cube-mark.svg (the mark), cube-mark-small.svg (16-32 px: a bigger cube, no inner rim), cube-mark-square.svg
(full bleed: apple-touch-icon, Discord avatar) and cube-mark-macos(-small).svg (Apple's icon grid).
`tools/brand/make_icons.py ... --style filled` renders the app icon set and the site's favicons from them (rsvg-convert).
"""
import math
import sys

TILE = ("#26252a", "#121114")  # top to bottom
TOP, LEFT, RIGHT, SEAM = "#ffb877", "#f28c3c", "#b8561a", "#141416"
# (cube radius: centre to corner, seam width, centre y) in the 56-unit tile.
REGULAR = (17.5, 1.5, 28.6)
SMALL = (23.0, 2.2, 28.6)  # 16-32 px: the cube fills the tile, thicker seams so they survive at 16 px


def f(v):
    return ("%.2f" % v).rstrip("0").rstrip(".")


def cube(spec):
    r, seam, cy = spec
    cx, c = 28.0, r * math.cos(math.pi / 6)
    top, bottom = (cx, cy - r), (cx, cy + r)
    ul, ur = (cx - c, cy - r / 2), (cx + c, cy - r / 2)
    ll, lr = (cx - c, cy + r / 2), (cx + c, cy + r / 2)
    mid = (cx, cy)

    def poly(pts, fill):
        return f'<path fill="{fill}" d="M' + "L".join(f"{f(x)} {f(y)}" for x, y in pts) + 'Z"/>'

    # The seams: the outline (round corners) and the three inner edges meeting at the near corner.
    edges = "M" + "L".join(f"{f(x)} {f(y)}" for x, y in (top, ur, lr, bottom, ll, ul)) + "Z"
    edges += "".join(f"M{f(mid[0])} {f(mid[1])}L{f(x)} {f(y)}" for x, y in (ul, ur, bottom))
    return (poly((top, ur, mid, ul), TOP) + poly((ul, mid, bottom, ll), LEFT) + poly((mid, ur, lr, bottom), RIGHT)
            + f'<path fill="none" stroke="{SEAM}" stroke-width="{f(seam)}" stroke-linejoin="round" d="{edges}"/>')


def tile(x, y, w, rx):
    return (f'<rect x="{f(x)}" y="{f(y)}" width="{f(w)}" height="{f(w)}" rx="{f(rx)}" fill="url(#cube-bg)"/>')


def write(path, body):
    grad = (f'<defs><linearGradient id="cube-bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{TILE[0]}"/>'
            f'<stop offset="1" stop-color="{TILE[1]}"/></linearGradient></defs>')
    with open(path, "w") as fh:
        fh.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56">{grad}{body}</svg>\n')


def main():
    out = sys.argv[1]
    # The mark (site header, favicons from 48 px, the app icon from 48 px): a thin light rim keeps the dark tile apart
    # from dark task bars and pages.
    rim = '<rect x="1.5" y="1.5" width="53" height="53" rx="12.5" fill="none" stroke="#ffffff" stroke-opacity=".14"/>'
    write(out + "/cube-mark.svg", tile(1, 1, 54, 13) + rim + cube(REGULAR))
    # 16-32 px (favicons, the app icon's small sizes, the title bar).
    write(out + "/cube-mark-small.svg", tile(0, 0, 56, 11) + cube(SMALL))
    # Full bleed, square: for masks that round or circle it themselves (apple-touch-icon, Discord avatars).
    write(out + "/cube-mark-square.svg", tile(0, 0, 56, 0) + cube(REGULAR))
    # macOS: Apple's icon grid (an 824 px tile centred in 1024, corner 185), so it sits with the other Dock icons.
    g, r = 56 * 824 / 1024, 56 * 185 / 1024
    o = (56 - g) / 2
    for name, spec in (("cube-mark-macos", REGULAR), ("cube-mark-macos-small", SMALL)):
        write(f"{out}/{name}.svg",
              tile(o, o, g, r) + f'<g transform="translate({f(o)} {f(o)}) scale({g / 56:.5f})">{cube(spec)}</g>')


if __name__ == "__main__":
    main()
