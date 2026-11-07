#!/usr/bin/env python3
"""Builds every SculptForge icon and logo file.

    python -I tools/brand/make_icons.py <bust.jpeg> [--discord DIR] [--style outline|filled]
    python -I tools/brand/make_icons.py --icns-only [--style outline|filled]

Which mark goes where: docs/brand.md. The app icon is the outline cube (Tony 11-06 21:1x: "I want that back as the icon
as it was before"): the files that shipped in 0.1-0.10, frozen in tools/brand/cube-outline/ (Lucide's "box" on a dark
tile, ISC, THIRD_PARTY_NOTICES.md). Those sizes are copied byte for byte (512/1024/256 PNGs, the .ico's frames); the
others (24 px, the .icns on Apple's grid, apple-touch-icon) are its SVG rendered with rsvg-convert. --style filled
builds the alternative instead: our drawn filled cube, tools/brand/cube-mark*.svg (make_cube_mark.py). The SF letters
(make_sf_mark.py) are retired.
The bust (<bust.jpeg>: the concept art, a rounded-square tile on a dark backdrop, found from its light rim) is the
brand: assets/branding/bust/ (About, What's New, the support card), the site header's lockup and What's New cards,
the press kit, the social previews and, with --discord DIR, the files uploaded by hand (Discord bot avatar and server
icon, GitHub avatar).
"""
import argparse
import io
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icns  # noqa: E402  (tools/brand/icns.py)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SS = 4  # supersampling of masks and drawn shapes
BASE = 1105.0  # the measured tile size the corner profile is sampled at (set by master_from_concept)


