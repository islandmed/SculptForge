// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live mode: Preferences > AI > Let AI tools control this window (off by default; hidden while AI is off).
// `sculptforge-cli --mcp --attach` connects through link/AiLink.h (a Unix socket / named pipe only this user can
// open; no network port). The rules are link/AiLinkSession.h's; here they meet the window:
//   - the Approve dialog, with the one-time code the user can read to the AI tool instead of clicking;
//   - the "AI connected" badge with Disconnect (the kill switch; turning the switch or AI off also cuts it);
//   - the Allow dialog before files outside the project, overwrites, scripts and the network;
//   - every command runs as if typed in the console (App::runCommand), its undo steps labelled "AI: ...";
//   - the live-window tools: viewport_image (what the user sees, as an image), view_set (camera), view_turntable
//     (the model from every side in one image) and stroke_screen (a brush stroke drawn in screen space).
// A background thread accepts and reads; everything else happens here on the main thread, one message at a time.
#include "app/App.h"

#include "app/I18n.h"
#include "app/Theme.h"
#include "app/Ui.h"
#include "cli/McpServer.h"
#include "cli/McpTools.h"
#include "core/AuditLog.h"
#include "core/ManagedPolicy.h"
#include "link/AiLink.h"
#include "link/AiLinkSession.h"

#include <IconsLucide.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <optional>
#include <sstream>
#include <mutex>
#include <thread>
#include <utility>

