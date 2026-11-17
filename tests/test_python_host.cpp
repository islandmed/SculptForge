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
#include "python/PyHost.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>

using namespace sf;

namespace {

struct Fixture {
    Document doc;
    CommandRegistry registry;
    python::Host* host = nullptr;
    std::string out, err; // what Python printed, by stream
    int appRuns = 0;      // commands that went through the app's side
    std::string loadError;

    Fixture() {
        SDL_SharedObject* lib = SDL_LoadObject(SF_PYHOST_PATH);
        auto create = lib ? reinterpret_cast<SfPyHostCreate>(SDL_LoadFunction(lib, "sf_pyhost_create")) : nullptr;
        if (!create) {
            loadError = SDL_GetError();
            return;
        }
        python::AppSide side;
        side.build = SF_PYHOST_BUILD;
        side.documentSize = sizeof(Document);
        side.meshSize = sizeof(Mesh);
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

TEST(python_host_completes_names) {
    Fixture& f = py();
    if (!f.host) return;
    const auto names = f.host->complete("sf.run");
    bool run = false, script = false;
    for (const auto& n : names) run |= n.rfind("sf.run", 0) == 0 && n.find("script") == std::string::npos, script |= n.rfind("sf.run_script", 0) == 0;
    CHECK(run && script);
}

#endif
