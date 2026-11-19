// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python scripting in the app (python/PyHost.h): the real sculptforge_pyhost library, loaded the way the app loads
// it, against a scene of the test's own. The console's lines and blocks, script files, errors that don't take the
// app down (exceptions, exit(), input()), commands going through the app's side, undoable edits, the main-thread
// guard and completion.
#include "TestHarness.h"

#if defined(SF_PYHOST_PATH)

#include "core/Commands.h"
#include "core/Document.h"
#include "core/SelectionSets.h"
#include "core/Undo.h"
#include "python/CoreCalls.h"
#include "python/PyHost.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

// The runtime a package build stages beside the host (its build folder has the install's layout), found the way the
// app finds it (app/Python.cpp): "" when there is none and the host falls back to the Python it was built with.
std::string runtimeHome() {
    namespace fs = std::filesystem;
    const fs::path host(SF_PYHOST_PATH);
    for (const fs::path& home : {host.parent_path() / "python", host.parent_path() / ".." / "Resources" / "python"}) {
        std::error_code ec;
        if (fs::is_directory(home, ec)) return home.lexically_normal().string();
    }
    return {};
}

// Commands on documents a script makes of its own, run by this binary's core (the app's, in the app).
int g_ownRuns = 0;
const python::CoreCalls& countingCore() {
    static const python::CoreCalls calls = [] {
        python::CoreCalls c = python::directCoreCalls();
        c.execute = [](Document& d, const std::string& name, const Args& args) {
            ++g_ownRuns;
            return python::directCoreCalls().execute(d, name, args);
        };
        return c;
    }();
    return calls;
}

struct Fixture {
    Document doc;
    CommandRegistry registry;
    python::Host* host = nullptr;
    std::string out, err; // what Python printed, by stream
    int appRuns = 0;      // commands that went through the app's side
    std::string loadError;
    std::vector<std::string> placed; // sf.ui.place calls
    uint64_t windowHandle = 0;       // what the app says its main window is (0: none, as offscreen)

    Fixture() {
        SDL_SharedObject* lib = SDL_LoadObject(SF_PYHOST_PATH);
        auto create = lib ? reinterpret_cast<SfPyHostCreate>(SDL_LoadFunction(lib, "sf_pyhost_create")) : nullptr;
        if (!create) {
            loadError = SDL_GetError();
            return;
        }
        python::AppSide side;
        side.build = SF_PYHOST_BUILD;
        side.core = &countingCore();
        side.documentSize = sizeof(Document);
        side.meshSize = sizeof(Mesh);
        side.home = runtimeHome();
        side.document = &doc;
        side.registry = &registry;
        side.run = [this](const std::string& name, const Args& args) {
            ++appRuns;
            return registry.execute(doc, name, args);
        };
        side.edit = [](Document& d, python::Edit what, const std::function<void()>& change) {
            UndoRecord r = what == python::Edit::Mask ? UndoRecord::captureMask(d.mesh, "Python: mask")
                                                      : UndoRecord::captureFaceGroups(d.mesh, "Python: face groups");
            change();
            d.undo.push(std::move(r));
        };
        side.replaceMesh = [](Document& d, Mesh&& m) { d.replaceMeshUndoable(std::move(m), "Python: set mesh"); };
        side.addSelectionSet = [](Document& d, SelectionSet&& s, const std::string& label, std::string& msg) {
            return addSelectionSet(d, std::move(s), label, msg);
        };
        side.selectPart = [](Document& d, int i) { d.selectPart(i); };
        side.write = [this](bool error, const std::string& text) { (error ? err : out) += text; };
        side.place = [this](const std::string& id, bool panel, python::Place where) {
            placed.push_back((panel ? "panel:" : "action:") + id + "@" + std::to_string(int(where)));
            return !(panel && where != python::Place::MyPanel);
        };
        side.windowHandle = [this] { return windowHandle; };
        char error[512] = "";
        host = create(python::kHostAbi, &side, error, int(sizeof error));
        if (!host) loadError = error;
    }
    python::Host::Status line(const std::string& l) { return host->runLine(l); }
    void clear() { out.clear(), err.clear(); }
};

