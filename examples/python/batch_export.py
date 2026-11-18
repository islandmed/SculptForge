# SculptForge for Python: batch export. Every model in a folder -> an even remesh, a reduced copy, GLB + USD.
#   pip install sculptforge
#   python batch_export.py OUT_FOLDER [MODEL_FOLDER]     (without a model folder it makes three to show the flow)
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import sys

import sculptforge as sf


def main(out: pathlib.Path, models: pathlib.Path | None) -> None:
    out.mkdir(parents=True, exist_ok=True)
    sources = sorted(p for p in models.iterdir() if p.suffix.lower() in {".obj", ".ply", ".fbx", ".glb", ".gltf", ".usda"}) if models else []
    if not sources:  # a demo: three spheres of growing detail
        sources = []
        for level in (3, 4, 5):
            doc = sf.Document()
            sf.run(doc, "new_sphere", subdivisions=level)
            path = out / f"sphere{level}.obj"
            sf.run(doc, "save_obj", path=path)
            sources.append(path)
    for src in sources:
        doc = sf.Document()
        try:
            sf.run(doc, "open", path=src)
            sf.run(doc, "remesh", resolution=128)
            sf.run(doc, "decimate", ratio=0.5)
            sf.run(doc, "export_glb", path=out / f"{src.stem}.glb")
            sf.run(doc, "export_usd", path=out / f"{src.stem}.usda")
        except sf.CommandError as e:  # one bad file doesn't stop the batch
            print(f"skipped {src.name}: {e}", file=sys.stderr)
            continue
        print(f"{src.name}: {doc.triangles.shape[0]} triangles")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "out"), pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None)