def tile_box(im):
    """The tile's outer edge (x0, y0, size) from its light rim, scanning in from the middle of each side."""
    g = im.convert("L")
    p = g.load()
    w, h = g.size
    cx, cy = w // 2, h // 2

    def first(coords, base):
        for i, (x, y) in enumerate(coords):
            if p[x, y] > base + 25:
                return i
        sys.exit("make_icons: no tile rim found")

    def base_at(x, y):
        return p[x, y]

    left = first([(x, cy) for x in range(w // 6, cx)], base_at(w // 6, cy)) + w // 6
    right = w - 1 - first([(x, cy) for x in range(w - 1 - w // 6, cx, -1)], base_at(w - 1 - w // 6, cy)) - w // 6
    top = first([(cx, y) for y in range(h // 12, cy)], base_at(cx, h // 12)) + h // 12
    bottom = h - 1 - first([(cx, y) for y in range(h - 1 - h // 12, cy, -1)], base_at(cx, h - 1 - h // 12)) - h // 12
    size = ((right - left) + (bottom - top)) / 2 + 1
    return left - 0.5, top - 0.5, size


def corner_profile(im, x0, y0, size):
    """How far the rim sits in from the tile's top edge, per column near the corner (in tile fractions)."""
    g = im.convert("L")
    p = g.load()
    inside = p[int(x0 + size / 2), int(y0 + size * 0.02)]
    prof = []
    for i in range(int(size * 0.3)):
        x = int(x0 + i)
        d = None
        for y in range(int(y0) - 3, int(y0 + size * 0.3)):
            if p[x, y] > max(85, inside * 1.2):
                d = y - y0
                break
        prof.append(d)
    # Columns whose rim scan failed (the backdrop is never brighter than the rim) take their neighbour's value.
    out, last = [], size * 0.3
    for d in prof:
        last = d if d is not None else last
        out.append(max(0.0, last) / size)
    # Monotone (the edge only falls towards the straight part).
    for i in range(1, len(out)):
        out[i] = min(out[i], out[i - 1])
    return out


def rounded_mask(n, profile, inset=0.0):
    """An anti-aliased mask n px wide: the tile's corner profile on all four corners, `inset` px in."""
    k = len(profile)

    def f(t):  # rim depth at distance t from the side (tile fractions)
        i = t * BASE
        if i >= k - 1:
            return 0.0
        a = int(i)
        return profile[a] + (profile[a + 1] - profile[a]) * (i - a)

    def finv(x):  # the largest t with f(t) >= x
        lo, hi = 0.0, k / BASE
        for _ in range(40):
            mid = (lo + hi) / 2
            lo, hi = (mid, hi) if f(mid) >= x else (lo, mid)
        return lo

    reach = k / BASE
    steps = 400
    corner = []  # the top-left corner, from the left side to the top side
    for i in range(steps + 1):
        t = reach * i / steps
        corner.append((t, max(f(t), finv(t))))
    m = n * SS
    ins = inset / n

    def P(x, y):
        return (ins + x * (1 - 2 * ins)) * m, (ins + y * (1 - 2 * ins)) * m

    pts = [P(x, y) for x, y in corner]  # top-left
    pts += [P(1 - x, y) for x, y in reversed(corner)]  # top-right
    pts += [P(1 - x, 1 - y) for x, y in corner]  # bottom-right
    pts += [P(x, 1 - y) for x, y in reversed(corner)]  # bottom-left
    big = Image.new("L", (m, m), 0)
    ImageDraw.Draw(big).polygon(pts, fill=255)
    return big.resize((n, n), Image.LANCZOS)


def master_from_concept(path):
    src = Image.open(path).convert("RGB")
    x0, y0, size = tile_box(src)
    prof = corner_profile(src, x0, y0, size)
    global BASE
    BASE = size
    n = 1024
    tile = src.resize((n, n), Image.LANCZOS, box=(x0, y0, x0 + size, y0 + size))
    mask = rounded_mask(n, prof, inset=1.0)
    out = tile.convert("RGBA")
    out.putalpha(mask)
    print(f"tile at x {x0:.1f} y {y0:.1f}, {size:.1f} px; corner reach {len(prof)} px")
    return out, prof


def shrink(im, n):
    """Down to n px in halving steps (keeps detail without ringing)."""
    cur = im
    while cur.width // 2 >= n * 2:
        cur = cur.resize((cur.width // 2, cur.height // 2), Image.LANCZOS)
    return cur.resize((n, n), Image.LANCZOS)


def social(master, w, h, font_path, tagline):
    """The link preview: the tile and the wordmark on the site's dark warm backdrop."""
    base = Image.new("RGB", (w, h), (14, 13, 14))
    warm = Image.new("RGB", (w, h), (42, 26, 18))
    ramp = Image.linear_gradient("L").rotate(90).resize((w, h))
    base = Image.composite(base, warm, ramp)
    img = base.convert("RGBA")
    t = int(h * 0.62)
    tile = shrink(master, t)
    shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    tx, ty = int(w * 0.08), (h - t) // 2
    glow = Image.new("RGBA", (t, t), (240, 136, 46, 0))
    glow.putalpha(tile.getchannel("A").point(lambda v: v * 70 // 255))
    shadow.paste(glow, (tx, ty))
    shadow = shadow.filter(ImageFilter.GaussianBlur(h * 0.05))
    img = Image.alpha_composite(img, shadow)
    img.alpha_composite(tile, (tx, ty))
    d = ImageDraw.Draw(img)
    big = ImageFont.truetype(font_path, int(h * 0.15))
    mid = ImageFont.truetype(font_path.replace("SemiBold", "Regular"), int(h * 0.05))
    x = tx + t + int(w * 0.06)
    bb = d.textbbox((0, 0), "SculptForge", font=big)
    th = bb[3] - bb[1]
    y = h // 2 - th // 2 - (int(h * 0.04) if tagline else 0)
    d.text((x, y - bb[1]), "SculptForge", font=big, fill=(244, 242, 240))
    if tagline:
        d.text((x + 4, y + th + int(h * 0.05)), tagline, font=mid, fill=(240, 136, 46))
    return img.convert("RGB")


def sf(name, n):
    """tools/brand/<name>.svg rendered n px wide."""
    return svg_png(os.path.join(ROOT, "tools", "brand", name + ".svg"), n)


STYLE = "outline"
OUTLINE = os.path.join(ROOT, "tools", "brand", "cube-outline")


def svg_png(svg, n):
    png = subprocess.run(["rsvg-convert", "-w", str(n), "-h", str(n), svg], check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def outline(n):
    """The shipped outline icon n px wide: its own PNG or .ico frame where it had one, else its SVG rendered."""
    path = os.path.join(OUTLINE, f"sculptforge-{n}.png")
    if os.path.exists(path):
        return Image.open(path).convert("RGBA")
    ico = Image.open(os.path.join(OUTLINE, "sculptforge.ico"))
    if (n, n) in ico.info["sizes"]:
        ico.size = (n, n)
        return ico.copy().convert("RGBA")
    return svg_png(os.path.join(OUTLINE, "sculptforge-icon.svg"), n)


def icon(n):
    """The app icon n px wide (filled: the small cut up to 32 px, the mark from 48)."""
    if STYLE == "outline":
        return outline(n)
    return sf("cube-mark-small" if n <= 32 else "cube-mark", n)


def mac(n):
    """macOS: the icon on Apple's grid (an 824 px tile centred in 1024)."""
    if STYLE != "outline":
        return sf("cube-mark-macos-small" if n <= 32 else "cube-mark-macos", n)
    k = round(n * 824 / 1024)
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    im.alpha_composite(svg_png(os.path.join(OUTLINE, "sculptforge-icon.svg"), k), ((n - k) // 2, (n - k) // 2))
    return im


def flat(im, bg=(0x0e, 0x0e, 0x11)):
    """On the tile's bottom colour, for places that round or circle it themselves."""
    out = Image.new("RGBA", im.size, bg + (255,))
    out.alpha_composite(im)
    return out.convert("RGB")


def write_icns(path):
    """macOS: every iconset slot on Apple's grid (tools/brand/icns.py: 16 and 32 px as RGB + mask, the rest PNG)."""
    def png(n):
        buf = io.BytesIO()
        mac(n).save(buf, format="PNG", optimize=True)
        return buf.getvalue()
    icns.write(path, lambda n: mac(n).convert("RGBA").tobytes(), png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bust", nargs="?")
    ap.add_argument("--discord")
    ap.add_argument("--style", choices=("outline", "filled"), default="outline")
    ap.add_argument("--icns-only", action="store_true", help="only assets/branding/sculptforge.icns (no bust needed)")
    a = ap.parse_args()
    global STYLE
    STYLE = a.style
    if a.icns_only:
        write_icns(os.path.join(ROOT, "assets", "branding", "sculptforge.icns"))
        return print("done")
    if not a.bust:
        ap.error("the bust concept art is needed (or --icns-only)")

    def png(im, *path):
        im.save(os.path.join(ROOT, *path), optimize=True)

    # The app icon.
    br = os.path.join("assets", "branding")
    for n in (1024, 512, 256, 128, 64, 48, 32, 24, 16):
        png(icon(n), br, f"sculptforge-{n}.png")
    ico = [icon(n) for n in (256, 128, 64, 48, 32, 24, 16)]  # Windows: each size its own frame
    ico[0].save(os.path.join(ROOT, br, "sculptforge.ico"), format="ICO", sizes=[(i.width, i.height) for i in ico],
                append_images=ico[1:])
    write_icns(os.path.join(ROOT, br, "sculptforge.icns"))
    png(icon(512), "packaging", "flatpak", "flatpark", "org.sculptforge.SculptForge", "org.sculptforge.SculptForge.png")

    # The site's favicons (as the app icon) and the press kit's app icon.
    png(icon(16), "website", "img", "favicon-16.png")
    png(icon(32), "website", "img", "favicon-32.png")
    png(icon(64), "website", "img", "favicon-64.png")
    if STYLE == "outline":  # the site's own cuts of 0.1-0.10 (71cd4f7)
        png(Image.open(os.path.join(OUTLINE, "site-favicon-64.png")), "website", "img", "favicon-64.png")
    fav = [icon(n) for n in (48, 32, 16)]
    fav[0].save(os.path.join(ROOT, "website", "favicon.ico"), format="ICO", sizes=[(i.width, i.height) for i in fav],
                append_images=fav[1:])
    square = flat(icon(1024)).resize((180, 180), Image.LANCZOS) if STYLE == "outline" else \
        sf("cube-mark-square", 180).convert("RGB")
    png(square, "website", "img", "apple-touch-icon.png")  # iOS rounds it
    png(icon(256), "website", "img", "icon-256.png")  # the Discord announcements' avatar
    if STYLE == "outline":
        png(Image.open(os.path.join(OUTLINE, "site-icon-256.png")), "website", "img", "icon-256.png")
    png(icon(1024), "website", "press", "sculptforge-icon-1024.png")  # also Discord Rich Presence's large image
    png(icon(256), "website", "press", "sculptforge-icon-256.png")
    frozen = os.path.join(ROOT, "website", "press", "sculptforge-app-icon-cube-outline-1024.png")
    if STYLE == "outline" and not os.path.exists(frozen):  # by URL (Discord Rich Presence): never overwritten
        icon(1024).save(frozen, optimize=True)

    # The bust: the illustration.
    bust, _ = master_from_concept(a.bust)
    png(bust, br, "bust", "bust-1024.png")
    png(shrink(bust, 256), br, "bust", "bust-256.png")  # embedded (BrandMark.cpp)
    png(shrink(bust, 256), "website", "img", "bust-256.png")  # the home page's What's New cards (with 512)
    png(shrink(bust, 512), "website", "img", "bust-512.png")
    for n in (44, 88, 132):  # the site header's brand lockup (44 px, 1x-3x)
        png(shrink(bust, n), "website", "img", f"bust-mark-{n}.png")
    png(bust, "website", "press", "sculptforge-bust-1024.png")
    font = os.path.join(ROOT, "website", "fonts", "Inter-SemiBold.ttf")
    tagline = "The free, open-source sculpting app."  # (the site's existing line)
    png(social(bust, 1200, 630, font, tagline), "website", "img", "social-preview.png")
    png(social(bust, 1280, 640, font, tagline), "docs", "images", "social-preview.png")  # GitHub's (uploaded by hand)

    if a.discord:
        # Discord and GitHub crop to circles: the bot avatar, the server icon and the GitHub avatar are the bust (the
        # brand, Tony 11-06), inset on the dark backdrop.
        for name, n in (("discord-bot-avatar", 512), ("discord-server-icon", 512), ("github-avatar", 500)):
            av = Image.new("RGBA", (n, n), (0x1c, 0x1a, 0x1c, 255))
            k = round(n * 440 / 512)
            av.alpha_composite(shrink(bust, k), ((n - k) // 2, (n - k) // 2))
            av.convert("RGB").save(os.path.join(a.discord, f"{name}-{n}.png"))
        # Discord server and developer portal icon: the app icon (Tony 11-06 21:02), on its tile's dark.
        flat(icon(512)).save(os.path.join(a.discord, f"discord-server-icon-cube-{STYLE}-512.png"))
        icon(1024).save(os.path.join(a.discord, f"discord-portal-icon-cube-{STYLE}-1024.png"))
    print("done")


if __name__ == "__main__":
    main()
