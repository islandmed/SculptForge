// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/AiLinkSession.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>

namespace sf::link {

namespace fs = std::filesystem;

namespace {

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string fromPath(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

// Absolute, with "." / ".." and symlinks resolved as far as the path exists. Relative paths are relative to the
// project folder; with no project they can't be placed (empty).
fs::path resolved(const std::string& path, const std::string& projectDir) {
    std::error_code ec;
    fs::path p = toPath(path);
    if (p.is_relative()) {
        if (projectDir.empty()) return {};
        p = toPath(projectDir) / p;
    }
    fs::path r = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : r;
}

bool inside(const fs::path& path, const fs::path& folder) {
    if (folder.empty() || path.empty()) return false;
    auto f = folder.begin(), p = path.begin();
    for (; f != folder.end(); ++f, ++p) {
        if (f->empty()) continue; // a trailing separator
        if (p == path.end() || *p != *f) return false;
    }
    return true;
}

bool isPathParam(const std::string& name) {
    return name == "path" || name == "file" || name == "folder" || name == "out" || name == "dir" ||
           name == "directory" || name == "output";
}

// Commands whose path argument is written (not read).
bool writes(const std::string& name) {
    static const char* const kWords[] = {"export", "save", "_send", "bake", "capture_", "pack_create"};
    for (const char* w : kWords)
        if (name.find(w) != std::string::npos) return true;
    return false;
}

// A file a later program might run: an AI tool may not write one without asking.
bool runnable(const fs::path& p) {
    std::string ext = fromPath(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    static const char* const kExt[] = {".exe", ".bat", ".cmd", ".com", ".ps1", ".vbs", ".js", ".sh", ".py", ".pl",
                                       ".rb", ".desktop", ".so", ".dll", ".dylib", ".lnk", ".app", ".command", ".scr", ".msi"};
    for (const char* e : kExt)
        if (ext == e) return true;
    const std::string file = fromPath(p.filename());
    return !file.empty() && file[0] == '.'; // dot-files: shell profiles, configs
}

} // namespace

// ---------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------

void AiLinkPolicy::addPicked(const std::string& path) {
    if (path.empty()) return;
    const std::string p = fromPath(resolved(path, {}));
    if (!p.empty() && std::find(picked.begin(), picked.end(), p) == picked.end()) picked.push_back(p);
}

bool AiLinkPolicy::hidden(const std::string& name) {
    return name == "pack_install" || name == "pack_remove" || name == "pack_enable" || name == "pack_disable" ||
           name == "ai_features";
}

bool AiLinkPolicy::allowedPath(const std::string& path) const {
    const fs::path p = resolved(path, projectDir);
    if (p.empty()) return false;
    if (!projectDir.empty() && inside(p, resolved(projectDir, {}))) return true;
    for (const std::string& pick : picked)
        if (p == toPath(pick) || inside(p, toPath(pick))) return true;
    return false;
}

AiDecision confirmBecause(const char* format, const std::string& subject) {
    AiDecision d{AiVerdict::Confirm, format, format, subject};
    if (const size_t at = d.reason.find("%s"); at != std::string::npos) d.reason.replace(at, 2, subject);
    return d;
}

AiDecision AiLinkPolicy::classify(const Command* cmd, const std::string& name, const Args& args) const {
    if (hidden(name)) return {AiVerdict::Deny, "Unknown tool: " + name};
    if (!cmd) return {}; // a live-window tool: the viewport, the camera, screen strokes

    static const char* const kNetwork[] = {"image_to_3d", "text_to_3d", "brush_ai", "comfyui_check",
                                           "brush_gallery_search", "brush_gallery_install", "unreal_editors",
                                           "unreal_send"};
    for (const char* n : kNetwork)
        if (name == n) return confirmBecause("Connects to the network");
    if (args.contains("url")) return confirmBecause("Connects to the network");
    if (name == "macro_run" || name.rfind("script", 0) == 0 || name.rfind("python", 0) == 0)
        return confirmBecause("Runs a macro or script");
    if (name == "splat_train") return confirmBecause("Runs outside programs (splat training)");
    if (name == "brush_delete" || name == "vdm_delete") return confirmBecause("Deletes a file from your library");

    const bool writer = writes(name);
    for (const auto& [key, value] : args) {
        if (!isPathParam(key)) continue;
        const std::string* s = std::get_if<std::string>(&value);
        if (!s || s->empty()) continue;
        const fs::path p = resolved(*s, projectDir);
        const std::string shown = p.empty() ? *s : fromPath(p);
        if (writer && !p.empty() && runnable(p)) return confirmBecause("Writes a program or settings file: %s", shown);
        std::error_code ec;
        if (writer && !p.empty() && fs::exists(p, ec) && !fs::is_directory(p, ec))
            return confirmBecause("Overwrites %s", shown);
        if (!allowedPath(*s))
            return confirmBecause(writer ? "Writes %s (outside the project folder and the files you picked)"
                                         : "Reads %s (outside the project folder and the files you picked)",
                                  shown);
    }
    if (name == "save_project") {
        const auto it = args.find("path");
        if (it == args.end() || std::get_if<std::string>(&it->second) == nullptr || std::get<std::string>(it->second).empty())
            return confirmBecause("Saves over the project");
    }
    return {};
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

bool RateLimiter::take(double now) {
    if (last_ >= 0.0) tokens_ = std::min(burst_, tokens_ + (now - last_) * rate_);
    last_ = now;
    if (tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    return true;
}

AiLinkSession::AiLinkSession(std::string code) : code_(std::move(code)) {}

AiLinkSession::Step AiLinkSession::reject(Step s) {
    if (++rejected_ >= kMaxRejected) {
        s.kind = Step::Kind::Close;
        state_ = State::Closed;
    }
    return s;
}

AiLinkSession::Step AiLinkSession::handle(const std::string& line, double now) {
    Step s;
    if (state_ == State::Closed) return s;
    Json msg;
    std::string err;
    if (!Json::parse(line, msg, err) || !msg.isObject()) {
        s.kind = Step::Kind::Reply;
        s.reply = errorReply(Json(), -32700, "Parse error");
        return reject(std::move(s));
    }

    if (state_ == State::Hello) {
        if (msg["hello"].asString() != "sculptforge-live") {
            s.kind = Step::Kind::Close;
            s.reply = R"({"error":"say hello first"})";
            state_ = State::Closed;
            return s;
        }
        client_ = sanitizeClientName(msg["client"].asString());
        state_ = State::Pairing;
        s.kind = Step::Kind::ShowApproval;
        s.reply = R"({"pairing":"waiting"})";
        return s;
    }

    if (state_ == State::Pairing) {
        if (msg.has("pair")) {
            if (sameCode(msg["pair"].asString(), code_)) {
                s.kind = Step::Kind::Reply;
                s.reply = approve();
                return s;
            }
            if (--attemptsLeft_ <= 0) {
                state_ = State::Closed;
                s.kind = Step::Kind::Close;
                s.reply = R"({"paired":false,"error":"wrong code: the connection is closed","attempts_left":0})";
                return s;
            }
            Json r = Json::object();
            r.set("paired", false);
            r.set("error", "wrong code");
            r.set("attempts_left", attemptsLeft_);
            s.kind = Step::Kind::Reply;
            s.reply = r.dump();
            return s;
        }
        s.kind = Step::Kind::Reply;
        s.reply = msg.has("id") ? errorReply(msg["id"], -32001, "Not approved yet: click Approve in SculptForge, or "
                                                                "send the code it shows")
                                : R"({"pairing":"waiting"})";
        return reject(std::move(s));
    }

    // Paired: MCP requests.
    if (!msg.has("id")) return s; // a notification
    const Json& id = msg["id"];
    const std::string method = msg["method"].asString();
    s.kind = Step::Kind::Reply;
    if (method == "ping") {
        Json r = Json::object();
        r.set("jsonrpc", "2.0");
        r.set("id", id);
        r.set("result", Json::object());
        s.reply = r.dump();
        return s;
    }
    if (method != "tools/list" && method != "tools/call") {
        s.reply = errorReply(id, -32601, "Method not found: " + method);
        return reject(std::move(s));
    }
    if (!limiter_.take(now)) {
        s.reply = errorReply(id, -32000, "Too many requests: slow down");
        return reject(std::move(s));
    }
    if (method == "tools/list") {
        s.kind = Step::Kind::ListTools;
        s.call.id = id;
        return s;
    }
    s.call.id = id;
    s.call.name = msg["params"]["name"].asString();
    if (s.call.name.empty() || !jsonToArgs(msg["params"]["arguments"], s.call.args)) {
        s.reply = errorReply(id, -32602, "Bad tool call");
        return reject(std::move(s));
    }
    s.kind = Step::Kind::Execute; // the app classifies it (it knows the registry): AiLinkPolicy::classify
    return s;
}

std::string AiLinkSession::approve() {
    if (state_ != State::Pairing) return {};
    state_ = State::Paired;
    attemptsLeft_ = 0;
    return R"({"paired":true})";
}

std::string AiLinkSession::deny() {
    state_ = State::Closed;
    return R"({"paired":false,"error":"the user declined the connection"})";
}

std::string AiLinkSession::listReply(const Json& id, Json tools) {
    Json result = Json::object();
    result.set("tools", std::move(tools));
    Json r = Json::object();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("result", std::move(result));
    return r.dump();
}

std::string AiLinkSession::resultReply(const Json& id, bool ok, const std::string& text) {
    Json t = Json::object();
    t.set("type", "text");
    t.set("text", text);
    Json content = Json::array();
    content.push(std::move(t));
    return contentReply(id, std::move(content), !ok);
}

std::string AiLinkSession::contentReply(const Json& id, Json content, bool isError) {
    Json result = Json::object();
    result.set("content", std::move(content));
    result.set("isError", isError);
    Json r = Json::object();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("result", std::move(result));
    return r.dump();
}

std::string AiLinkSession::errorReply(const Json& id, int code, const std::string& message) {
    Json e = Json::object();
    e.set("code", code);
    e.set("message", message);
    Json r = Json::object();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("error", std::move(e));
    return r.dump();
}

std::string AiLinkSession::declinedReply(const Json& id, const std::string& name) {
    return resultReply(id, false, "The user declined " + name + " in SculptForge. Don't retry it; ask them what to do instead.");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string makePairingCode() {
    std::random_device rd; // the OS's random source
    std::uniform_int_distribution<int> digit(0, 9);
    std::string code;
    for (int i = 0; i < 6; ++i) code += char('0' + digit(rd));
    return code;
}

bool sameCode(const std::string& a, const std::string& b) {
    // Spaces are ignored ("482 913" as the dialog shows it).
    auto digits = [](const std::string& s) {
        std::string d;
        for (char c : s)
            if (c != ' ') d += c;
        return d;
    };
    const std::string x = digits(a), y = digits(b);
    unsigned diff = unsigned(x.size() ^ y.size());
    for (size_t i = 0; i < y.size(); ++i) diff |= unsigned(i < x.size() ? x[i] ^ y[i] : 0xff);
    return diff == 0 && !y.empty();
}

std::string sanitizeClientName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (out.size() >= 40) break;
        if (c >= 0x20 && c < 0x7f) out += char(c);
    }
    return out.empty() ? "An AI tool" : out;
}

bool jsonToArgs(const Json& arguments, Args& out) {
    out.clear();
    if (arguments.isNull()) return true;
    if (!arguments.isObject() || arguments.members().size() > 64) return false;
    for (const auto& [key, value] : arguments.members()) {
        if (value.isBool()) out[key] = value.asBool();
        else if (value.isNumber()) out[key] = value.asNumber();
        else if (value.isString()) out[key] = value.asString();
        else out[key] = value.dump();
    }
    return true;
}

std::string describeArgs(const Args& args, size_t maxValue) {
    std::string out;
    for (const auto& [key, value] : args) {
        std::string v;
        if (const bool* b = std::get_if<bool>(&value)) v = *b ? "true" : "false";
        else if (const double* d = std::get_if<double>(&value)) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", *d);
            v = buf;
        } else v = std::get<std::string>(value);
        if (v.size() > maxValue) v = v.substr(0, maxValue) + "...";
        for (char& c : v)
            if (static_cast<unsigned char>(c) < 0x20) c = ' ';
        out += (out.empty() ? "" : ", ") + key + "=" + v;
    }
    return out;
}

} // namespace sf::link
