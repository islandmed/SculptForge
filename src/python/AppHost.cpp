// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's Python host (the sculptforge_pyhost library, PyHost.h): CPython embedded, with the `sculptforge` module
// (Bindings.cpp) built in and pointed at the open scene. The app loads it only when a script first runs.
//
// Python runs on the app's main thread, inside a frame, holding the GIL only while it runs: between runs other
// Python threads a script started may run, but only the main thread may touch the open scene (the guard below).
// The interpreter is isolated: no PYTHON* environment variables, no user site-packages, no current directory on
// sys.path; the scripts folder and its site-packages are put there instead.
#include "python/Bindings.h"
#include "python/PyHost.h"

#include "core/Document.h"
#include "core/SelectionSets.h" // (types only: every core function comes from the app, CoreCalls.h)

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <cstring>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#if defined(_WIN32)
#define SF_PYHOST_EXPORT __declspec(dllexport)
#else
#define SF_PYHOST_EXPORT __attribute__((visibility("default")))
#endif

namespace py = pybind11;
using namespace sf;

namespace {

python::AppSide g_app;
std::atomic<uint64_t> g_uiRevision{0}; // bumped by sf.ui whenever the declared interface changes
std::atomic<int64_t> g_timerDue{-1};    // steady clock ns of the next sf.ui.timer callback, -1: none

int64_t steadyNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::thread::id g_mainThread;

bool isApp(const Document& d) { return &d == g_app.document; }

// The open scene belongs to the app's main thread (it draws and sculpts it between runs).
void mainThreadOnly() {
    if (std::this_thread::get_id() != g_mainThread)
        throw std::runtime_error("the open scene can only be used from the main thread, not from a thread a script started");
}

std::shared_ptr<python::Api> g_hostApi; // the module's Api (its open scene changes when another App takes the host)
bool g_started = false;                  // this library started the interpreter

std::shared_ptr<const python::Api> appApi() {
    auto api = g_hostApi = std::make_shared<python::Api>();
    api->core = g_app.core;
    api->execute = [](Document& doc, const std::string& name, const Args& args) {
        if (!isApp(doc)) return g_app.core->execute(doc, name, args); // a Document() of the script's own
        mainThreadOnly();
        return g_app.run(name, args);
    };
    api->registry = []() -> const CommandRegistry& { return *g_app.registry; };
    api->edit = [](Document& d, python::Edit what, const std::function<void()>& change) {
        if (!isApp(d)) return change();
        mainThreadOnly();
        g_app.edit(d, what, change);
    };
    api->replaceMesh = [](Document& d, Mesh&& m) {
        if (!isApp(d)) return g_app.core->setMesh(d, std::move(m));
        mainThreadOnly();
        g_app.replaceMesh(d, std::move(m));
    };
    api->addSelectionSet = [](Document& d, SelectionSet&& set, const std::string& label, std::string& msg) {
        if (!isApp(d)) return g_app.core->addSelectionSet(d, std::move(set), label, msg);
        mainThreadOnly();
        return g_app.addSelectionSet(d, std::move(set), label, msg);
    };
    api->selectPart = [](Document& d, int i) {
        if (!isApp(d)) return g_app.core->selectPart(d, i);
        mainThreadOnly();
        g_app.selectPart(d, i);
    };
    api->appDocument = g_app.document;
    api->releaseGil = false; // commands on the open scene run in the app, on this thread
    return api;
}

// print() and tracebacks go to the console.
struct Writer {
    bool error = false;
    void write(const std::string& text) const {
        if (g_app.write && !text.empty()) g_app.write(error, text);
    }
};

// The console, file runs and completion, in Python (code.InteractiveConsole is the standard prompt's own loop).
constexpr const char* kHostPy = R"PY(
import code, io, rlcompleter, runpy, sys, traceback
import sculptforge as sf

class _NoInput(io.TextIOBase):  # stdin: nobody can type into it, so input() ends at once (EOFError), even after exit()
    def readable(self): return True
    def read(self, size=-1): return ""
    def readline(self, size=-1): return ""
    def close(self): pass
sys.stdin = _NoInput()

class _Console(code.InteractiveConsole):
    failed = False
    def showtraceback(self):
        self.failed = True
        super().showtraceback()
    def showsyntaxerror(self, filename=None, **kw):
        self.failed = True
        super().showsyntaxerror(filename, **kw)

_ns = {"__name__": "__console__", "__builtins__": __builtins__, "sf": sf, "doc": sf.document()}
_console = _Console(_ns, filename="<console>")
_EXIT = "exit() and quit() don't close SculptForge (close the console with the console key)"

def _push(line):
    _console.failed = False
    try:
        more = _console.push(line)
    except SystemExit:
        _console.resetbuffer()
        print(_EXIT, file=sys.stderr)
        return 1
    return 2 if more else (1 if _console.failed else 0)

def _cancel():
    _console.resetbuffer()

def _script_failed(e, path):
    if isinstance(e, SystemExit):
        return 0 if e.code in (None, 0) else 1
    tb = e.__traceback__
    while tb is not None and tb.tb_frame.f_code.co_filename != path:
        tb = tb.tb_next
    traceback.print_exception(type(e), e, tb if tb is not None else e.__traceback__)
    return 1

def _run_file(path):
    try:
        runpy.run_path(path, run_name="__main__")
        return 0
    except BaseException as e:
        return _script_failed(e, path)

# Bytes the app already read and checked, run as runpy runs a file: a fresh __main__ module, sys.argv[0] = path.
def _run_source(source, path):
    import sys, types
    main, argv0 = sys.modules.get("__main__"), sys.argv[0] if sys.argv else None
    try:
        code = compile(source, path, "exec", dont_inherit=True)
        mod = types.ModuleType("__main__")
        mod.__dict__.update(__file__=path, __cached__=None, __loader__=None, __package__=None, __spec__=None)
        sys.modules["__main__"] = mod
        if sys.argv:
            sys.argv[0] = path
        exec(code, mod.__dict__)
        return 0
    except BaseException as e:
        return _script_failed(e, path)
    finally:
        if main is not None:
            sys.modules["__main__"] = main
        if argv0 is not None:
            sys.argv[0] = argv0

# sf.ui (P2): the interface a script declares, drawn by SculptForge (app/PanelPython.cpp). Ids are lower-case words
# with dots; an action's app id is "script.py.<id>". Declaring again replaces (a panel keeps the values its
# controls had, so running a script again doesn't reset what the user set).
import re as _re
_ID = _re.compile(r"^[a-z0-9_][a-z0-9_.-]{0,63}$")
_ICONS = ("play", "sparkles", "wand", "box", "brush", "scissors", "layers", "download", "upload", "star", "file-code",
          "settings", "eye", "camera", "grid", "move", "rotate", "trash")

def _check_id(what, id):
    if not isinstance(id, str) or not _ID.match(id):
        raise ValueError(f"{what} id {id!r}: use lower-case letters, digits, '_', '-' and '.', at most 64")
    return id

def _call(fn, *args):
    try:
        fn(*args)
        return 0
    except SystemExit:
        return 0
    except BaseException:
        traceback.print_exc()
        return 1

class Panel:
    """A section of controls in File > Script (and in My Panel, if you put it there). Made by sf.ui.panel()."""
    def __init__(self, ui, id, title, old):
        self.id, self.title, self._ui, self._controls = id, title, ui, []
        self._old = {c["id"]: c["value"] for c in old._controls if c.get("id")} if old else {}
    def _add(self, c):
        if c.get("id") is not None:
            _check_id("control", c["id"])
            if any(x.get("id") == c["id"] for x in self._controls):
                raise ValueError(f"panel {self.id!r} already has a control {c['id']!r}")
        self._controls.append(c)
        sf._ui_touch()
        return self
    def label(self, text):
        """A line of text."""
        return self._add({"kind": 0, "label": str(text)})
    def separator(self):
        """A thin line between groups of controls."""
        return self._add({"kind": 1})
    def button(self, action=None, *, label=None, callback=None, help=""):
        """A button for an action: button("cleanup.run"), or button(label="Go", callback=go) for one of its own."""
        if callback is not None:
            if label is None:
                raise ValueError("a button with a callback needs a label")
            action = self.id + "." + (_re.sub(r"[^a-z0-9_]+", "_", str(label).lower()).strip("_") or "button")
            self._ui.action(action, label, callback, help=help)
        if action not in self._ui._actions:
            raise KeyError(f"no action {action!r}: declare it with sf.ui.action() first")
        return self._add({"kind": 2, "action": action, "label": label or self._ui._actions[action]["label"], "help": help})
    def slider(self, id, label, value, min, max, *, integer=False, on_change=None, help=""):
        """A slider from min to max. on_change(value) runs when it is let go."""
        if not min < max:
            raise ValueError("a slider needs min < max")
        v = self._old.get(id, value)
        v = float(round(v) if integer else v)
        return self._add({"kind": 3, "id": id, "label": str(label), "value": v, "min": float(min), "max": float(max),
                          "integer": bool(integer), "on_change": on_change, "help": help})
    def checkbox(self, id, label, value=False, *, on_change=None, help=""):
        """A check box. on_change(value) runs when it is clicked."""
        v = bool(self._old.get(id, value))
        return self._add({"kind": 4, "id": id, "label": str(label), "value": v, "on_change": on_change, "help": help})
    def value(self, id):
        """A control's current value (a float for sliders, int if integer=True; a bool for check boxes)."""
        for c in self._controls:
            if c.get("id") == id:
                return int(c["value"]) if c.get("integer") else c["value"]
        raise KeyError(f"panel {self.id!r} has no control {id!r}")
    __getitem__ = value

class _UI:
    """sf.ui: add actions (buttons, menu items, keys) and panels to SculptForge's interface."""
    def __init__(self):
        self._actions, self._panels = {}, {}
    def action(self, id, label, callback, *, icon=None, help=""):
        """An action: listed in File > Script and the command palette (F3); it can go on a shelf, in My Panel or on
        a key. Running it calls callback() as one undo step."""
        _check_id("action", id)
        if not callable(callback):
            raise TypeError("callback must be callable")
        if icon is not None and icon not in _ICONS:
            raise ValueError(f"icon {icon!r}: one of {', '.join(_ICONS)}")
        self._actions[id] = {"id": id, "label": str(label), "help": str(help), "icon": icon or "", "callback": callback}
        sf._ui_touch()
        return id
    def panel(self, id, title):
        """A panel (an empty one, replacing any panel with this id); add controls to what it returns."""
        _check_id("panel", id)
        p = Panel(self, id, str(title), self._panels.get(id))
        self._panels[id] = p
        sf._ui_touch()
        return p
    def value(self, path):
        """A control's value by "panel.control"."""
        panel, _, control = path.rpartition(".")
        if panel not in self._panels:
            raise KeyError(f"no panel {panel!r}")
        return self._panels[panel].value(control)
    def place(self, id, where="top"):
        """Puts an action (or a panel, by its id) on a shelf ("top", "bottom", "left", "right") or in "my_panel",
        unless it is there already. Your layout keeps it."""
        places = {"top": 0, "bottom": 1, "left": 2, "right": 3, "my_panel": 4}
        if where not in places:
            raise ValueError(f"where: one of {', '.join(places)}")
        panel = id in self._panels
        if not panel and id not in self._actions:
            raise KeyError(f"no action or panel {id!r}")
        if not sf._place(id, panel, places[where]):
            raise ValueError(f"{id!r} can't go in {where}" + (" (only actions go on shelves)" if panel else ""))
    def timer(self, callback, ms=30):
        """Calls callback() on SculptForge's main thread about every ms milliseconds (at least 10), between frames,
        until cancel_timer(). For other toolkits' windows: sf.ui.timer(qt_app.processEvents). A callback that fails
        is stopped. Returns the timer's id."""
        if not callable(callback):
            raise TypeError("callback must be callable")
        global _timer_id
        _timer_id += 1
        interval = max(10, int(ms)) / 1000.0
        _timers[_timer_id] = [callback, interval, _time.monotonic() + interval]
        _schedule()
        return _timer_id
    def cancel_timer(self, id):
        """Stops a timer."""
        _timers.pop(id, None)
        _schedule()
    def remove(self, id):
        """Removes an action or a panel."""
        if self._actions.pop(id, None) is None and self._panels.pop(id, None) is None:
            raise KeyError(id)
        sf._ui_touch()
    def clear(self):
        """Removes every action, panel and timer scripts declared."""
        self._actions.clear(), self._panels.clear(), _timers.clear()
        _schedule()
        sf._ui_touch()

import time as _time
_timers, _timer_id = {}, 0  # id -> [callback, interval (s), due (time.monotonic)]

def _schedule():
    sf._timer_next(min(t[2] for t in _timers.values()) - _time.monotonic() if _timers else -1.0)

def _run_timers():
    now, failed = _time.monotonic(), 0
    for tid, t in list(_timers.items()):
        if t[2] <= now and tid in _timers:
            t[2] = now + t[1]
            if _call(t[0]) != 0:
                _timers.pop(tid, None)
                print(f"timer {tid} stopped: its callback failed", file=sys.stderr)
                failed = 1
    _schedule()
    return failed

sf.ui = _UI()

def _ui_state():
    acts = [(a["id"], a["label"], a["help"], a["icon"]) for a in sf.ui._actions.values()]
    panels = []
    for p in sf.ui._panels.values():
        cs = [(c["kind"], c.get("id") or "", c.get("label") or "", c.get("help") or "", c.get("action") or "",
               float(c.get("value", 0.0)), float(c.get("min", 0.0)), float(c.get("max", 1.0)), bool(c.get("integer")))
              for c in p._controls]
        panels.append((p.id, p.title, cs))
    return acts, panels

def _run_action(id):
    a = sf.ui._actions.get(id)
    if a is None:
        print(f"no action {id!r} (run the script that declares it again)", file=sys.stderr)
        return 1
    return _call(a["callback"])

def _set_value(panel, control, value):
    p = sf.ui._panels.get(panel)
    c = next((c for c in p._controls if c.get("id") == control), None) if p else None
    if c is None:
        return 1
    c["value"] = bool(value) if c["kind"] == 4 else float(round(value) if c.get("integer") else value)
    if c.get("on_change"):
        return _call(c["on_change"], p.value(control))
    return 0

def _complete(text):
    c, out = rlcompleter.Completer(_ns), []
    while len(out) < 200:
        m = c.complete(text, len(out))
        if m is None:
            break
        out.append(m)
    return out
)PY";