Fixture& py() {
    static Fixture f;
    return f;
}

bool contains(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }

using S = python::Host::Status;

} // namespace

// A host from another build is refused, before Python starts.
TEST(python_host_refuses_another_build) {
    SDL_SharedObject* lib = SDL_LoadObject(SF_PYHOST_PATH);
    auto create = lib ? reinterpret_cast<SfPyHostCreate>(SDL_LoadFunction(lib, "sf_pyhost_create")) : nullptr;
    CHECK(create != nullptr);
    if (!create) return;
    Document doc;
    CommandRegistry registry;
    python::AppSide side;
    side.build = "0.0.0";
    side.core = &python::directCoreCalls();
    side.documentSize = sizeof(Document);
    side.meshSize = sizeof(Mesh);
    side.document = &doc;
    side.registry = &registry;
    side.run = [](const std::string&, const Args&) { return CommandResult{true, ""}; };
    char error[512] = "";
    CHECK(create(python::kHostAbi, &side, error, int(sizeof error)) == nullptr);
    CHECK(std::string(error).find("another SculptForge build") != std::string::npos);
    CHECK(create(python::kHostAbi + 1, &side, error, int(sizeof error)) == nullptr);
}

TEST(python_host_loads) {
    if (!py().host) std::fprintf(stderr, "python host: %s\n", py().loadError.c_str());
    CHECK(py().host != nullptr);
    if (!py().host) return;
    CHECK(py().host->version().rfind("3.", 0) == 0);
}

TEST(python_host_console_runs_against_the_open_scene) {
    Fixture& f = py();
    if (!f.host) return;
    f.clear();
    const int before = f.appRuns;
    CHECK(f.line("sf.run('new_sphere', subdivisions=2)") == S::Ok); // the open scene, without naming it
    CHECK(f.appRuns == before + 1);
    CHECK(f.doc.mesh.triangleCount() == 320);
    CHECK(f.line("print(len(doc.vertices), doc is sf.document())") == S::Ok);
    CHECK(contains(f.out, "162 True"));
    // A block waits for its end (an empty line), like Python's own prompt; leading spaces are its indentation.
    CHECK(f.line("for i in range(2):") == S::More);
    CHECK(f.line("    sf.run(doc, 'subdivide')") == S::More);
    CHECK(f.line("") == S::Ok);
    CHECK(f.doc.mesh.triangleCount() == 320 * 16);
    // Names live on between lines.
    CHECK(f.line("n = len(doc.triangles)") == S::Ok);
    f.clear();
    CHECK(f.line("n") == S::Ok);
    CHECK(contains(f.out, "5120"));
    // A Document of the script's own runs its commands itself, not through the app.
    const int runs = f.appRuns;
    CHECK(f.line("d = sf.Document(); sf.run(d, 'new_sphere', subdivisions=1); print(len(d.triangles))") == S::Ok);
    CHECK(f.appRuns == runs);
    CHECK(f.doc.mesh.triangleCount() == 5120);
}

TEST(python_host_errors_never_take_the_app_down) {
    Fixture& f = py();
    if (!f.host) return;
    f.clear();
    CHECK(f.line("1/0") == S::Error);
    CHECK(contains(f.err, "ZeroDivisionError"));
    f.clear();
    CHECK(f.line("sf.run('no_such_command')") == S::Error);
    CHECK(contains(f.err, "CommandError"));
    f.clear();
    CHECK(f.line("exit()") == S::Error); // SystemExit: the app stays
    CHECK(contains(f.err, "don't close SculptForge"));
    f.clear();
    CHECK(f.line("input('name? ')") == S::Error); // nobody can type into stdin: it ends at once
    if (!contains(f.err, "EOFError")) std::fprintf(stderr, "input(): out [%s] err [%s]\n", f.out.c_str(), f.err.c_str());
    CHECK(contains(f.err, "EOFError"));
    f.clear();
    CHECK(f.line("def broken(:") == S::Error);
    CHECK(contains(f.err, "SyntaxError"));
    CHECK(f.line("if True:") == S::More); // a block typed so far can be dropped
    f.host->cancelLine();
    CHECK(f.line("print('after')") == S::Ok);
    CHECK(contains(f.out, "after"));
}

