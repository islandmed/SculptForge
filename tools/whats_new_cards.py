#!/usr/bin/env python3
"""What's New card art: drawn, never captured. Each card of a version's ```features block (docs/guide/whats-new.md) names
an `icon:` (a Lucide outline icon, ICONS below); this script draws its picture (`image:`): the icon in a glowing tile on
the site's forge background, with no text in it, so the words (drawn by the app and the website) stay sharp in every
language. The window's hero band (`hero:`, images/wn-<0127>-hero.jpg) is the bust in forge light, its crop and light picked
from the version so releases look different, the version's `theme:` and tagline over it. The app (WhatsNewPopup.cpp), the guide chapter and the website show the same pictures.

  whats_new_cards.py render VERSION...       draw each card's art to docs/guide/images/wn-<0127>-<slug>.jpg (1200 x 680,
                                             2x the 600 x 340 card) and point its image: line there
  whats_new_cards.py fill VERSION            a version with fewer than 3 cards gets cards drafted from its highlight: lines
                                             (title before the colon, an icon picked from the words), then render
  whats_new_cards.py sheet OUT.jpg VERSION...  a contact sheet: each version as the What's New window would show it, 2880 wide
  whats_new_cards.py check [VERSION...]      every card names a known icon (default: the newest 3 versions)

release.sh (step bump) runs `fill X.Y.Z` after the notes are assembled; it never stops a release (a warning at most).
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
MD = ROOT / "docs/guide/whats-new.md"
IMAGES = ROOT / "docs/guide/images"
LUCIDE_H = ROOT / "third_party/lucide/IconsLucide.h"
LUCIDE = ROOT / "assets/fonts/lucide.ttf"
INTER = ROOT / "assets/fonts/Inter-Regular.ttf"
INTER_SEMI = ROOT / "assets/fonts/Inter-SemiBold.ttf"
BUST = ROOT / "assets/branding/bust/bust-1024.png"
sys.path.insert(0, str(ROOT / "tools/guide"))
from build_site import parse_features, split_highlight  # noqa: E402  (the website's parser: one set of keys)

BG, BG_TOP, PANEL = (15, 15, 18), (23, 23, 28), (27, 27, 33)  # site --bg, a step up, --panel
ACCENT, TEXT, MUTED, BORDER = (242, 140, 60), (236, 236, 241), (163, 163, 173), (42, 42, 49)
W, H = 1200, 680  # 2x the 600 x 340 card picture

# The icons a card may name: Lucide outline icons (lucide-static, as the app's icon font; ISC licence).
ICONS = ["sparkles", "arrow-left-right", "shirt", "puzzle", "person-standing", "spline", "diamond", "layers", "stamp",
         "grid-3x3", "image", "undo-2", "shield-check", "orbit", "layout-template", "palette", "keyboard", "gauge", "waves",
         "brush", "languages", "rocket", "save", "move", "cpu", "zoom-in", "scan", "mountain", "wand-sparkles", "box"]

# Words -> icon, for drafted cards (fill); first match wins. Icons must be in ICONS.
KEYWORDS = [
    (r"splat|gaussian", "sparkles"), (r"blender|bridge|round trip", "arrow-left-right"),
    (r"garment|cloth|drape|sew|fabric", "shirt"), (r"plugin|add-on|addon", "puzzle"), (r"pose|rig|skeleton", "person-standing"),
    (r"tube|curve|spline", "spline"), (r"crease|edge|sharp|bevel|flatten|hard surface", "diamond"),
    (r"layer", "layers"), (r"alpha|stamp|vdm|jitter", "stamp"), (r"udim|uv\b|grid", "grid-3x3"),
    (r"reference|image|picture", "image"), (r"undo|redo|history", "undo-2"), (r"save|autosave|backup|safer|safe", "shield-check"),
    (r"orbit|pan|zoom|camera|view", "orbit"), (r"layout|interface|welcome|window|panel|palette bar", "layout-template"),
    (r"paint|colour|color|rgb|mask", "palette"), (r"shortcut|key|hotkey", "keyboard"),
    (r"thread|faster|speed|fps|performance|dense|scan", "gauge"), (r"smooth|peak", "waves"), (r"brush", "brush"),
    (r"translat|language|chinese|spanish", "languages"), (r"splash|start", "rocket"),
]


def icon_table():
    """name -> character (the Lucide header's codepoints)."""
    codes = {m[0]: bytes(int(b, 16) for b in re.findall(r"\\x([0-9a-f]{2})", m[1])).decode()
             for m in re.findall(r'#define (ICON_LC_\w+) "([^"]+)"', LUCIDE_H.read_text())}
    return {name: codes["ICON_LC_" + name.upper().replace("-", "_")] for name in ICONS}


def versions(md):
    """[(version, start, end, block_start, block_end)] line indices of each section and its features block."""
    lines = md.split("\n")
    starts = [i for i, l in enumerate(lines) if re.match(r"^## Version \S+$", l)]
    out = []
    for n, s in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(lines)
        b = next((i for i in range(s + 1, end) if lines[i].strip()), None)
        if b is not None and lines[b].strip() == "```features":
            e = next(i for i in range(b + 1, end) if lines[i].strip() == "```")
            out.append((lines[s][len("## Version "):], s, end, b, e))
    return out


def features(md, version):
    lines = md.split("\n")
    for v, _, _, b, e in versions(md):
        if v == version:
            return parse_features("\n".join(lines[b + 1:e]))
    raise SystemExit(f"{MD.name}: no '## Version {version}' with a features block")


def slug(title):
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:28].rstrip("-")


