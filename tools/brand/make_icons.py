#!/usr/bin/env python3
"""Builds every SculptForge icon and logo file from the brand art.

    python -I tools/brand/make_icons.py <concept.jpeg> [--small icon-small.png] [--discord DIR]

<concept.jpeg>: the icon art (the rounded-square tile on a dark backdrop; the tile is found from its light rim).
--small: the simplified small-size companion (same design, flat shapes) for 16/24/32 px; without it one is drawn
here (bold orange bust merging into a dark anvil, one glowing seam).
Writes assets/branding/ (master, sizes, .ico, .icns), website/img/ (icon-256/512, social preview),
website/press/, docs/images/social-preview.png and, with --discord, the Discord server icon and bot avatar.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

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


def draw_small(prof):
    """The small-size companion: flat shapes that still read at 16 px."""
    n = 512
    m = n * SS
    u = m / 512.0
    img = Image.new("RGBA", (m, m), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Tile: dark, a touch lighter at the top (like the art).
    grad = Image.new("RGBA", (m, m))
    gd = ImageDraw.Draw(grad)
    for y in range(m):
        t = y / m
        c = tuple(int(a + (b - a) * t) for a, b in zip((0x3c, 0x37, 0x38), (0x17, 0x15, 0x18)))
        gd.line([(0, y), (m, y)], fill=c + (255,))
    S = lambda pts: [(x * u, y * u) for x, y in pts]
    orange, light, seam, iron, iron_hi = (0xE8, 0x7A, 0x34), (0xF6, 0xA5, 0x62), (0xFF, 0xD2, 0x5A), (0x6A, 0x63, 0x66), (0x8E, 0x86, 0x88)
    # Bust: head, neck, shoulders (one silhouette).
    d.ellipse(S([(206, 44), (306, 178)]), fill=orange)
    d.polygon(S([(230, 160), (282, 160), (292, 236), (220, 236)]), fill=orange)
    d.polygon(S([(98, 322), (116, 268), (176, 234), (336, 234), (396, 268), (414, 322)]), fill=orange)
    d.ellipse(S([(98, 250), (190, 330)]), fill=orange)  # round shoulders
    d.ellipse(S([(322, 250), (414, 330)]), fill=orange)
    # Anvil: the face with the horn to the left, the waist, the foot.
    d.polygon(S([(30, 322), (458, 322), (458, 374), (160, 374), (96, 352)]), fill=iron)
    d.polygon(S([(30, 322), (458, 322), (458, 334), (60, 328)]), fill=iron_hi)
    d.polygon(S([(206, 374), (392, 374), (350, 418), (248, 418)]), fill=iron)
    d.polygon(S([(170, 418), (428, 418), (446, 466), (152, 466)]), fill=iron)
    # The molten seam where the clay meets the iron, glowing.
    seam_layer = Image.new("RGBA", (m, m), (0, 0, 0, 0))
    ImageDraw.Draw(seam_layer).rounded_rectangle(S([(92, 308), (420, 330)]), radius=11 * u, fill=seam)
    halo = Image.new("RGBA", (m, m), (0, 0, 0, 0))
    ImageDraw.Draw(halo).rounded_rectangle(S([(84, 300), (428, 338)]), radius=19 * u, fill=(0xFF, 0x7A, 0x1E, 150))
    img = Image.alpha_composite(img, halo.filter(ImageFilter.GaussianBlur(14 * u)))
    img = Image.alpha_composite(img, seam_layer)
    shapes = Image.alpha_composite(grad, img)
    small = shapes.resize((n, n), Image.LANCZOS)
    small.putalpha(rounded_mask(n, prof, inset=0.5))
    return small


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("concept")
    ap.add_argument("--small")
    ap.add_argument("--discord")
    a = ap.parse_args()

    master, prof = master_from_concept(a.concept)
    if a.small:
        small = Image.open(a.small).convert("RGBA")
        small = small.resize((512, 512), Image.LANCZOS)
    else:
        small = draw_small(prof)

    def size(n):
        return shrink(master, n) if n >= 48 else shrink(small, n)

    br = os.path.join(ROOT, "assets", "branding")
    master.save(os.path.join(br, "sculptforge-1024.png"), optimize=True)
    small.save(os.path.join(br, "sculptforge-small-512.png"), optimize=True)
    shrink(small, 64).save(os.path.join(br, "sculptforge-small-64.png"), optimize=True)  # the in-app mark below 48 px
    for n in (512, 256, 128, 64, 48, 32, 24, 16):
        size(n).save(os.path.join(br, f"sculptforge-{n}.png"), optimize=True)
    # Windows: every size in one .ico (each frame its own image, not a rescale of one).
    ico = [size(n) for n in (256, 128, 64, 48, 32, 24, 16)]
    ico[0].save(os.path.join(br, "sculptforge.ico"), format="ICO", sizes=[(i.width, i.height) for i in ico],
                append_images=ico[1:])
    # macOS: the iconset sizes (16 and 32 from the small companion, the rest from the master).
    master.save(os.path.join(br, "sculptforge.icns"), format="ICNS",
                append_images=[size(n) for n in (16, 32, 64, 128, 256, 512)])

    # (The site's favicons, apple-touch-icon and header mark are the SF monogram: tools/brand/make_sf_mark.py.)
    img = os.path.join(ROOT, "website", "img")
    size(256).save(os.path.join(img, "icon-256.png"), optimize=True)  # the Discord announcements' avatar
    size(512).save(os.path.join(img, "icon-512.png"), optimize=True)  # (with icon-256: the home page's What's New cards)
    size(256).save(os.path.join(ROOT, "website", "press", "sculptforge-icon-256.png"), optimize=True)
    master.save(os.path.join(ROOT, "website", "press", "sculptforge-icon-1024.png"), optimize=True)

    font = os.path.join(ROOT, "website", "fonts", "Inter-SemiBold.ttf")
    tagline = "The free, open-source sculpting app."  # (the site's existing line)
    social(master, 1200, 630, font, tagline).save(os.path.join(img, "social-preview.png"), optimize=True)
    social(master, 1280, 640, font, tagline).save(os.path.join(ROOT, "docs", "images", "social-preview.png"),
                                                  optimize=True)  # GitHub's repository social preview (uploaded by hand)

    if a.discord:
        # Discord crops the server icon and the bot avatar to circles: the tile sits on its own dark, inset.
        av = Image.new("RGBA", (512, 512), (0x1c, 0x1a, 0x1c, 255))
        av.alpha_composite(shrink(master, 440), (36, 36))
        for name in ("discord-server-icon-512.png", "discord-bot-avatar-512.png"):
            av.convert("RGB").save(os.path.join(a.discord, name), optimize=True)
    print("done")


if __name__ == "__main__":
    main()
