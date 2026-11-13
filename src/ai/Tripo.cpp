// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Tripo.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "core/AuditLog.h"
#include "core/Json.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace sf::ai::tripo {

namespace {

constexpr int kMaxMinutes = 10;     // the whole wait for one model (Tripo answers in about a minute)
int gMaxSeconds = kMaxMinutes * 60;
constexpr int kTimeoutSeconds = 60; // one request
constexpr int kMaxPollFailures = 6; // transient answers in a row before giving up

glbfile::Transport gTransport; // (tests)
std::vector<int>* gWaits = nullptr;

void status(GenerationProgress* p, const std::string& text, int percent = -1) {
    if (!p) return;
    p->setStatus(text);
    p->percent = percent;
}

bool wait(GenerationProgress* p, int seconds) {
    if (gTransport) {
        if (gWaits) gWaits->push_back(seconds);
        return !(p && p->cancel);
    }
    for (int i = 0; i < seconds * 10; ++i) {
        if (p && p->cancel) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !(p && p->cancel);
}

std::string clip(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

// A flat error body: {"code": N, "message": "...", "suggestion": "..."}; N = 0 or missing is no error.
int bodyCode(const Json& j) { return j["code"].isNumber() ? int(j["code"].asNumber()) : 0; }

const HttpResponse kRefused = [] {
    HttpResponse r;
    r.error = "refused";
    return r;
}();

HttpResponse send(const std::string& method, const std::string& key, const std::string& path, const std::string& contentType,
                  const std::string& body) {
    const std::string url = std::string(kApiBase) + path;
    if (!apiUrl(url)) return kRefused;
    std::vector<std::string> headers = {"Authorization: Bearer " + key, "Accept: application/json"};
    if (!contentType.empty()) headers.push_back(contentType);
    if (gTransport) return gTransport(method, url, headers, body);
    if (method == "POST") return httpPostNoRedirect(url, headers, body, kTimeoutSeconds);
    // GET: this one URL only, the key on this host only; no hop elsewhere is followed.
    HttpResponse r;
    std::string got;
    r = httpGetRestricted(
        url, [](const std::string& u) { return apiUrl(u); },
        [&](const std::string& u) { return apiUrl(u) ? headers : std::vector<std::string>{}; }, kTimeoutSeconds,
        [&](const char* d, size_t n) {
            got.append(d, n);
            return got.size() < (1u << 20);
        });
    r.body = std::move(got);
    return r;
}

std::string taskPath(const std::string& id) {
    std::string clean;
    for (char c : id.substr(0, 80))
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') clean += c;
    return "/tasks/" + clean;
}

} // namespace

const std::vector<Model>& models() {
    static const std::vector<Model> m = {
        {"v3.1-20260211", "Tripo v3.1 (H3.1) - recommended"},
        {"v3.0-20250812", "Tripo v3.0"},
    };
    return m;
}

std::string resolvedModel(const std::string& model) {
    for (const Model& m : models())
        if (model == m.id) return model;
    return kDefaultModel;
}

bool apiUrl(const std::string& url) {
    net::UrlParts u;
    if (!net::parseUrl(url, u) || u.scheme != "https" || u.host != kApiHost || u.port != 443) return false;
    return u.path.rfind("/v3/", 0) == 0 && u.path.find("..") == std::string::npos;
}

bool downloadUrlAllowed(const std::string& url) { return glbfile::urlOnDomains(url, {"tripo3d.com", "tripo3d.ai"}); }

std::string imageBody(const std::string& model, const std::string& fileToken) {
    Json b = Json::object();
    b.set("input", fileToken);
    b.set("model", resolvedModel(model));
    b.set("texture", false); // the shape is what we sculpt (and the cheaper tier)
    b.set("pbr", false);     // (pbr true would force texture on)
    return b.dump();
}

std::string textBody(const std::string& model, const std::string& prompt) {
    Json b = Json::object();
    b.set("prompt", clip(prompt, 1024));
    b.set("model", resolvedModel(model));
    b.set("texture", false);
    b.set("pbr", false);
    return b.dump();
}

std::string uploadBody(const std::string& png, std::string& contentTypeHeader) {
    return multipartBody({{"file", png, "input.png", "image/png"}}, contentTypeHeader);
}

namespace {
// The error a body carries, "" when it carries none.
std::string codeMessage(const Json& j, bool& transient) {
    const int code = bodyCode(j);
    if (code == 0) return {};
    switch (code) {
    case 2: case 1002: return "Tripo rejected the API key";
    case 2010: return "Out of credits on your Tripo account";
    case 1007: return (transient = true, "Tripo is rate limiting - wait a moment and try again");
    case 2000: return (transient = true, "Tripo is already running your other generations - try again shortly");
    case 2008: return "Tripo refused the content";
    case 2015: return "Tripo retired this model version - update SculptForge";
    case 2018: return "Tripo found this model too complex";
    case 1000: case 1001: return (transient = true, "Tripo had a server error - try again");
    case 1003: case 1004: return "Tripo refused the request (" + clip(j["message"].asString(), 120) + ")";
    default: break;
    }
    const std::string msg = j["message"].isString() ? clip(j["message"].asString(), 160) : std::string();
    return "Tripo: error " + std::to_string(code) + (msg.empty() ? std::string() : " (" + msg + ")");
}
} // namespace

std::string errorMessage(long httpStatus, const std::string& transportError, const std::string& body, bool& transient) {
    transient = false;
    if (!transportError.empty()) return (transient = true, "Could not reach Tripo: " + clip(transportError, 160));
    Json j;
    std::string err;
    if (!body.empty() && Json::parse(body, j, err) && j.isObject())
        if (std::string m = codeMessage(j, transient); !m.empty()) return m;
    switch (httpStatus) {
    case 401: return "Tripo rejected the API key";
    case 403: return "Tripo refused the request: the key lacks permission or the account is out of credits";
    case 429: return (transient = true, "Tripo is rate limiting - wait a moment and try again");
    default: break;
    }
    if (httpStatus >= 500) transient = true;
    return "Tripo answered HTTP " + std::to_string(httpStatus);
}

bool parseUpload(const std::string& body, std::string& fileToken, std::string& error) {
    Json j;
    std::string err;
    bool transient = false;
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Tripo", false);
    if (std::string e = codeMessage(j, transient); !e.empty()) return (error = e, false);
    fileToken = j["data"]["file_token"].asString();
    if (fileToken.empty()) return (error = "Unexpected reply from Tripo", false);
    return true;
}

bool parseCreate(const std::string& body, std::string& taskId, std::string& error) {
    Json j;
    std::string err;
    bool transient = false;
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Tripo", false);
    if (std::string e = codeMessage(j, transient); !e.empty()) return (error = e, false);
    taskId = j["data"]["task_id"].asString();
    if (taskId.empty()) return (error = "Unexpected reply from Tripo", false);
    return true;
}

bool parseTask(const std::string& body, TaskStatus& out, std::string& error) {
    Json j;
    std::string err;
    bool transient = false;
    out = {};
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Tripo", false);
    if (std::string e = codeMessage(j, transient); !e.empty()) return (error = e, false);
    const Json& d = j["data"];
    if (!d.isObject()) return (error = "Unexpected reply from Tripo", false);
    const std::string s = d["status"].asString();
    if (d["progress"].isNumber()) out.percent = std::clamp(int(d["progress"].asNumber()), 0, 100);
    if (s == "success") {
        out.state = TaskState::Done;
        out.modelUrl = d["output"]["model_url"].asString();
        if (out.modelUrl.empty()) return (error = "Tripo finished but returned no model", false);
    } else if (s == "failed" || s == "cancelled" || s == "banned" || s == "expired") {
        out.state = TaskState::Failed;
        out.error = d["error_message"].isString() ? clip(d["error_message"].asString(), 160) : std::string();
    } else {
        out.state = s == "running" ? TaskState::Running : TaskState::Queued; // (queued, and anything unknown: keep waiting)
    }
    return true;
}

int pollDelaySeconds(int attempt) {
    static const int steps[] = {2, 2, 3, 3, 4};
    return attempt < int(std::size(steps)) ? steps[std::max(attempt, 0)] : 5;
}

GeneratorResult run(const AiSettings& s, const std::string& imagePath, const std::string& prompt, GenerationProgress* p) {
    GeneratorResult result;
    const bool text = imagePath.empty();
    const std::string model = resolvedModel(s.tripoModel);
    const auto start = std::chrono::steady_clock::now();

    // Gates before anything is sent: the AI switch, the studio policy, the audit log when it is required.
    if (!aiRequestAllowed("tripo", std::string(kApiBase) + "/generation/image-to-model", result.error)) return result;
    const std::string key = s.resolvedTripoKey();
    if (key.empty()) {
        result.error = "Add your Tripo API key (platform.tripo3d.ai > API keys) in Generate 3D";
        return result;
    }
    if (std::string why; audit::required() && !audit::writable(&why)) {
        result.error = why;
        return result;
    }
    auto record = [&](const GeneratorResult& r) {
        audit::Entry e;
        e.tool = "ai";
        e.command = text ? "text_to_3d" : "image_to_3d";
        e.args.set("service", "tripo");
        e.args.set("model", model);
        if (text) e.args.set("promptChars", double(prompt.size())); // (the length, not the words)
        e.ok = r.ok;
        e.message = r.ok ? "Model downloaded" : r.error;
        e.endpoint = kApiHost;
        e.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        audit::record(e);
    };
    bool transient = false;
    auto fail = [&](std::string error) {
        result = {};
        result.error = std::move(error);
        result.transient = transient;
        record(result);
        return result;
    };

    std::string png;
    if (!text) {
        std::string err;
        if (!prepareImagePng(imagePath, png, err)) {
            result.error = err;
            return result;
        }
    } else if (prompt.find_first_not_of(" \t\r\n") == std::string::npos) {
        result.error = "Describe the model you want";
        return result;
    }

    std::string input, err;
    if (!text) {
        status(p, "Sending the image to Tripo...");
        std::string contentType;
        const std::string body = uploadBody(png, contentType);
        HttpResponse up = send("POST", key, "/files", contentType, body);
        if (!up.error.empty() && !(p && p->cancel)) up = send("POST", key, "/files", contentType, body); // (one more try: nothing was billed)
        if (!up.error.empty() || up.status < 200 || up.status >= 300) return fail(errorMessage(up.status, up.error, up.body, transient));
        if (!parseUpload(up.body, input, err)) return fail(err);
    }

    status(p, "Starting the Tripo task...");
    // (Not retried: a create whose answer got lost may still have run, and would be billed twice.)
    const HttpResponse created = send("POST", key, text ? "/generation/text-to-model" : "/generation/image-to-model",
                                      "Content-Type: application/json", text ? textBody(model, prompt) : imageBody(model, input));
    if (!created.error.empty() || created.status < 200 || created.status >= 300)
        return fail(errorMessage(created.status, created.error, created.body, transient));
    std::string taskId;
    if (!parseCreate(created.body, taskId, err)) return fail(err);

    TaskStatus task;
    int failures = 0;
    for (int attempt = 0;; ++attempt) {
        if (!wait(p, pollDelaySeconds(attempt))) return fail("Cancelled (the Tripo task keeps running on your account)");
        const int secs = int(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
        if (secs > gMaxSeconds) {
            transient = true;
            return fail("Tripo took longer than " + std::to_string(kMaxMinutes) + " minutes - gave up (the task stays in your Tripo account)");
        }
        const HttpResponse st = send("GET", key, taskPath(taskId), {}, {});
        bool t = false;
        if (!st.error.empty() || st.status == 429 || st.status >= 500) { // transient: back off and ask again
            if (++failures >= kMaxPollFailures) return (transient = true, fail(errorMessage(st.status, st.error, st.body, t)));
            continue;
        }
        failures = 0;
        if (st.status < 200 || st.status >= 300) return fail(errorMessage(st.status, st.error, st.body, transient));
        if (!parseTask(st.body, task, err)) return fail(err);
        if (task.state == TaskState::Failed)
            return fail("Tripo could not make this model" + (task.error.empty() ? std::string() : ": " + task.error));
        if (task.state == TaskState::Done) break;
        const std::string head = task.state == TaskState::Queued ? "Waiting in Tripo's queue... " : "Generating 3D model... ";
        status(p, head + std::to_string(secs) + "s", task.state == TaskState::Running ? task.percent : -1);
    }

    // A refused host: the paid result isn't lost, it stays in the user's Tripo account under this task id.
    auto refusedHost = [&](const std::string& host) {
        std::string id;
        for (char c : taskId.substr(0, 64))
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') id += c;
        return fail("Refused the model file: it isn't on Tripo's servers (" + host +
                    "). The model stays in your Tripo account (platform.tripo3d.ai), task " + id);
    };
    if (!downloadUrlAllowed(task.modelUrl)) {
        net::UrlParts u;
        return refusedHost(net::parseUrl(task.modelUrl, u) ? u.host : std::string("not an https address"));
    }
    if (!aiRequestAllowed("tripo", task.modelUrl, err)) return fail(err);

    status(p, "Downloading model...");
    std::string bytes, refused;
    if (!glbfile::fetch(task.modelUrl, [](const std::string& u) { return downloadUrlAllowed(u); }, p, gTransport, bytes, err, refused)) {
        if (!refused.empty()) return refusedHost(refused);
        transient = err.rfind("Download failed", 0) == 0;
        return fail(err);
    }
    result = glbfile::save(bytes);
    if (!result.ok) return fail(result.error);
    record(result);
    return result;
}

void setTimeoutSecondsForTests(int seconds) { gMaxSeconds = seconds ? seconds : kMaxMinutes * 60; }

void setTransportForTests(glbfile::Transport transport, std::vector<int>* waits) {
    gTransport = std::move(transport);
    gWaits = waits;
}

} // namespace sf::ai::tripo