class AppHost final : public python::Host {
public:
    AppHost() {
        py::gil_scoped_acquire gil;
        helpers_ = std::make_unique<py::dict>();
        (*helpers_)["__builtins__"] = py::module_::import("builtins");
        py::exec(kHostPy, *helpers_);
    }
    ~AppHost() override {
        py::gil_scoped_acquire gil;
        helpers_.reset();
    }

    Status runLine(const std::string& line) override { return call("_push", line); }
    void cancelLine() override { call("_cancel"); }
    Status runFile(const std::string& path) override { return call("_run_file", path); }
    Status runSource(const std::string& source, const std::string& path) override {
        py::gil_scoped_acquire gil; // (py::bytes is made with the GIL held; call() takes it again, recursively)
        return call("_run_source", py::bytes(source), path);
    }
    std::vector<std::string> complete(const std::string& text) override {
        py::gil_scoped_acquire gil;
        try {
            return (*helpers_)["_complete"](text).cast<std::vector<std::string>>();
        } catch (const py::error_already_set&) {
            return {};
        }
    }
    uint64_t uiRevision() const override { return g_uiRevision.load(); }
    python::UiState ui() override {
        python::UiState out;
        py::gil_scoped_acquire gil;
        try {
            const py::tuple state = (*helpers_)["_ui_state"]();
            for (const auto& a : state[0].cast<py::list>()) {
                const auto t = a.cast<py::tuple>();
                out.actions.push_back({t[0].cast<std::string>(), t[1].cast<std::string>(), t[2].cast<std::string>(), t[3].cast<std::string>()});
            }
            for (const auto& p : state[1].cast<py::list>()) {
                const auto t = p.cast<py::tuple>();
                python::UiPanel panel{t[0].cast<std::string>(), t[1].cast<std::string>(), {}};
                for (const auto& c : t[2].cast<py::list>()) {
                    const auto f = c.cast<py::tuple>();
                    python::UiControl ctl;
                    ctl.kind = python::UiControl::Kind(f[0].cast<int>());
                    ctl.id = f[1].cast<std::string>(), ctl.label = f[2].cast<std::string>(), ctl.help = f[3].cast<std::string>();
                    ctl.action = f[4].cast<std::string>();
                    ctl.value = f[5].cast<double>(), ctl.min = f[6].cast<double>(), ctl.max = f[7].cast<double>();
                    ctl.integer = f[8].cast<bool>();
                    panel.controls.push_back(std::move(ctl));
                }
                out.panels.push_back(std::move(panel));
            }
        } catch (const std::exception& e) {
            Writer{true}.write(std::string("sf.ui: ") + e.what() + "\n");
        }
        return out;
    }
    Status runAction(const std::string& id) override { return call("_run_action", id); }
    bool timerDue(double& inSeconds) const override {
        const int64_t due = g_timerDue.load();
        if (due < 0) return false;
        inSeconds = double(due - steadyNs()) * 1e-9;
        return true;
    }
    Status runTimers() override { return call("_run_timers"); }
    Status setValue(const std::string& panel, const std::string& control, double value) override {
        return call("_set_value", panel, control, value);
    }
    std::string version() const override {
        py::gil_scoped_acquire gil;
        return py::module_::import("platform").attr("python_version")().cast<std::string>();
    }

private:
    template <class... A>
    Status call(const char* fn, A&&... args) {
        py::gil_scoped_acquire gil;
        try {
            const py::object r = (*helpers_)[fn](std::forward<A>(args)...);
            const int code = r.is_none() ? 0 : r.cast<int>();
            return code == 2 ? Status::More : code == 1 ? Status::Error : Status::Ok;
        } catch (py::error_already_set& e) { // (the helpers catch everything; this is a KeyboardInterrupt or worse)
            Writer{true}.write(std::string(e.what()) + "\n");
            return Status::Error;
        }
    }
    std::unique_ptr<py::dict> helpers_; // (destroyed with the GIL held)
};

} // namespace

