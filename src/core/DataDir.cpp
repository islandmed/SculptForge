// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DataDir.h"

#include "core/Json.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdint>
#include <cstdlib>
#elif !defined(__ANDROID__)
#include <unistd.h>
#include <climits>
#endif

namespace sf {

namespace {

namespace fs = std::filesystem;

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// The folder to look in, and whether the `data/` folder form counts: a package-manager or shared folder (an AppImage
// beside other downloads, a .app in Applications) is portable only by the explicit file.
struct ExeFolder {
    std::string dir;
    bool dataFolderMarks = true;
};

ExeFolder findExeFolder() {
    ExeFolder out;
    if (const std::string o = env("SF_PORTABLE_DIR"); !o.empty()) {
        out.dir = o;
        return out;
    }
#if defined(_WIN32)
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return out;
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    out.dir = pathString(fs::path(buf).parent_path());
#elif defined(__APPLE__)
    char raw[PATH_MAX];
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0) return out;
    char real[PATH_MAX];
    fs::path p = realpath(raw, real) ? fs::path(real) : fs::path(raw);
    p = p.parent_path();
    // A bundle (SculptForge.app/Contents/MacOS/sculptforge): the marker sits beside the .app.
    if (p.filename() == "MacOS" && p.parent_path().filename() == "Contents" && p.parent_path().parent_path().extension() == ".app") {
        p = p.parent_path().parent_path().parent_path();
        out.dataFolderMarks = false;
    }
    out.dir = pathString(p);
#elif defined(__ANDROID__)
    // (the app's own storage is already private to it)
#else
    std::error_code ec;
    if (const std::string image = env("APPIMAGE"); !image.empty() && fs::exists(utf8Path(image), ec)) {
        out.dir = pathString(utf8Path(image).parent_path()); // (the AppImage's mount is read-only and temporary)
        out.dataFolderMarks = false;
        return out;
    }
    if (!env("FLATPAK_ID").empty()) return out; // (a sandbox: its own per-app data folder is the portable one)
    char raw[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (n > 0) {
        raw[n] = '\0';
        out.dir = pathString(fs::path(raw).parent_path());
    }
#endif
    return out;
}

bool hasMarker(const ExeFolder& f) {
    if (f.dir.empty()) return false;
    std::error_code ec;
    const fs::path dir = utf8Path(f.dir);
    if (fs::is_regular_file(dir / "portable.txt", ec)) return true;
    // (a build folder can hold a data/ of its own)
    if (f.dataFolderMarks && fs::is_directory(dir / "data", ec) && !fs::exists(dir / "CMakeCache.txt", ec)) return true;
    return false;
}

// Creates the folder if needed and proves it takes a file.
bool writable(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) return false;
    const fs::path probe = dir / ".write-test";
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        f << "x";
        if (!f) return false;
    }
    fs::remove(probe, ec);
    return true;
}

std::mutex g_mutex;
DataLocation g_loc;
std::string g_key;
bool g_have = false;

} // namespace

std::string perUserDataDir() {
    // Matches SDL_GetPrefPath("SculptForge", "SculptForge"), which the app has always used for its settings.
#if defined(_WIN32)
    const std::string base = env("APPDATA");
    return base.empty() ? "" : base + "\\SculptForge\\SculptForge";
#elif defined(__APPLE__)
    return env("HOME") + "/Library/Application Support/SculptForge/SculptForge";
#else
    const std::string xdg = env("XDG_DATA_HOME");
    return (xdg.empty() ? env("HOME") + "/.local/share" : xdg) + "/SculptForge/SculptForge";
#endif
}

std::string executableDir() { return findExeFolder().dir; }

const DataLocation& dataLocation() {
    std::lock_guard lock(g_mutex);
    const std::string key = env("SF_PORTABLE_DIR") + "|" + env("SF_USER_DATA_DIR");
    if (g_have && key == g_key) return g_loc;
    DataLocation loc;
    loc.installedDir = perUserDataDir();
    loc.dir = loc.installedDir;
    const ExeFolder exe = findExeFolder();
    loc.exeDir = exe.dir;
    // A run isolated for a test, a render or an agent keeps to its own folder, marker or not.
    if (env("SF_USER_DATA_DIR").empty() && hasMarker(exe)) {
        const fs::path data = utf8Path(exe.dir) / "data";
        if (writable(data)) {
            loc.portable = true;
            loc.dir = pathString(data);
        } else {
            loc.readOnly = true;
        }
    }
    g_loc = loc;
    g_key = key;
    g_have = true;
    return g_loc;
}

