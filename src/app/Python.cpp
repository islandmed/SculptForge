// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Python.h"

#include "app/App.h"
#include "app/UserData.h"
#include "core/FileReplace.h"
#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "core/SelectionSets.h"
#include "core/ScriptPath.h"
#include "core/Sha256.h"
#include "core/Undo.h"
#include "python/CoreCalls.h"
#include "python/PyHost.h"

#include <IconsLucide.h>
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
// packages), Frameworks in the macOS bundle. Only there: no setting or environment variable can name another library.
std::string findHost() {
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
python::Host* loadHost(python::AppSide& side, std::string& path, std::string& error, std::string* fallbackHome = nullptr) {
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
    side.core = &python::directCoreCalls();
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
    if (fallbackHome) {
#if defined(_WIN32)
        auto fallback = reinterpret_cast<const char* (*)()>(reinterpret_cast<void*>(GetProcAddress(lib, "sf_pyhost_fallback_home")));
#else
        auto fallback = reinterpret_cast<const char* (*)()>(SDL_LoadFunction(lib, "sf_pyhost_fallback_home"));
#endif
        *fallbackHome = fallback ? fallback() : "?";
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
    std::string path, error, fallback;
    python::Host* host = loadHost(side, path, error, &fallback);
    // (packages check this line: a package must have no fallback, only the runtime beside the host)
    std::printf("python fallback home: %s\n", fallback.empty() ? "none" : fallback.c_str());
    std::printf("python home: %s (host %s)\n", side.home.empty() ? "(none beside the host)" : side.home.c_str(), path.c_str());
    if (!host) {
        std::fprintf(stderr, "python selftest: %s (%s)\n", error.c_str(), path.empty() ? "no host" : path.c_str());
        return 1;
    }
    bool ok = true;
    for (const char* line : {"import json, math, zlib, ctypes, sqlite3, ssl, hashlib, decimal, pathlib, threading",
                             "sf.run('new_sphere', subdivisions=2); doc.mask = [0.0] * len(doc.vertices)",
                             "assert len(doc.triangles) == 320, len(doc.triangles)",
                             "import sys; print('python selftest ok', sys.version.split()[0], sys.prefix); print('sys.path:', sys.path)"})
        ok = host->runLine(line) == python::Host::Status::Ok && ok;
    std::fprintf(ok ? stdout : stderr, "%s%s", out.c_str(), ok ? "" : "python selftest FAILED\n");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

PythonScripting::PythonScripting(App& app) : app_(app) {}
PythonScripting::~PythonScripting() = default; // (the host and Python stay until the process ends)

bool PythonScripting::allowed(std::string* why) const { return policy::pythonAllowed(why); }

void PythonScripting::readPrefs() const {
    enabled_ = 0;
    startup_.clear();
    std::ifstream f(fsPath(prefsFile()), std::ios::binary);
    Json j;
    std::string err;
    if (!f || !Json::parse(std::string((std::istreambuf_iterator<char>(f)), {}), j, err)) return;
    enabled_ = j["enabled"].asBool(false) ? 1 : 0;
    if (j["startup"].isArray())
        for (size_t i = 0; i < j["startup"].size(); ++i) {
            const Json& e = j["startup"][i];
            if (e["path"].isString() && e["sha256"].isString()) startup_.emplace_back(e["path"].asString(), e["sha256"].asString());
        }
}

void PythonScripting::savePrefs() const {
    const std::string path = prefsFile();
    if (path.empty()) return;
    Json j = Json::object();
    j.set("enabled", Json(enabled_ == 1));
    Json list = Json::array();
    for (const auto& [p, sha] : startup_) {
        Json e = Json::object();
        e.set("path", Json(p.c_str()));
        e.set("sha256", Json(sha.c_str()));
        list.push(std::move(e));
    }
    j.set("startup", std::move(list));
    const fs::path target = fsPath(path), temp = fsPath(path + ".tmp");
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        f << j.dump(true) << "\n";
    }
    std::string err;
    if (!replaceFile(temp, target, err)) SDL_Log("Python: couldn't save %s: %s", path.c_str(), err.c_str());
}

bool PythonScripting::enabled() const {
    if (enabled_ < 0) readPrefs();
    return enabled_ == 1 && allowed();
}

void PythonScripting::setEnabled(bool on) {
    if (enabled_ < 0) readPrefs();
    enabled_ = on ? 1 : 0;
    savePrefs();
}

namespace {
bool readBytes(const std::string& path, std::string& out) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
std::string fileSha256(const std::string& path) {
    std::string bytes;
    return readBytes(path, bytes) ? sha256Hex(bytes) : std::string();
}
} // namespace

bool PythonScripting::runsAtStartup(const std::string& path) const {
    if (enabled_ < 0) readPrefs();
    return std::any_of(startup_.begin(), startup_.end(), [&](const auto& e) { return e.first == path; });
}

bool PythonScripting::inScriptsFolder(const std::string& path) const {
    const std::string dir = scriptsFolder();
    if (dir.empty() || path.empty()) return false;
    std::error_code ec;
    const fs::path d = fs::weakly_canonical(fsPath(dir), ec), p = fs::weakly_canonical(fsPath(path), ec);
    if (ec) return false;
    return p.parent_path() == d; // directly in it (the list shows no sub-folders), no links or ".." out of it
}

bool PythonScripting::setRunAtStartup(const std::string& path, bool on) {
    if (enabled_ < 0) readPrefs();
    startup_.erase(std::remove_if(startup_.begin(), startup_.end(), [&](const auto& e) { return e.first == path; }), startup_.end());
    changed_.erase(path);
    bool ok = true;
    if (on) {
        const std::string sha = inScriptsFolder(path) ? fileSha256(path) : std::string();
        if (sha.empty()) ok = false; // only a script in the Scripts folder can run at start-up
        else startup_.emplace_back(path, sha);
    }
    savePrefs();
    return ok;
}

std::vector<std::string> PythonScripting::startupScripts() const {
    if (enabled_ < 0) readPrefs();
    std::vector<std::string> out;
    for (const auto& e : startup_) out.push_back(e.first);
    return out;
}

void PythonScripting::runStartupScripts(bool again) {
    if (startupRan_ && !again) return;
    startupRan_ = true;
    if (!enabled()) return; // (reads python.json: a few hundred bytes, once; off, or a policy's never: nothing runs)
    for (const auto [path, sha] : std::vector(startup_)) {
        const std::u8string name8 = fsPath(path).filename().u8string();
        const std::string name(name8.begin(), name8.end());
        // Read once: the bytes hashed are the bytes run (the file changing after the check changes nothing now).
        std::string bytes;
        const std::string now = inScriptsFolder(path) && readBytes(path, bytes) ? sha256Hex(bytes) : std::string();
        if (now.empty()) {
            app_.status_ = "Run at start-up: " + name + " isn't in the Scripts folder any more, so it didn't run";
            app_.console_.print(Console::Kind::Error, app_.status_);
        } else if (now != sha) {
            changed_.insert(path);
            app_.status_ = name + " changed since you chose to run it at start-up, so it didn't run. Preferences > Scripts: Trust this version";
            app_.console_.print(Console::Kind::Error, app_.status_);
        } else {
            app_.console_.print(Console::Kind::Info, "Run at start-up: " + std::string(name.begin(), name.end()));
            if (afterStartupCheck) afterStartupCheck(path);
            if (run([&] { return host_->runSource(bytes, path); }) == Result::Unavailable) return;
        }
    }
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
    std::string why;
    if (!allowed(&why)) {
        app_.console_.print(Console::Kind::Error, why);
        return false;
    }
    if (!enabled()) {
        app_.console_.print(Console::Kind::Error, "Python scripts are off: turn them on in Preferences > Scripts");
        return false;
    }
    if (host_) return true; // (turned off later: nothing runs, the loaded host stays idle)
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
    side.place = [this](const std::string& id, bool panel, python::Place where) { return place(id, panel, where); };
    side.windowHandle = [this]() -> uint64_t { return app_.nativeWindowHandle(); };
    const fs::path scripts = fsPath(scriptsFolder());
    if (!scripts.empty()) side.paths = {utf8(scripts), utf8(scripts / "site-packages")};
    std::string path;
    host_ = loadHost(side, path, loadError_);
    if (!host_) {
        app_.status_ = loadError_; // (fails closed: no Python, and the status bar and console say why)
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
    syncUi();
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

bool PythonScripting::timerWake(double& inSeconds) const { return host_ && host_->timerDue(inSeconds) && enabled(); }

void PythonScripting::tickTimers() {
    double in = 0.0;
    if (!host_ || !host_->timerDue(in) || in > 0.0 || !enabled()) return; // (Python turned off: timers wait, silent)
    run([&] { return host_->runTimers(); });
}

PythonScripting::Result PythonScripting::runAction(const std::string& id) {
    return run([&] { return host_->runAction(id); });
}

PythonScripting::Result PythonScripting::setValue(const std::string& panel, const std::string& control, double value) {
    for (auto& p : ui_.panels)
        if (p.id == panel)
            for (auto& c : p.controls)
                if (c.id == control) c.value = value;
    return run([&] { return host_->setValue(panel, control, value); });
}

namespace {
const char* uiIcon(const std::string& name) {
    static const std::pair<const char*, const char*> kIcons[] = {
        {"play", ICON_LC_PLAY}, {"sparkles", ICON_LC_SPARKLES}, {"wand", ICON_LC_WAND_SPARKLES}, {"box", ICON_LC_BOX},
        {"brush", ICON_LC_BRUSH}, {"scissors", ICON_LC_SCISSORS}, {"layers", ICON_LC_LAYERS}, {"download", ICON_LC_DOWNLOAD},
        {"upload", ICON_LC_UPLOAD}, {"star", ICON_LC_STAR}, {"file-code", ICON_LC_FILE_CODE}, {"settings", ICON_LC_SETTINGS},
        {"eye", ICON_LC_EYE}, {"camera", ICON_LC_CAMERA}, {"grid", ICON_LC_GRID_3X3}, {"move", ICON_LC_MOVE},
        {"rotate", ICON_LC_ROTATE_CW}, {"trash", ICON_LC_TRASH_2}};
    for (const auto& [n, icon] : kIcons)
        if (name == n) return icon;
    return ICON_LC_FILE_CODE;
}
} // namespace

void PythonScripting::syncUi() {
    if (!host_ || drawing_ || host_->uiRevision() == uiRevision_) return;
    uiRevision_ = host_->uiRevision();
    ui_ = host_->ui();
    app_.actions_.removePrefix(kActionPrefix);
    for (const python::UiAction& u : ui_.actions) {
        Action a;
        a.id = kActionPrefix + u.id;
        a.label = u.label;
        a.icon = uiIcon(u.icon);
        a.palette = "File > Script";
        a.description = u.help.empty() ? "A script's action (" + u.id + ")" : u.help;
        a.run = [this, id = u.id] { runAction(id); };
        app_.actions_.add(std::move(a));
    }
}

bool PythonScripting::place(const std::string& id, bool panel, python::Place where) {
    syncUi(); // (the script may have declared it a moment ago)
    const std::string entry = kActionPrefix + id;
    if (where == python::Place::MyPanel) {
        const std::string e = (panel ? "section:" : "action:") + entry;
        auto& list = app_.layout_.myPanel;
        return std::find(list.begin(), list.end(), e) != list.end() || app_.myPanelAdd(e, int(list.size()));
    }
    if (panel) return false; // a shelf holds actions
    UiLayout& l = app_.layout_;
    std::vector<std::string>& shelf = where == python::Place::TopShelf      ? l.topShelf
                                      : where == python::Place::BottomShelf ? l.bottomShelf
                                      : where == python::Place::LeftShelf   ? l.leftShelf
                                                                            : l.rightShelf;
    if (std::find(shelf.begin(), shelf.end(), entry) == shelf.end()) {
        shelf.push_back(entry);
        app_.noteLayoutChanged();
    }
    return true;
}

std::vector<std::string> PythonScripting::complete(const std::string& text) { return host_ ? host_->complete(text) : std::vector<std::string>(); }

std::string PythonScripting::scriptsFolder() const {
    return app_.library_.user.empty() ? std::string() : utf8(fsPath(app_.library_.user) / "Scripts");
}

const std::vector<std::string>& PythonScripting::scripts() {
    scan();
    return scripts_;
}

const std::vector<std::string>& PythonScripting::pathScripts() {
    scan();
    return pathScripts_;
}

void PythonScripting::scan() {
    const double now = double(SDL_GetTicks()) / 1000.0;
    if (now - scriptsScanned_ < 2.0) return;
    scriptsScanned_ = now;
    scripts_.clear(), pathScripts_.clear();
    if (!allowed()) return; // a studio policy of never hides them all
    const char* env = std::getenv("SCULPTFORGE_SCRIPT_PATH");
    std::vector<std::string> skipped;
    for (ScriptFile& f : listScripts(scriptsFolder(), splitSearchPath(env ? env : ""), &skipped))
        (f.fromSearchPath ? pathScripts_ : scripts_).push_back(std::move(f.path));
    for (const std::string& dir : skipped)
        if (skippedPaths_.insert(dir).second) app_.console_.print(Console::Kind::Info, "SCULPTFORGE_SCRIPT_PATH: skipped " + dir + " (not a readable folder)");
}

} // namespace sf