PYBIND11_EMBEDDED_MODULE(sculptforge, m) {
    python::bind(m, appApi(), "SculptForge: Python scripting in the app (the open scene is sf.document())");
    py::class_<Writer>(m, "_Writer").def("write", &Writer::write).def("flush", [](const Writer&) {});
    m.def("scripts_folder", []() -> py::object {
        if (g_app.paths.empty()) return py::none();
        return py::str(g_app.paths[0]);
    }, "The Scripts folder (its .py files are listed under File > Script), or None when there is none.");
    m.def("_ui_touch", [] { ++g_uiRevision; });
    m.def("_timer_next", [](double seconds) { g_timerDue = seconds < 0.0 ? -1 : steadyNs() + int64_t(seconds * 1e9); });
    m.def("_place", [](const std::string& id, bool panel, int where) {
        mainThreadOnly();
        return g_app.place && g_app.place(id, panel, python::Place(where));
    });
}

// The Python home this host falls back to when no runtime sits beside it: "" in packages (--python-selftest prints it
// and the package jobs check it is empty).
extern "C" SF_PYHOST_EXPORT const char* sf_pyhost_fallback_home() {
#if defined(SF_PYTHON_BUILD_HOME)
    return SF_PYTHON_BUILD_HOME;
#else
    return "";
#endif
}

