# Python scripting

> **In short:**
> - Turn it on in **Edit > Preferences > Scripts > Enable Python scripts** (off until you do).
> - The console's **Python** mode runs Python against the open scene; `.py` files in the Scripts folder are listed under
>   **File > Script**.
> - Each line you enter, and each script you run, is one undo step.
> - **A Python script is a program. Only run scripts you trust.**

SculptForge runs Python inside the app, with the same `sculptforge` module that studio pipelines import (see
[Scripting and automation](scripting.md#python)): every command, the Parts, the mesh data, selection sets and the brush
library, plus loops, conditions and functions around them.

## Scripts are trusted code

A Python script can do anything a program on your computer can: read and write your files, reach the network, start
other programs. SculptForge doesn't limit it, the way your system's own Python doesn't. So:

- **Only run scripts you trust**, as you would only run programs you trust.
- Python is **off** until you turn it on in **Edit > Preferences > Scripts**. A studio can keep it off on every computer with
  its policy file (`"python": {"scripts": "never"}`, see [For studios](studios.md)).
- A script runs **only when you run it**: a line in the console, a click in the Scripts list, or **Run script file...**.
  Nothing you open can start one: projects, models, packs, layouts, macros and presets carry no code that runs, and AI
  tools and macros have no command that runs Python.
- SculptForge opens no network port for scripting.

To share tools with people you don't know, make a [plugin](packs-and-macros.md) instead: plugins run sandboxed, with no
access to your files or the network.

## The console's Python mode

Open the console (<kbd>`</kbd>) and click **Python** in its header. Two names are ready:

| Name | What it is |
|---|---|
| `sf` | The `sculptforge` module |
| `doc` | The open scene (also `sf.document()`) |

```python
>>> sf.run('remesh', resolution=200)          # a command on the open scene
>>> [p['name'] for p in doc.parts]
['Head', 'Eyes', 'Teeth']
>>> for i in range(2):
...     sf.run('subdivide')
...
```

A line that opens a block (`for`, `if`, `def`...) waits for the rest, shown with `...`; an empty line runs it. <kbd>Tab</kbd>
completes names, <kbd>Up</kbd> and <kbd>Down</kbd> recall earlier lines. Commands a line runs appear in the console as
script lines, as when you click their buttons. `print()` and errors show in the console; an error stops that line or
script, never the app.

Each line you enter is one undo step, however many commands it ran: <kbd>Ctrl</kbd>+<kbd>Z</kbd> takes back the whole
loop above.

## Script files

Put `.py` files in the **Scripts** folder (**Edit > Preferences > Scripts > Open Scripts folder**, inside your SculptForge
library folder, `Documents/SculptForge/Scripts`). They are listed under **File > Script**; click one to run it. Names
that start with `_` (helper modules) aren't listed. **Run script file...** runs a `.py` file from anywhere.

A script runs as `__main__`, with `import sculptforge as sf`. Each run is one undo step. The Scripts folder is on the
import path, so scripts can share helper modules.

## The API

Everything in [the Python module](scripting.md#python) works in the app, plus:

| Function or property | What it does |
|---|---|
| `sf.document()` | The open scene: the same object every time |
| `sf.run(command, **params)` | Runs a command on the open scene (`sf.run(doc, ...)` works too) |
| `sf.Document()` | A scene of your own, apart from the open one: load, change and export files without touching what you're working on |

`doc.mask`, `doc.colors`, `doc.facegroups` and `doc.hidden` can be assigned (each assignment is undoable), and
`doc.set_mesh(vertices, triangles)` replaces the active Part's mesh as an undoable step.

**NumPy.** With NumPy installed, the mesh arrays are NumPy arrays, as in a pipeline. Without it they are lists:
`doc.vertices` is a list of `(x, y, z)` tuples, and assignments take lists. To add NumPy or any other package, install it
for the same Python version (shown in **Edit > Preferences > Scripts** once Python has run) into the Scripts folder's
`site-packages`:

```sh
python3.13 -m pip install --target "Documents/SculptForge/Scripts/site-packages" numpy
```

**Threads.** A script may start threads, but only the main thread may use the open scene: from another thread
`sf.run` and the assignments raise an error. A script runs until it finishes, and the window waits for it.

## Recipes

### Batch export

Every model in a folder, reduced and exported as glTF, without touching the open scene:

```python
import pathlib
import sculptforge as sf

src = pathlib.Path.home() / "Models" / "raw"
out = src / "glb"
out.mkdir(exist_ok=True)
for path in sorted(src.glob("*.obj")):
    d = sf.Document()                        # a scene of the script's own
    sf.run(d, "open", path=path)
    sf.run(d, "decimate", ratio=0.25)
    sf.run(d, "export_glb", path=out / (path.stem + ".glb"))
    print("exported", path.stem, len(d.triangles), "triangles")
```

### Rename Parts

Give every Part a prefix, for a naming convention:

```python
import sculptforge as sf

doc = sf.document()
for i, part in enumerate(doc.parts):
    if not part["name"].startswith("chr_"):
        sf.run("part_rename", index=i, to="chr_" + part["name"])
```

The whole rename is one undo step.

### A custom brush preset

Brushes are recipes (see [Brush Lab](brush-lab.md)); `brush_create` makes one, or updates it by name, and makes it the
active brush. In a script you can make a family of them:

```python
import sculptforge as sf

for name, depth in [("Ridge Soft", 0.25), ("Ridge Medium", 0.5), ("Ridge Hard", 0.8)]:
    sf.run("brush_create", name=name, direction="normal", depth=depth,
           plane="clay", planeAmount=0.6, pinch=0.3, smooth=0.2, hardness=0.4)
sf.run("brush_use", name="Ridge Medium")
```

`brush_install` keeps a brush in your library for the next session; `help brush_create` in the console lists every
recipe parameter.

*Thanks to Tpqz_ and chris_gardner on Discord for the idea.*