void resetDataLocation() {
    std::lock_guard lock(g_mutex);
    g_have = false;
}

std::string activeDataDir() { return dataLocation().dir; }

std::string portableCacheDir(const std::string& name, const std::string& perUserCache) {
    const DataLocation& loc = dataLocation();
    if (!loc.portable) return perUserCache;
    return pathString(utf8Path(loc.dir) / "cache" / utf8Path(name));
}

std::string portableFallbackNote() {
    const DataLocation& loc = dataLocation();
    if (!loc.readOnly) return {};
    return "This portable copy can't write to its own folder (read-only?): using this computer's settings for this session";
}

// --- The first-run import ---

namespace {

const char* const kOffered = ".import-offered";

bool skipOnImport(const std::string& name) {
    static const char* const skip[] = {"sculptforge.log", "sculptforge-previous.log", "clean_exit", "update_pending.json", "thumbs",
                                       "tools", "brush-gallery-cache", "blender", "starter-meshes", "audit", "benchmarks",
                                       "hardware_profile.json", "window.json", kOffered};
    for (const char* s : skip)
        if (name == s) return true;
    return name.rfind("autosave", 0) == 0 || name.rfind("featured", 0) == 0;
}

bool hasContent(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (!skipOnImport(pathString(e.path().filename()))) return true;
    return false;
}

} // namespace

bool importOffered() {
    const DataLocation& loc = dataLocation();
    if (!loc.portable) return false;
    std::error_code ec;
    return fs::exists(utf8Path(loc.dir) / kOffered, ec);
}

bool importAvailable() {
    const DataLocation& loc = dataLocation();
    if (!loc.portable || importOffered()) return false;
    std::error_code ec;
    if (fs::exists(utf8Path(loc.dir) / "ui.json", ec)) return false; // (this copy has been used already)
    return hasContent(utf8Path(loc.installedDir));
}

void markImportOffered() {
    const DataLocation& loc = dataLocation();
    if (!loc.portable) return;
    std::ofstream(utf8Path(loc.dir) / kOffered, std::ios::binary | std::ios::trunc) << "The import was offered on this machine.\n";
}

bool importInstalledData(std::string& summary, std::string& error) {
    const DataLocation& loc = dataLocation();
    if (!loc.portable) {
        error = "This is not a portable copy";
        return false;
    }
    const fs::path from = utf8Path(loc.installedDir), to = utf8Path(loc.dir);
    std::error_code ec;
    int files = 0, failed = 0;
    for (const auto& e : fs::directory_iterator(from, ec)) {
        const std::string name = pathString(e.path().filename());
        if (skipOnImport(name)) continue;
        std::error_code cp;
        if (e.is_directory(cp)) {
            fs::create_directories(to / e.path().filename(), cp);
            for (const auto& f : fs::recursive_directory_iterator(e.path(), cp)) {
                std::error_code one;
                const fs::path rel = fs::relative(f.path(), from, one);
                if (one) { ++failed; continue; }
                if (f.is_directory(one)) {
                    fs::create_directories(to / rel, one);
                } else if (f.is_regular_file(one)) {
                    fs::create_directories((to / rel).parent_path(), one);
                    fs::copy_file(f.path(), to / rel, fs::copy_options::skip_existing, one);
                    if (one) ++failed; else ++files;
                }
            }
        } else if (e.is_regular_file(cp)) {
            fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::skip_existing, cp);
            if (cp) ++failed; else ++files;
        }
    }
    // The preferences' library folder (Documents/SculptForge, or one the artist picked) belongs to the installed copy:
    // a portable copy keeps its own library in its data folder unless its owner picks one.
    const fs::path ui = to / "ui.json";
    {
        std::ifstream in(ui, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), {});
        Json j;
        std::string parseError;
        if (!text.empty() && Json::parse(text, j, parseError) && j.isObject() && j["libraryFolder"].isString() &&
            !j["libraryFolder"].asString().empty()) {
            j.set("libraryFolder", "");
            std::ofstream out(ui, std::ios::binary | std::ios::trunc);
            out << j.dump(true);
        }
    }
    summary = "Imported " + std::to_string(files) + " file" + (files == 1 ? "" : "s") + " from this computer's settings";
    if (failed) summary += " (" + std::to_string(failed) + " could not be copied)";
    return true;
}

} // namespace sf
