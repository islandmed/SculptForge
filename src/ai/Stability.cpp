// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Stability.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "core/AuditLog.h"
#include "core/Json.h"

#include <chrono>

namespace sf::ai::stability {

namespace {

constexpr int kTimeoutSeconds = 120; // SPAR3D answers in a few seconds; a cold start can take longer

glbfile::Transport gTransport; // (tests)

std::string clip(const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; }

} // namespace

bool apiUrl(const std::string& url) { return url == kApiUrl; }

std::string submitBody(const std::string& png, std::string& contentTypeHeader) {
    return multipartBody({{"image", png, "input.png", "image/png"}, {"texture_resolution", "1024"}}, contentTypeHeader);
}

std::string errorMessage(long httpStatus, const std::string& transportError, const std::string& body, bool& transient) {
    transient = false;
    if (!transportError.empty()) return (transient = true, "Could not reach Stability AI: " + clip(transportError, 160));
    Json j;
    std::string err, name, detail;
    if (!body.empty() && body[0] == '{' && Json::parse(body, j, err) && j.isObject()) {
        name = j["name"].asString();
        if (j["errors"].isArray() && j["errors"].size() > 0) detail = clip(j["errors"].items()[0].asString(), 160);
    }
    switch (httpStatus) {
    case 400: return "Stability AI refused the request" + (detail.empty() ? std::string() : " (" + detail + ")");
    case 401: return "Stability AI rejected the API key";
    case 402: return "Out of credits on your Stability AI account";
    case 403:
        if (name == "content_moderation") return "Stability AI refused the content (you were not charged)";
        return "Stability AI rejected the API key or the request";
    case 413: return "Stability AI refused the image: it is too large";
    case 429: return (transient = true, "Stability AI is rate limiting - wait a minute and try again");
    default: break;
    }
    if (name.find("credit") != std::string::npos || name.find("balance") != std::string::npos ||
        detail.find("credits") != std::string::npos)
        return "Out of credits on your Stability AI account";
    if (httpStatus >= 500) transient = true;
    return "Stability AI answered HTTP " + std::to_string(httpStatus) + (detail.empty() ? std::string() : " (" + detail + ")");
}

GeneratorResult run(const AiSettings& s, const std::string& imagePath, GenerationProgress* p) {
    GeneratorResult result;
    const auto start = std::chrono::steady_clock::now();
    if (imagePath.empty()) {
        result.error = "Stability SPAR3D builds from a picture: choose another service for text";
        return result;
    }

    // Gates before anything is sent: the AI switch, the studio policy, the audit log when it is required.
    if (!aiRequestAllowed("stability", kApiUrl, result.error)) return result;
    const std::string key = s.resolvedStabilityKey();
    if (key.empty()) {
        result.error = "Add your Stability AI API key (platform.stability.ai > Account > API keys) in Generate 3D";
        return result;
    }
    if (std::string why; audit::required() && !audit::writable(&why)) {
        result.error = why;
        return result;
    }
    bool transient = false;
    auto fail = [&](std::string error) {
        result = {};
        result.error = std::move(error);
        result.transient = transient;
        audit::Entry e;
        e.tool = "ai";
        e.command = "image_to_3d";
        e.args.set("service", "stability");
        e.ok = false;
        e.message = result.error;
        e.endpoint = kApiHost;
        e.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        audit::record(e);
        return result;
    };

    std::string png, err;
    if (!prepareImagePng(imagePath, png, err)) {
        result.error = err;
        return result;
    }
    if (p) {
        p->setStatus("Sending to Stability AI (SPAR3D)...");
        p->percent = -1;
    }
    std::string contentType;
    const std::string body = submitBody(png, contentType);
    // "Accept: model/gltf-binary" would be the only documented answer; sending */* keeps an error readable as JSON.
    const std::vector<std::string> headers = {"Authorization: Bearer " + key, contentType, "Accept: */*"};
    HttpResponse r;
    if (!apiUrl(kApiUrl)) return fail("refused");
    if (gTransport) r = gTransport("POST", kApiUrl, headers, body);
    else r = httpPostNoRedirect(kApiUrl, headers, body, kTimeoutSeconds);
    if (p && p->cancel) return fail("Cancelled");
    if (!r.error.empty() || r.status < 200 || r.status >= 300) return fail(errorMessage(r.status, r.error, r.body, transient));

    if (p) p->setStatus("Reading the model...");
    result = glbfile::save(r.body);
    if (!result.ok) {
        // A 200 with a JSON body would be an API change: say so, not "not a GLB".
        return fail(result.error);
    }
    audit::Entry e;
    e.tool = "ai";
    e.command = "image_to_3d";
    e.args.set("service", "stability");
    e.ok = true;
    e.message = "Model downloaded";
    e.endpoint = kApiHost;
    e.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    audit::record(e);
    return result;
}

void setTransportForTests(glbfile::Transport transport) { gTransport = std::move(transport); }

} // namespace sf::ai::stability