TEST(python_host_runs_script_files) {
    Fixture& f = py();
    if (!f.host) return;
    const auto dir = std::filesystem::temp_directory_path() / "sf_pyhost_test";
    std::filesystem::create_directories(dir);
    const std::string good = (dir / "good.py").string(), bad = (dir / "bad.py").string();
    std::ofstream(good) << "import sculptforge as sf\nsf.run('new_sphere', subdivisions=1)\nprint('ran', __name__)\n";
    std::ofstream(bad) << "x = 1\n\nraise ValueError('line three')\n";
    f.clear();
    CHECK(f.host->runFile(good) == S::Ok);
    CHECK(contains(f.out, "ran __main__"));
    CHECK(f.doc.mesh.triangleCount() == 80);
    f.clear();
    CHECK(f.host->runFile(bad) == S::Error);
    CHECK(contains(f.err, "line 3") && contains(f.err, "ValueError: line three"));
    CHECK(!contains(f.err, "runpy")); // the traceback starts at the script
    std::filesystem::remove_all(dir);
}

// Run at start-up runs the bytes it hashed, not the file as it is by then (app/Python.cpp runStartupScripts).
TEST(python_host_runs_the_source_it_was_given) {
    Fixture& f = py();
    if (!f.host) return;
    const auto dir = std::filesystem::temp_directory_path() / "sf_pyhost_source";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "start.py").string();
    std::ofstream(path) << "raise SystemExit('the file on disk ran')\n";
    f.clear();
    CHECK(f.host->runSource("# -*- coding: utf-8 -*-\nprint('given', __name__, __file__ == " + std::string("r'") + path +
                                "')\nimport sys; print('argv', sys.argv[0] == __file__)\n",
                            path) == S::Ok);
    CHECK(contains(f.out, "given __main__ True") && contains(f.out, "argv True"));
    CHECK(!contains(f.err, "the file on disk ran"));
    f.clear();
    CHECK(f.host->runSource("x = 1\nraise ValueError('from the bytes')\n", path) == S::Error);
    CHECK(contains(f.err, "line 2") && contains(f.err, "ValueError: from the bytes"));
    CHECK(f.host->runSource("print('after')\n", path) == S::Ok); // __main__ and argv put back
    CHECK(contains(f.out, "after"));
    std::filesystem::remove_all(dir);
}

TEST(python_host_edits_are_undoable) {
    Fixture& f = py();
    if (!f.host) return;
    f.registry.execute(f.doc, "new_sphere", {{"subdivisions", 2.0}});
    CHECK(f.doc.mesh.mask.empty());
    CHECK(f.line("doc.mask = [1.0] * len(doc.vertices)") == S::Ok);
    CHECK(f.doc.mesh.mask.size() == f.doc.mesh.vertexCount() && f.doc.mesh.mask[0] == 1.0f);
    CHECK(f.registry.execute(f.doc, "undo").ok);
    CHECK(f.doc.mesh.mask.empty() || f.doc.mesh.mask[0] == 0.0f);
    CHECK(f.line("doc.mask = [0.5, 0.5]") == S::Error); // the wrong length: a ValueError, nothing changed
    CHECK(f.doc.mesh.mask.empty() || f.doc.mesh.mask[0] == 0.0f);
}

