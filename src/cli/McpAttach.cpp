// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/McpAttach.h"

#include "cli/McpTools.h"
#include "core/Json.h"
#include "link/AiLink.h"
#include "link/AiLinkSession.h"

#include <iostream>

#ifndef SCULPTFORGE_VERSION
#define SCULPTFORGE_VERSION "unknown"
#endif

namespace sf {

namespace {

using link::AiLinkSession;

constexpr int kApprovalWaitMs = 60 * 1000;      // live_connect waits this long for a click on Approve
constexpr int kAnswerWaitMs = 30 * 60 * 1000;   // a tool call (a big remesh, or the user deciding in a dialog)

Json textContent(const std::string& text) {
    Json t = Json::object();
    t.set("type", "text");
    t.set("text", text);
    Json content = Json::array();
    content.push(std::move(t));
    return content;
}

Json tool(const std::string& name, const std::string& description, Json properties) {
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", std::move(properties));
    schema.set("required", Json::array());
    Json t = Json::object();
    t.set("name", name);
    t.set("description", description);
    t.set("inputSchema", std::move(schema));
    return t;
}

Json connectTool() {
    Json code = Json::object();
    code.set("type", "string");
    code.set("description", "The 6-digit code SculptForge shows, read to you by the user (leave out to wait for a click on Approve).");
    Json props = Json::object();
    props.set("code", std::move(code));
    return tool("live_connect",
                "Connect to the SculptForge window the user has open, to sculpt in it live. SculptForge shows an Approve "
                "dialog with a 6-digit code: the user clicks Approve, or reads you the code and you call this again with "
                "code=. Live mode must be on there (Preferences > AI > Let AI tools control this window).",
                std::move(props));
}

Json disconnectTool() {
    return tool("live_disconnect", "Disconnect from the SculptForge window (the user can also press Disconnect there).",
                Json::object());
}

class Attach {
public:
    Attach(std::ostream& out, std::string endpoint) : out_(out), endpoint_(std::move(endpoint)) {}

    void message(const std::string& line) {
        Json msg;
        std::string err;
        if (!Json::parse(line, msg, err) || !msg.isObject()) return send(AiLinkSession::errorReply(Json(), -32700, "Parse error: " + err));
        if (!msg.has("id")) return; // notifications
        const Json& id = msg["id"];
        const std::string method = msg["method"].asString();
        if (method == "initialize") return initialize(id, msg["params"]);
        if (method == "ping") return send(result(id, Json::object()));
        if (method == "prompts/list") return send(result(id, mcpPromptList()));
        if (method == "prompts/get") {
            Json r;
            if (!mcpPromptGet(msg["params"]["name"].asString(), msg["params"]["arguments"], r, err))
                return send(AiLinkSession::errorReply(id, -32602, err));
            return send(result(id, std::move(r)));
        }
        if (method == "tools/list") return toolsList(id, msg);
        if (method == "tools/call") return toolsCall(id, msg);
        send(AiLinkSession::errorReply(id, -32601, "Method not found: " + method));
    }

private:
    static std::string result(const Json& id, Json r) {
        Json m = Json::object();
        m.set("jsonrpc", "2.0");
        m.set("id", id);
        m.set("result", std::move(r));
        return m.dump();
    }

    void send(const std::string& line) { out_ << line << "\n" << std::flush; }

    void listChanged() { send(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})"); }

    void dropConnection() {
        const bool was = paired_;
        conn_.close();
        paired_ = false;
        if (was) listChanged();
    }

    void initialize(const Json& id, const Json& params) {
        client_ = params["clientInfo"]["name"].asString();
        Json r = Json::object();
        const std::string requested = params["protocolVersion"].asString();
        r.set("protocolVersion", requested.empty() ? "2025-06-18" : requested);
        Json caps = Json::object(), tools = Json::object(), prompts = Json::object();
        tools.set("listChanged", true);
        prompts.set("listChanged", false);
        caps.set("tools", tools);
        caps.set("prompts", prompts);
        r.set("capabilities", caps);
        Json info = Json::object();
        info.set("name", "sculptforge-live");
        info.set("version", SCULPTFORGE_VERSION);
        r.set("serverInfo", info);
        r.set("instructions",
              "Live mode: you sculpt in the SculptForge window the user has open, and they watch. Call live_connect "
              "first; the user approves in SculptForge. Then work in small steps and look after each one: "
              "viewport_image shows what the user sees, view_turntable shows the model from all sides, view_closeup "
              "shows one spot up close, view_set turns the camera. Every step lands in the undo history (labelled "
              "AI), so the user can step back. Some actions ask the user first (files outside the project folder, "
              "overwrites, scripts, network); if they decline, don't retry, ask them. " +
                  std::string(kMcpSculptWorkflow) +
                  " stroke_screen's radius is a fraction of the view's height: zoom in (view_set zoom=3 or more) "
                  "before small detail strokes, or use stroke with scene-unit radii.");
        send(result(id, std::move(r)));
    }

    // Reads the app's next line; false when the connection closed.
    bool readApp(Json& msg, int timeoutMs) {
        std::string line;
        const int got = conn_.readLine(line, timeoutMs, link::kAiLinkMaxReplyLine);
        if (got < 0) {
            dropConnection();
            return false;
        }
        if (got == 0) return false;
        std::string err;
        if (!Json::parse(line, msg, err)) msg = Json::object();
        return true;
    }

