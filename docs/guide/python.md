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
| `sf.scripts_folder()` | The Scripts folder |
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

## Buttons and panels from a script

A script can add to SculptForge's interface with `sf.ui`. SculptForge draws what it declares with its own controls:

- `sf.ui.action(id, label, callback, icon=None, help="")`: an **action**. It is listed in the command palette (<kbd>F3</kbd>),
  can go on a shelf, in My Panel or in a pie menu, and can have a key. Running it calls `callback()` as one undo step.
- `sf.ui.panel(id, title)`: a **panel**, a section under **File > Script** that My Panel can hold too. Add controls to it:
  `label(text)`, `separator()`, `button(action_id)` (or `button(label=..., callback=...)`), `slider(id, label, value,
  min, max, integer=False, on_change=None)` and `checkbox(id, label, value=False, on_change=None)`. `on_change(value)`
  runs when a slider is let go or a check box clicked; `panel.value(id)` or `sf.ui.value("panel.control")` reads a
  control.
- `sf.ui.place(id, where)`: puts an action on a shelf (`"top"`, `"bottom"`, `"left"`, `"right"`) or an action or a panel in
  `"my_panel"`, once; your layout keeps it.
- `sf.ui.remove(id)` and `sf.ui.clear()` take them away.

Ids are lower-case words with dots, such as `cleanup.run`. Running a script again replaces what it declared and keeps
the values you set in its panels. Icons: `play`, `sparkles`, `wand`, `box`, `brush`, `scissors`, `layers`, `download`,
`upload`, `star`, `file-code`, `settings`, `eye`, `camera`, `grid`, `move`, `rotate`, `trash`.

What a script declares lasts for the session. To have it every time, tick the script under **Run at start-up** in
**Edit > Preferences > Scripts**: ticked scripts run when SculptForge starts (with Python on). It is the only way a
script runs without you running it, so it is kept tight:

- Only scripts in the Scripts folder can be ticked, and each one is ticked on its own; untick it to stop it.
- SculptForge remembers each ticked script's exact contents. If the file has changed since, it doesn't run: the status
  bar says so, and **Trust this version** beside it in **Edit > Preferences > Scripts** accepts the new contents (look at
  what changed first).
- With Python off, or a studio policy that says never, nothing runs at start-up.

## Windows of other toolkits (Qt, PySide)

A script can open windows of its own beside SculptForge, made with a toolkit such as PySide (Qt). SculptForge runs
its interface on its main thread, and so must the toolkit: `sf.ui.timer(callback, ms)` calls `callback()` there,
between frames, about every `ms` milliseconds, and the toolkit's "process events" call goes in it. Never call the
toolkit's own `exec()` loop: it would never give the main thread back.

- `sf.ui.timer(callback, ms=30)` returns the timer's id; `sf.ui.cancel_timer(id)` stops it. A callback that fails is
  stopped, with its error in the console.
- What a timer's callback does to the scene is one undo step per tick, like any script.
- Only the main thread may use the open scene: a toolkit's own threads get an error from `sf.run`.

Install PySide for SculptForge's Python version into the Scripts folder's `site-packages` (see NumPy above):
`python3.13 -m pip install --target "Documents/SculptForge/Scripts/site-packages" PySide6`.

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

### A panel of your own

A clean-up panel: a resolution slider, a check box and a button, placed in My Panel. Save it in the Scripts folder and
tick it under **Run at start-up** to have it every time.

```python
import sculptforge as sf

def clean_up():
    sf.run("remesh", resolution=sf.ui.value("cleanup.resolution"))
    if sf.ui.value("cleanup.reduce"):
        sf.run("decimate", ratio=0.5)

sf.ui.action("cleanup.run", "Clean up", clean_up, icon="sparkles", help="Remesh the active Part at the panel's resolution")
panel = sf.ui.panel("cleanup", "Clean up")
panel.label("Remesh the active Part")
panel.slider("resolution", "Resolution", 200, 50, 800, integer=True)
panel.checkbox("reduce", "Halve the triangles after")
panel.button("cleanup.run")
sf.ui.place("cleanup", "my_panel")
```

### A PySide window

A window that lists the Parts and subdivides the one you pick. It stays open beside SculptForge until you close it.

```python
import sculptforge as sf
from PySide6 import QtWidgets

app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
win = QtWidgets.QWidget()
win.setWindowTitle("Parts")
layout = QtWidgets.QVBoxLayout(win)
parts = QtWidgets.QListWidget()
layout.addWidget(parts)
button = QtWidgets.QPushButton("Subdivide the selected Part")
layout.addWidget(button)

def refresh():
    parts.clear()
    parts.addItems([p["name"] for p in sf.document().parts])

def subdivide():
    row = parts.currentRow()
    if row >= 0:
        sf.run("part_select", index=row)
        sf.run("subdivide")

button.clicked.connect(subdivide)
refresh()
win.show()
timer = sf.ui.timer(app.processEvents, 30)   # Qt's events, on SculptForge's main thread
app.lastWindowClosed.connect(lambda: sf.ui.cancel_timer(timer))
```

*Thanks to Tpqz_ and chris_gardner on Discord for the idea.*
