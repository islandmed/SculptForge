// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ManagedPolicy.h"

#include "core/Json.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sf::policy {

namespace {

Policy gPolicy;
std::once_flag gOnce;

bool isUrl(const std::string& s) { return s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0; }

bool contains(const std::vector<std::string>& v, std::string_view s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool checkKeys(const Json& j, std::initializer_list<const char*> keys, const std::string& where, std::string& error) {
    if (!j.isObject()) return (error = where + " must be an object", false);
    for (const auto& [k, v] : j.members()) {
        bool known = false;
        for (const char* key : keys) known = known || k == key;
        if (!known) return (error = "unknown key \"" + (where.empty() ? k : where + "." + k) + "\"", false);
    }
    return true;
}

bool readLock(const Json& j, const char* key, Lock& out, const std::string& where, std::string& error) {
    if (!j.has(key)) return true;
    if (!j[key].isBool()) return (error = where + "." + key + " must be true or false", false);
    out = j[key].asBool() ? Lock::On : Lock::Off;
    return true;
}

bool readStrings(const Json& j, const char* key, std::vector<std::string>& out, const std::string& where,
                 std::string& error) {
    if (!j.has(key)) return true;
    if (!j[key].isArray()) return (error = where + "." + key + " must be a list of strings", false);
    for (const Json& v : j[key].items()) {
        if (!v.isString() || v.asString().empty()) return (error = where + "." + key + " must be a list of strings", false);
        out.push_back(v.asString());
    }
    return true;
}

// http(s) URL prefixes with no user info. Plain http only to this computer (a local model server, the mock IdP).
bool readUrls(const Json& j, const char* key, std::vector<std::string>& out, const std::string& where, std::string& error,
              bool httpsOnly = false) {
    if (!readStrings(j, key, out, where, error)) return false;
    for (const std::string& u : out) {
        net::UrlParts p;
        if (!isUrl(u) || !net::parseUrl(u, p)) return (error = where + "." + key + ": \"" + u + "\" is not an http(s) URL", false);
        if (httpsOnly && p.scheme != "https" && !net::isLoopbackAddress(p.host))
            return (error = where + "." + key + ": \"" + u + "\" must be https", false);
    }
    return true;
}

std::string envOr(const char* a, const char* b = nullptr) {
    if (const char* v = std::getenv(a); v && *v) return v;
    if (b)
        if (const char* v = std::getenv(b); v && *v) return v;
    return {};
}

// Path-safe: letters, digits, '.', '-', '_' (a user or host name that would climb out of the folder can't).
std::string safePart(std::string s) {
    for (char& c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            c = '_';
    if (s.empty() || s == "." || s == "..") s = "unknown";
    return s;
}

std::string hostName() {
#if defined(_WIN32)
    char buf[256];
    DWORD n = sizeof buf;
    if (GetComputerNameA(buf, &n)) return std::string(buf, n);
    return envOr("COMPUTERNAME");
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) == 0) return buf;
    return {};
#endif
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

Policy failClosed(const std::string& source, const std::string& error) {
    Policy p;
    p.present = true;
    p.broken = true;
    p.source = source;
    p.error = error;
    p.ai = Lock::Off;
    p.network = Lock::Off;
    p.mcp = Lock::Off;
    p.mcpLiveAllowed = false;
    p.updateCheck = Lock::Off;
    return p;
}

} // namespace

const std::vector<std::string>& mcpCategoryNames() {
    static const std::vector<std::string> names = {"info", "edit", "files", "scripts", "network", "ai"};
    return names;
}

const std::vector<std::string>& aiProviderIds() {
    static const std::vector<std::string> ids = {
        // Brush Lab (ai/Providers.h)
        "free", "anthropic", "openai", "ollama", "lmstudio", "vllm", "llamacpp", "litellm", "openrouter", "openai-compatible",
        // Generate 3D (ai/ImageTo3DService.h)
        "huggingface", "fal", "meshy", "hyper3d", "comfyui", "command"};
    return ids;
}

bool parsePolicy(const std::string& text, const std::string& source, Policy& out, std::string& error) {
    Json j;
    if (!Json::parseStrict(text, j, error)) return false;
    if (!checkKeys(j, {"format", "version", "organization", "ai", "network", "mcp", "audit", "signIn", "telemetry", "updates"},
                   "", error))
        return false;
    if (j["format"].asString() != kPolicyFormat) return (error = "format must be \"sculptforge-policy\"", false);
    if (!j["version"].isNumber() || int(j["version"].asNumber()) != kPolicyVersion) return (error = "unsupported version (1 is current)", false);
    Policy p;
    p.present = true;
    p.source = source;
    p.organization = j["organization"].asString();
    if (p.organization.size() > 80) return (error = "organization: at most 80 characters", false);

    if (j.has("ai")) {
        const Json& a = j["ai"];
        if (!checkKeys(a, {"enabled", "providers", "endpoints", "requireSignIn"}, "ai", error)) return false;
        if (!readLock(a, "enabled", p.ai, "ai", error) || !readStrings(a, "providers", p.aiProviders, "ai", error) ||
            !readUrls(a, "endpoints", p.aiEndpoints, "ai", error))
            return false;
        for (const std::string& id : p.aiProviders)
            if (!contains(aiProviderIds(), id)) return (error = "ai.providers: unknown provider \"" + id + "\"", false);
        if (a.has("requireSignIn") && !a["requireSignIn"].isBool()) return (error = "ai.requireSignIn must be true or false", false);
        p.requireSignIn = a["requireSignIn"].asBool(false);
    }
    if (j.has("network")) {
        const Json& n = j["network"];
        if (!checkKeys(n, {"enabled", "allow", "localNetwork"}, "network", error)) return false;
        if (!readLock(n, "enabled", p.network, "network", error) || !readUrls(n, "allow", p.networkAllow, "network", error))
            return false;
        if (n.has("localNetwork") && !n["localNetwork"].isBool()) return (error = "network.localNetwork must be true or false", false);
        p.localNetwork = n["localNetwork"].asBool(false);
    }
    if (j.has("mcp")) {
        const Json& m = j["mcp"];
        if (!checkKeys(m, {"enabled", "live", "categories"}, "mcp", error)) return false;
        if (!readLock(m, "enabled", p.mcp, "mcp", error) || !readStrings(m, "categories", p.mcpCategories, "mcp", error))
            return false;
        for (const std::string& c : p.mcpCategories)
            if (!contains(mcpCategoryNames(), c)) return (error = "mcp.categories: unknown category \"" + c + "\"", false);
        if (m.has("live")) {
            const std::string live = m["live"].asString();
            if (live != "never" && live != "allowed") return (error = "mcp.live must be \"never\" or \"allowed\"", false);
            p.mcpLiveAllowed = live == "allowed";
        }
        if (p.mcp == Lock::Off) p.mcpLiveAllowed = false;
    }
    if (j.has("audit")) {
        const Json& a = j["audit"];
        if (!checkKeys(a, {"path", "maxMB", "keep", "required"}, "audit", error)) return false;
        if (a.has("path") && (!a["path"].isString() || a["path"].asString().empty()))
            return (error = "audit.path must be a file path", false);
        p.auditPath = a["path"].asString();
        if (a.has("maxMB")) {
            if (!a["maxMB"].isNumber() || a["maxMB"].asNumber() < 0.1 || a["maxMB"].asNumber() > 4096)
                return (error = "audit.maxMB: 0.1 to 4096", false);
            p.auditMaxMB = a["maxMB"].asNumber();
        }
        if (a.has("keep")) {
            if (!a["keep"].isNumber() || a["keep"].asNumber() < 0 || a["keep"].asNumber() > 1000)
                return (error = "audit.keep: 0 to 1000 old files", false);
            p.auditKeep = int(a["keep"].asNumber());
        }
        if (a.has("required") && !a["required"].isBool()) return (error = "audit.required must be true or false", false);
        p.auditRequired = a["required"].asBool(false);
    }
    if (j.has("signIn")) {
        const Json& s = j["signIn"];
        if (!checkKeys(s, {"issuer", "clientId", "clientSecret", "scopes", "audience", "useFor"}, "signIn", error)) return false;
        p.issuer = s["issuer"].asString();
        p.clientId = s["clientId"].asString();
        p.audience = s["audience"].asString();
        p.clientSecret = s["clientSecret"].asString();
        net::UrlParts u;
        if (!isUrl(p.issuer) || !net::parseUrl(p.issuer, u) || (u.scheme != "https" && !net::isLoopbackAddress(u.host)))
            return (error = "signIn.issuer must be the identity provider's https URL", false);
        if (p.clientId.empty() || p.clientId.size() > 256) return (error = "signIn.clientId: the app's client id", false);
        if (!readStrings(s, "scopes", p.scopes, "signIn", error) || !readUrls(s, "useFor", p.signInUseFor, "signIn", error, true))
            return false;
        for (const std::string& sc : p.scopes)
            if (sc.find_first_of(" \t\r\n\"") != std::string::npos) return (error = "signIn.scopes: one scope per entry", false);
    }
    if (p.requireSignIn && !p.signInConfigured()) return (error = "ai.requireSignIn needs signIn.issuer and signIn.clientId", false);
    if (j.has("telemetry")) {
        // SculptForge has no telemetry. The key exists so an IT checklist can state it; anything but "none" is a mistake.
        const Json& t = j["telemetry"];
        if (!((t.isString() && (t.asString() == "none" || t.asString() == "off")) || (t.isBool() && !t.asBool())))
            return (error = "telemetry: SculptForge has no telemetry; the only value is \"none\"", false);
    }
    if (j.has("updates")) {
        const Json& u = j["updates"];
        if (!checkKeys(u, {"check", "channel", "feed", "mirror"}, "updates", error)) return false;
        if (!readLock(u, "check", p.updateCheck, "updates", error)) return false;
        p.updateChannel = u["channel"].asString();
        if (u.has("channel") && p.updateChannel != "stable" && p.updateChannel != "preview")
            return (error = "updates.channel must be \"stable\" or \"preview\"", false);
        p.updateFeed = u["feed"].asString();
        p.updateMirror = u["mirror"].asString();
        net::UrlParts parts;
        if (!p.updateFeed.empty() && (!isUrl(p.updateFeed) || !net::parseUrl(p.updateFeed, parts)))
            return (error = "updates.feed must be an http(s) URL", false);
        if (!p.updateMirror.empty() && isUrl(p.updateMirror) && !net::parseUrl(p.updateMirror, parts))
            return (error = "updates.mirror must be an http(s) URL or a file", false);
    }
    out = std::move(p);
    return true;
}

std::string systemPolicyPath() {
#if defined(_WIN32)
    const std::string base = envOr("ProgramData", "ALLUSERSPROFILE");
    return (base.empty() ? std::string("C:\\ProgramData") : base) + "\\SculptForge\\policy.json";
#elif defined(__APPLE__)
    return "/Library/Application Support/SculptForge/policy.json";
#else
    return "/etc/sculptforge/policy.json";
#endif
}

Policy loadPolicyFile(const std::string& path, const std::string& origin, std::vector<std::string>* messages) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (messages) messages->push_back("Policy " + origin + " can't be read: AI, network features and MCP are off");
        return failClosed(origin, "can't be read");
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string error;
    Policy p;
    if (text.size() > (1u << 20)) error = "larger than 1 MB";
    else if (parsePolicy(text, origin, p, error)) {
#if !defined(_WIN32)
        // A system policy anyone can edit locks nothing: say so (it still applies).
        struct stat st{};
        if (stat(path.c_str(), &st) == 0 && (st.st_mode & 0022) && messages)
            messages->push_back("Policy " + path + " is writable by other users: make it writable by administrators only");
#endif
        return p; // (describe() says what it locks: sculptforge-cli --policy, Preferences > AI)
    }
    if (messages) messages->push_back("Policy " + origin + ": " + error + ". AI, network features and MCP are off until it is fixed");
    return failClosed(origin, error);
}

