// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A portable copy keeps all its data in <program folder>/data (core/DataDir.h): the marker rules, the read-only
// fallback, the first-run import, and a scripted session (settings, a brush, an autosave, a cache) that must
// write nothing outside data/. Without the marker the paths are the per-user ones main always used.
#include "TestHarness.h"

#include "ai/BrushAssistant.h"
#include "app/UserData.h"
#include "core/BrushLibrary.h"
#include "core/Commands.h"
#include "core/DataDir.h"
#include "core/Document.h"
#include "core/MicroStore.h"
#include "core/Project.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace sf;
namespace fs = std::filesystem;

namespace {

void setEnvVar(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
void unsetEnvVar(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// The environment variables any data path derives from, saved and restored around a test.
struct EnvGuard {
    std::map<std::string, std::pair<bool, std::string>> saved;
    EnvGuard() {
        for (const char* k : {"HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA", "USERPROFILE",
                              "SF_PORTABLE_DIR", "SF_USER_DATA_DIR", "SF_MICRO_SWAP_DIR", "APPIMAGE", "FLATPAK_ID"}) {
            const char* v = std::getenv(k);
            saved[k] = {v != nullptr, v ? v : ""};
        }
    }
    ~EnvGuard() {
        for (const auto& [k, v] : saved) {
            if (v.first) setEnvVar(k.c_str(), v.second);
            else unsetEnvVar(k.c_str());
        }
        resetDataLocation();
    }
};

fs::path freshDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("sculptforge_portable_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

size_t countFiles(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) ++n;
    return n;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

// A "program folder" and a "machine": every per-user variable points at an empty folder (the sentinel) that must stay
// empty while the program folder carries a marker.
struct Machine {
    EnvGuard env;
    fs::path root, exe, sentinel;
    explicit Machine(const char* name) {
        root = freshDir(name);
        exe = root / "program folder with spaces";
        sentinel = root / "machine";
        fs::create_directories(exe);
        fs::create_directories(sentinel);
        const std::string s = sentinel.string();
        for (const char* k : {"HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "APPDATA", "LOCALAPPDATA", "USERPROFILE"})
            setEnvVar(k, s);
        unsetEnvVar("SF_USER_DATA_DIR");
        unsetEnvVar("SF_MICRO_SWAP_DIR");
        unsetEnvVar("APPIMAGE");
        unsetEnvVar("FLATPAK_ID");
        setEnvVar("SF_PORTABLE_DIR", exe.string());
        resetDataLocation();
    }
    ~Machine() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path data() const { return exe / "data"; }
};

} // namespace

TEST(portable_marker_file_sets_the_data_folder) {
    Machine m("marker_file");
    write(m.exe / "portable.txt", "portable\n");
    resetDataLocation();
    const DataLocation& loc = dataLocation();
    CHECK(loc.portable && !loc.readOnly);
    CHECK(fs::path(loc.dir) == m.data());
    CHECK(fs::is_directory(m.data())); // created on first run
    CHECK(fs::path(appDataDir()) == m.data());
    CHECK(fs::path(userDataDir()).parent_path() == m.data()); // (the app's own form has a trailing separator)
    CHECK(userDataDir().back() == '/' || userDataDir().back() == '\\');
    CHECK(countFiles(m.sentinel) == 0); // nothing under the machine's own folders
}

TEST(portable_data_folder_marks_too) {
    Machine m("marker_folder");
    fs::create_directories(m.data());
    resetDataLocation();
    CHECK(dataLocation().portable);
    CHECK(fs::path(appDataDir()) == m.data());
    CHECK(countFiles(m.sentinel) == 0);
}

TEST(portable_no_marker_paths_are_the_per_user_ones) {
    Machine m("no_marker");
    resetDataLocation();
    CHECK(!dataLocation().portable && !dataLocation().readOnly);
    CHECK(!fs::exists(m.data())); // no data/ made
    // main's formula, spelled out
#if defined(_WIN32)
    const std::string expected = m.sentinel.string() + "\\SculptForge\\SculptForge";
#elif defined(__APPLE__)
    const std::string expected = m.sentinel.string() + "/Library/Application Support/SculptForge/SculptForge";
#else
    const std::string expected = m.sentinel.string() + "/SculptForge/SculptForge";
#endif
    CHECK(appDataDir() == expected);
    CHECK(perUserDataDir() == expected);
    // the platform folders are read at each call, as before (tests and UI runs point them elsewhere)
    const fs::path other = m.root / "other";
    fs::create_directories(other);
    setEnvVar("XDG_DATA_HOME", other.string());
    setEnvVar("APPDATA", other.string());
#if !defined(__APPLE__)
    CHECK(appDataDir().rfind(other.string(), 0) == 0);
#endif
}

TEST(portable_read_only_falls_back_to_the_per_user_folder) {
    Machine m("read_only");
    write(m.exe / "portable.txt", "portable\n");
    write(m.data(), "a file where the folder should be"); // can't be created or written, on any OS and as any user
    resetDataLocation();
    const DataLocation& loc = dataLocation();
    CHECK(!loc.portable && loc.readOnly);
    CHECK(loc.dir == perUserDataDir());
    CHECK(appDataDir() == perUserDataDir());
    CHECK(!portableFallbackNote().empty()); // said once in the status line
    CHECK(!importAvailable()); // and nothing is ever imported into the machine's folder
}

TEST(portable_isolated_runs_ignore_the_marker) {
    Machine m("isolated");
    write(m.exe / "portable.txt", "portable\n");
    setEnvVar("SF_USER_DATA_DIR", (m.root / "isolated").string()); // what UI tests, renders and agent runs set
    resetDataLocation();
    CHECK(!dataLocation().portable);
    CHECK(!fs::exists(m.data()));
    CHECK(userDataDir().find("isolated") != std::string::npos);
}

TEST(portable_session_writes_nothing_outside_data) {
    Machine m("session");
    write(m.exe / "portable.txt", "portable\n");
    resetDataLocation();
    std::string err;

    // A settings change (AI settings file) and the preference files the app writes into userDataDir()
    ai::AiSettings s;
    CHECK(s.save(ai::defaultAiSettingsPath(), err));
    write(fs::path(userDataDir()) / "ui.json", "{}");
    write(fs::path(userDataDir()) / "panels.json", "{}");
    write(fs::path(userDataDir()) / "imgui.ini", "[Window]\n"); // a layout

    // A brush saved to the library
    BrushLibraryPaths paths = BrushLibraryPaths::fromEnvironment();
    CHECK(fs::path(paths.userDir).string().rfind(m.data().string(), 0) == 0);
    Document doc;
    loadBrushLibrary(doc, paths);
    CHECK(!doc.brushes.empty());
    doc.brushes[0].depth = -0.5f;
    CHECK(saveBrushToLibrary(doc, 0, err, false, paths));

    // An autosave
    CHECK(saveProject(userDataDir() + "autosave.sfproj", doc, err, true));
    // (the real crash log is checked by the portable_data ctest, which starts the program: tests/portable_data.cmake)

    // The cache a big sculpt swaps to
    CHECK(fs::path(MicroStore::swapFolder()) == m.data() / "cache" / "micro");

    CHECK(fs::exists(m.data() / "ai.json"));
    CHECK(fs::exists(m.data() / "ui.json"));
    CHECK(fs::exists(m.data() / "imgui.ini"));
    CHECK(fs::exists(m.data() / "autosave.sfproj"));
    CHECK(fs::exists(fs::path(paths.userDir)));
    CHECK(countFiles(fs::path(paths.userDir)) > 0);
    CHECK(countFiles(m.sentinel) == 0); // not one file under HOME, XDG_*, APPDATA, LOCALAPPDATA
}

TEST(portable_import_copies_and_never_writes_to_the_installed_folder) {
    Machine m("import");
    const fs::path installed = m.sentinel; // the machine's data: perUserDataDir()
    const fs::path user = fs::path(perUserDataDir());
    write(user / "ui.json", "{\"uiScale\":1.5,\"libraryFolder\":\"/home/someone/Documents/SculptForge\"}");
    write(user / "panels.json", "{\"mine\":1}");
    write(user / "brushes" / "Carve.sfbrush", "{}");
    write(user / "sculptforge.log", "log");
    write(user / "autosave.sfproj", "crash leftovers");
    write(user / "thumbs" / "a.png", "x");
    write(user / "hardware_profile.json", "{}");
    std::map<std::string, std::string> before;
    for (fs::recursive_directory_iterator it(installed), end; it != end; ++it)
        if (it->is_regular_file()) before[it->path().string()] = read(it->path());

    write(m.exe / "portable.txt", "portable\n");
    resetDataLocation();
    CHECK(dataLocation().portable);
    CHECK(importAvailable());
    CHECK(!importOffered());

    std::string summary, err;
    CHECK(importInstalledData(summary, err));
    CHECK(fs::exists(m.data() / "panels.json"));
    CHECK(read(m.data() / "panels.json") == "{\"mine\":1}");
    CHECK(fs::exists(m.data() / "brushes" / "Carve.sfbrush"));
    CHECK(fs::exists(m.data() / "ui.json"));
    CHECK(read(m.data() / "ui.json").find("Documents") == std::string::npos); // the library stays in the portable folder
    CHECK(read(m.data() / "ui.json").find("1.5") != std::string::npos);
    CHECK(!fs::exists(m.data() / "sculptforge.log")); // logs, crash leftovers, caches, hardware: not copied
    CHECK(!fs::exists(m.data() / "autosave.sfproj"));
    CHECK(!fs::exists(m.data() / "thumbs"));
    CHECK(!fs::exists(m.data() / "hardware_profile.json"));

    // a copy: the installed folder is exactly as it was
    std::map<std::string, std::string> after;
    for (fs::recursive_directory_iterator it(installed), end; it != end; ++it)
        if (it->is_regular_file()) after[it->path().string()] = read(it->path());
    CHECK(before == after);

    markImportOffered();
    CHECK(importOffered());
    CHECK(!importAvailable()); // asked once
}

TEST(portable_import_is_not_offered_without_installed_data_or_to_a_used_copy) {
    Machine m("import_none");
    write(m.exe / "portable.txt", "portable\n");
    resetDataLocation();
    CHECK(!importAvailable()); // nothing on this machine
    write(fs::path(perUserDataDir()) / "ui.json", "{}");
    CHECK(importAvailable());
    write(m.data() / "ui.json", "{}"); // the portable copy has settings of its own
    CHECK(!importAvailable());
}