namespace sf {

namespace {

constexpr double kApprovalSeconds = 120.0; // an unanswered Approve dialog says no

double seconds() { return double(SDL_GetTicksNS()) * 1e-9; }

// The dialogs look like the app's others (Save changes?, Delete brush): text wrapped at one width, the answer first
// and the refusal second, both buttons one width (110 px, wider only if a translation needs it).
constexpr float kDialogWrap = 400.0f;
float dialogButtonWidth(const char* const (&labels)[2], float scale) {
    float w = 110.0f * scale;
    for (const char* l : labels)
        w = std::max(w, ImGui::CalcTextSize(l, nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x);
    return w;
}

Json schemaProp(const char* type, const char* description) {
    Json p = Json::object();
    p.set("type", type);
    p.set("description", description);
    return p;
}

Json liveTool(const char* name, const char* description, std::vector<std::pair<const char*, Json>> props) {
    Json properties = Json::object();
    for (auto& [k, v] : props) properties.set(k, std::move(v));
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

Json liveTools() {
    Json tools = Json::array();
    const auto view = [] {
        return std::vector<std::pair<const char*, Json>>{
            {"preset", schemaProp("string", "front, back, left, right, top, bottom or three_quarter (front-right, a little from above).")},
            {"yaw", schemaProp("number", "Degrees around the vertical axis: 0 = front, 90 = the model's right side, 180 = back.")},
            {"elevation", schemaProp("number", "Degrees above the model (-89..89): 0 = level, 30 = looking down a little.")},
            {"frame", schemaProp("string", "Frame the view on: all (every visible Part), active (the active Part) or a Part's name.")},
            {"zoom", schemaProp("number", "Closer (2 = twice as close) or further (0.5) after framing.")},
            {"ortho", schemaProp("boolean", "Orthographic (true) or perspective (false).")}};
    };
    Json image = viewportImageTool(); // (one schema with the headless server: cli/McpTools.h)
    image.set("description", "See the model: the viewport exactly as the user sees it (without the interface), as a PNG. "
                             "The view options turn the user's camera too (they stay; with none, the view as it is). "
                             "Look after every step and correct what you see.");
    tools.push(std::move(image));
    tools.push(liveTool("view_set", "Turn and frame the camera the user sees (and viewport_image renders).", view()));
    tools.push(liveTool("view_turntable",
                        "The model from several sides in one image (a grid, labelled by yaw), to check form and "
                        "proportions all around. The user's camera is put back afterwards.",
                        {{"views", schemaProp("number", "4, 6 or 8 views around (default 4: front, right, back, left).")},
                         {"elevation", schemaProp("number", "Degrees above the model (default 10).")},
                         {"size", schemaProp("number", "Each view's side in pixels (192-512, default 320).")}}));
    tools.push(liveTool(
        "stroke_screen",
        "Brush stroke drawn on the screen, as a user drags across the viewport: points in viewport coordinates "
        "(0,0 = top left, 1,1 = bottom right), projected onto the model (points off the model are skipped). Use "
        "viewport_image to see where things are. One undo step.",
        {{"points", schemaProp("string", "\"u,v u,v ...\" in 0..1 viewport coordinates (at least one on the model).")},
         {"brush", schemaProp("string", "Brush name (Standard, Clay, Clay Block, Crease, Inflate, Smooth, Pinch...). Default: the active brush.")},
         {"radius", schemaProp("number", "Brush radius as a fraction of the viewport's height (default 0.04).")},
         {"strength", schemaProp("number", "0-1 (default 0.5).")},
         {"invert", schemaProp("boolean", "Carve instead of build (like holding Alt).")},
         {"symmetry", schemaProp("string", "Mirror across these world planes, e.g. \"x\" (default none).")}}));
    return tools;
}

bool isLiveTool(const std::string& name) {
    return name == "viewport_image" || name == "view_set" || name == "view_turntable" || name == "stroke_screen";
}

// A studio's policy (core/ManagedPolicy.h) may allow only some kinds of tool: the rest are neither listed nor run.
bool policyAllowsTool(const Command* cmd, const std::string& name, std::string* why = nullptr) {
    std::vector<std::string> params;
    if (cmd)
        for (const auto& p : cmd->params) params.push_back(p.name);
    return policy::mcpToolAllowed(name, params, why);
}

// One line in the audit log (core/AuditLog.h) per live-mode call, refusals too.
void auditLive(const std::string& client, const std::string& name, const Args& args, bool ok, const std::string& message,
               int64_t ms = -1) {
    audit::Entry entry;
    entry.client = "mcp-live:" + client;
    entry.tool = "mcp-live";
    entry.command = name;
    entry.args = audit::summarizeArgs(args);
    entry.ok = ok;
    entry.message = message;
    entry.ms = ms;
    audit::record(entry);
}

// Commands that start a new scene (Document::setMesh: the edit history is cleared).
bool replacesScene(const std::string& name) {
    static const char* const kNames[] = {"new_sphere", "new_plane", "new_head", "new_starter_mesh", "new_bench_sphere",
                                         "open", "open_obj", "open_project", "calibrate", "bridge_import", "bridge_receive",
                                         "blender_receive", "maya_receive"};
    for (const char* n : kNames)
        if (name == n) return true;
    return false;
}

std::string degrees(float radians) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.0f", double(glm::degrees(radians)));
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// State: the listener thread and what the main thread is doing with the connection
// ---------------------------------------------------------------------------

struct App::AiLinkState {
    link::AiLinkListener listener;
    link::AiLinkConn conn;
    std::mutex connMutex;  // conn: reads (thread) and writes (main thread)
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> drop{false}; // the main thread asks the thread to close the connection

    struct Event {
        enum class Kind { Connected, Line, Closed } kind;
        std::string line;
    };
    std::mutex eventMutex;
    std::deque<Event> events;

    std::unique_ptr<link::AiLinkSession> session;
    std::deque<std::string> waiting; // lines that came while a dialog was open
    double approvalSince = 0.0;
    bool approving = false;
    bool focusDialog = false; // a dialog just opened: bring it to the front once
    struct Pending {
        link::AiLinkCall call;
        std::string reason;
    };
    std::optional<Pending> confirm;
    // A call whose command works on a worker (uv_auto: App::commandRunning): its reply, and the next call, wait for it.
    struct Running {
        link::AiLinkCall call;
        uint32_t part = 0;
        size_t undoCount = 0;
        uint64_t revision = 0;
        std::chrono::steady_clock::time_point start;
    };
    std::optional<Running> running;
    std::string error; // why the listener isn't running
    int commands = 0;  // tool calls run this connection

    void push(Event e) {
        {
            std::lock_guard lock(eventMutex);
            events.push_back(std::move(e));
        }
        wakeMainLoop();
    }

    void send(const std::string& line) {
        std::lock_guard lock(connMutex);
        conn.writeLine(line);
    }

    void run() {
        while (!stop) {
            if (!connected) {
                link::AiLinkConn client;
                bool refused = false;
                if (!listener.accept(client, 200, &refused)) continue;
                std::lock_guard lock(connMutex);
                conn = std::move(client);
                connected = true;
                drop = false;
                push({Event::Kind::Connected, {}});
                continue;
            }
            // A second client while one is connected: told so and closed.
            {
                link::AiLinkConn other;
                if (listener.accept(other, 0)) other.writeLine(R"({"error":"SculptForge is already connected to an AI tool"})");
            }
            std::string line;
            int got;
            {
                std::lock_guard lock(connMutex);
                got = drop ? -1 : conn.readLine(line, 50);
                if (got < 0) conn.close();
            }
            if (got > 0) push({Event::Kind::Line, std::move(line)});
            else if (got < 0) {
                connected = false;
                push({Event::Kind::Closed, {}});
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); // (lets the main thread write)
            }
        }
    }
};

void App::AiLinkStateDeleter::operator()(AiLinkState* s) const {
    if (!s) return;
    s->stop = true;
    if (s->thread.joinable()) s->thread.join();
    delete s;
}

// ---------------------------------------------------------------------------
// The switch
// ---------------------------------------------------------------------------

void App::setAiLiveControl(bool on) {
    if (std::string why; on && !policy::mcpLiveAllowed(&why)) {
        status_ = why;
        return;
    }
    aiSettings_.liveControl = on;
    std::string err;
    if (!uiConfigDeterministic_ && !aiSettingsPath_.empty() && !aiSettings_.save(aiSettingsPath_, err)) status_ = err;
    syncAiLink();
    if (!aiLink_ && on && ai::aiEnabled()) return; // (status_ says why)
    status_ = on && aiLink_ ? tr("AI tools can ask to control this window (Preferences > AI)")
                            : tr("AI tools can't control this window");
}

void App::syncAiLink() {
    const bool want = ai::aiEnabled() && aiSettings_.liveControl && policy::mcpLiveAllowed(); // (a policy: never listen)
    if (want == bool(aiLink_)) return;
    if (!want) {
        if (aiLink_ && aiLink_->session) consoleEcho("ai_live", {}, {true, "AI tool disconnected"});
        aiLink_.reset();
        return;
    }
    std::unique_ptr<AiLinkState, AiLinkStateDeleter> s(new AiLinkState);
    std::string err;
    const std::string endpoint = link::aiLinkEndpoint(err);
    if (endpoint.empty() || !s->listener.open(endpoint, err)) {
        status_ = std::string(tr("Live mode can't start: ")) + err;
        SDL_Log("Live mode: %s", err.c_str());
        aiSettings_.liveControl = false;
        return;
    }
    SDL_Log("Live mode: listening on %s", endpoint.c_str());
    AiLinkState* raw = s.get();
    s->thread = std::thread([raw] { raw->run(); });
    aiLink_ = std::move(s);
}

void App::aiLinkDisconnect(const std::string& why) {
    if (!aiLink_ || !aiLink_->connected) return;
    aiLink_->drop = true;
    {
        std::lock_guard lock(aiLink_->connMutex);
        aiLink_->conn.shutdown();
    }
    status_ = why;
}

void App::aiLinkPicked(const std::string& path) {
    if (aiLink_ && aiLink_->session) aiLink_->session->policy.addPicked(path);
    aiLinkPickedPaths_.push_back(path);
}

std::string App::aiLinkClient() const {
    return aiLink_ && aiLink_->session ? aiLink_->session->client() : std::string();
}

int App::aiLinkStateValue() const {
    if (!aiLink_) return 0;
    if (!aiLink_->session) return 1;
    if (aiLink_->session->state() == link::AiLinkSession::State::Pairing) return 2;
    return aiLink_->confirm ? 4 : 3;
}

void App::drawAiLinkPrefs() {
    if (!ai::aiEnabled()) return;
    if (!policy::mcpLiveAllowed()) { // the studio's policy says never: the switch stays off and nothing listens
        ImGui::BeginDisabled();
        bool off = false;
        ImGui::Checkbox(tr("Let AI tools control this window##prefs.ai_live"), &off);
        ImGui::EndDisabled();
        ui::ManagedBadge(); // (its own line: the narrow Preferences window would clip it)
        return;
    }
    bool on = aiSettings_.liveControl;
    if (ImGui::Checkbox(tr("Let AI tools control this window##prefs.ai_live"), &on)) setAiLiveControl(on);
    ui::HelpAction(*this, "prefs.ai_live");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", tr("An AI tool on this computer can ask to work in this window while you watch. You approve "
                                "it with a 6-digit code. Every step lands in Undo. Files outside the project, scripts and "
                                "the network ask you first. What it sees goes to that tool's AI service. Nothing listens "
                                "on the network. To set up your tool: Help > Connect an AI tool."));
    ImGui::PopStyleColor();
    if (aiLink_ && !aiLink_->error.empty()) ImGui::TextColored(theme::danger, "%s", aiLink_->error.c_str());
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void App::pollAiLink() {
    syncAiLink();
    if (!aiLink_) return;
    AiLinkState& s = *aiLink_;
    std::deque<AiLinkState::Event> events;
    {
        std::lock_guard lock(s.eventMutex);
        events.swap(s.events);
    }
    for (auto& e : events) {
        if (e.kind == AiLinkState::Event::Kind::Connected) {
            s.session = std::make_unique<link::AiLinkSession>(link::makePairingCode());
            s.running.reset();
            s.session->policy.projectDir = macroProjectDir();
            for (const std::string& p : aiLinkPickedPaths_) s.session->policy.addPicked(p);
            s.waiting.clear();
            s.confirm.reset();
            s.approving = false;
            s.commands = 0;
        } else if (e.kind == AiLinkState::Event::Kind::Closed) {
            if (s.session && s.session->state() == link::AiLinkSession::State::Paired) {
                status_ = tr("AI tool disconnected");
                consoleEcho("ai_live", {}, {true, "AI tool disconnected"});
            }
            s.session.reset();
            s.waiting.clear();
            s.confirm.reset();
            s.running.reset();
            s.approving = false;
        } else if (s.session) {
            s.waiting.push_back(std::move(e.line));
        }
    }
    if (!s.session) return;
    if (s.approving && seconds() - s.approvalSince > kApprovalSeconds) {
        s.send(s.session->deny());
        s.approving = false;
        aiLinkDisconnect(tr("AI connection request timed out"));
        return;
    }
    if (s.running && !commandRunning()) { // its worker is done: the reply goes out now
        const AiLinkState::Running r = std::move(*s.running);
        s.running.reset();
        CommandResult done{lastCommandOk_, status_};
        takeCommandResult(done);
        labelAiUndo(r.part, r.undoCount, r.revision);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - r.start).count();
        auditLive(s.session->client(), r.call.name, r.call.args, done.ok, done.message, int64_t(ms));
        s.send(link::AiLinkSession::resultReply(r.call.id, done.ok, done.message));
    }
    // One message at a time; none while a dialog waits for the user or a command works on a worker.
    while (!s.waiting.empty() && !s.confirm && !s.running && s.session && s.session->state() != link::AiLinkSession::State::Closed) {
        const bool wasPairing = s.session->state() == link::AiLinkSession::State::Pairing;
        const std::string line = std::move(s.waiting.front());
        s.waiting.pop_front();
        link::AiLinkSession::Step step = s.session->handle(line, seconds());
        using K = link::AiLinkSession::Step::Kind;
        if (!step.reply.empty() && step.kind != K::Execute && step.kind != K::ListTools) s.send(step.reply);
        if (wasPairing && s.session->state() == link::AiLinkSession::State::Paired) { // the right code
            s.approving = false;
            status_ = std::string(tr("AI connected: ")) + s.session->client();
            consoleEcho("ai_live", {}, {true, "AI connected: " + s.session->client()});
        }
        switch (step.kind) {
        case K::None:
        case K::Reply: break;
        case K::Close:
            aiLinkDisconnect(tr("AI tool disconnected"));
            return;
        case K::ShowApproval:
            s.approving = true;
            s.focusDialog = true;
            s.approvalSince = seconds();
            break;
        case K::ListTools: {
            Json tools = Json::array();
            const Json commands = mcpToolList(commands_), live = liveTools();
            for (const Json& t : commands.items()) // (mcpToolList leaves out what the policy doesn't allow)
                if (!link::AiLinkPolicy::hidden(t["name"].asString())) tools.push(t);
            for (const Json& t : live.items())
                if (policyAllowsTool(nullptr, t["name"].asString())) tools.push(t);
            s.send(link::AiLinkSession::listReply(step.call.id, std::move(tools)));
            break;
        }
        case K::Execute:
        case K::Confirm: {
            const std::string& name = step.call.name;
            const Command* cmd = commands_.find(name);
            if (!cmd && !isLiveTool(name)) {
                s.send(link::AiLinkSession::resultReply(step.call.id, false, "Unknown tool: " + name));
                break;
            }
            if (std::string why; !policyAllowsTool(isLiveTool(name) ? nullptr : cmd, name, &why) ||
                                 (audit::required() && !audit::writable(&why))) {
                auditLive(s.session->client(), name, step.call.args, false, why);
                s.send(link::AiLinkSession::resultReply(step.call.id, false, why));
                break;
            }
            s.session->policy.projectDir = macroProjectDir();
            link::AiDecision d = s.session->policy.classify(isLiveTool(name) ? nullptr : cmd, name, step.call.args);
            // A new scene clears the history: unsaved work couldn't be undone back.
            if (d.verdict == link::AiVerdict::Allow && doc_.dirty() && replacesScene(name))
                d = link::confirmBecause("Replaces the scene: your unsaved changes would be lost");
            if (d.verdict == link::AiVerdict::Deny) {
                auditLive(s.session->client(), name, step.call.args, false, d.reason);
                s.send(link::AiLinkSession::resultReply(step.call.id, false, d.reason));
            } else if (d.verdict == link::AiVerdict::Confirm) {
                // The dialog and the status line in the interface language; the tool only ever gets English.
                std::string shown = d.format ? tr(d.format) : d.reason;
                if (const size_t at = shown.find("%s"); d.format && at != std::string::npos) shown.replace(at, 2, d.subject);
                s.confirm = AiLinkState::Pending{std::move(step.call), shown};
                s.focusDialog = true;
                status_ = std::string(tr("Your AI tool asks: ")) + shown;
            } else {
                aiLinkExecute(step.call);
            }
            break;
        }
        }
    }
}

void App::aiLinkExecute(const link::AiLinkCall& call) {
    AiLinkState& s = *aiLink_;
    ++s.commands;
    const auto start = std::chrono::steady_clock::now();
    const auto elapsedMs = [&] {
        return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };
    if (isLiveTool(call.name)) {
        bool ok = true;
        Json content = aiLiveTool(call.name, call.args, ok);
        auditLive(s.session->client(), call.name, call.args, ok, ok ? "" : "failed", elapsedMs()); // (never the image)
        s.send(link::AiLinkSession::contentReply(call.id, std::move(content), !ok));
        return;
    }
    // As if typed in the console; the steps it adds to the history say who made them.
    const uint32_t part = activePartInfoId();
    const size_t before = doc_.undo.undoCount();
    const uint64_t revision = doc_.undo.revision();
    runCommand(call.name, call.args);
    if (lastCommandOk_ && commandRunning()) {
        s.running = AiLinkState::Running{call, part, before, revision, start};
        return;
    }
    labelAiUndo(part, before, revision);
    auditLive(s.session->client(), call.name, call.args, lastCommandOk_, status_, elapsedMs());
    s.send(link::AiLinkSession::resultReply(call.id, lastCommandOk_, status_));
}

uint32_t App::activePartInfoId() const { return doc_.activePartInfo().id; }

void App::labelAiUndo(uint32_t part, size_t before, uint64_t revision) {
    if (doc_.undo.revision() == revision || !doc_.undo.canUndo()) return;
    // New steps on the same Part: every one of them; on another Part (a new Part made active): its last one.
    const size_t added = doc_.activePartInfo().id == part && doc_.undo.undoCount() > before ? doc_.undo.undoCount() - before : 1;
    for (size_t k = 0; k < added; ++k) {
        UndoRecord* r = k == 0 ? doc_.undo.lastUndo() : nullptr;
        if (k > 0) r = const_cast<UndoRecord*>(doc_.undo.undoStep(k));
        if (r && r->label.rfind("AI: ", 0) != 0) r->label = "AI: " + r->label;
    }
}

// ---------------------------------------------------------------------------
// The live-window tools
// ---------------------------------------------------------------------------

bool App::aiApplyView(const Args& args, std::string& error) {
    const std::string preset = argString(args, "preset");
    if (!preset.empty()) {
        static const struct { const char* name; float yaw, pitch; } kPresets[] = {
            {"front", 0, 0}, {"back", 180, 0}, {"right", 90, 0}, {"left", -90, 0},
            {"top", 0, -89}, {"bottom", 0, 89}, {"three_quarter", 35, -15}};
        bool found = false;
        for (const auto& p : kPresets)
            if (preset == p.name) {
                camera_.yaw = glm::radians(p.yaw);
                camera_.pitch = glm::radians(p.pitch);
                camera_.roll = 0.0f;
                found = true;
            }
        if (!found) {
            error = "preset must be front, back, left, right, top, bottom or three_quarter";
            return false;
        }
    }
    if (args.contains("yaw")) camera_.yaw = glm::radians(float(argNumber(args, "yaw", 0.0))), camera_.roll = 0.0f;
    if (args.contains("elevation"))
        camera_.pitch = -glm::radians(std::clamp(float(argNumber(args, "elevation", 0.0)), -89.0f, 89.0f)), camera_.roll = 0.0f;
    if (args.contains("ortho")) camera_.ortho = argBool(args, "ortho", false), autoOrtho_ = false;
    const std::string frame = argString(args, "frame");
    if (frame == "all") camera_.frame(sceneBounds());
    else if (frame == "active") camera_.frame(doc_.mesh.bounds());
    else if (!frame.empty()) {
        const int i = doc_.findPart(frame);
        if (i < 0) {
            error = "No Part \"" + frame + "\" (see scene_overview)";
            return false;
        }
        camera_.frame(doc_.partMesh(i).bounds());
    }
    if (args.contains("zoom")) {
        const float z = float(argNumber(args, "zoom", 1.0));
        if (!(z > 0.05f && z < 20.0f)) {
            error = "zoom must be between 0.05 and 20";
            return false;
        }
        camera_.distance /= z;
    }
    return true;
}

bool App::aiRenderView(const Camera& cam, int width, int height, Image& img, std::string& error) {
    img.width = width;
    img.height = height;
    RenderSettings settings = renderSettings_;
    settings.dimInactive = false;
    settings.showMask = true; // (the agent sees the mask it painted)
    settings.resolutionScale = 1.0f;
    constexpr GLenum kFramebufferBinding = 0x8CA6, kViewport = 0x0BA2;
    GLint fbo = 0, viewport[4] = {};
    gl::GetIntegerv(kFramebufferBinding, &fbo);
    gl::GetIntegerv(kViewport, viewport);
    const bool ok = renderer_.renderToImage(cam, width, height, settings, img.rgba, error);
    gl::BindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
    gl::Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return ok;
}

Json App::aiLiveTool(const std::string& name, const Args& args, bool& ok) {
    Json content = Json::array();
    auto text = [&](const std::string& t) {
        Json j = Json::object();
        j.set("type", "text");
        j.set("text", t);
        content.push(std::move(j));
    };
    auto image = [&](const Image& img) {
        std::vector<uint8_t> png;
        if (!img.encodePng(png)) return false;
        Json j = Json::object();
        j.set("type", "image");
        j.set("data", link::base64Encode(png.data(), png.size()));
        j.set("mimeType", "image/png");
        content.push(std::move(j));
        return true;
    };
    auto fail = [&](const std::string& e) {
        ok = false;
        text(e);
        return content;
    };
    ok = true;
    std::string err;
    const auto viewText = [this] {
        return "Camera: yaw " + degrees(camera_.yaw) + ", elevation " + degrees(-camera_.pitch) +
               (camera_.ortho ? ", orthographic" : ", perspective") + "; active Part: " + doc_.activePartInfo().name;
    };

    if (name == "view_set" || name == "viewport_image") {
        if (!aiApplyView(args, err)) return fail(err);
        if (name == "view_set") {
            text(viewText());
            return content;
        }
        const CanvasRect c = canvasRect();
        const float aspect = c.h > 1.0f ? c.w / c.h : 1.0f;
        const int longSide = std::clamp(int(argNumber(args, "size", 768)), 256, 1536);
        const int w = aspect >= 1.0f ? longSide : std::max(1, int(std::lround(longSide * aspect)));
        const int h = aspect >= 1.0f ? std::max(1, int(std::lround(longSide / aspect))) : longSide;
        Camera cam = camera_;
        cam.aspect = float(w) / float(h);
        Image img;
        if (!aiRenderView(cam, w, h, img, err) || !image(img)) return fail("Can't render the view: " + err);
        text(viewText() + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
        return content;
    }

    if (name == "view_turntable") {
        const int views = int(argNumber(args, "views", 4));
        if (views != 4 && views != 6 && views != 8) return fail("views must be 4, 6 or 8");
        const int tile = std::clamp(int(argNumber(args, "size", 320)), 192, 512);
        const int cols = views / 2, rows = 2;
        Camera cam = camera_;
        cam.frame(sceneBounds());
        cam.aspect = 1.0f;
        cam.roll = 0.0f;
        cam.pitch = -glm::radians(std::clamp(float(argNumber(args, "elevation", 10.0)), -89.0f, 89.0f));
        Image sheet;
        sheet.width = cols * tile;
        sheet.height = rows * tile;
        sheet.rgba.assign(size_t(sheet.width) * sheet.height * 4, 0);
        std::string yaws;
        for (int v = 0; v < views; ++v) {
            cam.yaw = glm::radians(360.0f * float(v) / float(views));
            Image img;
            if (!aiRenderView(cam, tile, tile, img, err)) return fail("Can't render the view: " + err);
            const int ox = (v % cols) * tile, oy = (v / cols) * tile;
            for (int y = 0; y < tile; ++y)
                std::memcpy(&sheet.rgba[(size_t(oy + y) * sheet.width + ox) * 4], &img.rgba[size_t(y) * tile * 4], size_t(tile) * 4);
            yaws += (v ? ", " : "") + degrees(cam.yaw);
        }
        if (!image(sheet)) return fail("Can't encode the image");
        text(std::to_string(views) + " views, left to right then top to bottom, yaw " + yaws + " degrees (0 = front, 90 = "
             "the model's right side); elevation " + degrees(-cam.pitch) + ". The user's camera didn't move.");
        return content;
    }

    if (name == "stroke_screen") {
        const CanvasRect c = canvasRect();
        if (c.w < 2.0f || c.h < 2.0f) return fail("The viewport is too small");
        std::string worldPoints;
        int hits = 0, misses = 0;
        float radius = 0.0f;
        const float radiusFrac = std::clamp(float(argNumber(args, "radius", 0.04)), 0.002f, 0.5f);
        std::stringstream in(argString(args, "points"));
        std::string token;
        while (in >> token) {
            float u = 0, v = 0;
            if (std::sscanf(token.c_str(), "%f,%f", &u, &v) != 2 || !std::isfinite(u) || !std::isfinite(v))
                return fail("points must be \"u,v u,v ...\" in 0..1 (bad: " + token + ")");
            glm::vec3 origin, dir;
            partCamera().screenRay(u * c.w, v * c.h, c.w, c.h, origin, dir); // (the active Part's space, #94)
            RayHit hit;
            if (!doc_.bvh.raycast(doc_.mesh, origin, dir, hit)) {
                ++misses;
                continue;
            }
            hit.position = glm::vec3(doc_.activePartInfo().transform * glm::vec4(hit.position, 1.0f)); // stroke takes scene points
            if (radius == 0.0f) radius = camera_.worldPerPixel(hit.position, c.h) * radiusFrac * c.h;
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s%g,%g,%g", worldPoints.empty() ? "" : " ", double(hit.position.x),
                          double(hit.position.y), double(hit.position.z));
            worldPoints += buf;
            ++hits;
            if (hits > 2000) return fail("at most 2000 points");
        }
        if (hits == 0) return fail("No point is on the active Part (see viewport_image; 0,0 is the top left)");
        const glm::vec3 f = camera_.forward();
        char view[96];
        std::snprintf(view, sizeof view, "%g,%g,%g", double(f.x), double(f.y), double(f.z));
        Args a{{"points", worldPoints}, {"view", std::string(view)}, {"radius", double(radius)},
               {"strength", argNumber(args, "strength", 0.5)}, {"invert", argBool(args, "invert", false)}};
        if (args.contains("brush")) a["brush"] = argString(args, "brush");
        if (args.contains("symmetry")) a["symmetry"] = argString(args, "symmetry");
        const uint32_t part = activePartInfoId();
        const size_t before = doc_.undo.undoCount();
        const uint64_t revision = doc_.undo.revision();
        runCommand("stroke", a);
        labelAiUndo(part, before, revision);
        ok = lastCommandOk_;
        text(status_ + (misses ? " (" + std::to_string(misses) + " points were off the model)" : ""));
        return content;
    }
    return fail("Unknown tool: " + name);
}

// ---------------------------------------------------------------------------
// Dialogs and the badge
// ---------------------------------------------------------------------------

void App::drawAiLink() {
    if (!aiLink_ || !aiLink_->session) return;
    AiLinkState& s = *aiLink_;
    const float sc = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 centre(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    const ImGuiWindowFlags dialog = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize;

    if (s.approving && s.session->state() == link::AiLinkSession::State::Pairing) {
        ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f)); // (Appearing: an auto-sized window has no size yet)
        if (std::exchange(s.focusDialog, false)) ImGui::SetNextWindowFocus();
        bool open = true;
        if (ImGui::Begin(tr("Let an AI tool control this window?###ailinkapprove"), nullptr, dialog)) {
            ImGui::PushTextWrapPos(kDialogWrap * sc);
            ImGui::TextWrapped(tr("\"%s\" on this computer asks to sculpt in this window."), s.session->client().c_str());
            ImGui::TextUnformatted(tr("Click Approve, or tell your AI tool this code:"));
            const std::string& code = s.session->code();
            if (fontCardTitle_) ImGui::PushFont(fontCardTitle_); // (digits at display size, not a scaled-up 15 px)
            ImGui::TextColored(theme::accent, "%s %s", code.substr(0, 3).c_str(), code.substr(3).c_str());
            if (fontCardTitle_) ImGui::PopFont();
            ImGui::TextDisabled("%s", tr("Approval lasts until it disconnects. Every step lands in Undo. Files outside the "
                                         "project, scripts and the network ask you first. What it sees goes to that "
                                         "tool's AI service."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const char* labels[] = {tr("Approve##ailink"), tr("Deny##ailink")};
            const ImVec2 size(dialogButtonWidth(labels, sc), 0);
            const bool approve = ImGui::Button(labels[0], size);
            ImGui::SameLine();
            if (ImGui::Button(labels[1], size) || ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
            if (approve) {
                s.send(s.session->approve());
                s.approving = false;
                showConnectAi_ = false; // its steps are done; the badge takes over
                status_ = std::string(tr("AI connected: ")) + s.session->client();
                consoleEcho("ai_live", {}, {true, "AI connected: " + s.session->client()});
            }
        }
        ImGui::End();
        if (!open) {
            s.send(s.session->deny());
            s.approving = false;
            aiLinkDisconnect(tr("AI connection declined"));
        }
        return;
    }
    if (s.session->state() != link::AiLinkSession::State::Paired) return;

    // The badge: bottom right of the canvas (clear of the history strip on top), while connected.
    const CanvasRect c = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w - 12 * sc, c.y + c.h - 12 * sc), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8 * sc);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::accent);
    if (ImGui::Begin("##ailinkbadge", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        const float pulse = 0.55f + 0.45f * std::sin(float(seconds()) * 3.0f);
        ImVec4 dot = theme::accent;
        dot.w = pulse;
        ImGui::TextColored(dot, ICON_LC_SPARKLES);
        ImGui::SameLine();
        ImGui::Text(tr("AI connected: %s"), s.session->client().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(tr("%d steps"), s.commands);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Disconnect##ailink"))) aiLinkDisconnect(tr("AI tool disconnected"));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Stop the AI tool now. Its steps stay in Undo."));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (!s.confirm) return;
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f)); // (Appearing: an auto-sized window has no size yet)
    if (std::exchange(s.focusDialog, false)) ImGui::SetNextWindowFocus();
    bool open = true;
    bool allow = false;
    if (ImGui::Begin(tr("Allow this AI step?###ailinkconfirm"), nullptr, dialog)) {
        ImGui::PushTextWrapPos(kDialogWrap * sc);
        ImGui::TextWrapped(tr("%s wants to run:"), s.session->client().c_str());
        if (fontMono_) ImGui::PushFont(fontMono_);
        ImGui::TextColored(theme::accent, "%s", s.confirm->call.name.c_str());
        const std::string args = link::describeArgs(s.confirm->call.args);
        if (!args.empty()) ImGui::TextWrapped("%s", args.c_str());
        if (fontMono_) ImGui::PopFont();
        ImGui::TextColored(theme::danger, "%s", s.confirm->reason.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const char* labels[] = {tr("Allow once##ailink"), tr("Deny##ailinkconfirm")};
        const ImVec2 size(dialogButtonWidth(labels, sc), 0);
        allow = ImGui::Button(labels[0], size);
        ImGui::SameLine();
        if (ImGui::Button(labels[1], size) || ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    }
    ImGui::End();
    if (allow) {
        AiLinkState::Pending p = std::move(*s.confirm);
        s.confirm.reset();
        // The user said yes to this file: later steps may use it without asking again (as if picked).
        for (const char* key : {"path", "file", "folder", "out"})
            if (const std::string f = argString(p.call.args, key); !f.empty()) aiLinkPicked(f);
        aiLinkExecute(p.call);
    } else if (!open) {
        auditLive(s.session->client(), s.confirm->call.name, s.confirm->call.args, false, "Declined by the user");
        s.send(link::AiLinkSession::declinedReply(s.confirm->call.id, s.confirm->call.name));
        s.confirm.reset();
        status_ = tr("AI step declined");
    }
}

} // namespace sf
