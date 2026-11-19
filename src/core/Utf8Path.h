// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File names are UTF-8 std::string everywhere in the app. On Windows a name outside the system code page goes wrong
// two ways: std::filesystem::path(std::string) reads the bytes as that code page (a garbled name on a Western install,
// an exception where the bytes aren't valid in it, as on a Japanese system), and path::string() converts back to it
// (a '?' name, or an exception in some standard libraries). Nothing caught the exception, so the app closed with no
// message (gh#223). So: a std::string name goes in through pathFromUtf8 and comes out through pathToUtf8 (or
// fileNameUtf8 / stemUtf8 / extensionUtf8 for a piece of it); never path(std::string) or path::string() on a name
// that came from a user, a dialog, a drop or a project.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace sf {

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// "テスト.glb" from "C:/Models/テスト.glb"; "" for a folder path ending in a separator.
inline std::string fileNameUtf8(std::string_view utf8Path) { return pathToUtf8(pathFromUtf8(utf8Path).filename()); }
inline std::string stemUtf8(std::string_view utf8Path) { return pathToUtf8(pathFromUtf8(utf8Path).stem()); }
// ".glb" (as written: lower-case it for a comparison).
inline std::string extensionUtf8(std::string_view utf8Path) { return pathToUtf8(pathFromUtf8(utf8Path).extension()); }
inline std::string parentUtf8(std::string_view utf8Path) { return pathToUtf8(pathFromUtf8(utf8Path).parent_path()); }

// `--acp-selftest` (the app and the command-line program): on Windows 0 when the process code page is UTF-8, as the
// manifest asks (packaging/windows/sculptforge.manifest), and a narrow std::filesystem::path of a Japanese name keeps
// its bytes; elsewhere 0. Prints what it found.
int utf8CodePageSelfTest();

} // namespace sf
