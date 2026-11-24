// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Utf8Path.h"

#include <cstdio>
#include <exception>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sf {

int utf8CodePageSelfTest() {
#if defined(_WIN32)
    const unsigned acp = GetACP();
    // The word "test" in katakana: three characters, nine UTF-8 bytes, invalid or different in most legacy code pages.
    const std::string name = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.glb";
    bool kept = false;
    std::string what;
    try {
        kept = pathToUtf8(std::filesystem::path(name)) == name; // the narrow constructor, as old code used it
    } catch (const std::exception& e) { // (a Japanese code page: the bytes aren't valid in it; gh#223 closed the app on this)
        what = e.what();
    }
    std::printf("acp=%u narrow-path-keeps-utf8=%d%s%s\n", acp, kept ? 1 : 0, what.empty() ? "" : " threw: ", what.c_str());
    return acp == 65001 && kept ? 0 : 1;
#else
    std::printf("acp=utf-8 (not Windows)\n");
    return 0;
#endif
}

} // namespace sf