def tag(version):
    return "wn-" + "".join(version.split(".")[:3]).rjust(4, "0")


# ---------------------------------------------------------------------------
# The art
# ---------------------------------------------------------------------------

def radial(w, h, cx, cy, r, gain=1.0, power=1.6):
    d = max(2, round(2 * r))
    g = Image.radial_gradient("L").resize((d, d), Image.BICUBIC).point(lambda v: round(255 * gain * max(0.0, 1 - v / 181) ** power))
    m = Image.new("L", (w, h), 0)
    m.paste(g, (round(cx - d / 2), round(cy - d / 2)))
    return m


def over(img, rgb, mask):
    return Image.composite(Image.new("RGB", img.size, rgb), img, mask)


def backdrop(w, h, cx, cy, s):
    """The forge background: a gradient, the ember glow under the art, a dot grid fading out from the middle."""
    img = Image.new("RGB", (w, h), BG)
    ramp = Image.linear_gradient("L").resize((w, h))
    img = Image.composite(Image.new("RGB", (w, h), BG), Image.new("RGB", (w, h), BG_TOP), ramp)
    img = ImageChops.screen(img, over(Image.new("RGB", (w, h)), ACCENT, radial(w, h, cx, h * 1.02, w * 0.55, 0.30)))
    img = ImageChops.screen(img, over(Image.new("RGB", (w, h)), ACCENT, radial(w, h, cx, cy, h * 0.55, 0.16)))
    dots = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(dots)
    step, r = 18 * s, 1.15 * s
    y = (h % step) / 2
    while y < h:
        x = (w % step) / 2
        while x < w:
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
            x += step
        y += step
    fade = radial(w, h, cx, cy, w * 0.62, 1.0, 1.2)
    img = over(img, TEXT, ImageChops.multiply(dots, fade.point(lambda v: round(v * 0.10))))
    return img


def rings(img, cx, cy, radii, s):
    d = ImageDraw.Draw(img, "RGBA")
    for r, a in radii:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ACCENT + (round(255 * a),), width=max(1, round(1.2 * s)))


