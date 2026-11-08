// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Consent.h"

#include "ai/Hyper3D.h"
#include "ai/ImageTo3DService.h"
#include "ai/Providers.h"
#include "core/ManagedPolicy.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <cctype>

namespace sf::ai {

const char* kSendNotAllowed = "Not sent: ";

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// The host of an http(s) URL, lower case, without brackets ("[::1]" -> "::1"); "" when it isn't one (or has user info).
std::string hostOf(const std::string& url) {
    const size_t sep = url.find("://");
    if (sep == std::string::npos) return {};
    const std::string scheme = lower(url.substr(0, sep));
    if (scheme != "http" && scheme != "https") return {};
    const size_t start = sep + 3;
    const std::string authority = url.substr(start, url.find_first_of("/?#", start) - start);
    if (authority.empty() || authority.find('@') != std::string::npos) return {};
    if (authority[0] == '[') {
        const size_t close = authority.find(']');
        return close == std::string::npos ? std::string() : lower(authority.substr(1, close - 1));
    }
    return lower(authority.substr(0, authority.find(':')));
}

bool loopbackHost(const std::string& h) {
    if (h == "localhost" || h == "::1") return true;
    // 127.0.0.0/8 as a numeric address only ("127.evil.example" is a name, not loopback).
    if (h.rfind("127.", 0) != 0) return false;
    int dots = 0;
    for (char c : h)
        if (c == '.') ++dots;
        else if (c < '0' || c > '9') return false;
    return dots == 3;
}

// A target at URL that isn't one of the named services: the artist's own server, keyed by its host.
SendTarget hostTarget(const std::string& url, std::string label, std::string what, std::string payer, std::string policyId) {
    SendTarget t;
    t.host = hostOf(url);
    t.id = t.host.empty() ? url : t.host;
    t.label = label.empty() ? t.host : std::move(label);
    t.what = std::move(what);
    t.payer = std::move(payer);
    t.policyId = std::move(policyId);
    t.url = url;
    t.local = localUrl(url);
    return t;
}

constexpr const char* kBrushWhat = "your brush description and the current brush settings";
constexpr const char* kImageWhat = "this image (framed, up to 1024 px)";
constexpr const char* kTextWhat = "your description of the model";

struct HfFamily {
    const char* owner; // the Space owner: "tencent" in tencent-hunyuan3d-2-1.hf.space
    const char* label;
};
constexpr HfFamily kHfFamilies[] = {
    {"tencent", "Hugging Face (Tencent)"},
    {"microsoft", "Hugging Face (Microsoft)"},
    {"stabilityai", "Hugging Face (Stability AI)"},
};
constexpr const char* kHfPayer = "Free: Hugging Face's shared GPUs (a daily allowance on your account or connection)";

SendTarget hfTarget(const std::string& space, bool text) {
    SendTarget t = hostTarget(space, {}, text ? kTextWhat : kImageWhat, kHfPayer, "huggingface");
    const std::string owner = t.host.substr(0, t.host.find('-'));
    t.id = "hf-" + owner;
    t.label = "Hugging Face (" + owner + ")";
    for (const HfFamily& f : kHfFamilies)
        if (owner == f.owner) t.label = f.label;
    return t;
}

SendTarget hfFamilyTarget(const HfFamily& f) {
    for (const HfPreset& p : hfPresets())
        if (hostOf(p.space).rfind(std::string(f.owner) + "-", 0) == 0) return hfTarget(p.space, false);
    return {};
}

} // namespace

bool localUrl(const std::string& url) { return loopbackHost(hostOf(url)); }

SendTarget brushTarget(const AiSettings& s) {
    const std::string url = requestUrl(s);
    if (s.provider == AiSettings::Provider::Free)
        return hostTarget(url, "Pollinations.ai", kBrushWhat, "Free: Pollinations.ai pays (rate limited), no account", "free");
    const ProviderPreset* p = findProviderPreset(providerId(s));
    const bool named = p && *p->endpoint && hostOf(url) == hostOf(p->endpoint) && !p->local;
    std::string payer = p && p->needsKey ? std::string("You: billed to your ") + (named ? p->label : "API") + " key"
                                         : "Whoever runs that server (your key, if you added one)";
    return hostTarget(url, named ? p->label : "", kBrushWhat, std::move(payer), providerId(s));
}

