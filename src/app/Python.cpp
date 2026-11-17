// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Python.h"

#include "app/App.h"
#include "app/UserData.h"
#include "core/FileReplace.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "core/SelectionSets.h"
#include "core/Undo.h"
#include "python/PyHost.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace sf {

namespace fs = std::filesystem;

namespace {

fs::path fsPath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string prefsFile() {
    const std::string dir = userDataDir();
    return dir.empty() ? std::string() : dir + "python.json";
}

#if defined(_WIN32)
constexpr const char* kHostFile = "sculptforge_pyhost.dll";
#elif defined(__APPLE__)
constexpr const char* kHostFile = "sculptforge_pyhost.dylib";
#else
constexpr const char* kHostFile = "sculptforge_pyhost.so";
#endif

// Where the host library is: next to the executable (Windows, a build folder), lib/sculptforge beside bin/ (Linux
// packages), Frameworks in the macOS bundle. SF_PYHOST names another (tests).
std::string findHost() {
    if (const char* over = std::getenv("SF_PYHOST"); over && *over) return over;
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    const fs::path exe = fsPath(base);
    for (const fs::path& p : {exe / kHostFile, exe / ".." / "lib" / "sculptforge" / kHostFile, exe / ".." / "Frameworks" / kHostFile}) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return utf8(p.lexically_normal());
    }
    return {};
}

// A direct edit from Python on the open scene, as one undo step (the attribute's old values, then the change).
void recordEdit(Document& doc, python::Edit what, const std::function<void()>& change) {
    UndoRecord r = what == python::Edit::Mask         ? UndoRecord::captureMask(doc.mesh, "Python: mask")
                   : what == python::Edit::Colors     ? UndoRecord::captureColors(doc.mesh, "Python: colours")
                   : what == python::Edit::FaceGroups ? UndoRecord::captureFaceGroups(doc.mesh, "Python: face groups")
                                                      : UndoRecord::captureHidden(doc.mesh, "Python: visibility");
    change();
    doc.undo.push(std::move(r));
}

// Loads the host library and starts Python in it with `side` (its build stamp and home filled in here). Null with
// `error` set when there is no host, it doesn't load or Python doesn't start; `path` is the host's file.
python::Host* loadHost(python::AppSide& side, std::string& path, std::string& error) {
    path = findHost();
    if (path.empty()) {
        error = "Python isn't part of this build of SculptForge";
        return nullptr;
    }
#if defined(_WIN32)
    // The host and the Python DLLs it needs, from the app's own folder and System32 only (never the current folder
    // or PATH).
    HMODULE lib = LoadLibraryExW(fsPath(path).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto create = lib ? reinterpret_cast<SfPyHostCreate>(reinterpret_cast<void*>(GetProcAddress(lib, "sf_pyhost_create"))) : nullptr;
    const std::string loadWhy = lib ? "no sf_pyhost_create in it" : "Windows error " + std::to_string(GetLastError());
#else
    SDL_SharedObject* lib = SDL_LoadObject(path.c_str());
    auto create = lib ? reinterpret_cast<SfPyHostCreate>(SDL_LoadFunction(lib, "sf_pyhost_create")) : nullptr;
    const std::string loadWhy = SDL_GetError();
#endif
    if (!create) {
        error = "Python couldn't be loaded: " + loadWhy;
        return nullptr; // (the library stays loaded if it loaded: never unloaded, like a started host)
    }
    side.build = SCULPTFORGE_VERSION;
    side.documentSize = sizeof(Document);
    side.meshSize = sizeof(Mesh);
    // The runtime shipped with the app: python/ beside the host, or in the macOS bundle's Resources (none: the Python
    // the host was built with, as a source build or a distribution package has it).
    for (const fs::path& home : {fsPath(path).parent_path() / "python", fsPath(path).parent_path() / ".." / "Resources" / "python"}) {
        std::error_code ec;
        if (fs::is_directory(home, ec)) {
            side.home = utf8(home.lexically_normal());
            break;
        }
    }
    char why[512] = "";
    python::Host* host = create(python::kHostAbi, &side, why, int(sizeof why));
    if (!host) error = std::string("Python couldn't start: ") + why;
    return host;
}

} // namespace

