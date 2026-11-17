// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DataDir.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace sf {

// The user data folder (settings, the log, imgui.ini), with its trailing separator; "" when there is none. It is
// SDL_GetPrefPath's (or a portable copy's data/ folder: core/DataDir.h), unless SF_USER_DATA_DIR names another: isolateSettings (main.cpp) sets it for UI tests, renders
// and agent runs. On Windows SDL asks the shell for %APPDATA% and ignores the variable isolateSettings set, so every
// Windows UI test shared the runner's real settings and read the ones an earlier test left (0.11.2: support_card
// found another test's "supporter").
inline std::string userDataDir() {
#if defined(_WIN32)
    const wchar_t* over = _wgetenv(L"SF_USER_DATA_DIR"); // (wide: getenv gives the ANSI code page, not UTF-8)
#else
    const char* over = std::getenv("SF_USER_DATA_DIR");
#endif
    if (over && *over) {
#if defined(_WIN32)
        const std::filesystem::path base(over);
#else
        const std::filesystem::path base(std::u8string(over, over + std::strlen(over)));
#endif
        const std::filesystem::path dir = base / "SculptForge" / "SculptForge";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return std::string();
        const std::u8string s = (dir / "").u8string(); // (the trailing separator, as SDL_GetPrefPath has it)
        return std::string(s.begin(), s.end());
    }
    const DataLocation& where = dataLocation(); // a portable copy (core/DataDir.h) keeps everything in its own data/ folder
    if (where.portable) {
        const std::u8string s = (std::filesystem::path(std::u8string(where.dir.begin(), where.dir.end())) / "").u8string();
        return std::string(s.begin(), s.end());
    }
    std::string dir;
    if (char* pref = SDL_GetPrefPath("SculptForge", "SculptForge")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

} // namespace sf