TEST(python_host_threads_cannot_touch_the_open_scene) {
    Fixture& f = py();
    if (!f.host) return;
    f.clear();
    const int runs = f.appRuns;
    CHECK(f.line("import threading; errs = []") == S::Ok);
    CHECK(f.line("def job():") == S::More);
    CHECK(f.line("    try: sf.run('subdivide')") == S::More);
    CHECK(f.line("    except Exception as e: errs.append(str(e))") == S::More);
    CHECK(f.line("") == S::Ok);
    CHECK(f.line("t = threading.Thread(target=job); t.start(); t.join(); print(errs)") == S::Ok);
    CHECK(f.appRuns == runs);
    CHECK(contains(f.out, "main thread"));
}

// The host has no core of its own (CoreCalls.h): a script's edits land in the app's own state, and documents a script
// makes run on the app's core.
TEST(python_host_uses_the_apps_core) {
    Fixture& f = py();
    if (!f.host) return;
    f.clear();
    CHECK(f.line("sf.run('new_sphere', subdivisions=1)") == S::Ok);
    CHECK(f.line("doc.mask = [0.5] * len(doc.vertices)") == S::Ok);
    CHECK(f.doc.mesh.mask.size() == f.doc.mesh.vertexCount());
    CHECK(!f.doc.mesh.mask.empty() && f.doc.mesh.mask[0] == 0.5f);
    CHECK(f.line("doc.set_mesh([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 1, 2)])") == S::Ok);
    CHECK(f.doc.mesh.triangleCount() == 1 && f.doc.mesh.positions[1].x == 1.0f);
    CHECK(f.doc.mesh.normals.size() == 3); // finished by the app's core
    const int before = g_ownRuns;
    CHECK(f.line("own = sf.Document(); sf.run(own, 'new_sphere', subdivisions=1); print(len(own.triangles))") == S::Ok);
    CHECK(g_ownRuns == before + 1);
    CHECK(contains(f.out, "80"));
    CHECK(f.line("print(sf.run_script(own, 'subdivide')[0] != '', len(own.triangles))") == S::Ok);
    CHECK(g_ownRuns == before + 2);
    CHECK(contains(f.out, "True 320"));
    CHECK(f.line("print('new_sphere' in sf.tool_schema())") == S::Ok);
    CHECK(contains(f.out, "True"));
    CHECK(f.doc.mesh.triangleCount() == 1); // the open scene untouched by the script's own document
}

TEST(python_host_completes_names) {
    Fixture& f = py();
    if (!f.host) return;
    const auto names = f.host->complete("sf.run");
    bool run = false, script = false;
    for (const auto& n : names) run |= n.rfind("sf.run", 0) == 0 && n.find("script") == std::string::npos, script |= n.rfind("sf.run_script", 0) == 0;
    CHECK(run && script);
}

