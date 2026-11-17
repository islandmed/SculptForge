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
std::thread::id g_mainThread;

bool isApp(const Document& d) { return &d == g_app.document; }

// The open scene belongs to the app's main thread (it draws and sculpts it between runs).
void mainThreadOnly() {
    if (std::this_thread::get_id() != g_mainThread)
        throw std::runtime_error("the open scene can only be used from the main thread, not from a thread a script started");
}

std::shared_ptr<const python::Api> appApi() {
    auto api = std::make_shared<python::Api>();
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

def _run_file(path):
    try:
        runpy.run_path(path, run_name="__main__")
        return 0
    except SystemExit as e:
        return 0 if e.code in (None, 0) else 1
    except BaseException as e:
        tb = e.__traceback__
        while tb is not None and tb.tb_frame.f_code.co_filename != path:
            tb = tb.tb_next
        traceback.print_exception(type(e), e, tb if tb is not None else e.__traceback__)
        return 1

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
    std::vector<std::string> complete(const std::string& text) override {
        py::gil_scoped_acquire gil;
        try {
            return (*helpers_)["_complete"](text).cast<std::vector<std::string>>();
        } catch (const py::error_already_set&) {
            return {};
        }
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
    if (Py_IsInitialized()) return fail("Python is already running in this process");
    g_app = *app;
    g_mainThread = std::this_thread::get_id();

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
        PyEval_SaveThread(); // hold the GIL only while running (AppHost::call)
        return host;
    } catch (const std::exception& e) {
        return fail(std::string("Python started but the sculptforge module didn't: ") + e.what());
    }
}
