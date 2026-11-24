# src/python

The C++ side of `import sculptforge`. One API, two ways to run it:

| Target | Built when | What it is |
|---|---|---|
| `sculptforge_python` (`module.cpp`, `Bindings.cpp`, `CoreCalls.cpp`) | `-DSCULPTFORGE_BUILD_PYTHON=ON` | The compiled `sculptforge._core` for pipelines, wheels and `pip install sculptforge`. Runs commands itself, no window, no network. The package around it (type stubs, `Document` helpers) is [python/](../../python/README.md) |
| `sculptforge_pyhost` (`AppHost.cpp`, `Bindings.cpp`, `PyHost.h`) | `SCULPTFORGE_APP_PYTHON` (follows the option above), not on Android | CPython embedded for scripts inside the app. The app loads it the first time a script runs (`src/app/Python.cpp`), so a session that never runs Python never loads Python |

`Bindings.cpp` is the API shared by both. What differs is how a document is changed: the module runs commands
itself; in the app, edits go through the app so they echo in the console, refresh the interface and land in its undo
history. The host has no copy of the core. Every core function it calls goes through the table in `CoreCalls.h`, which
the app fills with its own, so there is one core per process; the host is linked with no undefined symbols allowed, so
a call that bypasses the table fails the build.

## Rules

- **Scripts are trusted local code** (like any program the user runs), so they are off until the user turns them on
  (Preferences > Scripts) and a studio policy can lock them off. No file, pack, macro, plugin or AI tool may start one.
- The host runs Python on the app's main thread, inside a frame, and only that thread may touch the open scene. The
  interpreter is isolated: no `PYTHON*` environment variables, no user site-packages, no current directory on
  `sys.path`. It opens no port.
- `PyHost.h` is the one line between the app and the host. Both halves are built from one source tree by one
  compiler, so C++ types cross it; the host refuses an app from another build. It is not a plugin interface (plugins
  are WebAssembly: [sdk/plugin](../../sdk/plugin/README.md)).
- None of the `CoreCalls` functions throw: a failure comes back as `false`, an error message or a failed command
  result, and the binding turns a failed command into `sculptforge.CommandError`.

Design and phases: [docs/design/python-scripting.md](../../docs/design/python-scripting.md). User guides:
[docs/guide/python.md](../../docs/guide/python.md) (in the app) and
[docs/guide/python-package.md](../../docs/guide/python-package.md) (the package).

## Test it

`tests/test_python.py` and the pytest suite in `tests/python` (`python_api` and `python_package` in ctest, when the
module is built with NumPy and pytest present), `tests/test_python_host.cpp` (the app's host, loaded as the app loads
it) and the `python_selftest` (`sculptforge --python-selftest`). See [tests/README.md](../../tests/README.md).
Example scripts: [examples/python](../../examples/python/README.md).
