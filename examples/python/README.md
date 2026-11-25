# Python examples

Scripts for the `sculptforge` Python package ([python](../../python/README.md); guide:
[docs/guide/python-package.md](../../docs/guide/python-package.md)). They run without the app, a window or a network.

| Script | What it does | Run it |
|---|---|---|
| `batch_export.py` | Every model in a folder: remesh evenly, make a reduced copy, write GLB and USD. One bad file is reported and skipped. Without a model folder it makes three spheres to show the flow | `python batch_export.py OUT_FOLDER [MODEL_FOLDER]` |
| `procedural_asset.py` | A sphere, a ring of brush strokes placed by code, a masked band kept from the strokes, then GLB. The same input gives the same bytes every run | `python procedural_asset.py OUT_FOLDER [BUMPS]` |
| `test_my_asset.py` | Your own asset rules (a triangle budget, a size limit, no degenerate triangles, a clean GLB export) as `pytest` tests; swap the sphere for `sf.Document.open("your_asset.fbx")` | `pytest test_my_asset.py` |

The scripts are GPL-3.0-or-later, and the scripting exception lets what you write from them be under any licence.
