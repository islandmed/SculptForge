// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Python module for studio pipelines (`import sculptforge`, without the app): the API in Bindings.cpp, running
// commands itself. It is sculptforge._core; the package around it is python/sculptforge (pip install sculptforge).
#include "python/Bindings.h"

#include "core/AuditLog.h"
#include "core/FileReplace.h"
#include "core/StudioProfile.h"
#include "ai/AiPolicy.h"

#include <pybind11/pybind11.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace py = pybind11;
namespace fs = std::filesystem;

namespace {

fs::path u8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string str(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// The commands that write the file named by `path` (sf.commands(): saves and exports).
bool writesPath(const std::string& name) {
    return name.starts_with("save") || name.starts_with("export_") || name.ends_with("_save") ||
           name.find("_export") != std::string::npos || name == "pack_create" || name == "capture_alpha" ||
           name == "face_take_bake";
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

// A save or export writes into a new hidden folder beside the target, and each finished file then replaces its
// target in one rename (core/FileReplace.h): an engine crash or a failed export never leaves a half-written file
// where the user's was. Every file the command wrote moves (an OBJ's .mtl, export_lods' levels). Where no folder can
// be made there (a missing or read-only folder), the command runs as asked and reports that itself.
sf::CommandResult executeSafely(sf::Document& doc, const std::string& name, const sf::Args& args) {
    const auto& core = sf::python::directCoreCalls();
    const auto it = args.find("path");
    if (!writesPath(name) || it == args.end() || !std::holds_alternative<std::string>(it->second))
        return core.execute(doc, name, args);
    std::error_code ec;
    const fs::path target = fs::absolute(u8(std::get<std::string>(it->second)), ec);
    if (ec || target.filename().empty()) return core.execute(doc, name, args);
    static std::atomic<unsigned> counter{0};
    const fs::path temp = target.parent_path() /
        (".sculptforge-write-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(++counter));
    if (!fs::create_directory(temp, ec) || ec) return core.execute(doc, name, args);
    sf::Args redirected = args;
    redirected["path"] = str(temp / target.filename());
    sf::CommandResult r = core.execute(doc, name, redirected);
    std::vector<fs::path> written; // listed first: files never move while the folder is being walked
    if (r.ok)
        for (auto f = fs::recursive_directory_iterator(temp, ec); !ec && f != fs::recursive_directory_iterator(); f.increment(ec))
            if (f->is_regular_file(ec)) written.push_back(f->path());
    for (const fs::path& file : written) {
        const fs::path dest = target.parent_path() / file.lexically_relative(temp);
        fs::create_directories(dest.parent_path(), ec);
        std::string err;
        if (!sf::replaceFile(file, dest, err)) { // (files already moved are whole; this one and the rest are not written)
            r = {false, err};
            break;
        }
    }
    fs::remove_all(temp, ec);
    for (const bool generic : {false, true}) { // a message may name the path either way ('\\' or '/' on Windows)
        auto s = [&](const fs::path& p) {
            if (!generic) return str(p);
            const std::u8string g = p.generic_u8string();
            return std::string(g.begin(), g.end());
        };
        replaceAll(r.message, s(temp / target.filename()), s(target));
        replaceAll(r.message, s(temp), s(target.parent_path()));
    }
    return r;
}

} // namespace

PYBIND11_MODULE(_core, m) {
    // Studio mode (SF_STUDIO_PROFILE, SF_OFFLINE=1 or a studio build; core/StudioProfile.h), as in the app.
    sf::ai::setStudioAiAllowed(sf::startStudioMode("", "", false).profile.ai);
    sf::audit::setClient("python"); // (the AI audit log, core/AuditLog.h)
    auto api = std::make_shared<sf::python::Api>();
    api->core = &sf::python::directCoreCalls();
    api->execute = executeSafely;
    api->registry = api->core->registry;
    sf::python::bind(m, std::move(api), "SculptForge: free, open-source digital sculpting - Python API for pipelines");
}