def art(icon, chars, w=W, h=H):
    """One card's picture: no text, the icon in a glowing tile on the forge background."""
    S = 2  # drawn at twice the size, then scaled down: smooth edges
    W2, H2, s = w * S, h * S, (h * S) / 340.0  # s: picture pixels per logical pixel of the 600 x 340 card
    cx, cy = W2 / 2, H2 * 0.5
    img = backdrop(W2, H2, cx, cy, s)
    rings(img, cx, cy, [(118 * s, 0.17), (168 * s, 0.10), (226 * s, 0.055)], s)
    t, rr = 150 * s, 34 * s  # the tile and its corners
    x0, y0 = cx - t / 2, cy - t / 2
    glow = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(glow).rounded_rectangle((x0, y0 + 6 * s, x0 + t, y0 + t + 6 * s), rr, fill=150)
    img = over(img, (0, 0, 0), glow.filter(ImageFilter.GaussianBlur(14 * s)))
    halo = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(halo).rounded_rectangle((x0, y0, x0 + t, y0 + t), rr, fill=90)
    img = over(img, ACCENT, halo.filter(ImageFilter.GaussianBlur(26 * s)))
    tile = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(tile).rounded_rectangle((x0, y0, x0 + t, y0 + t), rr, fill=255)
    fill = Image.composite(Image.new("RGB", (W2, H2), (19, 19, 24)), Image.new("RGB", (W2, H2), (44, 44, 53)), vramp(H2, y0, y0 + t).resize((W2, H2)))
    img = Image.composite(fill, img, tile)
    d = ImageDraw.Draw(img, "RGBA")
    d.rounded_rectangle((x0, y0, x0 + t, y0 + t), rr, outline=(255, 255, 255, 30), width=max(1, round(1.2 * s)))
    d.line((x0 + rr, y0 + 1, x0 + t - rr, y0 + 1), fill=(255, 255, 255, 46), width=max(1, round(1.2 * s)))
    f = ImageFont.truetype(str(LUCIDE), round(74 * s))
    ch = chars[icon]
    bb = d.textbbox((0, 0), ch, font=f)
    at = (cx - (bb[0] + bb[2]) / 2, cy - (bb[1] + bb[3]) / 2)
    gl = Image.new("L", (W2, H2), 0)
    ImageDraw.Draw(gl).text(at, ch, font=f, fill=200)
    img = over(img, ACCENT, gl.filter(ImageFilter.GaussianBlur(10 * s)).point(lambda v: round(v * 0.55)))
    ImageDraw.Draw(img).text(at, ch, font=f, fill=ACCENT)
    vig = radial(W2, H2, cx, cy, W2 * 0.75, 1.0, 0.9).point(lambda v: round(255 * (0.55 + 0.45 * v / 255)))
    img = ImageChops.multiply(img, Image.merge("RGB", (vig, vig, vig)))
    return img.resize((w, h), Image.LANCZOS)


def vramp(h, y0, y1):
    """'L' mask w x h: 0 above y0 (the first colour), 255 below y1 (the second)."""
    col = Image.new("L", (1, h))
    col.putdata([0 if y <= y0 else 255 if y >= y1 else round(255 * (y - y0) / (y1 - y0)) for y in range(h)])
    return col


def bust_cutout(size):
    """The bust (assets/branding) out of its grey tile: the clay keeps its light, the anvil and the tile fall into the
    dark, and the tile's edges fade out, so it stands in the forge light instead of on a sticker."""
    src = Image.open(BUST).convert("RGBA").resize((size, size), Image.LANCZOS)
    rgb, alpha = src.convert("RGB"), src.getchannel("A")
    _, sat, val = rgb.convert("HSV").split()
    key = ImageChops.lighter(sat.point(lambda v: max(0, min(255, (v - 70) * 3))),
                             val.point(lambda v: max(0, min(255, (v - 150) * 4)))).filter(ImageFilter.GaussianBlur(size / 300))
    lit = ImageChops.multiply(rgb, Image.new("RGB", rgb.size, (255, 248, 240)))
    dark = Image.eval(rgb, lambda v: round(v * 0.30))
    out = Image.composite(lit, dark, key)
    fade = radial(size, size, size * 0.5, size * 0.52, size * 0.50, 1.0, 0.55)
    keep = ImageChops.lighter(fade, key)  # the clay stays, the tile around it fades
    return out, ImageChops.multiply(alpha, keep)


HERO_LOOKS = [  # the same bust, a different crop and light per release (picked from the version): x, size, lift, mirror, light
    dict(cx=0.80, size=1.15, dy=0.06, flip=False, light=(0.0, -0.05, 0.38, 0.50)),
    dict(cx=0.76, size=1.55, dy=0.22, flip=True, light=(-0.10, -0.20, 0.30, 0.55)),  # close on the face, lit from the left
    dict(cx=0.83, size=1.00, dy=0.02, flip=False, light=(0.0, 0.30, 0.45, 0.60)),    # the whole bust, the forge glowing under it
    dict(cx=0.74, size=1.35, dy=0.14, flip=False, light=(0.12, -0.15, 0.32, 0.45)),  # three-quarter crop, lit from the right
    dict(cx=0.82, size=1.25, dy=0.10, flip=True, light=(0.0, 0.10, 0.50, 0.40)),     # mirrored, a wide soft glow
]


def hero_look(version):
    parts = [int(x) for x in re.findall(r"\d+", version)][:3] + [0, 0, 0]
    return HERO_LOOKS[(parts[0] * 7 + parts[1] * 3 + parts[2]) % len(HERO_LOOKS)]