// sculptforge --python-selftest (the release packages' check that the Python they ship starts): the host from the
// installed layout, on a scene of its own, imports the standard library's compiled modules and runs commands.
int pythonSelfTest() {
    Document doc;
    CommandRegistry registry;
    python::AppSide side;
    side.document = &doc;
    side.registry = &registry;
    side.run = [&](const std::string& name, const Args& args) { return registry.execute(doc, name, args); };
    side.edit = recordEdit;
    side.replaceMesh = [](Document& d, Mesh&& m) { d.replaceMeshUndoable(std::move(m), "Python: set mesh"); };
    side.addSelectionSet = [](Document& d, SelectionSet&& set, const std::string& label, std::string& msg) {
        return addSelectionSet(d, std::move(set), label, msg);
    };
    side.selectPart = [](Document& d, int i) { d.selectPart(i); };
    std::string out;
    side.write = [&](bool, const std::string& text) { out += text; };
    std::string path, error;
    python::Host* host = loadHost(side, path, error);
    if (!host) {
        std::fprintf(stderr, "python selftest: %s (%s)\n", error.c_str(), path.empty() ? "no host" : path.c_str());
        return 1;
    }
    bool ok = true;
    for (const char* line : {"import json, math, zlib, ctypes, sqlite3, ssl, hashlib, decimal, pathlib, threading",
                             "sf.run('new_sphere', subdivisions=2); doc.mask = [0.0] * len(doc.vertices)",
                             "assert len(doc.triangles) == 320, len(doc.triangles)",
                             "import sys; print('python selftest ok', sys.version.split()[0], sys.prefix)"})
        ok = host->runLine(line) == python::Host::Status::Ok && ok;
    std::fprintf(ok ? stdout : stderr, "%s%s", out.c_str(), ok ? "" : "python selftest FAILED\n");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

PythonScripting::PythonScripting(App& app) : app_(app) {}
PythonScripting::~PythonScripting() = default; // (the host and Python stay until the process ends)

bool PythonScripting::allowed(std::string* why) const { return policy::pythonAllowed(why); }

bool PythonScripting::enabled() const {
    if (enabled_ < 0) {
        enabled_ = 0;
        std::ifstream f(fsPath(prefsFile()), std::ios::binary);
        Json j;
        std::string err;
        if (f && Json::parse(std::string((std::istreambuf_iterator<char>(f)), {}), j, err)) enabled_ = j["enabled"].asBool(false) ? 1 : 0;
    }
    return enabled_ == 1 && allowed();
}

void PythonScripting::setEnabled(bool on) {
    enabled_ = on ? 1 : 0;
    const std::string path = prefsFile();
    if (path.empty()) return;
    Json j = Json::object();
    j.set("enabled", Json(on));
    const fs::path target = fsPath(path), temp = fsPath(path + ".tmp");
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
    }
    std::string err;
    if (!replaceFile(temp, target, err)) SDL_Log("Python: couldn't save %s: %s", path.c_str(), err.c_str());
}

void PythonScripting::write(bool error, const std::string& text) {
    std::lock_guard<std::mutex> lock(outMutex_);
    out_.emplace_back(error, text);
}

void PythonScripting::drainOutput() {
    std::vector<std::pair<bool, std::string>> out;
    {
        std::lock_guard<std::mutex> lock(outMutex_);
        if (out_.empty()) return;
        out.swap(out_);
    }
    for (auto& [error, text] : out) {
        // print() writes "text" and "\n" separately: whole lines go to the console, the rest waits for its newline.
        std::string& part = partial_[error ? 1 : 0];
        part += text;
        const size_t nl = part.find_last_of('\n');
        if (nl == std::string::npos) continue;
        app_.console_.print(error ? Console::Kind::Error : Console::Kind::Info, part.substr(0, nl));
        part.erase(0, nl + 1);
    }
}

