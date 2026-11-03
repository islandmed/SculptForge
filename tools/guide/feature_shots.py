#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Screenshots of feature panels for the user guide, taken by running feature UI tests in the default look.

    python3 tools/guide/feature_shots.py --app build/sculptforge --out build/guide-features
    python3 tools/guide/make_images.py build/guide-shots --features build/guide-features

Tests written for the Classic look and the ZBrush-style palette bar run from a converted copy: the look line becomes
the default look and each palette click opens its task menu and section instead. Shots land in <out>/<test>/.
"""
import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ["bake", "fill_layers", "texture_paint", "uv_editor", "face_shapes", "gizmo", "pbr_viewport", "pbr_channels",
         "performance", "timeline", "vdm", "kelvinlet_capture", "blender_bridge"]
DEFAULT_LOOK = "run ui_look look=forge scheme=tokyo_night layout=sculptforge"
# Palette -> (task menu, section) in the SculptForge layout (src/app/Palettes.cpp, taskMenus).
MENUS = {
    "file": ("File##menu.file", "File##section.file"), "script": ("File##menu.file", "Script##section.script"),
    "edit": ("Edit##menu.edit", "Edit##section.edit"), "preferences": ("Edit##menu.edit", "Preferences##section.preferences"),
    "brush": ("Sculpt##menu.sculpt", "Brush##section.brush"), "draw": ("Sculpt##menu.sculpt", "Draw & Tablet##section.draw"),
    "stroke": ("Sculpt##menu.sculpt", "Stroke##section.stroke"), "alpha": ("Sculpt##menu.sculpt", "Alpha##section.alpha"),
    "transform": ("Sculpt##menu.sculpt", "Symmetry & Gizmo##section.transform"), "color": ("Paint##menu.paint", None),
    "tool": ("Mesh##menu.mesh", "Parts & Geometry##section.tool"), "generate": ("Mesh##menu.mesh", "Generate 3D##section.generate"),
    "render": ("View##menu.view", None), "material": ("Render##menu.render", "Materials##section.material"),
    "splats": ("Render##menu.render", "Gaussian Splats##section.splats"), "help": ("Help##menu.help", None),
}


def convert(text: str) -> str:
    text = re.sub(r"^run ui_look .*$", DEFAULT_LOOK, text, flags=re.M)

    def menu(m):
        top, section = MENUS[m.group(1)]
        return f'click "{top}"' + (f'\nclick "{section}"' if section else "")

    return re.sub(r'click "[^"#]*##palette\.([a-z]+)"', menu, text)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default="build/sculptforge")
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("build/guide-features"))
    args = ap.parse_args()
    env = dict(os.environ, SDL_APP_ID=os.environ.get("SDL_APP_ID", "sculptforge-agent"))
    scratch = lambda: set(ROOT.glob("sf_ui_*")) | set(ROOT.glob("ui-shots"))  # files some tests leave in the working folder
    before = scratch()
    for name in TESTS:
        src = ROOT / "tests" / "ui" / f"{name}.sfui"
        tmp = src.with_name(f"_guide_{name}.sfui")  # beside the original, so its relative paths still resolve
        tmp.write_text(convert(src.read_text(encoding="utf-8")), encoding="utf-8")
        try:
            r = subprocess.run([args.app, "--ui-test", str(tmp), "--ui-out", str(args.out / name)], env=env, cwd=ROOT,
                               capture_output=True, text=True, timeout=900)
        finally:
            tmp.unlink()
        fails = [line for line in r.stdout.splitlines() if line.startswith("FAIL")]
        # A failed check late in a test still leaves the shots before it; make_images.py says if one is missing.
        print(f"{name}: {'ok' if r.returncode == 0 else fails[0] if fails else f'exit {r.returncode}'}")
    for extra in scratch() - before:
        shutil.rmtree(extra) if extra.is_dir() else extra.unlink()
    return 0


if __name__ == "__main__":
    sys.exit(main())
