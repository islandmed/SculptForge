// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Hyper3D.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "core/AuditLog.h"
#include "core/Json.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace sf::ai::hyper3d {

namespace {

constexpr int kMaxMinutes = 20;
constexpr int kTimeoutSeconds = 60;

Transport gTransport;              // (tests)
std::vector<int>* gWaits = nullptr;
size_t gMaxBytes = kMaxDownloadBytes;

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

bool hostUnder(const std::string& host, const char* domain) {
    const std::string d = domain;
    return host == d || (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0 &&
                         host[host.size() - d.size() - 1] == '.');
}

// Hyper3D answers application errors with HTTP 201 and {"error": CODE, "message": ...}.
std::string bodyError(const Json& j) {
    const std::string code = j["error"].isString() ? j["error"].asString() : std::string();
    if (code.empty()) return {};
    const std::string msg = j["message"].isString() ? j["message"].asString().substr(0, 200) : std::string();
    if (code == "API_INSUFFICIENT_FUNDS") return "Out of credits on your Hyper3D account";
    if (code == "API_NO_ACTIVE_SUBSCRIPTION") return "Your Hyper3D account has no active API subscription";
    if (code == "API_SUBSCRIPTION_PLAN_TOO_LOW")
        return "Your Hyper3D plan doesn't include this tier (Basic API access covers Extreme-Low and Low)";
    if (code == "API_PARALLELISM_LIMIT_REACHED") return "Hyper3D is already running your other generations - try again shortly";
    if (code == "API_OBJECT_NOT_FOUND_ON_IMAGE") return "Hyper3D found no object in the image";
    if (code == "IMAGE_CONTENT_VIOLATION") return "Hyper3D refused the content";
    return "Hyper3D: " + code.substr(0, 60) + (msg.empty() ? std::string() : " (" + msg + ")");
}

std::string httpError(const HttpResponse& r) {
    if (!r.error.empty()) return "Could not reach Hyper3D: " + r.error;
    switch (r.status) {
    case 401:
    case 403: return "Hyper3D rejected the API key";
    case 429: return "Hyper3D is rate limiting - wait a moment and try again";
    default: return "Hyper3D answered HTTP " + std::to_string(r.status);
    }
}

// One API call: a POST to exactly kApiBase + path, the key only here.
HttpResponse post(const std::string& key, const std::string& path, const std::string& contentType, const std::string& body) {
    const std::string url = std::string(kApiBase) + path;
    if (!apiUrl(url)) {
        HttpResponse r;
        r.error = "refused";
        return r;
    }
    const std::vector<std::string> headers = {"Authorization: Bearer " + key, contentType, "Accept: application/json"};
    if (gTransport) return gTransport("POST", url, headers, body);
    return httpPostNoRedirect(url, headers, body, kTimeoutSeconds);
}

uint32_t le32(const std::string& b, size_t at) {
    return uint32_t(uint8_t(b[at])) | uint32_t(uint8_t(b[at + 1])) << 8 | uint32_t(uint8_t(b[at + 2])) << 16 |
           uint32_t(uint8_t(b[at + 3])) << 24;
}

} // namespace

const std::vector<Tier>& tiers() {
    static const std::vector<Tier> t = {
        {"Gen-2.5-Extreme-Low", "Gen-2.5 Extreme Low (fastest)"},
        {"Gen-2.5-Low", "Gen-2.5 Low"},
        {"Gen-2.5-Medium", "Gen-2.5 Medium"},
        {"Gen-2.5-High", "Gen-2.5 High"},
        {"Gen-2.5-Extreme-High", "Gen-2.5 Extreme High (more credits)"},
    };
    return t;
}

std::string resolvedTier(const std::string& tier) {
    for (const Tier& t : tiers())
        if (tier == t.id) return tier;
    return kDefaultTier;
}

bool apiUrl(const std::string& url) {
    net::UrlParts u;
    if (!net::parseUrl(url, u) || u.scheme != "https" || u.host != kApiHost || u.port != 443) return false;
    return u.path.rfind("/api/v2/", 0) == 0 && u.path.find("..") == std::string::npos;
}