def hero_art(w, h, look=HERO_LOOKS[0]):
    """The release's hero: the bust in forge light on the right, room for the words on the left (no text)."""
    s = h / 236.0
    cx, cy = w * look["cx"], h * 0.56
    img = backdrop(w, h, cx, cy, s)
    rings(img, cx, cy, [(92 * s, 0.20), (132 * s, 0.12), (182 * s, 0.07), (244 * s, 0.04), (320 * s, 0.025)], s)
    b = round(h * look["size"])
    lx, ly, lr, lg = look["light"]
    img = ImageChops.screen(img, over(Image.new("RGB", (w, h)), ACCENT, radial(w, h, cx + lx * b, cy + ly * b, b * lr, lg)))
    bust, mask = bust_cutout(b)
    if look["flip"]:
        bust, mask = bust.transpose(Image.Transpose.FLIP_LEFT_RIGHT), mask.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    img.paste(bust, (round(cx - b / 2), round(cy - b / 2 + h * look["dy"])), mask)
    left = vramp(w, w * 0.30, w * 0.62).resize((h, w)).transpose(Image.Transpose.ROTATE_90).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    img = Image.composite(img, Image.eval(img, lambda v: round(v * 0.55)), left)  # quieter under the words
    return img


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

def set_images(md, version, images):
    """Each card's image: line set to IMAGES[i] (added after its line:), in VERSION's block."""
    lines = md.split("\n")
    for v, _, _, b, e in versions(md):
        if v != version:
            continue
        out, card, i = [], -1, b + 1
        block = lines[b + 1:e]
        res = []
        for ln in block:
            key = ln.split(":", 1)[0].strip()
            if key == "title":
                card += 1
            if key == "image":
                continue
            res.append(ln)
            if key == "line" and card >= 0 and images[card]:
                res.append(f"image: {images[card]}")
        out = lines[:b + 1] + res + lines[e:]
        return "\n".join(out)
    return md


def set_hero(md, version, rel):
    """VERSION's hero: line set to REL (after its tagline/theme)."""
    lines = md.split("\n")
    for v, _, _, b, e in versions(md):
        if v == version:
            block = [l for l in lines[b + 1:e] if not l.startswith("hero:")]
            at = max([i for i, l in enumerate(block) if l.startswith(("date:", "tagline:", "theme:"))], default=-1) + 1
            return "\n".join(lines[:b + 1] + block[:at] + [f"hero: {rel}"] + block[at:] + lines[e:])
    return md


def render(version_list):
    chars = icon_table()
    md = MD.read_text()
    for version in version_list:
        rel = f"images/{tag(version)}-hero.jpg"
        hero_art(2880, 708, hero_look(version)).save(ROOT / "docs/guide" / rel, "JPEG", quality=86, optimize=True, progressive=True)
        print("hero", rel)
        md = set_hero(md, version, rel)
        f = features(md, version)
        images = []
        for c in f["cards"]:
            if not c.get("icon"):
                images.append(c["image"])  # an old card without an icon keeps its picture
                continue
            if c["icon"] not in chars:
                raise SystemExit(f"Version {version}: card '{c['title']}': unknown icon '{c['icon']}' (ICONS in this script)")
            rel = f"images/{tag(version)}-{slug(c['title'])}.jpg"
            art(c["icon"], chars).save(ROOT / "docs/guide" / rel, "JPEG", quality=90, optimize=True, progressive=True)
            images.append(rel)
            print("art", rel)
        md = set_images(md, version, images)
    MD.write_text(md)


def pick_icon(text, chars):
    for pattern, icon in KEYWORDS:
        if re.search(pattern, text, re.I) and icon in chars:
            return icon
    return "sparkles"


def short_title(text, n=6):
    words = re.sub(r"\s*\(.*?\)", "", text).split()
    return " ".join(words[:n]).rstrip(",;:.")


def fill(version):
    """Cards drafted from the highlights when the version has fewer than 3 (the hand-written ones stay first)."""
    chars = icon_table()
    md = MD.read_text()
    f = features(md, version)
    have = {c["title"].lower() for c in f["cards"]}
    new = []
    for h in f["highlights"]:
        if len(f["cards"]) + len(new) >= 3 or len(f["cards"]) >= 3:
            break
        title, rest = split_highlight(h)
        title = short_title(title or h)
        if title.lower() in have or any(title.lower() in t for t in have):
            continue
        line = re.sub(r"\s*\((thanks|requested by|#)[^)]*\)", "", rest or h).strip()
        line = line[0].upper() + line[1:] if line else title
        line = line if line.endswith(".") else line + "."
        new += [f"title: {title}", f"line: {line}", f"icon: {pick_icon(h, chars)}",
                f"action: guide:whats-new.md#version-{''.join(version.split('.'))}"]
        have.add(title.lower())
    if new:
        lines = md.split("\n")
        e = next(e for v, _, _, b, e in versions(md) if v == version)
        MD.write_text("\n".join(lines[:e] + new + lines[e:]))
        print(f"Version {version}: drafted {len(new) // 4} cards from its highlights")
    render([version])


