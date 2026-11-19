# SculptForge for Python

The sculpting engine of [SculptForge](https://sculptforge.app), the free and open-source digital sculpting app, as a
Python package: build procedural assets, batch-process models, and test and automate your pipeline with loops and
functions instead of shell scripts.

```sh
pip install sculptforge     # once it is on PyPI; until then see the guide: https://sculptforge.app/guide/python-package.html
```

```python
import sculptforge as sf

doc = sf.Document()
sf.run(doc, "new_sphere", subdivisions=5)
sf.run(doc, "remesh", resolution=128)
verts, tris = doc.vertices, doc.triangles      # NumPy (N, 3) float32 and (M, 3) uint32
doc.mask = (verts[:, 1] > 0).astype("float32")  # mask the top half
sf.run(doc, "export_glb", path="sphere.glb")

print([c["name"] for c in sf.commands()])       # every command, with its parameters
```

- Every editor command (`sf.commands()` lists them), Parts, layers, masks, face groups, paint and selection sets.
- Imports and exports OBJ, PLY, FBX, glTF/GLB and USD (`.usda`, `.usdz`).
- The engine runs in your Python process: no window, no network, no port, nothing that runs on its own.
- Failures raise `sculptforge.CommandError`; long operations release the GIL. Type hints included.

Wheels for CPython 3.12 to 3.14 on Windows x64, macOS (Apple silicon and Intel) and Linux (x86_64 and aarch64,
glibc 2.28 or later). Guide: https://sculptforge.app/guide/python-package.html

SculptForge is free software under the GNU General Public License v3.0 or later, with the SculptForge Scripting
Exception: your scripts and tools that use the package through its public API can be under any licence.