SendTarget generateTarget(const AiSettings& s, bool text) {
    const std::string& svc = s.generatorService;
    const char* what = text ? kTextWhat : kImageWhat;
    if (svc == "inflate" || svc == "command") {
        SendTarget t;
        t.id = svc;
        t.label = svc == "inflate" ? "Offline blockout" : "Your generator command";
        t.policyId = svc;
        t.local = true;
        return t;
    }
    if (svc == "fal") return hostTarget(s.falEndpoint, "fal.ai", what, "You: billed per generation to your fal.ai key", "fal");
    if (svc == "meshy") return hostTarget(s.meshyEndpoint, "Meshy", what, "You: billed per generation to your Meshy key", "meshy");
    if (svc == "hyper3d")
        return hostTarget(hyper3d::kApiBase, "Hyper3D Rodin", what, "You: billed per generation to your Hyper3D key", "hyper3d");
    if (svc == "comfyui") {
        SendTarget t = comfyTarget(s.comfyUrl);
        t.what = what;
        return t;
    }
    return hfTarget(resolveHfPreset(s, text).space, text);
}

bool generateKeyMissing(const AiSettings& s) {
    const std::string& svc = s.generatorService;
    return (svc == "fal" && s.resolvedFalKey().empty()) || (svc == "meshy" && s.resolvedMeshyKey().empty()) ||
           (svc == "hyper3d" && s.resolvedHyper3dKey().empty());
}

SendTarget comfyTarget(const std::string& url) {
    return hostTarget(url, "ComfyUI at " + hostOf(url), "a check that the server answers, then your image or description",
                      "Whoever runs that ComfyUI server", "comfyui");
}

std::vector<SendTarget> knownTargets(const AiSettings& s) {
    std::vector<SendTarget> out;
    auto add = [&](SendTarget t) {
        if (t.local || t.id.empty()) return;
        for (const SendTarget& o : out)
            if (o.id == t.id) return;
        out.push_back(std::move(t));
    };
    for (const ProviderPreset& p : providerPresets())
        if (*p.endpoint && !p.local) add(brushTarget(withPreset(s, p.id)));
    add(brushTarget(s)); // a custom server
    for (const HfFamily& f : kHfFamilies) add(hfFamilyTarget(f));
    for (const char* svc : {"fal", "meshy", "hyper3d", "comfyui"}) {
        AiSettings g = s;
        g.generatorService = svc;
        add(generateTarget(g, false));
    }
    for (const std::string& id : s.sendAllowed) { // allowed earlier, on a server these settings no longer name
        SendTarget t;
        t.id = t.label = t.host = id;
        add(std::move(t));
    }
    return out;
}

bool policyPreAllowed(const SendTarget& t) {
    const policy::Policy& p = policy::current();
    if (!p.present || p.broken || p.ai == policy::Lock::Off) return false;
    if (std::find(p.aiProviders.begin(), p.aiProviders.end(), t.policyId) != p.aiProviders.end()) return true;
    for (const std::string& prefix : p.aiEndpoints)
        if (!t.url.empty() && net::urlUnderPrefix(t.url, prefix)) return true;
    return false;
}

bool sendRemembered(const AiSettings& s, const std::string& id) {
    return std::find(s.sendAllowed.begin(), s.sendAllowed.end(), id) != s.sendAllowed.end();
}

bool sendAllowed(const AiSettings& s, const SendTarget& t) {
    if (!policy::aiProviderAllowed(t.policyId) || (!t.url.empty() && !policy::aiEndpointAllowed(t.url))) return false;
    return t.local || policyPreAllowed(t) || sendRemembered(s, t.id);
}

void allowSend(AiSettings& s, const std::string& id) {
    if (!id.empty() && !sendRemembered(s, id)) s.sendAllowed.push_back(id);
}

void revokeSend(AiSettings& s, const std::string& id) {
    s.sendAllowed.erase(std::remove(s.sendAllowed.begin(), s.sendAllowed.end(), id), s.sendAllowed.end());
}

std::string notAllowedError(const SendTarget& t) {
    return std::string(kSendNotAllowed) + "allow " + t.label + " (" + t.host +
           ") first: the notice in the AI Brush or Generate 3D panel, or Preferences > AI";
}

bool checkSendAllowed(const AiSettings& s, const SendTarget& t, std::string& error) {
    if (!policy::aiProviderAllowed(t.policyId, &error)) return false; // a studio's "no" wins over an Allow
    if (!t.url.empty() && !policy::aiEndpointAllowed(t.url, &error)) return false;
    if (sendAllowed(s, t)) return true;
    error = notAllowedError(t);
    return false;
}

} // namespace sf::ai
