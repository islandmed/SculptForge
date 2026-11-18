# SculptForge for Python: a procedural asset. A sphere, a ring of brush strokes placed by code, a masked band
# kept from the strokes, then GLB. Same input, same bytes, every run.
#   python procedural_asset.py OUT_FOLDER [BUMPS]
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import pathlib
import sys

import numpy as np

import sculptforge as sf


def main(out: pathlib.Path, bumps: int = 8) -> None:
    out.mkdir(parents=True, exist_ok=True)
    doc = sf.Document()
    sf.run(doc, "new_sphere", subdivisions=5)

    # Keep a band around the middle as it is: mask it (1 = protected), as NumPy.
    y = doc.vertices[:, 1]
    doc.mask = (np.abs(y) < 0.1).astype(np.float32)

    # Bumps in a ring above the band: each is one stroke, projected onto the surface from its own side.
    for i in range(bumps):
        a = 2 * math.pi * i / bumps
        x, z = math.cos(a), math.sin(a)
        sf.run(doc, "stroke", brush="Inflate", points=f"{0.8 * x:.4f},0.45,{0.8 * z:.4f}",
               view=f"{-x:.4f},0,{-z:.4f}", radius=0.18, strength=0.5)
    # A crease along the band's top edge.
    ring = " ".join(f"{math.cos(t):.4f},0.12,{math.sin(t):.4f}" for t in np.linspace(0, math.pi, 12))
    sf.run(doc, "stroke", brush="Crease", points=ring, view="0,0,-1", radius=0.08, strength=0.4)

    sf.run(doc, "mask_clear")
    sf.run(doc, "export_glb", path=out / "procedural.glb")
    print(f"procedural.glb: {doc.vertices.shape[0]} vertices")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "out"), int(sys.argv[2]) if len(sys.argv) > 2 else 8)
