// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The local API (#36, a first step for #20): other programs on this computer run SculptForge commands in the
// open app. Off by default (Preferences > Input > Local API). It listens on 127.0.0.1 only and needs the token
// in <settings>/api.json (readable by this user only). POST /command with {"command": "...", "args": {...}}
// returns {"ok": ..., "message": "..."}; commands run on the main thread between frames, like a click would.
#include "app/App.h"
#include "core/Json.h"
#include "link/HttpServer.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>

namespace sf {

struct LiveApiState {
    link::HttpServer server;
    std::string token;
    struct Job {
        std::string command;
        Args args;
        bool done = false;
        bool ok = false;
        std::string message;
    };
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::shared_ptr<Job>> queue;
};

namespace {
Args argsFromJson(const Json& j) {
    Args a;
    if (!j.isObject()) return a;
    for (const auto& [key, v] : j.members()) {
        if (v.isBool()) a[key] = v.asBool();
        else if (v.isNumber()) a[key] = v.asNumber();
        else if (v.isString()) a[key] = v.asString();
        else a[key] = v.dump(false);
    }
    return a;
}
} // namespace

void App::setLiveApi(bool on) {
    if (on == bool(liveApi_)) return;
    if (!on) {
        liveApi_->server.stop();
        {
            std::lock_guard lock(liveApi_->mutex);
            for (auto& job : liveApi_->queue) job->done = true, job->message = "SculptForge's local API was turned off";
            liveApi_->queue.clear();
        }
        liveApi_->cv.notify_all();
        liveApi_.reset();
        std::error_code ec;
        std::filesystem::remove(configDir_ + "api.json", ec);
        status_ = "Local API off";
        return;
    }
    auto st = std::make_shared<LiveApiState>();
    std::random_device rd;
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", unsigned(rd() & 0xff));
    st->token = hex;
    link::HttpServer::Options opt;
    opt.bindAddress = "127.0.0.1";
    opt.port = 47420;
    std::weak_ptr<LiveApiState> weak = st;
    const std::string token = st->token;
    std::string error;
    const bool started = st->server.start(opt, [weak, token](const link::HttpRequest& req) {
        auto s = weak.lock();
        if (!s) return link::HttpResponse::json(503, R"({"ok":false,"message":"stopped"})");
        // Not for web pages: browsers send Origin, and a rebound DNS name shows in Host.
        if (!req.header("origin").empty())
            return link::HttpResponse::json(403, R"({"ok":false,"message":"requests from web pages are refused"})");
        const std::string host = req.header("host");
        if (!host.empty() && host.rfind("127.0.0.1", 0) != 0 && host.rfind("localhost", 0) != 0)
            return link::HttpResponse::json(403, R"({"ok":false,"message":"only 127.0.0.1"})");
        if (req.header("x-sculptforge-token") != token)
            return link::HttpResponse::json(401, R"({"ok":false,"message":"missing or wrong X-SculptForge-Token"})");
        if (req.method == "GET" && req.path == "/info") {
            Json j;
            j.set("app", std::string("SculptForge"));
            j.set("version", std::string(SCULPTFORGE_VERSION));
            return link::HttpResponse::json(200, j.dump(false));
        }
        if (req.method != "POST" || req.path != "/command")
            return link::HttpResponse::json(404, R"({"ok":false,"message":"POST /command or GET /info"})");
        Json body;
        std::string err;
        if (!Json::parse(req.body, body, err) || !body["command"].isString())
            return link::HttpResponse::json(400, R"({"ok":false,"message":"body must be {\"command\": \"...\", \"args\": {...}}"})");
        auto job = std::make_shared<LiveApiState::Job>();
        job->command = body["command"].asString();
        job->args = argsFromJson(body["args"]);
        std::unique_lock lock(s->mutex);
        s->queue.push_back(job);
        wakeMainLoop();
        if (!s->cv.wait_for(lock, std::chrono::seconds(120), [&] { return job->done; }))
            return link::HttpResponse::json(504, R"({"ok":false,"message":"timed out waiting for the app"})");
        Json out;
        out.set("ok", job->ok);
        out.set("message", job->message);
        return link::HttpResponse::json(job->ok ? 200 : 422, out.dump(false));
    }, error);
    if (!started) {
        status_ = "Local API: " + error;
        uiPrefs_.liveApi = false;
        return;
    }
    // The token and port for clients, readable by this user only.
    Json info;
    info.set("port", double(st->server.port()));
    info.set("token", st->token);
    const std::string path = configDir_ + "api.json";
    {
        std::ofstream f(path);
        f << info.dump(true);
    }
    std::error_code ec;
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    liveApi_ = st;
    status_ = "Local API on 127.0.0.1:" + std::to_string(st->server.port()) + " (token in " + path + ")";
}

void App::pollLiveApi() {
    if (!liveApi_) return;
    std::deque<std::shared_ptr<LiveApiState::Job>> jobs;
    {
        std::lock_guard lock(liveApi_->mutex);
        jobs.swap(liveApi_->queue);
    }
    if (jobs.empty()) return;
    for (auto& job : jobs) {
        runCommand(job->command, job->args);
        std::lock_guard lock(liveApi_->mutex);
        job->ok = lastCommandOk_;
        job->message = status_;
        job->done = true;
        if (status_.size() > 200) status_ = job->command + ": done (local API)"; // e.g. mesh_read's data
    }
    liveApi_->cv.notify_all();
}

} // namespace sf