const Policy& startPolicy(std::vector<std::string>* messages) {
    std::call_once(gOnce, [messages] {
        const std::string system = systemPolicyPath();
        const std::string env = envOr("SF_POLICY");
        if (std::ifstream(system, std::ios::binary)) {
            if (!env.empty() && messages) messages->push_back("Policy: SF_POLICY is ignored while " + system + " is installed");
            gPolicy = loadPolicyFile(system, system, messages);
        } else if (!env.empty()) {
            gPolicy = loadPolicyFile(env, "SF_POLICY=" + env, messages);
        }
    });
    return gPolicy;
}

const Policy& current() { return gPolicy; }

void setPolicyForTests(const Policy& p) { gPolicy = p; }
void resetPolicyForTests() { gPolicy = Policy{}; }

std::string managedBy() {
    return gPolicy.organization.empty() ? std::string(kManagedLabel) : "Managed by " + gPolicy.organization;
}

bool aiProviderAllowed(std::string_view providerId, std::string* why) {
    if (gPolicy.ai == Lock::Off) {
        if (why) *why = "AI features are turned off by your studio's policy";
        return false;
    }
    if (gPolicy.aiProviders.empty() || contains(gPolicy.aiProviders, providerId)) return true;
    if (why) *why = "Your studio's policy doesn't allow this AI provider (" + std::string(providerId) + ")";
    return false;
}