bool downloadUrlAllowed(const std::string& url) {
    net::UrlParts u;
    if (!net::parseUrl(url, u) || u.scheme != "https" || u.port != 443) return false;
    return hostUnder(u.host, "hyper3d.com") || hostUnder(u.host, "hyper3d.ai") || hostUnder(u.host, "deemos.com");
}

std::string submitBody(const std::string& tier, const std::string& png, const std::string& prompt,
                       std::string& contentTypeHeader) {
    std::vector<MultipartField> fields;
    if (!png.empty()) fields.push_back({"images", png, "input.png", "image/png"});
    else fields.push_back({"prompt", prompt.substr(0, 800)});
    fields.push_back({"tier", resolvedTier(tier)});
    fields.push_back({"geometry_file_format", "glb"});
    fields.push_back({"material", "None"}); // the shape is what we sculpt
    fields.push_back({"mesh_mode", "Raw"});
    return multipartBody(fields, contentTypeHeader);
}

bool parseSubmit(const std::string& body, std::string& taskUuid, std::string& subscriptionKey, std::string& error) {
    Json j;
    std::string err;
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Hyper3D", false);
    if (std::string e = bodyError(j); !e.empty()) return (error = e, false);
    taskUuid = j["uuid"].asString();
    subscriptionKey = j["jobs"]["subscription_key"].asString();
    if (taskUuid.empty() || subscriptionKey.empty()) return (error = "Unexpected reply from Hyper3D", false);
    return true;
}

bool parseStatus(const std::string& body, JobState& state, int& queue, std::string& error) {
    Json j;
    std::string err;
    queue = -1;
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Hyper3D", false);
    if (std::string e = bodyError(j); !e.empty()) return (error = e, false);
    if (!j["jobs"].isArray() || j["jobs"].size() == 0) return (error = "Unexpected reply from Hyper3D", false);
    bool allDone = true, anyGenerating = false;
    for (const Json& job : j["jobs"].items()) {
        const std::string s = job["status"].asString();
        if (s == "Failed") return (state = JobState::Failed, true);
        if (s != "Done") allDone = false;
        if (s == "Generating") anyGenerating = true;
        if (job["queue_length"].isNumber()) queue = int(job["queue_length"].asNumber());
    }
    if (j["queue_length"].isNumber()) queue = int(j["queue_length"].asNumber());
    state = allDone ? JobState::Done : anyGenerating ? JobState::Generating : JobState::Waiting;
    return true;
}

bool parseDownload(const std::string& body, std::string& url, std::string& error) {
    Json j;
    std::string err;
    if (!Json::parse(body, j, err) || !j.isObject()) return (error = "Unexpected reply from Hyper3D", false);
    if (std::string e = bodyError(j); !e.empty()) return (error = e, false);
    for (const Json& f : j["list"].items()) {
        std::string name = f["name"].asString();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".glb") == 0 && f["url"].isString()) {
            url = f["url"].asString();
            return true;
        }
    }
    return (error = "Hyper3D finished but returned no GLB", false);
}

int pollDelaySeconds(int attempt) {
    static const int steps[] = {5, 5, 7, 10, 15, 20, 30};
    return attempt < int(std::size(steps)) ? steps[std::max(attempt, 0)] : 30;
}

bool checkGlb(const std::string& b, std::string& error) {
    error = "The downloaded file is not a valid GLB model";
    if (b.size() < 20 || b.size() > gMaxBytes) return false;
    if (b.compare(0, 4, "glTF") != 0 || le32(b, 4) != 2 || le32(b, 8) != b.size()) return false;
    size_t at = 12;
    bool first = true;
    while (at < b.size()) {
        if (b.size() - at < 8) return false;
        const uint64_t len = le32(b, at);
        const uint32_t type = le32(b, at + 4);
        if (len % 4 != 0 || len > b.size() - at - 8) return false;
        if (first) {
            if (type != 0x4E4F534Au || len == 0) return false; // "JSON"
            Json j;
            std::string err;
            if (!Json::parse(b.substr(at + 8, size_t(len)), j, err) || !j.isObject() || !j["asset"].isObject()) return false;
            first = false;
        }
        at += 8 + size_t(len);
    }
    if (first) return false;
    error.clear();
    return true;
}

