// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpServer.h"

#include "cli/McpBatch.h"
#include "cli/McpScene.h"
#include "cli/McpTools.h"
#include "core/AuditLog.h"
#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "core/OpControl.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "unknown"
#endif

namespace sf {

namespace {

constexpr const char* kLatestProtocol = "2025-06-18";

// Installing, removing or switching packs changes what every later session loads: that stays the user's own
// decision (the app's review screen, or sculptforge-cli run by hand), never an AI agent's. Plugins run only when the
// user runs them (core/Plugin.h).
bool hiddenFromAgents(const std::string& name) {
    return name == "pack_install" || name == "pack_remove" || name == "pack_enable" || name == "pack_disable" ||
           name.rfind("plugin_", 0) == 0;
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
    const Json outputSchema = mcpOutputSchema(mcpMayReadScene()); // (cli/McpScene.h: every result's structuredContent)
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
        tool.set("outputSchema", outputSchema);
        tools.push(tool);
    }
    for (const char* image : {"viewport_image", "view_closeup"}) { // (cli/McpTools.h; reads: "info")
        if (!policy::mcpToolAllowed(image, {})) continue;
        Json tool = std::string(image) == "viewport_image" ? viewportImageTool() : viewCloseupTool();
        tool.set("outputSchema", outputSchema);
        tools.push(tool);
    }
    const Json history = historyToolList(); // (cli/McpBatch.h)
    for (Json t : history.items()) {
        t.set("outputSchema", outputSchema);
        tools.push(t);
    }
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

bool mcpToolCallable(const CommandRegistry& registry, const std::string& name) {
    return registry.find(name) && !hiddenFromAgents(name);
}

CommandResult callRegistryTool(const CommandRegistry& registry, Document& doc, const std::string& name, const Json& arguments,
                              int64_t* ms) {
    const Command* command = registry.find(name);
    if (!command || hiddenFromAgents(name)) return {false, "Unknown tool: " + name};
    Args args;
    for (const auto& [key, value] : arguments.members()) {
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
    if (ms) *ms = entry.ms;
    return r;
}

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
        if (mcpMayReadScene()) {
            Json resources = Json::object();
            resources.set("subscribe", false);
            resources.set("listChanged", false);
            caps.set("resources", resources); // the scene as JSON (cli/McpScene.h)
        }
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
                   "captures); every tool call is one undo step (undo takes it back). " + std::string(kMcpSculptWorkflow) +
                   " The prompts are ready-made recipes (a creature head, a game-ready asset, rock variations, a "
                   "lesson). File paths are on the user's computer.");
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
    if (method == "resources/templates/list") return reply(id, mcpResourceTemplates()).dump();
    if (method == "resources/list" || method == "resources/read") {
        // Reading the scene is not a tool call (no undo step), but it is an agent looking at the user's work: the
        // audit log has it, and with audit.required a read whose line can't be written is refused.
        const std::string uri = msg["params"]["uri"].asString();
        audit::Entry entry;
        entry.tool = "mcp";
        entry.command = method;
        if (method == "resources/read") entry.args.set("uri", uri);
        const auto start = std::chrono::steady_clock::now();
        Json result;
        std::string error;
        bool ok = mcpMayReadScene(&error) && (!audit::required() || audit::writable(&error));
        if (ok && method == "resources/list") result = mcpResourceList(doc);
        else if (ok) ok = mcpResourceRead(doc, uri, result, error);
        entry.ok = ok;
        entry.message = ok ? "resource" : error;
        entry.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        audit::record(entry);
        if (!ok) return errorReply(id, -32002, error).dump(); // (the spec's "resource not found" / refused)
        return reply(id, result).dump();
    }
    if (method == "tools/call") {
        const std::string name = msg["params"]["name"].asString();
        if (name == "viewport_image" || name == "view_closeup") {
            Json content;
            std::string error;
            const auto start = std::chrono::steady_clock::now();
            bool ok = policy::mcpToolAllowed(name, {}, &error) && (!audit::required() || audit::writable(&error));
            ok = ok && (name == "view_closeup" ? renderCloseup(doc, msg["params"]["arguments"], content, error)
                                               : renderViewportImage(doc, msg["params"]["arguments"], content, error));
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
            result.set("structuredContent", mcpStructuredResult(doc, ok, ok ? "image" : error, entry.ms));
            result.set("isError", !ok);
            return reply(id, result).dump();
        }
        if (isHistoryTool(name)) {
            const auto start = std::chrono::steady_clock::now();
            HistoryToolReply h = callHistoryTool(registry, doc, name, msg["params"]["arguments"]);
            const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            Json text = Json::object();
            text.set("type", "text");
            text.set("text", h.text);
            Json content = Json::array();
            content.push(text);
            Json result = Json::object();
            result.set("content", content);
            // The scene's numbers like any tool's, plus the tool's own fields (a batch's steps, a checkpoint's marks).
            Json structured = mcpStructuredResult(doc, h.ok, h.text, ms);
            for (const auto& [key, value] : h.structured.members())
                if (key != "ok" && key != "message" && key != "ms") structured.set(key, value);
            result.set("structuredContent", structured);
            result.set("isError", !h.ok);
            return reply(id, result).dump();
        }
        if (!mcpToolCallable(registry, name)) return errorReply(id, -32602, "Unknown tool: " + name).dump();
        int64_t ms = 0;
        CommandResult r = callRegistryTool(registry, doc, name, msg["params"]["arguments"], &ms);
        Json text = Json::object();
        text.set("type", "text");
        text.set("text", r.message);
        Json content = Json::array();
        content.push(text);
        Json result = Json::object();
        result.set("content", content);
        result.set("structuredContent", mcpStructuredResult(doc, r.ok, r.message, ms)); // (cli/McpScene.h)
        result.set("isError", !r.ok);
        return reply(id, result).dump();
    }
    return errorReply(id, -32601, "Method not found: " + method).dump();
}

Json mcpToolList(const CommandRegistry& registry) { return toolList(registry)["tools"]; }

namespace {

// One request waiting for, or running on, the worker.
struct McpJob {
    std::string line;
    std::string idKey;    // the request id as JSON text ("" for a message that is not a request)
    Json progressToken;   // params._meta.progressToken (null: the client doesn't want progress)
    std::shared_ptr<OpControl> control = std::make_shared<OpControl>();
};

constexpr auto kProgressInterval = std::chrono::milliseconds(100); // (at most 10 progress notifications a second)

} // namespace

// The stdio loop. Requests run one at a time, in order, on a worker thread (the scene is one Document); this thread
// keeps reading, so a ping is answered and notifications/cancelled reaches the request in flight while a long
// operation runs. Replies and progress notifications go out whole, one line each, under one lock.
int runMcpServer(const CommandRegistry& registry, std::istream& in, std::ostream& out) {
    if (std::string why; !policy::mcpAllowed(&why)) {
        std::cerr << "sculptforge: " << why << "\n";
        return 3;
    }
    Document doc;
    // stdout is the protocol: library messages go to stderr.
    for (const auto& message : loadBrushLibrary(doc)) std::cerr << "sculptforge: " << message << "\n";

    std::mutex outMutex;
    auto send = [&](const std::string& line) {
        std::lock_guard lock(outMutex);
        out << line << "\n" << std::flush;
    };

    std::mutex mutex; // guards queue, running, inputClosed
    std::condition_variable wake;
    std::deque<std::shared_ptr<McpJob>> queue;
    std::shared_ptr<McpJob> running;
    bool inputClosed = false;

    std::thread worker([&] {
        for (;;) {
            std::shared_ptr<McpJob> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return !queue.empty() || inputClosed; });
                if (queue.empty()) return; // (input closed and everything answered)
                job = std::move(queue.front());
                queue.pop_front();
                running = job;
            }
            OpControl& control = *job->control;
            if (!job->progressToken.isNull()) {
                auto last = std::chrono::steady_clock::time_point();
                double lastValue = -1.0;
                control.onProgress = [&, token = job->progressToken, last, lastValue](float fraction, const std::string& message) mutable {
                    if (control.cancel.load()) return; // (the client has stopped listening)
                    const auto now = std::chrono::steady_clock::now();
                    if (lastValue >= 0.0 && now - last < kProgressInterval) return;
                    const double percent = std::round(double(fraction) * 1000.0) / 10.0;
                    if (percent <= lastValue) return; // (progress only ever increases)
                    last = now;
                    lastValue = percent;
                    Json params = Json::object();
                    params.set("progressToken", token);
                    params.set("progress", percent);
                    params.set("total", 100.0);
                    if (!message.empty()) params.set("message", message);
                    Json note = Json::object();
                    note.set("jsonrpc", "2.0");
                    note.set("method", "notifications/progress");
                    note.set("params", params);
                    send(note.dump());
                };
            }
            std::string response;
            {
                OpControlScope scope(&control);
                response = handleMcpMessage(registry, doc, job->line);
            }
            // A request the client cancelled and the operation stopped for gets no reply (the client has dropped it). One
            // that finished past its last cancel point is answered: its changes are in the scene.
            if (!response.empty() && !control.honoured.load()) send(response);
            {
                std::lock_guard lock(mutex);
                running.reset();
            }
        }
    });

    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        Json msg;
        std::string err;
        const bool parsed = Json::parse(line, msg, err) && msg.isObject();
        const std::string method = parsed ? msg["method"].asString() : std::string();
        if (parsed && !msg.has("id")) {
            if (method == "notifications/cancelled") {
                const std::string target = msg["params"]["requestId"].dump();
                std::lock_guard lock(mutex);
                if (running && running->idKey == target) {
                    running->control->cancel = true; // (the operation stops at its next cancel point)
                } else {
                    for (auto it = queue.begin(); it != queue.end(); ++it)
                        if ((*it)->idKey == target) {
                            queue.erase(it); // (never started: nothing to undo, nothing to answer)
                            break;
                        }
                }
            }
            continue; // (notifications/initialized and the rest)
        }
        if (parsed && method == "ping") {
            bool busy;
            {
                std::lock_guard lock(mutex);
                busy = running || !queue.empty();
            }
            if (busy) { // (a long operation is running: answer now, the worker is not free)
                Json result = Json::object();
                send(reply(msg["id"], result).dump());
                continue;
            }
        }
        auto job = std::make_shared<McpJob>();
        job->line = line;
        if (parsed) {
            job->idKey = msg["id"].dump();
            const Json& token = msg["params"]["_meta"]["progressToken"];
            if (method == "tools/call" && (token.isString() || token.isNumber())) job->progressToken = token;
        }
        {
            std::lock_guard lock(mutex);
            queue.push_back(std::move(job));
        }
        wake.notify_one();
    }
    {
        std::lock_guard lock(mutex);
        inputClosed = true;
    }
    wake.notify_one();
    worker.join();
    return 0;
}

} // namespace sf