bool aiEndpointAllowed(const std::string& url, std::string* why) {
    if (gPolicy.ai == Lock::Off) {
        if (why) *why = "AI features are turned off by your studio's policy";
        return false;
    }
    if (gPolicy.aiEndpoints.empty()) return true;
    for (const std::string& prefix : gPolicy.aiEndpoints)
        if (net::urlUnderPrefix(url, prefix)) return true;
    if (why) *why = "Your studio's policy doesn't allow AI requests to this address";
    return false;
}

bool mcpAllowed(std::string* why) {
    if (gPolicy.mcp != Lock::Off) return true;
    if (why) *why = "AI tools (MCP) are turned off by your studio's policy";
    return false;
}

bool mcpLiveAllowed(std::string* why) {
    if (gPolicy.mcp != Lock::Off && gPolicy.mcpLiveAllowed) return true;
    if (why) *why = "Your studio's policy doesn't let AI tools control the app window";
    return false;
}

std::vector<std::string> mcpCategories(std::string_view name, const std::vector<std::string>& paramNames) {
    static const char* kNetwork[] = {"brush_gallery_search", "brush_gallery_install", "unreal_editors", "unreal_send",
                                     "export_unreal"};
    static const char* kScripts[] = {"macro_run", "run_script", "python"};
    static const char* kBridges[] = {"bridge_send", "bridge_receive", "bridge_import", "bridge_export",
                                     "blender_send", "blender_receive", "maya_send",     "maya_receive"};
    static const char* kAi[] = {"brush_ai", "image_to_3d", "text_to_3d", "comfyui_check"};
    auto in = [&](const auto& list) {
        for (const char* n : list)
            if (name == n) return true;
        return false;
    };
    std::vector<std::string> c;
    if (in(kAi)) c.push_back("ai"), c.push_back("network");
    if (in(kNetwork)) c.push_back("network");
    if (in(kScripts)) c.push_back("scripts");
    bool files = in(kBridges);
    for (const std::string& p : paramNames) // (link/AiLinkSession.cpp isPathParam: the same names)
        files = files || p == "path" || p == "file" || p == "folder" || p == "out" || p == "dir" || p == "directory" ||
                p == "output";
    if (files) c.push_back("files");
    // Looking (the viewport as an image, the camera) reads the scene; stroke_screen edits it.
    const bool info = name == "mesh_info" || name.ends_with("_info") || name.ends_with("_list") || name == "brush_library" ||
                      name == "viewport_image" || name == "view_turntable" || name == "view_set";
    if (c.empty()) c.push_back(info ? "info" : "edit");
    return c;
}

