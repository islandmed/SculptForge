# Python scripting in the app

Asked for by Tpqz_ and chris_gardner on Discord (#ideas): Python in the regular app, not only the pipeline module;
scripts that add their own buttons and panels; their own Qt windows beside the app.

## Phases

1. **Python in the console and script files** (built).
2. **Script-made interface** (built): actions (menu items, shelf buttons), and panels of labels, buttons, sliders and check
   boxes, declared by a script and drawn by SculptForge's own interface.
3. **An event-loop hook for other toolkits** (PySide windows beside the app): a timer on the main thread. Needs its
   own security review (below) before it ships.

## P1: how it is built

**Two libraries.** The app (`sculptforge`) never links Python. `sculptforge_pyhost` (src/python/AppHost.cpp) holds
CPython (pybind11's embed) and the `sculptforge` module (src/python/Bindings.cpp, the same code as the pipeline
module), and no core: the core functions the module calls come from the app. The app loads it the first time a script runs (app/Python.cpp): a session that
never runs Python never maps libpython, so start-up and frames cost nothing. It is never unloaded (CPython can't be).

**The line between them** (python/PyHost.h): one C entry point, `sf_pyhost_create`, given an `AppSide` and returning a
`Host`. C++ types cross it (std::function, Document&): both halves are built from one source tree by one compiler in
one build, and the host refuses an `AppSide` from another build (version, ABI number, sizeof(Document), sizeof(Mesh)).
It is not a plugin interface and never will be: plugins are WebAssembly (core/Plugin.h).

**One core state.** The host links none of the core (its headers only). Every core function the API calls goes through a
table, `CoreCalls` (python/CoreCalls.h), that the app fills from its own core and hands over in `AppSide`; the host is
linked with no undefined symbols allowed, so a call that bypasses the table fails the build. One core per process
means one set of its globals (undo sequence numbers, the undo batch, AI settings) and a small host (the pipeline
module fills the same table from the core it links). On top of that, everything that changes the open scene goes
through the app: commands run through `App::runCommand` (they echo in the console, refresh the interface, record in
macros and land in undo), attribute assignments record their undo step in the app, `set_mesh` and selection sets
likewise. `sf.Document()` scenes a script makes for itself run the app's core commands directly, with no undo.

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
- **Only the shipped runtime in packages.** The host is found only beside the app (no setting or environment variable
  names another library), and a package starts only the CPython it ships beside the host. A fallback to the build
  Python's own home exists only in source builds against an installed Python (a developer's, AUR, brew): that
  machine's own Python. It is never compiled into a package (CMakeLists.txt: not with SCULPTFORGE_PYTHON_RUNTIME),
  `--python-selftest` prints it, and every package job fails unless it says "none" (input/CEO, 11-17). A package whose
  runtime is missing has no Python and says why in the status bar.

## P2: script-made interface

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
  script in Preferences > Scripts, the only auto-run path, kept tight (input/CEO condition, 11-17): opt-in per script;
  listed there with its off switch (ticked scripts gone from the folder stay listed); only files directly in the
  Scripts folder (canonical path check, at tick time and at start); remembered with the file's SHA-256 in python.json; a
  mismatch doesn't run, says why in the status bar and the console, and Preferences offers Trust this version (one
  click; it doesn't run it then); Python off or the policy's never: nothing runs. The file is read once: the bytes
  hashed are the bytes run (Host::runSource), so a change after the check can't run. tests/ui/python_startup.sfui.
- Floor tier: nothing per frame unless a script panel is on screen; a script action costs what any action costs.

## P3: other toolkits

`sf.ui.timer(callback, ms)` calls `callback` on the main thread, between frames, about every `ms` (at least 10 ms).
The host keeps the next due time in an atomic (steady clock); the main loop's wait ends at it
(`PythonScripting::timerWake`) and each loop pass runs the due callbacks (`tickTimers`), as one undo batch, then the
frame is drawn only if something changed (frameKey). With no timer set, or Python never loaded, both are a pointer
check: nothing per frame, no wake-ups. A PySide script calls `app.processEvents()` from it, so its windows stay alive
beside SculptForge, their signals arrive on the main thread, and no second thread touches the scene.

### Security review (P3)

The hook adds a way for code that is already running to keep running later. It adds no way for code to start.

- **What can run.** Only callbacks that a script the user ran registered (console, Scripts list, Run script file, a
  ticked Run at start-up script). No file, pack, layout, macro, project or AI tool registers a timer; timers live in
  memory and none survives a restart (a Run at start-up script registers its own again: its SHA-256 is checked first).
- **Attack surface.** SculptForge opens nothing for it: no listener, port, socket, pipe, IPC or watched file. A
  toolkit the script imports may open what the script asks it to (a Qt window talks to the window system; Qt's own
  networking only if the script uses it): that is the script's code, under the trusted-code rule the guide states.
- **Threads.** Callbacks run on the main thread holding the GIL; a toolkit's other threads can't change the scene
  (mainThreadOnly). Qt's QApplication made from a timer is on the main thread, as Qt requires.
- **Failure.** A callback that raises is stopped with its traceback (it can't spin errors every frame); one that
  blocks freezes the window like any long script (stated in the guide: never exec()). SystemExit in a callback is
  ignored. The interval floor (10 ms) keeps a careless 0 from spinning the CPU.
- **Policy.** `python.scripts: never` keeps the host from loading, so no timer can exist.
- **Reviewed by:** second opinions on the diff (second-opinion/outbox/python-app-p3-review) and the FULL review lane.
