# The Python package

> **In short:**
> - `pip install sculptforge`, then `import sculptforge as sf`: the sculpting engine in your own Python, no app needed.
> - Build procedural assets, batch-convert folders of models, and test your pipeline, with loops, functions and NumPy.
> - Every editor command, Parts, layers, masks, face groups, paint and selection sets; imports and exports OBJ, PLY,
>   FBX, glTF/GLB, STL and USD.
> - Runs in your process with no window, no network and no port. Same input, same output, byte for byte.

From SculptForge 0.13.8 the engine is on PyPI for CPython 3.12, 3.13 and 3.14 on Windows x64, macOS (Apple silicon and
Intel, macOS 11 or later) and Linux (x86_64 and aarch64, glibc 2.28 or later: Ubuntu 20.04, Debian 11, RHEL 8 and
newer). It is the same `sculptforge` module that runs [inside the app](python.md), without the open scene.

```sh
python -m pip install sculptforge
```

Use a virtual environment (`python -m venv .venv`) per project, as with any package. NumPy comes with it.

## A first script

```python
import numpy as np
import sculptforge as sf

doc = sf.Document()
doc.new_sphere(5)

# Brush strokes from code: a brush, a path of points (any (K, 3) array), a radius and a strength.
doc.stroke("Clay", [[-0.6, 0.2, 1], [0, 0.4, 1], [0.6, 0.2, 1]], radius=0.25, strength=0.6)
doc.stroke("Crease", np.array([[-0.5, -0.3, 1], [0.5, -0.3, 1]]), radius=0.12, strength=0.5)

verts = doc.vertices                              # NumPy (N, 3) float32, a copy
doc.mask = (verts[:, 1] < 0).astype("float32")    # protect the lower half (1 = masked)
doc.stroke("Inflate", [0, 0.8, 1], radius=0.3, strength=0.4, symmetry="x")   # one point: one dab
doc.clear_mask()

doc.remesh(128)
doc.save("asset.glb")                             # the format follows the file name
```

Every method runs one of SculptForge's commands, and `sf.run(doc, command, **params)` runs any of them directly:
`sf.commands()` lists them all with their parameters, the same ones as the [console and sculptforge-cli](scripting.md),
and `sf.command("stroke")` describes one. Paths can be strings or `pathlib.Path`.

## What you can do

| Function, method or property | What it does |
|---|---|
| `sf.Document()`, `sf.Document.open(path)` | A new, empty scene; or one with a project (`.sfproj`) or model (OBJ, PLY, FBX, glTF/GLB, USD, STL) in it |
| `doc.save(path, **options)` (also `doc.export`) | Saves by file name: `.sfproj`, `.obj`, `.glb`, `.gltf`, `.usda`, `.usdz`, `.usd`, `.fbx`, `.ply`, `.stl`; `doc.export_lods(path, levels)` writes a LOD chain |
| `doc.new_sphere(subdivisions)` | Replaces the scene with a sphere |
| `doc.stroke(brush, points, radius=, strength=, view=, symmetry=, ...)` | One brush stroke along a path; the same input gives the same result |
| `doc.remesh(resolution)`, `doc.decimate(ratio)` or `decimate(triangles=n)`, `doc.subdivide(levels)` | Even surface, fewer triangles, smooth subdivision |
| `doc.divide()`, `doc.subdivision_levels`, `doc.subdivision_level = n` | Subdivision levels: add one, count them, move between them |
| `doc.mask`, `doc.clear_mask()`, `doc.invert_mask()` | The per-vertex mask as a NumPy array (1 = masked) |
| `doc.colors`, `doc.facegroups`, `doc.hidden` | Per-vertex paint, per-triangle face groups and visibility: read and assign NumPy arrays (`None` when absent) |
| `doc.vertices`, `doc.base_vertices`, `doc.normals`, `doc.triangles` | Mesh arrays of the active Part (copies) |
| `doc.set_mesh(vertices, triangles)` | Replaces the active Part's mesh with your own |
| `doc.parts`, `doc.part_names()`, `doc.active_part` | The Parts and which one the arrays and methods act on (index or name) |
| `doc.add_part("cube")` or `add_part(path=...)`, `doc.rename_part(p, to)`, `doc.remove_part(p)` | Add, rename and delete Parts |
| `doc.layers`, `doc.layer_delta(i)` | Sculpt layers as `(name, weight)` and their per-vertex offsets |
| `doc.selection_sets`, `doc.selection_set(name)`, `doc.set_selection_set(name, mask=...)` | Selection sets as arrays |
| `sf.run(doc, command, **params)`, `sf.run_script(doc, script)` | Any command, or several in [script syntax](scripting.md#the-script-language); raise `sf.CommandError` on failure |
| `sf.commands()`, `sf.command(name)`, `sf.tool_schema()` | Every command and its parameters; one command; the same as LLM tool definitions (JSON) |

The package ships type hints: editors complete names and check argument types.

## Batch export

```python
import pathlib
import sculptforge as sf

for src in sorted(pathlib.Path("scans").glob("*.obj")):
    try:
        doc = sf.Document.open(src)
        doc.remesh(128)
        doc.decimate(0.5)
        doc.save(f"out/{src.stem}.usda")
    except sf.CommandError as e:     # one bad file doesn't stop the batch
        print("skipped", src.name, e)
```

## Testing your assets

The engine gives the same result for the same input on every run, so it fits in a test suite:

```python
import numpy as np, sculptforge as sf

def test_triangle_budget():
    doc = sf.Document.open("hero.fbx")
    assert doc.triangles.shape[0] <= 200_000
```

Run it with `pytest`. More in the examples folder of the source:
[batch export, a procedural asset and asset tests](https://github.com/islandmed/SculptForge/tree/main/examples/python).

## Safe saving

Every save and export writes to a hidden temporary folder beside the target and then replaces the file in one step.
If a command fails, or your script is stopped halfway, the file you had is still there, whole.

## Threads

Commands release the GIL while they run, so your other Python threads (a progress display, file copies, network
uploads of finished files) keep going during a long remesh or export.

## Trust

The package is a library, like any other you install: it runs only what your script asks for. It opens no network
port, makes no network connection (the AI service commands say they are not available in the package) and starts
nothing on its own. Open only files you trust, as with any program.

## Licence

SculptForge, including this package, is free software under the GNU General Public License v3.0 or later, with the
**SculptForge Scripting Exception**: your scripts and tools that use the package through its public API (and
programs that call `sculptforge-cli`) can be under any licence, closed or open, and you can ship them to clients and
vendors. Only SculptForge itself, and changes you make to it, stay under the GPL. Models you make with it are yours.
[For studios](studios.md) explains more.
