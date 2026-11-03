#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns the raw UI screenshots of tools/guide/shots.sfui into the user guide's images.

    build/sculptforge --ui-test tools/guide/shots.sfui --ui-out build/guide-shots
    python3 tools/guide/make_images.py build/guide-shots [--preview sheet.png]
    python3 tools/guide/make_images.py build/wn-shots --whats-new   (What's New's 600x340 pictures)

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
ACCENT = (122, 162, 247)  # Tokyo Night blue, the default look's accent

# name: (screenshot, (x0, y0, x1, y1) crop in 1600x960 window pixels, max width)
CROPS = {
    "palette-dropdown": ("palette-dropdown", (140, 0, 770, 295), 630),
    "palette-tray": ("palette-tray", (1280, 560, 1600, 960), 320),
    "draw-palette": ("draw-palette", (140, 0, 770, 600), 630),
    "lightbox": ("lightbox", (90, 85, 1020, 230), 930),
    "brush-picker": ("brush-picker", (250, 82, 725, 414), 475),
    "stroke-picker": ("stroke-picker", (0, 190, 420, 450), 420),
    "alpha-picker": ("alpha-picker", (0, 295, 575, 668), 575),
    "material-picker": ("material-picker", (25, 320, 505, 545), 480),
    "custom-matcaps": ("custom-matcaps", (436, 0, 980, 700), 544),
    "ctrl-help": ("ctrl-help", (1060, 500, 1600, 740), 540),
    "command-palette": ("command-palette", (355, 100, 960, 500), 605),
    "console": ("console", (0, 698, 1275, 866), MAX_WIDTH),
    "preferences": ("preferences", (180, 0, 715, 960), 535),
    "right-shelf": ("overview", (1218, 90, 1278, 625), 60),
    "bottom-shelf": ("overview", (0, 864, 1280, 960), MAX_WIDTH),
    "sculpt-lion": ("overview", (0, 0, 1600, 960), 1100),
    "brush-lab": ("brush-lab", (118, 102, 575, 332), 457),
    "brush-library": ("brush-library", (405, 600, 765, 960), 360),
    "community-brushes": ("community-brushes", (395, 138, 1205, 820), 810),
    "generate-3d": ("generate-3d", (882, 102, 1198, 830), 316),
    "export": ("export", (1280, 40, 1600, 780), 320),
    "masking": ("masking", (300, 46, 1600, 800), 960),
    "polypaint": ("polypaint", (230, 20, 1100, 800), 870),
    "layers": ("layers", (1280, 40, 1600, 560), 320),
    "paint-layers": ("paint-layers", (300, 46, 1600, 800), 960),
    "subtools": ("subtools", (380, 46, 1600, 900), 960),
    "polygroups": ("polygroups", (300, 100, 1600, 860), 960),
    "splats": ("splats", (300, 90, 1280, 865), 980),
    "user-guide": ("user-guide", (138, 108, 1180, 842), 1042),
    "extract": ("extract", (380, 140, 1000, 740), 620),
    "make-sculptable-panel": ("sculptable-panel", (1276, 500, 1600, 960), 324),
    "qremesh": ("qremesh", (0, 0, 1600, 960), MAX_WIDTH),
}

# What's New feature pictures (tools/guide/whats_new_shots.sfui): name: (screenshot, crop with the 600:340 shape), saved
# at exactly 600x340 (--whats-new).
WHATS_NEW = {
    "wn-new-look": ("wn-new-look", (0, 30, 1340, 789)),
    "wn-wayland": ("wn-wayland", (0, 0, 600, 340)),
    "wn-memberships": ("wn-memberships", (117, 90, 1483, 864)),
    "wn-full-alpha": ("wn-full-alpha", (300, 290, 1020, 698)),
    "wn-benchmark": ("wn-benchmark", (500, 190, 1100, 530)),
    "wn-whats-new": ("wn-whats-new", (170, 120, 1430, 834)),
    "wn-starter-meshes": ("wn-starter-meshes", (108, 145, 1212, 770)),
    "wn-brush-settings": ("wn-brush-settings", (160, 145, 880, 553)),
}
WN_SIZE = (600, 340)

