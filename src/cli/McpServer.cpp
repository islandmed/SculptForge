// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpServer.h"

#include "cli/McpTools.h"
#include "core/AuditLog.h"
#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"

#include <chrono>
#include <iostream>

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "unknown"
#endif

namespace sf {

namespace {

constexpr const char* kLatestProtocol = "2025-06-18";

// Installing, removing or switching packs changes what every later session loads: that stays the user's own
// decision (the app's review screen, or sculptforge-cli run by hand), never an AI agent's.
bool hiddenFromAgents(const std::string& name) {
    return name == "pack_install" || name == "pack_remove" || name == "pack_enable" || name == "pack_disable";
}

std::vector<std::string> paramNames(const Command& c) {
    std::vector<std::string> names;
    for (const auto& p : c.params) names.push_back(p.name);
    return names;
}

// A studio's policy (core/ManagedPolicy.h) may allow only some kinds of tool: the rest are neither listed nor run.
bool policyAllows(const Command& c, std::string* why = nullptr) { return policy::mcpToolAllowed(c.name, paramNames(c), why); }

Json toolList(const CommandRegistry& registry) {
    Json tools = Json::array();
    for (const auto& c : registry.all()) {
        if (hiddenFromAgents(c.name) || !policyAllows(c)) continue;
        Json props = Json::object(), required = Json::array();
        for (const auto& p : c.params) {
            Json prop = Json::object();
            prop.set("type", p.type == ParamSpec::Type::Bool ? "boolean" : p.type == ParamSpec::Type::Number ? "number" : "string");
            prop.set("description", p.description);
            props.set(p.name, prop);
            if (p.required) required.push(p.name);
        }
        Json schema = Json::object();
        schema.set("type", "object");
        schema.set("properties", props);
        schema.set("required", required);
        Json tool = Json::object();
        tool.set("name", c.name);
        tool.set("description", c.description);
        tool.set("inputSchema", schema);
        tools.push(tool);
    }
    if (policy::mcpToolAllowed("viewport_image", {})) tools.push(viewportImageTool()); // (cli/McpTools.h; reads: "info")
    Json result = Json::object();
    result.set("tools", tools);
    return result;
}

Json reply(const Json& id, Json result) {
    Json r = Json::object();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("result", std::move(result));
    return r;
}

Json errorReply(const Json& id, int code, const std::string& message) {
    Json e = Json::object();
    e.set("code", code);
    e.set("message", message);
    Json r = Json::object();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("error", e);
    return r;
}

} // namespace

std::string handleMcpMessage(const CommandRegistry& registry, Document& doc, const std::string& line) {
    Json msg;
    std::string err;
    if (!Json::parse(line, msg, err) || !msg.isObject()) return errorReply(Json(), -32700, "Parse error: " + err).dump();
    const std::string method = msg["method"].asString();
    const Json& id = msg["id"];
    if (!msg.has("id")) return {}; // notification (e.g. notifications/initialized)

    if (method == "initialize") {
        audit::setClient(audit::clientFromInitialize(msg["params"])); // (the audit log's "client")
        Json result = Json::object();
        const std::string requested = msg["params"]["protocolVersion"].asString();
        result.set("protocolVersion", requested.empty() ? kLatestProtocol : requested);
        Json caps = Json::object(), tools = Json::object();
        tools.set("listChanged", false);
        caps.set("tools", tools);
        Json prompts = Json::object();
        prompts.set("listChanged", false);
        caps.set("prompts", prompts); // the recipes (cli/McpTools.h)
        result.set("capabilities", caps);
        Json info = Json::object();
        info.set("name", "sculptforge");
        info.set("title", "SculptForge");
        info.set("version", SCULPTFORGE_VERSION);
        result.set("serverInfo", info);
        result.set("instructions",
                   "SculptForge is a digital sculpting tool. One scene is kept for the whole session: start with "
                   "new_sphere, open (models, Gaussian-splat captures, USD) or image_to_3d, shape it with "
                   "subdivide/remesh/layers/decimate, then export_glb, export_usd, export_lods or splat_save. "
                   "A scene can hold several Parts (separate meshes, see part_list): edits apply to the active "
                   "one, exports write every visible one. "
                   "Check each step by eye with viewport_image and by numbers with mesh_info (splat_info for "
                   "captures); every tool call is one undo step (undo takes it back). The prompts are ready-made "
                   "recipes (a creature head, a game-ready asset, rock variations, a lesson). File paths are on "
                   "the user's computer.");
        return reply(id, result).dump();
    }
    if (method == "ping") return reply(id, Json::object()).dump();
    if (method == "tools/list") return reply(id, toolList(registry)).dump();
    if (method == "prompts/list") return reply(id, mcpPromptList()).dump();
    if (method == "prompts/get") {
        Json result;
        std::string error;
        if (!mcpPromptGet(msg["params"]["name"].asString(), msg["params"]["arguments"], result, error))
            return errorReply(id, -32602, error).dump();
        return reply(id, result).dump();
    }
    if (method == "tools/call") {
        const std::string name = msg["params"]["name"].asString();
        if (name == "viewport_image") {
            Json content;
            std::string error;
            const auto start = std::chrono::steady_clock::now();
            bool ok = policy::mcpToolAllowed(name, {}, &error) && (!audit::required() || audit::writable(&error));
            ok = ok && renderViewportImage(doc, msg["params"]["arguments"], content, error);
            if (!ok) {
                Json text = Json::object();
                text.set("type", "text");
                text.set("text", error);
                content = Json::array();
                content.push(text);
            }
            audit::Entry entry; // (the picture itself is never logged)
            entry.tool = "mcp";
            entry.command = name;
            entry.ok = ok;
            entry.message = ok ? "image" : error;
            entry.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            audit::record(entry);
            Json result = Json::object();
            result.set("content", content);
            result.set("isError", !ok);
            return reply(id, result).dump();
        }
        const Command* command = registry.find(name);
        if (!command || hiddenFromAgents(name)) return errorReply(id, -32602, "Unknown tool: " + name).dump();
        Args args;
        for (const auto& [key, value] : msg["params"]["arguments"].members()) {
            if (value.isBool()) args[key] = value.asBool();
            else if (value.isNumber()) args[key] = value.asNumber();
            else if (value.isString()) args[key] = value.asString();
            else args[key] = value.dump();
        }
        // Every call goes into the audit log (core/AuditLog.h), refused ones too; with audit.required, a call whose
        // line can't be written is refused before it runs.
        audit::Entry entry;
        entry.tool = "mcp";
        entry.command = name;
        entry.args = audit::summarizeArgs(args);
        std::string why;
        CommandResult r;
        const auto start = std::chrono::steady_clock::now();
        if (!policyAllows(*command, &why)) r = {false, why};
        else if (audit::required() && !audit::writable(&why)) r = {false, why};
        else r = registry.execute(doc, name, args);
        entry.ok = r.ok;
        entry.message = r.message;
        entry.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        audit::record(entry);
        Json text = Json::object();
        text.set("type", "text");
        text.set("text", r.message);
        Json content = Json::array();
        content.push(text);
        Json result = Json::object();
        result.set("content", content);
        result.set("isError", !r.ok);
        return reply(id, result).dump();
    }
    return errorReply(id, -32601, "Method not found: " + method).dump();
}

Json mcpToolList(const CommandRegistry& registry) { return toolList(registry)["tools"]; }

int runMcpServer(const CommandRegistry& registry, std::istream& in, std::ostream& out) {
    if (std::string why; !policy::mcpAllowed(&why)) {
        std::cerr << "sculptforge: " << why << "\n";
        return 3;
    }
    Document doc;
    // stdout is the protocol: library messages go to stderr.
    for (const auto& message : loadBrushLibrary(doc)) std::cerr << "sculptforge: " << message << "\n";
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const std::string response = handleMcpMessage(registry, doc, line);
        if (!response.empty()) out << response << "\n" << std::flush;
    }
    return 0;
}

} // namespace sf
