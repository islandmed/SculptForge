#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns the raw UI screenshots of tools/guide/shots.sfui into the user guide's images.

    build/sculptforge --ui-test tools/guide/shots.sfui --ui-out build/guide-shots
    python3 tools/guide/make_images.py build/guide-shots [--preview sheet.png]

Each image is a crop of a 1600x960 screenshot (coordinates below), optionally labelled, scaled to at most
MAX_WIDTH and saved as a JPEG of a few dozen KB in docs/guide/images. Needs Pillow.
"""
import argparse
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "guide" / "images"
FONT = ROOT / "assets" / "fonts" / "Inter-SemiBold.ttf"
MAX_WIDTH = 1100
ACCENT = (240, 136, 46)

# name: (screenshot, (x0, y0, x1, y1) crop in 1600x960 window pixels, max width)
CROPS = {
    "palette-dropdown": ("palette-dropdown", (0, 0, 620, 560), 620),
    "palette-tray": ("palette-tray", (1276, 46, 1600, 520), 324),
    "draw-palette": ("draw-palette", (240, 20, 640, 330), 400),
    "lightbox": ("lightbox", (100, 100, 900, 300), 800),
    "brush-picker": ("brush-picker", (250, 100, 740, 430), 490),
    "stroke-picker": ("stroke-picker", (0, 200, 440, 360), 440),
    "alpha-picker": ("alpha-picker", (0, 310, 600, 520), 600),
    "material-picker": ("material-picker", (0, 330, 520, 590), 520),
    "custom-matcaps": ("custom-matcaps", (382, 122, 900, 700), 518),
    "ctrl-help": ("ctrl-help", (1040, 180, 1600, 420), 560),
    "command-palette": ("command-palette", (340, 120, 980, 250), 640),
    "console": ("console", (0, 690, 1280, 900), MAX_WIDTH),
    "preferences": ("preferences", (440, 20, 790, 690), 350),
    "right-shelf": ("overview", (1200, 110, 1290, 560), 90),
    "bottom-shelf": ("overview", (0, 890, 1280, 960), MAX_WIDTH),
    "sculpt-lion": ("overview", (0, 46, 1280, 900), 960),
    "brush-lab": ("brush-lab", (110, 120, 580, 370), 470),
    "brush-library": ("brush-library", (140, 40, 480, 960), 340),
    "community-brushes": ("community-brushes", (390, 130, 1210, 820), 820),
    "generate-3d": ("generate-3d", (882, 132, 1204, 862), 322),
    "export": ("export", (1276, 46, 1600, 700), 324),
    "masking": ("masking", (420, 46, 1600, 800), 960),
    "polypaint": ("polypaint", (190, 20, 900, 800), 710),
    "layers": ("layers", (1276, 46, 1600, 560), 324),
    "paint-layers": ("paint-layers", (420, 46, 1600, 800), 960),
    "subtools": ("subtools", (380, 46, 1600, 900), 960),
    "polygroups": ("polygroups", (300, 100, 1600, 860), 960),
    "splats": ("splats", (420, 46, 1600, 900), 960),
    "user-guide": ("user-guide", (130, 140, 1190, 880), 1060),
    "extract": ("extract", (380, 140, 1000, 740), 620),
    "make-sculptable-panel": ("sculptable-panel", (1276, 500, 1600, 960), 324),
}

# The labelled overview: region name and box in window pixels.
REGIONS = [
    ("Title strip", (0, 0, 1600, 24)),
    ("Palette bar", (0, 24, 1276, 46)),
    ("Top shelf", (8, 50, 1216, 114)),
    ("Left shelf", (2, 118, 100, 640)),
    ("Canvas", (102, 118, 1216, 895)),
    ("Right shelf", (1218, 118, 1276, 560)),
    ("Bottom shelf", (2, 897, 1276, 960)),
    ("Right tray (Tool palette)", (1280, 50, 1600, 560)),
]


def font(size):
    try:
        return ImageFont.truetype(str(FONT), size)
    except OSError:
        return ImageFont.load_default()


def save(img: Image.Image, name: str, max_width: int):
    if img.width > max_width:
        img = img.resize((max_width, round(img.height * max_width / img.width)), Image.LANCZOS)
    path = OUT / f"{name}.jpg"
    img.convert("RGB").save(path, "JPEG", quality=84, optimize=True, progressive=True)
    return path


def label(draw, xy, text, size=17, anchor="la"):
    f = font(size)
    x0, y0, x1, y1 = draw.textbbox(xy, text, font=f, anchor=anchor)
    draw.rounded_rectangle((x0 - 7, y0 - 4, x1 + 7, y1 + 5), radius=6, fill=ACCENT)
    draw.text(xy, text, font=f, fill=(255, 255, 255), anchor=anchor)


def overview(shots: pathlib.Path):
    img = Image.open(shots / "overview.png").convert("RGB")
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    for _, box in REGIONS:
        d.rectangle(box, outline=ACCENT + (255,), width=3)
    img = Image.alpha_composite(img.convert("RGBA"), over)
    d = ImageDraw.Draw(img)
    # Where each label sits, and the point its leader line goes to (None: the label is inside its region).
    spots = {
        "Title strip": ((760, 170), (760, 12)), "Palette bar": ((420, 170), (420, 35)),
        "Top shelf": ((1060, 170), (1060, 100)), "Left shelf": ((112, 400), None),
        "Canvas": ((640, 800), None), "Right shelf": ((1206, 590), None),
        "Bottom shelf": ((640, 880), None), "Right tray (Tool palette)": ((1440, 600), None),
    }
    anchors = {"Left shelf": "lm", "Right shelf": "rm", "Canvas": "mm", "Bottom shelf": "mb",
               "Right tray (Tool palette)": "mm"}
    for name, _ in REGIONS:
        at, target = spots[name]
        if target:
            d.line((at, target), fill=ACCENT, width=3)
            d.ellipse((target[0] - 5, target[1] - 5, target[0] + 5, target[1] + 5), fill=ACCENT)
        label(d, at, name, 19, anchors.get(name, "mm"))
    return save(img, "interface-overview", 1280)


def remesh(shots: pathlib.Path):
    box = (380, 230, 940, 780)
    before = Image.open(shots / "remesh-before.png").convert("RGB").crop(box)
    after = Image.open(shots / "remesh-after.png").convert("RGB").crop(box)
    img = Image.new("RGB", (before.width * 2 + 12, before.height), (20, 20, 22))
    img.paste(before, (0, 0))
    img.paste(after, (before.width + 12, 0))
    d = ImageDraw.Draw(img)
    label(d, (before.width // 2, 24), "Before: decimated, uneven", 20, "mt")
    label(d, (before.width + 12 + after.width // 2, 24), "After Voxel Remesh", 20, "mt")
    return save(img, "remesh", 900)


def mirror_weld(shots: pathlib.Path):
    """A head sculpted on one side, then after Mirror And Weld X."""
    box = (420, 150, 900, 850)
    before = Image.open(shots / "mirror-weld-before.png").convert("RGB").crop(box)
    after = Image.open(shots / "mirror-weld-after.png").convert("RGB").crop(box)
    img = Image.new("RGB", (before.width * 2 + 12, before.height), (20, 20, 22))
    img.paste(before, (0, 0))
    img.paste(after, (before.width + 12, 0))
    d = ImageDraw.Draw(img)
    label(d, (before.width // 2, 24), "Sculpted on one side", 20, "mt")
    label(d, (before.width + 12 + after.width // 2, 24), "After Mirror And Weld X", 20, "mt")
    return save(img, "mirror-weld", 900)


def subdivision(shots: pathlib.Path):
    box = (250, 125, 1070, 890)
    names = [("sdiv-1", "SDiv 1: 108 triangles"), ("sdiv-3", "SDiv 3: 1,728"), ("sdiv-6", "SDiv 6: 110,592")]
    panels = [Image.open(shots / f"{n}.png").convert("RGB").crop(box) for n, _ in names]
    w, h = panels[0].size
    img = Image.new("RGB", (w * 3 + 24, h), (20, 20, 22))
    d = ImageDraw.Draw(img)
    for i, (p, (_, text)) in enumerate(zip(panels, names)):
        img.paste(p, (i * (w + 12), 0))
        label(d, (i * (w + 12) + w // 2, 24), text, 22, "mt")
    return save(img, "subdivision-levels", 1200)


def dyntopo(shots: pathlib.Path):
    box = (300, 125, 1020, 845)
    names = [("dyntopo-wire", "Dynamic on: new triangles where the brush went"), ("dyntopo-shaded", "Tendril horns")]
    panels = [Image.open(shots / f"{n}.png").convert("RGB").crop(box) for n, _ in names]
    w, h = panels[0].size
    img = Image.new("RGB", (w * 2 + 12, h), (20, 20, 22))
    d = ImageDraw.Draw(img)
    for i, (p, (_, text)) in enumerate(zip(panels, names)):
        img.paste(p, (i * (w + 12), 0))
        label(d, (i * (w + 12) + w // 2, 24), text, 22, "mt")
    return save(img, "dynamic-topology", 1000)


def qremesh_guides(shots: pathlib.Path):
    box = (440, 290, 880, 720)
    names = [("guides-without", "Quad Remesh"), ("guides-drawn", "Quad Remesh Guides drawn"), ("guides-with", "Quad Remesh with guides")]
    panels = [Image.open(shots / f"{n}.png").convert("RGB").crop(box) for n, _ in names]
    w, h = panels[0].size
    img = Image.new("RGB", (w * 3 + 24, h), (20, 20, 22))
    d = ImageDraw.Draw(img)
    for i, (p, (_, text)) in enumerate(zip(panels, names)):
        img.paste(p, (i * (w + 12), 0))
        label(d, (i * (w + 12) + w // 2, 16), text, 20, "mt")
    return save(img, "qremesh-guides", 1200)


def make_sculptable(shots: pathlib.Path):
    box = (455, 240, 865, 770)
    names = [("sculptable-capture", "Splat capture"), ("sculptable-review", "Review: landmarks and loops"),
             ("sculptable-result", "Sculptable quads")]
    panels = [Image.open(shots / f"{n}.png").convert("RGB").crop(box) for n, _ in names]
    w, h = panels[0].size
    img = Image.new("RGB", (w * 3 + 24, h), (20, 20, 22))
    d = ImageDraw.Draw(img)
    for i, (p, (_, text)) in enumerate(zip(panels, names)):
        img.paste(p, (i * (w + 12), 0))
        label(d, (i * (w + 12) + w // 2, 16), text, 20, "mt")
    return save(img, "make-sculptable", 1200)


def shading(shots: pathlib.Path):
    """Smooth and Flat side by side (the same head, eyes kept smooth), with the palette's shading rows below."""
    box = (420, 120, 1000, 870)
    smooth = Image.open(shots / "shading-smooth.png").convert("RGB").crop(box)
    flat = Image.open(shots / "shading-flat.png").convert("RGB").crop(box)
    rows = Image.open(shots / "shading-palette.png").convert("RGB").crop((384, 456, 700, 550))
    img = Image.new("RGB", (smooth.width * 2, smooth.height), (31, 31, 33))
    img.paste(smooth, (0, 0))
    img.paste(flat, (smooth.width, 0))
    img.paste(rows, (img.width - rows.width - 12, img.height - rows.height - 12))
    d = ImageDraw.Draw(img)
    label(d, (16, 16), "Smooth")
    label(d, (smooth.width + 16, 16), "Flat (Eyes: Smooth)")
    return save(img, "shading", 960)


