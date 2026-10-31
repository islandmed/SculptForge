// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Trash.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace sf {

namespace {
namespace fs = std::filesystem;
fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
} // namespace

#if defined(_WIN32)

bool trashSupported() { return true; }

bool moveToTrash(const std::string& path, std::string& where, std::string& error) {
    std::error_code ec;
    const fs::path full = fs::absolute(utf8Path(path), ec);
    if (ec || !fs::exists(full, ec)) {
        error = "No file " + path;
        return false;
    }
    std::wstring from = full.wstring();
    from.push_back(L'\0'); // the list ends with two NULs
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted || fs::exists(full, ec)) {
        error = "The Recycle Bin didn't take " + path;
        return false;
    }
    where = "the Recycle Bin";
    return true;
}

#elif defined(__APPLE__)

bool trashSupported() { return false; }

bool moveToTrash(const std::string&, std::string&, std::string& error) {
    error = "Moving files to the Trash isn't supported on macOS yet";
    return false;
}

#else

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// The trashinfo Path: an absolute path, percent-encoded except unreserved characters and '/'.
std::string encodePath(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

} // namespace

bool trashSupported() { return !env("XDG_DATA_HOME").empty() || !env("HOME").empty(); }

bool moveToTrash(const std::string& path, std::string& where, std::string& error) {
    const std::string data = env("XDG_DATA_HOME").empty() ? env("HOME") + "/.local/share" : env("XDG_DATA_HOME");
    if (data.empty() || data == "/.local/share") {
        error = "No home folder for the trash";
        return false;
    }
    std::error_code ec;
    const fs::path file = fs::absolute(utf8Path(path), ec);
    if (ec || !fs::is_regular_file(file, ec)) {
        error = "No file " + path;
        return false;
    }
    const fs::path trash = utf8Path(data) / "Trash", files = trash / "files", info = trash / "info";
    fs::create_directories(files, ec);
    fs::create_directories(info, ec);
    if (ec) {
        error = "Can't create the trash folder " + pathString(trash) + ": " + ec.message();
        return false;
    }
    // A name free in both folders: "Gold.png", then "Gold.2.png", "Gold.3.png" ...
    const std::string stem = pathString(file.stem()), ext = pathString(file.extension());
    std::string name = stem + ext;
    for (int n = 2; fs::exists(files / utf8Path(name), ec) || fs::exists(info / utf8Path(name + ".trashinfo"), ec); ++n)
        name = stem + "." + std::to_string(n) + ext;
    char date[32];
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &local);
    const fs::path infoFile = info / utf8Path(name + ".trashinfo");
    {
        std::ofstream f(infoFile, std::ios::binary | std::ios::trunc);
        f << "[Trash Info]\nPath=" << encodePath(pathString(file)) << "\nDeletionDate=" << date << "\n";
        if (!f) {
            error = "Can't write " + pathString(infoFile);
            return false;
        }
    }
    fs::rename(file, files / utf8Path(name), ec); // fails across drives: the home trash is for files on its drive
    if (ec) {
        fs::remove(infoFile, ec);
        error = "Can't move " + path + " to the trash (" + ec.message() + ")";
        return false;
    }
    where = "the trash";
    return true;
}

#endif

} // namespace sf