bool mcpToolAllowed(std::string_view name, const std::vector<std::string>& paramNames, std::string* why) {
    if (!mcpAllowed(why)) return false;
    if (gPolicy.mcpCategories.empty()) return true;
    for (const std::string& c : mcpCategories(name, paramNames))
        if (!contains(gPolicy.mcpCategories, c)) {
            if (why) *why = "Your studio's policy doesn't allow AI tools to use " + std::string(name) + " (" + c + ")";
            return false;
        }
    return true;
}

std::string describe(const Policy& p) {
    if (!p.present) return "no policy: nothing is managed";
    if (p.broken) return "broken (" + p.error + "): AI, network features and MCP are off";
    std::ostringstream o;
    auto lock = [](Lock l) { return l == Lock::Off ? "off" : l == Lock::On ? "on" : "artist's choice"; };
    auto list = [](const std::vector<std::string>& v, const char* none) {
        if (v.empty()) return std::string(none);
        std::string s;
        for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
        return s;
    };
    if (!p.organization.empty()) o << p.organization << ": ";
    o << "AI " << lock(p.ai);
    if (p.ai != Lock::Off) {
        o << " (providers: " << list(p.aiProviders, "any") << "; endpoints: " << list(p.aiEndpoints, "any");
        if (p.requireSignIn) o << "; sign-in required";
        o << ")";
    }
    o << "; network " << (p.network == Lock::Off ? "off (allowed: " + list(p.networkAllow, "nothing") + ")" : "on");
    o << "; MCP " << (p.mcp == Lock::Off ? "off" : "on (tools: " + list(p.mcpCategories, "all") + "; live mode " +
                                                        (p.mcpLiveAllowed ? "allowed" : "never") + ")");
    o << "; audit log " << (p.auditPath.empty() ? "default path" : p.auditPath) << (p.auditRequired ? " (required)" : "");
    if (p.signInConfigured()) o << "; sign-in " << p.issuer;
    o << "; telemetry none (SculptForge has none)";
    o << "; updates " << (p.updateCheck == Lock::Off ? "off" : p.updateChannel.empty() ? "artist's choice" : p.updateChannel);
    if (!p.updateFeed.empty()) o << " from " << p.updateFeed;
    return o.str();
}

std::string expandPathTemplate(const std::string& path) {
    std::string out = path;
    if (out.find('{') == std::string::npos) return out;
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char date[16];
    std::strftime(date, sizeof date, "%Y-%m-%d", &tm);
    replaceAll(out, "{user}", safePart(envOr("USER", "USERNAME")));
    replaceAll(out, "{host}", safePart(hostName()));
    replaceAll(out, "{date}", date);
    return out;
}

} // namespace sf::policy
