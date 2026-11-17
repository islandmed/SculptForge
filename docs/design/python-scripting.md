# Python scripting in the app

Asked for by Tpqz_ and chris_gardner on Discord (#ideas): Python in the regular app, not only the pipeline module;
scripts that add their own buttons and panels; their own Qt windows beside the app.

## Phases

1. **Python in the console and script files** (this note's first part, built).
2. **Script-made interface**: actions (menu items, shelf buttons), and panels of labels, buttons, sliders and check
   boxes, declared by a script and drawn by SculptForge's own interface.
3. **An event-loop hook for other toolkits** (PySide windows beside the app): a timer on the main thread. Needs its
   own security review (below) before it ships.

## P1: how it is built

**Two libraries.** The app (`sculptforge`) never links Python. `sculptforge_pyhost` (src/python/AppHost.cpp) holds
CPython (pybind11's embed), the `sculptforge` module (src/python/Bindings.cpp, the same code as the pipeline module)
and its own static copy of the core. The app loads it the first time a script runs (app/Python.cpp): a session that
never runs Python never maps libpython, so start-up and frames cost nothing. It is never unloaded (CPython can't be).

**The line between them** (python/PyHost.h): one C entry point, `sf_pyhost_create`, given an `AppSide` and returning a
`Host`. C++ types cross it (std::function, Document&): both halves are built from one source tree by one compiler in
one build, and the host refuses an `AppSide` from another build (version, ABI number, sizeof(Document), sizeof(Mesh)).
It is not a plugin interface and never will be: plugins are WebAssembly (core/Plugin.h).

**One core state.** The host's copy of the core has its own globals (undo sequence numbers, the undo batch, AI
settings). So everything that changes the open scene goes back through the app: commands run through
`App::runCommand` (they echo in the console, refresh the interface, record in macros and land in undo), attribute
assignments record their undo step in the app, `set_mesh` and selection sets likewise. The host's core runs only on
`sf.Document()` scenes a script makes for itself. On Linux the host's core symbols are local (`--exclude-libs`), so the
two copies never resolve to each other.

**Threads.** Python runs on the main thread, inside the frame, holding the GIL only while it runs; between runs a
script's own threads may run, but any change to the open scene from them raises an error (`mainThreadOnly`). print()
from any thread is queued and shown on the next frame.

**Undo.** Each console line or script runs in an `UndoBatchScope`: every step it pushes, on any Part, carries one batch
id, and Undo takes them back together. While it runs the app's prompts (memory, subdivision levels) and background
commands are off, as for a macro: a script reads what each command did on its next line.

**The interpreter.** Isolated (`PyConfig_InitIsolatedConfig`): no PYTHON* variables, no user site-packages, no
current folder on sys.path, no signal handlers, no .pyc writes. sys.path gets the Scripts folder and its site-packages.
stdin is empty (input() ends at once instead of hanging the window); SystemExit is caught. The console is
`code.InteractiveConsole`, the prompt Python's own REPL uses.

**The runtime.** Packages ship python-build-standalone's CPython 3.13 (pinned by hash, tools/ci/python-runtime.sh),
without its tests, Tk, IDLE, ensurepip and headers: lib/sculptforge/python (Linux), bin\python plus the DLLs next to
the exe (Windows, loaded with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | APPLICATION_DIR | SYSTEM32), and on macOS a universal
build (lipo of the arm64 and x86_64 trees) in Resources/python with the host in Frameworks, each library signed on
its own. Source builds and distribution packages use the Python they were built with. `sculptforge
--python-selftest` starts it from the installed files; every package job runs it.

**No NumPy shipped.** The module returns lists when NumPy can't be imported (Bindings.cpp `rowsOut`/`rowsIn`); a user
adds NumPy to Scripts/site-packages. For meshes of millions of vertices NumPy is the way; a zero-copy buffer view is a
possible later step.

## Security (all phases)

A Python script is trusted local code, like any program the user runs: it can read and write files, reach the
network and start programs. SculptForge doesn't pretend to sandbox it; plugins are the sandboxed way to share tools.
What SculptForge guarantees:

- **Off by default.** Preferences > Scripts > Enable Python scripts, stored in python.json in the settings folder,
  not ui.json: no layout, preset or project can turn it on. A studio policy (`"python": {"scripts": "never"}`) locks it
  off; a broken policy fails closed (off).
- **Never auto-run.** A script runs only from the console, a click in the Scripts list or Run script file. No file
  format carries code that runs (projects, meshes, packs, layouts, macros, presets); there is no `python` command, so
  macros, `--run`, sculptforge-cli and AI tools (MCP, live mode) can't reach Python. tests/ui/python_console.sfui opens
  a project and files holding Python text and checks the host was never loaded.
- **No ports, no network from the app** for scripting. The host opens no socket and adds no IPC.

## P2: script-made interface (plan)

```python
import sculptforge as sf

def clean():
    sf.run("remesh", resolution=sf.ui.value("cleanup.resolution"))

sf.ui.action("cleanup.run", "Clean up", clean, icon="sparkles", help="Remesh at the panel's resolution")
p = sf.ui.panel("cleanup", "Clean up")
p.label("Remesh the active Part")
p.slider("resolution", "Resolution", 200, 50, 800, integer=True)
p.checkbox("keep", "Keep a copy", False)
p.button("cleanup.run")
```

- An action becomes an `Action` (`script.<script>.<id>`): the command palette finds it, it can go on any shelf, in a
  pie menu and in My Panel (Alt-drag), and it can have a key. Running it is one undo step, like a script.
- A panel is a section of the Script palette (`script.<id>`), so My Panel can hold it as it holds any section.
  Controls are drawn by our widgets from a list the host hands over; values live in the app (no Python per frame),
  and a change calls the script's callback once (a slider: when released).
- Script UI lives as long as the session. To have it at every start, the user ticks **Run at start-up** for that
  script in Preferences > Scripts (an explicit opt-in per file, remembered in python.json by path and content hash: a
  script that changed asks again). Nothing else runs at start-up.
- Floor tier: nothing per frame unless a script panel is on screen; a script action costs what any action costs.

## P3: other toolkits (plan, security review below)

`sf.ui.timer(callback, ms)` calls `callback` on the main thread, between frames, at most every `ms` (the app keeps a
next-due time; while no timer is registered nothing runs). A PySide script calls `app.processEvents()` from it, so
its windows stay alive beside SculptForge without a second thread touching the scene.

### Security review for P3 (to complete before P3 is READY)

- What runs: only callbacks a user-run script registered; they stop when the script's timers are cleared or the
  app quits. No timer survives a restart unless the script itself is a Run at start-up script.
- Attack surface: SculptForge adds none (no listener, port, IPC or file it watches). Qt itself may open what the
  script asks for; that is the script's code, under the same trusted-code rule.
- Failure modes: a callback that raises is turned off with a message (it doesn't fire again); one that takes too
  long blocks the frame like any script; Qt's own event loop is never entered with exec() from the main thread
  (documented: processEvents only).
- Review: second opinions on the diff and the reviewer's FULL lane.
