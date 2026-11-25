// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/UiTestSeed.h"

#include "core/Json.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace sf::uitest {

namespace {

bool inside(const std::string& dir, const std::string& root) { // dir is root or below it (lexically)
    if (dir.empty() || root.empty()) return false;
    std::error_code ec;
    const std::filesystem::path d = std::filesystem::absolute(dir, ec).lexically_normal();
    const std::filesystem::path r = std::filesystem::absolute(root, ec).lexically_normal();
    const std::string ds = d.generic_string(), rs = r.generic_string();
    const std::string base = rs.size() > 1 && rs.back() == '/' ? rs.substr(0, rs.size() - 1) : rs;
    return ds == base || ds.compare(0, base.size() + 1, base + "/") == 0;
}

} // namespace

std::string seedPrefs(const std::string& script, const std::string& isolatedDir, const std::string& userDataDir,
                      const char* isolateEnv, const char* version) {
    if (script.empty()) return "no script";
    std::ifstream f(script);
    std::string line, json;
    bool found = false;
    for (int n = 0; n < 20 && std::getline(f, line); ++n) {
        if (line.compare(0, std::strlen(kPrefsTag), kPrefsTag) == 0) {
            json = line.substr(std::strlen(kPrefsTag));
            found = true;
            break;
        }
    }
    if (!found) return "none";
    // The guards: an isolated profile only (never the user's own settings), and not when isolation is switched off.
    if (isolatedDir.empty()) return "refused: the run is not isolated (no --ui-test profile)";
    if (isolateEnv && std::strcmp(isolateEnv, "0") == 0) return "refused: SF_ISOLATE=0";
    if (!inside(userDataDir, isolatedDir)) return "refused: the settings folder is not inside the isolated profile";
    Json prefs;
    std::string err;
    if (!Json::parseStrict(json, prefs, err) || !prefs.isObject()) return "refused: '# prefs:' is not a JSON object (" + err + ")";
    if (version && *version && !prefs.has("last_seen_version")) prefs.set("last_seen_version", std::string(version));
    if (!prefs.has("new_look_card_done")) prefs.set("new_look_card_done", true);
    std::ofstream out(userDataDir + "project.json", std::ios::binary);
    if (!(out << prefs.dump())) return "refused: project.json not written";
    return std::string();
}

} // namespace sf::uitest