bool PythonScripting::load() {
    if (host_) return true;
    std::string why;
    if (!allowed(&why)) {
        app_.console_.print(Console::Kind::Error, why);
        return false;
    }
    if (!enabled()) {
        app_.console_.print(Console::Kind::Error, "Python scripts are off: turn them on in Preferences > Scripts");
        return false;
    }
    if (!loadError_.empty()) {
        app_.console_.print(Console::Kind::Error, loadError_);
        return false;
    }
    ++loads_;
    python::AppSide side;
    side.document = &app_.doc_;
    side.registry = &app_.commands_;
    side.run = [this](const std::string& name, const Args& args) {
        app_.runCommand(name, args);
        return CommandResult{app_.lastCommandOk_, app_.status_};
    };
    side.edit = recordEdit;
    side.replaceMesh = [](Document& doc, Mesh&& m) { doc.replaceMeshUndoable(std::move(m), "Python: set mesh"); };
    side.addSelectionSet = [](Document& doc, SelectionSet&& set, const std::string& label, std::string& msg) {
        return addSelectionSet(doc, std::move(set), label, msg);
    };
    side.selectPart = [this](Document&, int i) { app_.runCommand("part_select", {{"index", double(i)}}); };
    side.write = [this](bool error, const std::string& text) { write(error, text); };
    const fs::path scripts = fsPath(scriptsFolder());
    if (!scripts.empty()) side.paths = {utf8(scripts), utf8(scripts / "site-packages")};
    std::string path;
    host_ = loadHost(side, path, loadError_);
    if (!host_) {
        app_.console_.print(Console::Kind::Error, loadError_);
        SDL_Log("Python: %s (%s)", loadError_.c_str(), path.c_str());
        return false;
    }
    version_ = host_->version();
    SDL_Log("Python %s loaded from %s", version_.c_str(), path.c_str());
    return true;
}

// Runs `body` as one undo step, with the app's prompts (memory, subdivision levels) and background commands off:
// a script reads what each command did on its next line.
template <class F>
PythonScripting::Result PythonScripting::run(F&& body) {
    if (!load()) return Result::Unavailable;
    python::Host::Status st;
    {
        UndoBatchScope batch;
        ++app_.macros_.runDepth;
        st = body();
        --app_.macros_.runDepth;
    }
    drainOutput();
    return st == python::Host::Status::Ok ? Result::Ok : st == python::Host::Status::More ? Result::More : Result::Error;
}

PythonScripting::Result PythonScripting::runLine(const std::string& line) {
    return run([&] { return host_->runLine(line); });
}

void PythonScripting::cancelLine() {
    if (host_) host_->cancelLine();
}

PythonScripting::Result PythonScripting::runFile(const std::string& path) {
    return run([&] { return host_->runFile(path); });
}

std::vector<std::string> PythonScripting::complete(const std::string& text) { return host_ ? host_->complete(text) : std::vector<std::string>(); }

std::string PythonScripting::scriptsFolder() const {
    return app_.library_.user.empty() ? std::string() : utf8(fsPath(app_.library_.user) / "Scripts");
}

const std::vector<std::string>& PythonScripting::scripts() {
    const double now = double(SDL_GetTicks()) / 1000.0;
    if (now - scriptsScanned_ < 2.0) return scripts_;
    scriptsScanned_ = now;
    scripts_.clear();
    const std::string dir = scriptsFolder();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(fsPath(dir), ec)) return scripts_;
    for (fs::directory_iterator it(fsPath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        std::string name = utf8(p.filename());
        if (name.empty() || name[0] == '.' || name[0] == '_') continue; // hidden files and helper modules (_util.py)
        std::string ext = utf8(p.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".py" && it->is_regular_file(ec)) scripts_.push_back(utf8(p));
    }
    std::sort(scripts_.begin(), scripts_.end());
    return scripts_;
}

} // namespace sf