# Feature panels from tools/guide/feature_shots.py (one folder per UI test): name -> ("test/shot", crop, max width).
FEATURE_CROPS = {
    "bake": ("bake/high-and-baked-low", (0, 0, 1600, 960), MAX_WIDTH),
    "bake-mesh-maps": ("bake/bake-palette", (1280, 280, 1600, 720), 320),
    "blender-menu": ("blender_bridge/menu", (130, 0, 1010, 580), 880),
    "capture-from-view": ("kelvinlet_capture/capture_window", (440, 140, 1200, 640), 760),
    "fill-layer-panel": ("fill_layers/fill-layer-panel", (1280, 60, 1600, 960), 320),
    "gizmo-pose": ("gizmo/posing-mid-drag", (110, 200, 1000, 860), 890),
    "pbr-viewport": ("pbr_viewport/pbr-photo-studio", (0, 0, 1600, 960), MAX_WIDTH),
    "pbr-environment": ("pbr_viewport/pbr-outdoor-background", (100, 85, 1215, 870), 960),
    "pbr-brush-material": ("pbr_channels/brush-material", (280, 20, 640, 960), 360),
    "pbr-solo-roughness": ("pbr_channels/solo-roughness", (300, 85, 1600, 870), 960),
    "performance-panel": ("performance/performance-panel", (180, 20, 715, 800), 535),
    "texture-painting": ("texture_paint/stroke", (0, 0, 1600, 960), MAX_WIDTH),
    "timeline-strip": ("timeline/strokes", (100, 85, 1215, 640), 960),
    "uv-editor": ("uv_editor/unwrapped", (100, 85, 1215, 870), 960),
    "uv-editor-seams": ("uv_editor/seam-tool", (100, 85, 1215, 870), 960),
    "uv-editor-stretch": ("uv_editor/stretch", (100, 85, 1215, 870), 960),
    "uv-editor-trim": ("uv_editor/trim", (100, 85, 1215, 870), 960),
    "uv-editor-udim": ("uv_editor/udim", (100, 85, 1215, 870), 960),
    "vdm-brushes": ("vdm/stamped", (300, 40, 1600, 960), 960),
    "facial-shapes-panel": ("face_shapes/head-neutral", (300, 40, 1600, 960), 960),
}

# The labelled overview: region name and box in window pixels (the SculptForge layout).
REGIONS = [
    ("Menu bar", (0, 0, 1600, 40)),
    ("Top shelf", (6, 44, 1270, 82)),
    ("Left shelf", (8, 92, 102, 616)),
    ("Canvas", (106, 90, 1212, 862)),
    ("Right shelf", (1220, 90, 1276, 624)),
    ("Bottom shelf", (6, 866, 1276, 958)),
    ("Right tray (Tool panel)", (1282, 46, 1596, 956)),
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
    draw.text(xy, text, font=f, fill=(26, 27, 38), anchor=anchor)  # dark on the light accent


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
        "Menu bar": ((560, 150), (860, 20)), "Top shelf": ((900, 150), (900, 63)),
        "Left shelf": ((112, 700), (55, 560)), "Canvas": ((660, 800), None), "Right shelf": ((1206, 700), (1248, 600)),
        "Bottom shelf": ((1080, 800), (1080, 905)), "Right tray (Tool panel)": ((1440, 640), None),
    }
    anchors = {"Left shelf": "lm", "Right shelf": "rm", "Canvas": "mm", "Bottom shelf": "mm",
               "Right tray (Tool panel)": "mm"}
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
    rows = Image.open(shots / "shading-palette.png").convert("RGB").crop((625, 405, 972, 495))
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
    ap.add_argument("--features", type=pathlib.Path, help="also crop the feature panels from tools/guide/feature_shots.py's --out")
    ap.add_argument("--only", action="append", help="make only this image (repeatable), e.g. --only qremesh-guides")
    ap.add_argument("--first-sculpt", action="store_true",
                    help="the shots are tests/ui/first_sculpt.sfui's: make images/first-sculpt/*.jpg")
    ap.add_argument("--whats-new", action="store_true", help="make only the What's New pictures (whats_new_shots.sfui)")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if args.first_sculpt:
        for p in first_sculpt(args.shots):
            print(f"{p.relative_to(ROOT)}  {p.stat().st_size // 1024} KB")
        return 0
    if args.whats_new:
        for name, (shot, box) in WHATS_NEW.items():
            src = args.shots / f"{shot}.png"
            if not src.exists():
                print(f"missing screenshot {src}", file=sys.stderr)
                return 1
            img = Image.open(src).convert("RGB").crop(box).resize(WN_SIZE, Image.LANCZOS)
            path = OUT / f"{name}.jpg"
            img.save(path, "JPEG", quality=90, optimize=True, progressive=True, subsampling=0)
            print(f"{path.relative_to(ROOT)}  {path.stat().st_size // 1024} KB")
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
    for name, (shot, box, width) in FEATURE_CROPS.items() if args.features else ():
        if not wanted(name):
            continue
        src = args.features / f"{shot}.png"
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
