// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ScriptPath.h"

#include "core/Utf8Path.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace sf {
namespace fs = std::filesystem;

namespace {
std::string lowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
} // namespace

std::vector<ScriptFile> listScripts(const std::string& scriptsDir, const std::vector<std::string>& searchPath,
                                    std::vector<std::string>* skipped) {
    std::vector<ScriptFile> out;
    std::set<std::string> names;   // lower-case file names listed so far
    std::set<fs::path> folders;    // folders read (canonical)
    auto add = [&](const std::string& dir, bool fromSearchPath) {
        if (dir.empty()) return;
        std::error_code ec;
        const fs::path d = pathFromUtf8(dir);
        const fs::path canon = fs::canonical(d, ec);
        if (ec || !fs::is_directory(canon, ec)) {
            if (fromSearchPath && skipped && std::find(skipped->begin(), skipped->end(), dir) == skipped->end()) skipped->push_back(dir);
            return;
        }
        if (!folders.insert(canon).second) return;
        std::vector<std::string> found;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = fileNameUtf8(pathToUtf8(it->path()));
            if (name.empty() || name[0] == '.' || name[0] == '_') continue; // hidden files and helper modules (_util.py)
            if (lowerAscii(extensionUtf8(name)) == ".py" && it->is_regular_file(ec)) found.push_back(pathToUtf8(it->path()));
        }
        if (ec && fromSearchPath && skipped) skipped->push_back(dir); // read part of it, then failed
        std::sort(found.begin(), found.end());
        for (std::string& p : found)
            if (names.insert(lowerAscii(fileNameUtf8(p))).second) out.push_back({std::move(p), fromSearchPath});
    };
    add(scriptsDir, false);
    for (const std::string& dir : searchPath) add(dir, true);
    return out;
}

} // namespace sf
