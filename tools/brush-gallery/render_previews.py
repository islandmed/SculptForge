#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render a preview image for every .sfbrush brush in a folder.

Each preview is the same shot, so a gallery reads as one set: a sculpting
sphere with three S-curve strokes of the brush, rendered headless by
`sculptforge --render` with the Clay Lit Sphere from the front.

    python3 render_previews.py repo/brushes --out repo/previews
    python3 render_previews.py my_brush.sfbrush --out .          # one brush
    python3 render_previews.py repo/brushes --out repo/previews --changed   # only new/edited brushes

Needs the `sculptforge` app (an OpenGL 3.3 context; on a headless Linux box run
it under `xvfb-run`). Standard library only; if Pillow is installed the PNGs
are also shrunk to a 256-colour palette (about a third of the size, no visible
difference on these renders).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Three S-curves across the front of the unit sphere (x, y; the stroke
# projects along -Z onto the surface). Mirrored top and bottom, so the
# preview is balanced whatever the brush does.
CURVES = [
    [(-0.75, 0.42), (-0.25, 0.58), (0.25, 0.30), (0.75, 0.45)],
    [(-0.90, 0.00), (-0.30, 0.15), (0.30, -0.15), (0.90, 0.00)],
    [(-0.75, -0.45), (-0.25, -0.30), (0.25, -0.58), (0.75, -0.42)],
]
PLANE_CURVES = [  # the same layout on a plane seen from above (x, z)
    [(-0.80, -0.50), (-0.30, -0.62), (0.30, -0.36), (0.80, -0.48)],
    [(-0.85, 0.00), (-0.30, -0.12), (0.30, 0.14), (0.85, 0.00)],
    [(-0.80, 0.48), (-0.30, 0.36), (0.30, 0.62), (0.80, 0.50)],
]


def find_app(explicit):
    """The sculptforge executable: --app, $SCULPTFORGE, a build folder next to this repo, or PATH."""
    candidates = [explicit, os.environ.get("SCULPTFORGE")]
    root = Path(__file__).resolve().parents[2]
    for build in ("build", "build-release", "out/build"):
        candidates += [root / build / "sculptforge", root / build / "sculptforge.exe",
                       root / build / "Release" / "sculptforge.exe"]
    candidates.append(shutil.which("sculptforge"))
    for c in candidates:
        if c and Path(c).is_file():
            return str(c)
    sys.exit("error: cannot find the sculptforge app; pass --app PATH or set SCULPTFORGE")


def quote(text):
    """A double-quoted sculptforge script value."""
    return '"' + str(text).replace("\\", "\\\\").replace('"', '\\"') + '"'


def cavity_mask(brush_path):
    try:
        value = json.loads(Path(brush_path).read_text(encoding="utf-8")).get("cavityMask", 0)
        return float(value) if isinstance(value, (int, float)) else 0.0
    except (OSError, ValueError):
        return 0.0


def preview_script(brush_path, subject, radius, material):
    if subject == "plane":
        lines = ["new_plane resolution=900"]
        curves, view = PLANE_CURVES, "0,-1,0"
        points = lambda c: " ".join(f"{x},0,{z}" for x, z in c)
    else:
        lines = ["new_sphere subdivisions=8"]
        curves, view = CURVES, "0,0,-1"
        points = lambda c: " ".join(f"{x},{y},0" for x, y in c)
    # Brushes masked to ridges or cavities do nothing on a smooth surface: give
    # them raised forms (or grooves) along the same curves to work on first.
    mask = cavity_mask(brush_path)
    if abs(mask) >= 0.25:
        invert = "true" if mask > 0 else "false"
        for c in curves:
            lines.append(f"stroke brush=Standard strength=1 invert={invert} points={quote(points(c))} view={view} "
                         f"radius={radius * 1.2:g}")
    lines.append(f"brush_load path={quote(brush_path)}")
    for i, c in enumerate(curves):
        lines.append(f"stroke points={quote(points(c))} view={view} radius={radius} seed={i + 1}")
    lines.append(f"set_material name={quote(material)}")
    return "; ".join(lines)


def render(app, brush, out, args):
    view = "0,-58" if args.subject == "plane" else "0,0"
    zoom = "2.3" if args.subject == "plane" else "1.75"
    out.unlink(missing_ok=True)  # a failed render must not leave the old preview behind
    cmd = [app, "--render", str(out), "--size", args.size, "--view", view, "--zoom", zoom,
           "--run", preview_script(brush.resolve(), args.subject, args.radius, args.material)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    log = result.stdout + result.stderr
    if result.returncode != 0 or not out.is_file():
        return log.strip() or f"exit code {result.returncode}"
    return None


def shrink_png(path):
    """Palette-quantize with Pillow when it is available (optional)."""
    try:
        from PIL import Image
    except ImportError:
        return
    with Image.open(path) as im:
        small = im.convert("RGB").quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    small.save(path, optimize=True)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("inputs", nargs="+", help=".sfbrush files or folders of them")
    p.add_argument("--out", required=True, help="folder for the PNG previews (<brush file name>.png)")
    p.add_argument("--app", help="path to the sculptforge executable")
    p.add_argument("--size", default="512x384", help="image size WxH (default 512x384)")
    p.add_argument("--subject", choices=["sphere", "plane"], default="sphere", help="what to sculpt on")
    p.add_argument("--material", default="Clay", help="Lit Sphere (Clay, Red Wax, Skin, White, Bronze, Chrome)")
    p.add_argument("--radius", type=float, default=0.12, help="brush radius in world units (sphere radius 1)")
    p.add_argument("--no-shrink", action="store_true", help="keep the full-colour PNGs even if Pillow is installed")
    p.add_argument("--changed", action="store_true", help="skip brushes whose preview is newer than the brush")
    args = p.parse_args()

    brushes = []
    for i in args.inputs:
        path = Path(i)
        brushes += sorted(path.glob("*.sfbrush")) if path.is_dir() else [path]
    if not brushes:
        sys.exit("error: no .sfbrush files found")
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    app = find_app(args.app)

    failed = 0
    for brush in brushes:
        out = out_dir / (brush.stem + ".png")
        if args.changed and out.is_file() and out.stat().st_mtime >= brush.stat().st_mtime:
            continue
        error = render(app, brush, out, args)
        if error:
            failed += 1
            print(f"FAILED {brush}: {error}", file=sys.stderr)
        else:
            if not args.no_shrink:
                shrink_png(out)
            print(f"rendered {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
