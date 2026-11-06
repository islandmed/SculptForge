// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AuditLog.h"

#include "core/BrushLibrary.h"
#include "core/ManagedPolicy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace sf::audit {

namespace fs = std::filesystem;

namespace {

std::mutex gMutex;
std::string gClient = "app";
std::string gTestPath;

fs::path fsp(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// Printable ASCII only, cut to N characters (log lines stay one line, readable by any tool).
std::string clean(const std::string& s, size_t n) {
    std::string out;
    for (unsigned char c : s) {
        if (out.size() >= n) {
            out += "...";
            break;
        }
        out += c >= 0x20 && c != 0x7f ? char(c) : ' ';
    }
    return out;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool secretKey(const std::string& key) {
    const std::string k = lower(key);
    for (const char* w : {"key", "token", "secret", "password", "passwd", "auth", "credential", "cookie"})
        if (k.find(w) != std::string::npos) return true;
    return false;
}

// Arguments that carry artwork or bulk data rather than settings.
bool dataKey(const std::string& key) {
    const std::string k = lower(key);
    for (const char* w : {"json", "code", "data", "image", "points", "pixels", "mesh", "base64", "content"})
        if (k == w || k.ends_with(std::string("_") + w)) return true;
    return false;
}

bool textKey(const std::string& key) {
    const std::string k = lower(key);
    for (const char* w : {"prompt", "description", "text", "request", "message"})
        if (k == w || k.ends_with(std::string("_") + w)) return true;
    return false;
}

std::string nowIso() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const int ms = int(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

std::string envOr(const char* a, const char* b) {
    if (const char* v = std::getenv(a); v && *v) return v;
    if (const char* v = std::getenv(b); v && *v) return v;
    return {};
}

void rotate(const fs::path& path, uint64_t maxBytes, int keep) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size < maxBytes) return;
    if (keep <= 0) {
        fs::remove(path, ec);
        return;
    }
    auto numbered = [&](int i) { return fs::path(path.native() + fs::path("." + std::to_string(i)).native()); };
    fs::remove(numbered(keep), ec);
    for (int i = keep - 1; i >= 1; --i)
        if (fs::exists(numbered(i), ec)) fs::rename(numbered(i), numbered(i + 1), ec);
    fs::rename(path, numbered(1), ec);
}

} // namespace

void setClient(const std::string& c) {
    std::lock_guard lock(gMutex);
    gClient = clean(c, 64);
}

std::string client() {
    std::lock_guard lock(gMutex);
    return gClient;
}

std::string clientFromInitialize(const Json& params) {
    const Json& info = params["clientInfo"];
    std::string name = info["name"].asString(), version = info["version"].asString();
    if (name.empty()) name = "unknown";
    return clean("mcp:" + name + (version.empty() ? "" : " " + version), 64);
}

Json summarizeArgs(const Args& args) {
    Json out = Json::object();
    for (const auto& [key, value] : args) {
        if (secretKey(key)) {
            out.set(key, "<redacted>");
        } else if (const auto* s = std::get_if<std::string>(&value)) {
            if (dataKey(key)) out.set(key, "<" + std::to_string(s->size()) + " bytes>");
            else if (textKey(key) && s->size() > 120) out.set(key, "<" + std::to_string(s->size()) + " characters>");
            else out.set(key, clean(*s, 120));
        } else if (const auto* b = std::get_if<bool>(&value)) {
            out.set(key, *b);
        } else {
            out.set(key, std::get<double>(value));
        }
    }
    return out;
}

std::string logPath() {
    if (!gTestPath.empty()) return gTestPath;
    const policy::Policy& p = policy::current();
    if (!p.auditPath.empty()) return policy::expandPathTemplate(p.auditPath);
    const std::string dir = appDataDir();
#if defined(_WIN32)
    return (dir.empty() ? std::string(".") : dir) + "\\audit\\ai-audit.jsonl";
#else
    return (dir.empty() ? std::string(".") : dir) + "/audit/ai-audit.jsonl";
#endif
}

bool required() { return policy::current().auditRequired; }

bool record(const Entry& e, std::string* error) {
    Json line = Json::object();
    line.set("time", nowIso());
    line.set("user", clean(envOr("USER", "USERNAME"), 64));
    {
        // (the host name is the same one the path template uses)
        std::string host = policy::expandPathTemplate("{host}");
        line.set("host", host);
    }
    line.set("client", e.client.empty() ? client() : clean(e.client, 64));
    line.set("tool", clean(e.tool, 32));
    line.set("command", clean(e.command, 64));
    line.set("args", e.args.isObject() ? e.args : Json::object());
    line.set("result", e.ok ? "ok" : "error");
    if (!e.message.empty()) line.set("message", clean(e.message, 200));
    if (e.ms >= 0) line.set("ms", double(e.ms));
    if (!e.endpoint.empty()) line.set("endpoint", clean(e.endpoint, 120));
    const std::string text = line.dump() + "\n";

    std::lock_guard lock(gMutex);
    const fs::path path = fsp(logPath());
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const policy::Policy& p = policy::current();
    rotate(path, uint64_t(std::max(0.1, p.auditMaxMB) * 1024 * 1024), p.auditKeep);
    // Append mode: several processes (the app, the CLI, an agent) can share one file.
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (f) f << text;
    if (f) f.flush();
    if (!f) {
        if (error) *error = "can't write the audit log " + logPath();
        return false;
    }
#if !defined(_WIN32)
    if (p.auditPath.empty() && gTestPath.empty()) fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, ec);
#endif
    return true;
}

bool writable(std::string* error) {
    std::lock_guard lock(gMutex);
    const fs::path path = fsp(logPath());
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (f) return true;
    if (error) *error = "can't write the audit log " + logPath() + ", which your studio requires";
    return false;
}

void setPathForTests(const std::string& path) {
    std::lock_guard lock(gMutex);
    gTestPath = path;
}

} // namespace sf::audit
