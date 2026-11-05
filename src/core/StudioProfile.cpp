// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/StudioProfile.h"

#include "core/Json.h"
#include "core/Pack.h"
#include "core/UpdateCheck.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace fs = std::filesystem;

namespace {

// UTF-8 strings <-> paths (Windows paths are UTF-16).
fs::path fsp(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string str(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

bool idOk(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

bool isUrl(const std::string& s) { return s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0; }

// A relative path inside DIR (no absolute path, no "..").
bool relativeOk(const std::string& p) {
    if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos) return false;
    for (const auto& part : fsp(p))
        if (part == "..") return false;
    return true;
}

std::string resolve(const std::string& dir, const std::string& p) {
    return dir.empty() ? p : str((fsp(dir) / fsp(p)).lexically_normal());
}

StudioStart gStudio;

} // namespace

bool parseStudioProfile(const std::string& text, const std::string& dir, StudioProfile& out, std::string& error) {
    Json j;
    if (!Json::parseStrict(text, j, error)) return false;
    if (!j.isObject()) return (error = "a studio profile is a JSON object", false);
    static const char* kKeys[] = {"format", "version", "id", "name", "offline", "updates", "allow", "localNetwork",
                                  "ai", "packs", "brushPaths", "preferences", "branding"};
    for (const auto& [k, v] : j.members()) {
        bool known = false;
        for (const char* key : kKeys) known = known || k == key;
        if (!known) return (error = "unknown key \"" + k + "\"", false);
    }
    if (j["format"].asString() != kStudioProfileFormat) return (error = "format must be \"sculptforge-studio\"", false);
    if (int(j["version"].asNumber()) != kStudioProfileVersion) return (error = "unsupported version", false);
    StudioProfile p;
    p.dir = dir;
    p.id = j["id"].asString();
    p.name = j["name"].asString();
    if (!idOk(p.id)) return (error = "id: lower-case letters, digits, '-' and '_'", false);
    if (p.name.empty() || p.name.size() > 80) return (error = "name: 1 to 80 characters", false);
    p.offline = j["offline"].asBool(true);
    p.ai = j["ai"].asBool(false);
    p.localNetwork = j["localNetwork"].asBool(false);
    auto location = [&](const std::string& s, std::string& to, const char* what) {
        if (s.empty()) return true;
        net::UrlParts u;
        if (isUrl(s)) {
            if (!net::parseUrl(s, u)) return (error = std::string(what) + ": not a plain http(s) URL", false);
            to = s;
        } else {
            to = fsp(s).is_absolute() ? s : resolve(dir, s);
        }
        return true;
    };
    if (!location(j["updates"]["feed"].asString(), p.updateFeed, "updates.feed") ||
        !location(j["updates"]["mirror"].asString(), p.updateMirror, "updates.mirror"))
        return false;
    for (const Json& a : j["allow"].items()) {
        net::UrlParts u;
        if (!net::parseUrl(a.asString(), u)) return (error = "allow: \"" + a.asString() + "\" is not an http(s) URL", false);
        p.allow.push_back(a.asString());
    }
    for (const Json& a : j["packs"].items()) {
        if (!relativeOk(a.asString())) return (error = "packs: paths stay inside the profile's folder", false);
        p.packs.push_back(resolve(dir, a.asString()));
    }
    for (const Json& a : j["brushPaths"].items()) {
        if (a.asString().empty()) return (error = "brushPaths: empty path", false);
        p.brushPaths.push_back(a.asString());
    }
    if (const std::string& pr = j["preferences"].asString(); !pr.empty()) {
        if (!relativeOk(pr)) return (error = "preferences: paths stay inside the profile's folder", false);
        p.preferences = resolve(dir, pr);
    }
    if (const std::string& logo = j["branding"]["logo"].asString(); !logo.empty()) {
        if (!relativeOk(logo)) return (error = "branding.logo: paths stay inside the profile's folder", false);
        p.logo = resolve(dir, logo);
    }
    p.title = j["branding"]["title"].asString();
    out = std::move(p);
    return true;
}

bool loadStudioProfile(const std::string& path, StudioProfile& out, std::string& error) {
    std::ifstream f(fsp(path), std::ios::binary);
    if (!f) return (error = "can't read " + path, false);
    const std::string text{std::istreambuf_iterator<char>(f), {}};
    if (!parseStudioProfile(text, str(fsp(path).parent_path()), out, error)) return false;
    out.source = path;
    return true;
}

net::NetworkPolicy studioNetworkPolicy(const StudioProfile& p) {
    net::NetworkPolicy n;
    n.offline = p.offline || isStudioBuild();
    n.localNetwork = p.localNetwork;
    for (const std::string* u : {&p.updateFeed, &p.updateMirror})
        if (isUrl(*u)) n.allow.push_back(*u);
    n.allow.insert(n.allow.end(), p.allow.begin(), p.allow.end());
    return n;
}

bool isStudioBuild() { return *builtInStudioProfile() != '\0'; }

std::string findInstalledStudioDir(const std::string& exeDir) {
    if (exeDir.empty()) return {};
    std::error_code ec;
    fs::path dir = fsp(exeDir);
    for (int up = 0; up < 4; ++up) {
        for (const char* sub : {"studio", "share/sculptforge/studio", "Resources/studio"})
            if (fs::is_regular_file(dir / sub / "studio.json", ec)) return str((dir / sub).lexically_normal());
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

StudioStart startStudioMode(const std::string& exeDir, const std::string& overridePath, bool forceOffline) {
    StudioStart s;
    std::string error;
    const char* envPath = std::getenv("SF_STUDIO_PROFILE");
    const char* envOffline = std::getenv("SF_OFFLINE");
    const std::string installed = findInstalledStudioDir(exeDir);
    bool found = false;
    if (const std::string p = !overridePath.empty() ? overridePath : (envPath ? envPath : ""); !p.empty()) {
        found = loadStudioProfile(p, s.profile, error);
        if (!found) s.messages.push_back("Studio profile " + p + ": " + error);
    } else if (isStudioBuild()) {
        found = parseStudioProfile(builtInStudioProfile(), installed, s.profile, error);
        if (found) s.profile.source = "built in";
        else s.messages.push_back("Built-in studio profile: " + error);
    } else if (!installed.empty()) {
        found = loadStudioProfile(installed + "/studio.json", s.profile, error);
        if (!found) s.messages.push_back("Studio profile " + installed + "/studio.json: " + error);
    }
    net::NetworkPolicy policy;
    if (found) policy = studioNetworkPolicy(s.profile);
    // Fail closed: a studio build, --offline or SF_OFFLINE=1 is offline even without a usable profile.
    if (isStudioBuild() || forceOffline || (envOffline && std::string(envOffline) == "1")) policy.offline = true;
    s.active = policy.offline;
    net::setNetworkPolicy(policy);
    if (found && !s.profile.brushPaths.empty()) { // read-only shares, ahead of any already set
        std::string value;
#if defined(_WIN32)
        const char sep = ';';
#else
        const char sep = ':';
#endif
        for (const std::string& d : s.profile.brushPaths) value += (value.empty() ? "" : std::string(1, sep)) + d;
        if (const char* old = std::getenv("SCULPTFORGE_BRUSH_PATH"); old && *old) value += sep + std::string(old);
#if defined(_WIN32)
        _putenv_s("SCULPTFORGE_BRUSH_PATH", value.c_str());
#else
        setenv("SCULPTFORGE_BRUSH_PATH", value.c_str(), 1);
#endif
    }
    if (found) installStudioPacks(s.profile, s.messages);
    gStudio = s;
    return s;
}

void installStudioPacks(const StudioProfile& p, std::vector<std::string>& messages) {
    if (packsSafeMode()) return; // --no-packs
    std::vector<InstalledPack> installed = listInstalledPacks();
    for (const std::string& path : p.packs) {
        PackReview review;
        std::string error;
        if (!reviewPack(path, review, error)) {
            messages.push_back("Studio pack " + path + ": " + error);
            continue;
        }
        bool current = false;
        for (const InstalledPack& i : installed)
            current = current || (i.manifest.id == review.manifest.id && i.manifest.version == review.manifest.version);
        if (current) continue; // installed already, this version
        if (review.needsNewerApp) {
            messages.push_back("Studio pack " + path + " needs a newer SculptForge");
            continue;
        }
        if (!installPack(review, error)) messages.push_back("Studio pack " + path + ": " + error);
    }
}

const StudioStart& studioMode() { return gStudio; }

std::string updateFeedSource(const StudioStart& s, const char* envFeed, std::string& fallback) {
    fallback.clear();
    if (s.active) { // only the studio's own channel, then its mirror
        if (s.profile.updateFeed.empty()) return s.profile.updateMirror;
        fallback = s.profile.updateMirror;
        return s.profile.updateFeed;
    }
    if (envFeed && *envFeed) return envFeed;
    fallback = kUpdateFeedMirrorUrl;
    return kUpdateFeedUrl;
}

} // namespace sf