    // The app's answer to a pairing message: approved now, refused (closed), or still waiting.
    std::string pairingOutcome(const Json& msg) {
        if (msg["paired"].asBool()) {
            paired_ = true;
            listChanged();
            return {};
        }
        if (msg.has("paired")) {
            const std::string e = msg["error"].asString();
            if (!conn_.valid() || msg["attempts_left"].asNumber(1) <= 0 || e.find("declined") != std::string::npos) dropConnection();
            return e.empty() ? "not approved" : e;
        }
        return "waiting";
    }

    void toolsList(const Json& id, const Json& msg) {
        if (!paired_) {
            Json tools = Json::array();
            tools.push(connectTool());
            Json r = Json::object();
            r.set("tools", tools);
            return send(result(id, std::move(r)));
        }
        Json reply;
        if (!forward(id, msg, reply)) return;
        Json tools = Json::array();
        for (const Json& t : reply["result"]["tools"].items()) tools.push(t);
        tools.push(disconnectTool());
        Json r = Json::object();
        r.set("tools", tools);
        send(result(id, std::move(r)));
    }

    void toolsCall(const Json& id, const Json& msg) {
        const std::string name = msg["params"]["name"].asString();
        if (name == "live_connect") return send(AiLinkSession::contentReply(id, textContent(connect(msg["params"]["arguments"]["code"].asString())), !paired_));
        if (name == "live_disconnect") {
            dropConnection();
            return send(AiLinkSession::resultReply(id, true, "Disconnected from SculptForge"));
        }
        if (!paired_)
            return send(AiLinkSession::resultReply(id, false, "Not connected to SculptForge: call live_connect first."));
        Json reply;
        if (forward(id, msg, reply)) send(reply.dump());
    }

    // Sends a request to the app and waits for its answer (the same id). False (answered here) if the app left.
    bool forward(const Json& id, const Json& msg, Json& reply) {
        Json request = Json::object();
        request.set("jsonrpc", "2.0");
        request.set("id", id);
        request.set("method", msg["method"]);
        request.set("params", msg["params"]);
        if (conn_.writeLine(request.dump())) {
            for (;;) {
                Json m;
                if (!readApp(m, kAnswerWaitMs)) break;
                if (m["id"].dump() == id.dump()) {
                    reply = std::move(m);
                    return true;
                }
            }
        }
        dropConnection();
        send(AiLinkSession::resultReply(id, false, "SculptForge closed the connection (the user disconnected, turned live mode "
                                                   "off or closed the window). Call live_connect to ask again."));
        return false;
    }

    std::string connect(const std::string& code) {
        if (paired_) return "Already connected to SculptForge.";
        if (!conn_.valid()) {
            std::string err, endpoint = endpoint_.empty() ? link::aiLinkEndpoint(err) : endpoint_;
            if (endpoint.empty() || !link::aiLinkConnect(conn_, endpoint, err))
                return "Can't reach SculptForge: " + err +
                       ". Ask the user to open it and turn on Preferences > AI > Let AI tools control this window.";
            Json hello = Json::object();
            hello.set("hello", "sculptforge-live");
            hello.set("v", 1);
            hello.set("client", client_.empty() ? "AI tool" : client_);
            Json m;
            if (!conn_.writeLine(hello.dump()) || !readApp(m, 10000) || m["pairing"].asString() != "waiting") {
                dropConnection();
                return "SculptForge didn't accept the connection (is another AI tool connected?).";
            }
        }
        // An Approve click that came in since the last call.
        for (Json m; conn_.valid() && readApp(m, 0);)
            if (const std::string o = pairingOutcome(m); o != "waiting") return o.empty() ? connectedText() : "Not connected: " + o;
        if (!conn_.valid()) return "Not connected: SculptForge closed the connection.";
        if (!code.empty()) {
            Json pair = Json::object();
            pair.set("pair", code);
            Json m;
            if (!conn_.writeLine(pair.dump()) || !readApp(m, 10000)) {
                dropConnection();
                return "Not connected: SculptForge closed the connection.";
            }
            const std::string o = pairingOutcome(m);
            if (o.empty()) return connectedText();
            if (o == "waiting") return "Still waiting for approval.";
            return "Not connected: " + o +
                   (conn_.valid() ? " (" + std::to_string(int(m["attempts_left"].asNumber())) + " tries left)" : "");
        }
        for (Json m; readApp(m, kApprovalWaitMs);)
            if (const std::string o = pairingOutcome(m); o != "waiting") return o.empty() ? connectedText() : "Not connected: " + o;
        if (!conn_.valid()) return "Not connected: SculptForge closed the connection.";
        return "Waiting for approval: ask the user to click Approve in SculptForge, or to read you the 6-digit code it "
               "shows, then call live_connect again with code=.";
    }

    static std::string connectedText() {
        return "Connected to SculptForge. Its tools are now listed (call tools/list again if you don't see them): start "
               "with scene_overview and viewport_image.";
    }

    std::ostream& out_;
    std::string endpoint_, client_;
    link::AiLinkConn conn_;
    bool paired_ = false;
};

} // namespace

int runMcpAttach(std::istream& in, std::ostream& out, const std::string& endpoint) {
    Attach attach(out, endpoint);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        attach.message(line);
    }
    return 0;
}

} // namespace sf