// sf.ui (P2): what a script declares reaches the app as plain data; actions and controls call back into Python.
TEST(python_host_script_interface) {
    Fixture& f = py();
    if (!f.host) return;
    f.registry.execute(f.doc, "new_sphere", {{"subdivisions", 1.0}});
    const uint64_t rev = f.host->uiRevision();
    f.clear();
    CHECK(f.line("sf.ui.action('test.grow', 'Grow', lambda: sf.run('subdivide'), icon='sparkles', help='One level')") == S::Ok);
    CHECK(f.line("p = sf.ui.panel('test', 'Test tools'); p.label('Hello'); p.separator(); p.button('test.grow')") == S::Ok);
    CHECK(f.line("p.slider('res', 'Resolution', 64, 16, 256, integer=True); p.checkbox('flag', 'Flag', on_change=lambda v: print('flag', v))") == S::Ok);
    CHECK(f.host->uiRevision() != rev);
    python::UiState ui = f.host->ui();
    CHECK(ui.actions.size() == 1 && ui.actions[0].id == "test.grow" && ui.actions[0].icon == "sparkles" && ui.actions[0].help == "One level");
    CHECK(ui.panels.size() == 1 && ui.panels[0].title == "Test tools" && ui.panels[0].controls.size() == 5);
    if (ui.panels.size() == 1 && ui.panels[0].controls.size() == 5) {
        const auto& c = ui.panels[0].controls;
        CHECK(c[0].kind == python::UiControl::Kind::Label && c[0].label == "Hello");
        CHECK(c[2].kind == python::UiControl::Kind::Button && c[2].action == "test.grow" && c[2].label == "Grow");
        CHECK(c[3].kind == python::UiControl::Kind::Slider && c[3].integer && c[3].value == 64 && c[3].max == 256);
        CHECK(c[4].kind == python::UiControl::Kind::Checkbox && c[4].value == 0.0);
    }
    // The action runs its callback; a control's change is stored, then on_change hears it.
    const size_t tris = f.doc.mesh.triangleCount();
    CHECK(f.host->runAction("test.grow") == S::Ok && f.doc.mesh.triangleCount() == tris * 4);
    CHECK(f.host->runAction("nope") == S::Error);
    CHECK(f.host->setValue("test", "flag", 1.0) == S::Ok && contains(f.out, "flag True"));
    CHECK(f.host->setValue("test", "res", 99.6) == S::Ok);
    f.clear();
    CHECK(f.line("print(sf.ui.value('test.res'), p['flag'])") == S::Ok && contains(f.out, "100 True"));
    // Declaring the panel again (running the script again) keeps what the user set.
    CHECK(f.line("p = sf.ui.panel('test', 'Test tools'); p.slider('res', 'Resolution', 64, 16, 256, integer=True)") == S::Ok);
    ui = f.host->ui();
    CHECK(ui.panels.size() == 1 && ui.panels[0].controls.size() == 1 && ui.panels[0].controls[0].value == 100);
    // A callback that fails says why and leaves the app running.
    CHECK(f.line("sf.ui.action('test.bad', 'Bad', lambda: 1/0)") == S::Ok);
    f.clear();
    CHECK(f.host->runAction("test.bad") == S::Error && contains(f.err, "ZeroDivisionError"));
    // Ids, icons and callbacks are checked when declared.
    CHECK(f.line("sf.ui.action('Bad Id', 'x', print)") == S::Error);
    CHECK(f.line("sf.ui.action('ok.id', 'x', print, icon='nope')") == S::Error);
    CHECK(f.line("sf.ui.action('ok.id', 'x', 'not callable')") == S::Error);
    CHECK(f.line("p.slider('res', 'Again', 1, 0, 2)") == S::Error); // one control per id
    // place: actions on shelves or in My Panel, panels only in My Panel.
    f.placed.clear();
    CHECK(f.line("sf.ui.place('test.grow', 'top'); sf.ui.place('test', 'my_panel')") == S::Ok);
    CHECK(f.line("sf.ui.place('test', 'left')") == S::Error);
    CHECK(f.placed.size() == 3 && f.placed[0] == "action:test.grow@0" && f.placed[1] == "panel:test@4");
    CHECK(f.line("sf.ui.clear()") == S::Ok && f.host->ui().actions.empty());
}

// sf.ui.main_window_handle: the app's native window handle as an int, None when it has none (Wayland, offscreen).
TEST(python_host_main_window_handle) {
    Fixture& f = py();
    if (!f.host) return;
    f.windowHandle = 0;
    f.clear();
    CHECK(f.line("print(repr(sf.ui.main_window_handle()))") == S::Ok && contains(f.out, "None"));
    f.windowHandle = 0x1234abcdef; // wider than 32 bits: an HWND or NSWindow* on a 64-bit OS
    f.clear();
    CHECK(f.line("h = sf.ui.main_window_handle(); print(type(h).__name__, h == 0x1234abcdef)") == S::Ok &&
          contains(f.out, "int True"));
    // Main thread only, like the rest of sf.ui.
    f.clear();
    CHECK(f.line("import threading; r = []; t = threading.Thread(target=lambda: r.append(sf.ui.main_window_handle())); "
                 "t.start(); t.join(); print(len(r))") == S::Ok &&
          contains(f.out, "0"));
    f.windowHandle = 0;
}