# The lesson in first-sculpt.md (tests/ui/first_sculpt.sfui): name -> crop in the 1600x960 window, or None for the
# PNG Save Render wrote into the test's library. The canvas crop leaves out the shelves; full-width shots show menus.
FIRST_SCULPT_CANVAS = (110, 160, 1210, 860)
FIRST_SCULPT = {
    "01-sphere": (0, 0, 1600, 960), "02-mask": FIRST_SCULPT_CANVAS, "02-walls": FIRST_SCULPT_CANVAS,
    "03-colour": (0, 0, 1210, 860), "04-cap": FIRST_SCULPT_CANVAS, "05-dome": FIRST_SCULPT_CANVAS,
    "06-divide": FIRST_SCULPT_CANVAS, "07-door": FIRST_SCULPT_CANVAS, "08-windows": FIRST_SCULPT_CANVAS,
    "09-spots": FIRST_SCULPT_CANVAS, "10-spots": FIRST_SCULPT_CANVAS, "11-paint": FIRST_SCULPT_CANVAS,
    "12-material": (0, 0, 1210, 860), "12-render": None,
}


def first_sculpt(shots: pathlib.Path) -> list:
    out = OUT / "first-sculpt"
    out.mkdir(parents=True, exist_ok=True)
    made = []
    for name, box in FIRST_SCULPT.items():
        if box is None:
            renders = sorted((shots / "library").rglob("Render *.png"))
            if not renders:
                raise SystemExit(f"no Save Render output under {shots}/library")
            img = Image.open(renders[-1]).convert("RGB")
            w, h = img.size  # the canvas's shape: trim to the model's middle
            img = img.crop((w * 23 // 100, h * 12 // 100, w * 77 // 100, h * 88 // 100))
        else:
            img = Image.open(shots / f"{name}.png").convert("RGB").crop(box)
        width = 960 if box is None or box[2] - box[0] > 1100 else 720
        made.append(save(img, f"first-sculpt/{name}", width))
    return made


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("shots", type=pathlib.Path, help="folder with the screenshots (--ui-out of shots.sfui)")
    ap.add_argument("--preview", type=pathlib.Path, help="also write a contact sheet of every image here")
    ap.add_argument("--only", action="append", help="make only this image (repeatable), e.g. --only qremesh-guides")
    ap.add_argument("--first-sculpt", action="store_true",
                    help="the shots are tests/ui/first_sculpt.sfui's: make images/first-sculpt/*.jpg")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if args.first_sculpt:
        for p in first_sculpt(args.shots):
            print(f"{p.relative_to(ROOT)}  {p.stat().st_size // 1024} KB")
        return 0
    composed = {"interface-overview": overview, "remesh": remesh, "subdivision-levels": subdivision,
                "qremesh-guides": qremesh_guides, "dynamic-topology": dyntopo,
                "make-sculptable": make_sculptable, "shading": shading, "mirror-weld": mirror_weld}
    wanted = lambda name: not args.only or name in args.only
    made = [make(args.shots) for name, make in composed.items() if wanted(name)]
    for name, (shot, box, width) in CROPS.items():
        if not wanted(name):
            continue
        src = args.shots / f"{shot}.png"
        if not src.exists():
            print(f"missing screenshot {src}", file=sys.stderr)
            return 1
        made.append(save(Image.open(src).convert("RGB").crop(box), name, width))
    for p in made:
        print(f"{p.relative_to(ROOT)}  {p.stat().st_size // 1024} KB")
    if args.preview:
        thumbs = [Image.open(p) for p in made]
        cols, cell = 5, 320
        sheet = Image.new("RGB", (cols * cell, ((len(thumbs) + cols - 1) // cols) * cell), (40, 40, 40))
        for i, t in enumerate(thumbs):
            t.thumbnail((cell - 8, cell - 24))
            sheet.paste(t, ((i % cols) * cell + 4, (i // cols) * cell + 4))
            ImageDraw.Draw(sheet).text(((i % cols) * cell + 4, (i // cols) * cell + cell - 18), made[i].stem, fill="yellow")
        sheet.save(args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