std::string makePrivateDir(std::string& error) {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    if (ec) return (error = "No temporary folder: " + ec.message(), std::string());
#if defined(_WIN32)
    // (the temp folder is the user's own, under %LOCALAPPDATA%)
    std::random_device rd;
    for (int i = 0; i < 16; ++i) {
        const std::filesystem::path dir = base / ("sculptforge-hyper3d-" + std::to_string(rd()) + std::to_string(rd()));
        if (std::filesystem::create_directory(dir, ec)) {
            const auto u8 = dir.u8string();
            return std::string(u8.begin(), u8.end());
        }
    }
    error = "Could not make a temporary folder";
    return {};
#else
    std::string templ = (base / "sculptforge-hyper3d-XXXXXX").string();
    if (!mkdtemp(templ.data())) return (error = "Could not make a temporary folder", std::string()); // (mode 0700)
    return templ;
#endif
}

GeneratorResult run(const AiSettings& s, const std::string& imagePath, const std::string& prompt, GenerationProgress* p) {
    GeneratorResult result;
    const bool text = imagePath.empty();
    const std::string tier = resolvedTier(s.hyper3dTier);
    const auto start = std::chrono::steady_clock::now();

    // Gates before anything is sent: the AI switch, the studio policy, the audit log when it is required.
    if (!aiRequestAllowed("hyper3d", std::string(kApiBase) + "/rodin", result.error)) return result;
    const std::string key = s.resolvedHyper3dKey();
    if (key.empty()) {
        result.error = "Add your Hyper3D API key (hyper3d.ai > API dashboard) in Generate 3D";
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
        e.args.set("service", "hyper3d");
        e.args.set("tier", tier);
        if (text) e.args.set("promptChars", double(prompt.size())); // (the length, not the words)
        e.ok = r.ok;
        e.message = r.ok ? "Model downloaded" : r.error;
        e.endpoint = kApiHost;
        e.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        audit::record(e);
    };
    auto fail = [&](std::string error) {
        result = {};
        result.error = std::move(error);
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

    status(p, "Sending to Hyper3D Rodin...");
    std::string contentType;
    const std::string body = submitBody(tier, png, prompt, contentType);
    const HttpResponse submitted = post(key, "/rodin", contentType, body);
    if (!submitted.error.empty() || submitted.status < 200 || submitted.status >= 300) return fail(httpError(submitted));
    std::string taskUuid, subscriptionKey, err;
    if (!parseSubmit(submitted.body, taskUuid, subscriptionKey, err)) return fail(err);

    Json statusBody = Json::object();
    statusBody.set("subscription_key", subscriptionKey);
    for (int attempt = 0;; ++attempt) {
        if (!wait(p, pollDelaySeconds(attempt)))
            return fail("Cancelled (the Hyper3D task keeps running on your account)");
        if (std::chrono::steady_clock::now() - start > std::chrono::minutes(kMaxMinutes))
            return fail("Timed out waiting for Hyper3D");
        const HttpResponse st = post(key, "/status", "Content-Type: application/json", statusBody.dump());
        if (!st.error.empty() || st.status == 429 || st.status >= 500) continue; // transient: back off and ask again
        if (st.status < 200 || st.status >= 300) return fail(httpError(st));
        JobState state;
        int queue = -1;
        if (!parseStatus(st.body, state, queue, err)) return fail(err);
        const int secs = int(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
        if (state == JobState::Failed) return fail("Hyper3D could not make this model");
        if (state == JobState::Done) break;
        status(p, state == JobState::Waiting
                      ? "Waiting in queue" + (queue >= 0 ? " (" + std::to_string(queue) + " ahead)" : std::string()) + "... " +
                            std::to_string(secs) + "s"
                      : "Generating 3D model... " + std::to_string(secs) + "s");
    }

    Json downloadBody = Json::object();
    downloadBody.set("task_uuid", taskUuid);
    const HttpResponse listed = post(key, "/download", "Content-Type: application/json", downloadBody.dump());
    if (!listed.error.empty() || listed.status < 200 || listed.status >= 300) return fail(httpError(listed));
    std::string fileUrl;
    if (!parseDownload(listed.body, fileUrl, err)) return fail(err);
    // A refused host: the paid result isn't lost, it stays in the user's Hyper3D account under this task id.
    auto refusedHost = [&](const std::string& host) {
        std::string id;
        for (char c : taskUuid.substr(0, 64))
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-') id += c;
        return fail("Refused the model file: it isn't on Hyper3D's servers (" + host +
                    "). The model stays in your Hyper3D account (hyper3d.ai dashboard), task " + id);
    };
    if (!downloadUrlAllowed(fileUrl)) {
        net::UrlParts u;
        return refusedHost(net::parseUrl(fileUrl, u) ? u.host : std::string("not an https address"));
    }
    if (!aiRequestAllowed("hyper3d", fileUrl, err)) return fail(err);

    // The file: no key sent, Hyper3D's hosts only (each redirect too), capped, then checked before anyone parses it.
    status(p, "Downloading model...");
    std::string bytes;
    bool tooBig = false;
    auto get = [&](const std::string& url, const std::function<bool(const std::string&)>& allowed, int stall,
                   const std::function<bool(const char*, size_t)>& onData,
                   const std::function<bool(uint64_t, uint64_t)>& onProgress) {
        if (!gTransport) return httpGetRestricted(url, allowed, stall, onData, onProgress);
        HttpResponse r = gTransport("GET", url, {}, {});
        if (!r.body.empty() && !onData(r.body.data(), r.body.size())) r.error = "stopped";
        r.body.clear();
        return r;
    };
    const HttpResponse got = get(
        fileUrl, [](const std::string& u) { return downloadUrlAllowed(u); }, kTimeoutSeconds,
        [&](const char* d, size_t n) {
            if (bytes.size() + n > gMaxBytes) return (tooBig = true, false);
            bytes.append(d, n);
            return true;
        },
        [&](uint64_t received, uint64_t total) {
            if (total > gMaxBytes) return (tooBig = true, false);
            if (p && total > 0) p->percent = int(received * 100 / total);
            return !(p && p->cancel);
        });
    if (tooBig) return fail("The model file is larger than 200 MB - refused");
    if (p && p->cancel) return fail("Cancelled");
    if (got.error.rfind("refused to download from ", 0) == 0) { // (a redirect left Hyper3D's servers)
        net::UrlParts u;
        const std::string to = got.error.substr(25);
        return refusedHost(net::parseUrl(to, u) ? u.host : std::string("not an https address"));
    }
    if (!got.error.empty() || got.status != 200) return fail("Download failed: " + (got.error.empty() ? "HTTP " + std::to_string(got.status) : got.error));
    if (!checkGlb(bytes, err)) return fail(err);

    const std::string dir = makePrivateDir(err);
    if (dir.empty()) return fail(err);
    const std::string path = dir + "/model.glb";
    {
        std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        if (!f || !f.write(bytes.data(), std::streamsize(bytes.size()))) {
            f.close();
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec);
            return fail("Cannot write the model file");
        }
    }
    result = {};
    result.ok = true;
    result.outputPath = path;
    result.privateDir = dir;
    record(result);
    return result;
}

void setMaxDownloadBytesForTests(size_t bytes) { gMaxBytes = bytes ? bytes : kMaxDownloadBytes; }

void setTransportForTests(Transport transport, std::vector<int>* waits) {
    gTransport = std::move(transport);
    gWaits = waits;
}

} // namespace sf::ai::hyper3d