// sf.ui.timer (P3): the app's loop asks when the next is due and runs the due ones on its own thread.
TEST(python_host_timers) {
    Fixture& f = py();
    if (!f.host) return;
    double in = 0.0;
    CHECK(f.line("sf.ui.clear()") == S::Ok);
    CHECK(!f.host->timerDue(in)); // none: the app's loop never wakes for Python
    CHECK(f.line("ticks = []; t = sf.ui.timer(lambda: ticks.append(1), 10)") == S::Ok);
    CHECK(f.host->timerDue(in) && in > 0.0 && in <= 0.011);
    CHECK(f.host->runTimers() == S::Ok); // not due yet: nothing runs
    f.clear();
    CHECK(f.line("print(len(ticks))") == S::Ok && contains(f.out, "0"));
    SDL_Delay(15);
    CHECK(f.host->timerDue(in) && in <= 0.0);
    CHECK(f.host->runTimers() == S::Ok);
    f.clear();
    CHECK(f.line("print(len(ticks))") == S::Ok && contains(f.out, "1"));
    CHECK(f.line("sf.ui.cancel_timer(t)") == S::Ok && !f.host->timerDue(in));
    // A failing callback is stopped, with a message.
    CHECK(f.line("bad = sf.ui.timer(lambda: 1/0, 10)") == S::Ok);
    SDL_Delay(15);
    f.clear();
    CHECK(f.host->runTimers() == S::Error && contains(f.err, "ZeroDivisionError") && contains(f.err, "stopped"));
    CHECK(!f.host->timerDue(in));
}

// A new App in the same process (the warm UI test server makes one per script): the running Python serves it,
// pointed at its scene, with a fresh console and sf.ui (CPython can't start twice in one process).
TEST(python_host_serves_a_new_app) {
    Fixture& f = py();
    if (!f.host) return;
    CHECK(f.line("sf.ui.action('old.one', 'Old', print); leftover = 1") == S::Ok);
    SDL_SharedObject* lib = SDL_LoadObject(SF_PYHOST_PATH);
    auto create = lib ? reinterpret_cast<SfPyHostCreate>(SDL_LoadFunction(lib, "sf_pyhost_create")) : nullptr;
    CHECK(create != nullptr);
    if (!create) return;
    Document doc2;
    CommandRegistry registry;
    int runs = 0;
    std::string out;
    python::AppSide side;
    side.build = SF_PYHOST_BUILD;
    side.core = &python::directCoreCalls();
    side.documentSize = sizeof(Document);
    side.meshSize = sizeof(Mesh);
    side.home = runtimeHome();
    side.document = &doc2;
    side.registry = &registry;
    side.run = [&](const std::string& name, const Args& args) {
        ++runs;
        return registry.execute(doc2, name, args);
    };
    side.write = [&](bool, const std::string& text) { out += text; };
    char error[512] = "";
    python::Host* host2 = create(python::kHostAbi, &side, error, int(sizeof error));
    CHECK(host2 != nullptr);
    if (!host2) {
        std::fprintf(stderr, "second host: %s\n", error);
        return;
    }
    CHECK(host2->runLine("sf.run('new_sphere', subdivisions=1)") == S::Ok && runs == 1 && doc2.mesh.triangleCount() == 80);
    CHECK(host2->runLine("print('leftover' in globals(), len(sf.ui._actions), doc is sf.document())") == S::Ok);
    CHECK(out.find("False 0 True") != std::string::npos);
    // (the fixture's own host belongs to the App before it; nothing calls it after this test)
}

#endif