def check(version_list):
    chars = icon_table()
    md = MD.read_text()
    version_list = version_list or [v for v, *_ in versions(md)][:3]
    bad = []
    for version in version_list:
        for c in features(md, version)["cards"]:
            if c.get("icon") and c["icon"] not in chars:
                bad.append(f"Version {version}: '{c['title']}': unknown icon '{c['icon']}'")
            if len(c["title"].split()) > 6:
                bad.append(f"Version {version}: '{c['title']}': a card title has at most 6 words")
            if len(c["line"]) > 90:
                bad.append(f"Version {version}: '{c['title']}': a card's line has at most 90 characters")
    print("\n".join(bad) or "ok")
    return 1 if bad else 0


def wrap(d, text, font, width):
    lines, cur = [], ""
    for w_ in text.split():
        t = (cur + " " + w_).strip()
        if d.textlength(t, font=font) > width and cur:
            lines.append(cur)
            cur = w_
        else:
            cur = t
    return lines + ([cur] if cur else [])


def window(version, f, chars, k=2.0):
    """The What's New window as the app draws it (WhatsNewPopup.cpp), at K pixels per logical pixel, for the sheet."""
    LW, LH = 1000, 690
    w, h = round(LW * k), round(LH * k)
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img, "RGBA")
    F = lambda p, semi=False: ImageFont.truetype(str(INTER_SEMI if semi else INTER), round(p * k))
    pad = 32 * k
    # hero
    hx0, hy0, hx1, hy1 = pad, pad, w - pad, pad + 230 * k
    hero = Image.open(ROOT / "docs/guide" / f["hero"]).convert("RGB")
    hw, hh = round(hx1 - hx0), round(hy1 - hy0)
    sc = max(hw / hero.width, hh / hero.height)
    hero = hero.resize((round(hero.width * sc), round(hero.height * sc)), Image.LANCZOS)
    hero = hero.crop(((hero.width - hw) // 2, (hero.height - hh) // 2, (hero.width - hw) // 2 + hw, (hero.height - hh) // 2 + hh))
    m = Image.new("L", hero.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, hero.width - 1, hero.height - 1), 16 * k, fill=255)
    img.paste(hero, (round(hx0), round(hy0)), m)
    d.rounded_rectangle((hx0, hy0, hx1, hy1), 16 * k, outline=(255, 255, 255, 24), width=max(1, round(k)))
    x, y = hx0 + 40 * k, hy0 + 46 * k
    fx = x
    for ch in f"WHAT'S NEW IN {version}":
        d.text((fx, y), ch, font=F(12, True), fill=ACCENT)
        fx += d.textlength(ch, font=F(12, True)) + 1.8 * k
    y += 28 * k
    for ln in wrap(d, f.get("theme") or f["tagline"], F(38, True), 540 * k):
        d.text((x, y), ln, font=F(38, True), fill=(250, 250, 252))
        y += 46 * k
    y += 8 * k
    for ln in (wrap(d, f["tagline"], F(16), 500 * k) if f.get("theme") else [])[:3]:
        d.text((x, y), ln, font=F(16), fill=MUTED)
        y += 23 * k
    # cards: 3 to 5 in a row (picture on top); 1 or 2 side by side with the picture on the left
    cards = f["cards"]
    n = len(cards)
    gap = 16 * k
    top = hy1 + 20 * k
    cw = (w - 2 * pad - gap * (n - 1)) / max(1, n)
    ch_ = h - top - 76 * k - 20 * k
    wide = False  # (owner 11-11: always the row, never one wide card)
    for i, c in enumerate(cards):
        x0 = pad + i * (cw + gap)
        d.rounded_rectangle((x0, top, x0 + cw, top + ch_), 14 * k, fill=PANEL, outline=(255, 255, 255, 22), width=max(1, round(k)))
        pw, ph = (min(cw * 0.55, ch_ * 600 / 340), ch_) if wide else (cw, cw * 340 / 600)
        if c.get("image"):
            a = Image.open(ROOT / "docs/guide" / c["image"]).convert("RGB")
            sc = max(pw / a.width, ph / a.height)
            a = a.resize((round(a.width * sc), round(a.height * sc)), Image.LANCZOS)
            a = a.crop(((a.width - round(pw)) // 2, (a.height - round(ph)) // 2, (a.width - round(pw)) // 2 + round(pw), (a.height - round(ph)) // 2 + round(ph)))
            mk = Image.new("L", a.size, 0)
            ImageDraw.Draw(mk).rounded_rectangle((0, 0, a.width + (40 if wide else -1), a.height + (-1 if wide else 40)), 14 * k, fill=255)
            img.paste(a, (round(x0), round(top)), mk)
        if wide:
            tx, tw = x0 + pw + 28 * k, cw - pw - 56 * k
            ty = top + ch_ * 0.5 - 70 * k
            tsize, lsize = 24, 16
        else:
            tx, ty, tw = x0 + 16 * k, top + ph + 16 * k, cw - 32 * k
            tsize, lsize = 17, 13.5
        if c.get("stat"):
            d.text((tx, ty - 4 * k), c["stat"], font=F(30, True), fill=ACCENT)
            ty += 38 * k
        for ln in wrap(d, c["title"], F(tsize, True), tw):
            d.text((tx, ty), ln, font=F(tsize, True), fill=(250, 250, 252))
            ty += tsize * 1.3 * k
        ty += 4 * k
        for ln in wrap(d, c["line"], F(lsize), tw)[:5]:
            d.text((tx, ty), ln, font=F(lsize), fill=MUTED)
            ty += lsize * 1.42 * k
        label = "Read more  \u2192" if c["action"].startswith("guide:") else "Try it  \u2192"
        by = ty + 16 * k if wide else top + ch_ - 16 * k - 30 * k
        bw = d.textlength(label, font=F(13.5, True)) + 30 * k
        d.rounded_rectangle((tx, by, tx + bw, by + 30 * k), 15 * k, fill=(38, 38, 46) if i else ACCENT,
                            outline=(255, 255, 255, 30) if i else None)
        d.text((tx + 15 * k, by + 7 * k), label, font=F(13.5, True), fill=(250, 250, 252) if i else BG)
    # footer
    fy = h - 38 * k
    d.line((pad, h - 76 * k, w - pad, h - 76 * k), fill=(255, 255, 255, 20), width=max(1, round(k)))
    d.text((pad, fy - 9 * k), "Full release notes", font=F(14, True), fill=TEXT)
    d.text((pad + 170 * k, fy - 9 * k), "Join the Discord", font=F(14, True), fill=TEXT)
    cw2 = d.textlength("Close", font=F(14, True)) + 36 * k
    d.rounded_rectangle((w - pad - cw2, fy - 17 * k, w - pad, fy + 17 * k), 17 * k, fill=(38, 38, 46), outline=(255, 255, 255, 50))
    d.text((w - pad - cw2 + 18 * k, fy - 9 * k), "Close", font=F(14, True), fill=TEXT)
    return img


def sheet(out, version_list):
    chars = icon_table()
    md = MD.read_text()
    shots = [window(v, features(md, v), chars, 2.0) for v in version_list]
    W_ = 2880
    scale = (W_ - 160) / shots[0].width
    shots = [s.resize((round(s.width * scale), round(s.height * scale)), Image.LANCZOS) for s in shots]
    S = Image.new("RGB", (W_, sum(s.height + 120 for s in shots) + 80), (8, 8, 10))
    d = ImageDraw.Draw(S)
    y = 80
    for v, s in zip(version_list, shots):
        d.text((80, y - 10), f"Version {v}", font=ImageFont.truetype(str(INTER_SEMI), 34), fill=ACCENT)
        S.paste(s, (80, y + 40))
        y += s.height + 120
    S.save(out, "JPEG", quality=90)
    print("sheet", out)


if __name__ == "__main__":
    cmd, args = (sys.argv[1], sys.argv[2:]) if len(sys.argv) > 1 else ("", [])
    if cmd == "render" and args:
        render(args)
    elif cmd == "fill" and len(args) == 1:
        fill(args[0])
    elif cmd == "sheet" and len(args) >= 2:
        sheet(args[0], args[1:])
    elif cmd == "check":
        sys.exit(check(args))
    else:
        sys.exit(__doc__)