extern "C" SF_PYHOST_EXPORT python::Host* sf_pyhost_create(int abi, const python::AppSide* app, char* error, int errorSize) {
    const auto fail = [&](const std::string& why) -> python::Host* {
        if (error && errorSize > 0) {
            std::strncpy(error, why.c_str(), size_t(errorSize) - 1);
            error[errorSize - 1] = 0;
        }
        return nullptr;
    };
    if (abi != python::kHostAbi || !app || app->build != SCULPTFORGE_VERSION || app->documentSize != sizeof(Document) ||
        app->meshSize != sizeof(Mesh) || !app->document || !app->registry || !app->run || !app->core)
        return fail("this Python host belongs to another SculptForge build (reinstall SculptForge)");
    if (Py_IsInitialized() && !g_started) return fail("Python is already running in this process");
    g_app = *app;
    g_mainThread = std::this_thread::get_id();
    if (g_started) {
        // A new App in the same process (the warm UI test server makes one per script): CPython starts only once
        // per process, so the running one is pointed at the new scene, with a fresh console, sf.ui and no timers.
        if (g_hostApi) g_hostApi->appDocument = g_app.document;
        g_timerDue = -1;
        ++g_uiRevision;
        try {
            py::gil_scoped_acquire gil;
            py::list path = py::module_::import("sys").attr("path");
            for (auto it = g_app.paths.rbegin(); it != g_app.paths.rend(); ++it) path.insert(0, *it);
        } catch (const std::exception& e) {
            return fail(std::string("Python's path: ") + e.what());
        }
        return new AppHost(); // (the previous App's host is gone with its App; never freed: tiny)
    }

#if !defined(_WIN32)
    // The app loaded this library with its symbols local; Python's compiled modules (math, _ssl...) look for
    // libpython's in the global scope. Reopen libpython as global (it is loaded already: NOLOAD).
    if (Dl_info info{}; dladdr(reinterpret_cast<void*>(&Py_IsInitialized), &info) && info.dli_fname)
        dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
#endif
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.install_signal_handlers = 0; // Ctrl+C and the rest stay the app's
    config.write_bytecode = 0;          // no __pycache__ folders in the scripts folder or the app's install
    PyStatus status = PyStatus_Ok();
    std::string home = g_app.home;
#if defined(SF_PYTHON_BUILD_HOME)
    // No runtime shipped with this build (a source or distribution build against an installed Python): that Python.
    // Never compiled into a package that ships a runtime (CMakeLists.txt): there, no runtime beside the host = no Python.
    if (home.empty()) home = SF_PYTHON_BUILD_HOME;
#endif
    if (!home.empty()) status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        return fail("Python's home folder: " + std::string(status.err_msg ? status.err_msg : "invalid"));
    }
    try {
        py::initialize_interpreter(&config, 0, nullptr, false); // (clears config)
    } catch (const std::exception& e) {
        return fail(std::string("Python didn't start: ") + e.what());
    }
    try {
        py::module_ sys = py::module_::import("sys");
        py::list path = sys.attr("path");
        for (auto it = g_app.paths.rbegin(); it != g_app.paths.rend(); ++it) path.insert(0, *it);
        py::module_ sf = py::module_::import("sculptforge");
        sys.attr("stdout") = py::cast(Writer{false});
        sys.attr("stderr") = py::cast(Writer{true});
        sys.attr("argv") = py::list(py::make_tuple("")); // (scripts that read sys.argv[0])
        auto* host = new AppHost();
        g_started = true;
        PyEval_SaveThread(); // hold the GIL only while running (AppHost::call)
        return host;
    } catch (const std::exception& e) {
        return fail(std::string("Python started but the sculptforge module didn't: ") + e.what());
    }
}
