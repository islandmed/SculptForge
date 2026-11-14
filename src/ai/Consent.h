// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// First-use notice for every third-party AI host. Before the first request to a service on someone else's computer
// (Pollinations, each Hugging Face Space family, fal.ai, Meshy, Hyper3D, Tripo, any remote OpenAI-compatible server, a remote
// ComfyUI) the panel says what is sent, to which host and who pays, and nothing is sent until the artist presses Allow.
// Allow is remembered per service (ai.json "sendAllowed"); Preferences > AI lists each one with Revoke. This computer
// (localhost, 127.x, ::1) needs no notice. A studio's policy (core/ManagedPolicy.h) that lists a provider in
// ai.providers has decided for its artists (pre-allowed); one it leaves out stays forbidden (aiRequestAllowed).
// The AI master switch (ai/AiPolicy.h) still comes first.
#pragma once

#include "ai/BrushAssistant.h"

#include <string>
#include <vector>

namespace sf::ai {

struct SendTarget {
    std::string id;       // what Allow remembers: the host ("text.pollinations.ai", "openrouter.ai"), or "hf-tencent"
    std::string label;    // "Pollinations.ai", "Hugging Face (Tencent)"
    std::string host;     // "text.pollinations.ai", "tencent-hunyuan3d-2-1.hf.space"
    std::string what;     // "your brush description and the current brush settings"
    std::string payer;    // "Free: Pollinations.ai pays (rate limited), no account"
    std::string policyId; // the provider id a studio's policy lists ("free", "huggingface", "fal", ...)
    std::string url;      // where it goes (the studio policy's ai.endpoints are checked against it)
    bool local = false;   // this computer: no notice
};

// True for http(s) URLs on this computer (localhost, 127.x, ::1).
bool localUrl(const std::string& url);

// What the AI Brush (Brush Lab) would send with these settings, and where. Also covers Find models.
SendTarget brushTarget(const AiSettings& settings);
// What Generate 3D would send (TEXT: a description instead of an image). Offline blockout and your own command are local.
SendTarget generateTarget(const AiSettings& settings, bool text);
// A paid Generate 3D service (fal.ai, Meshy, Hyper3D, Tripo) with no key: it refuses on its own before sending anything, so
// "add your key" comes first and the notice after.
bool generateKeyMissing(const AiSettings& settings);
// ComfyUI's address check (local unless the artist points it at another computer).
SendTarget comfyTarget(const std::string& url);

// Every online service the app knows, in the order Preferences > AI lists them (custom hosts come from SETTINGS).
std::vector<SendTarget> knownTargets(const AiSettings& settings);

// The studio's policy lists this provider: its artists don't see the notice.
bool policyPreAllowed(const SendTarget& target);
// May the app send to TARGET now? Local, pre-allowed by the policy, or allowed by the artist.
bool sendAllowed(const AiSettings& settings, const SendTarget& target);
// Remember (or forget) the artist's Allow.
void allowSend(AiSettings& settings, const std::string& id);
void revokeSend(AiSettings& settings, const std::string& id);
bool sendRemembered(const AiSettings& settings, const std::string& id);

// The error a request returns while TARGET is not allowed (nothing was sent). Starts with kSendNotAllowed.
extern const char* kSendNotAllowed;
std::string notAllowedError(const SendTarget& target);
// The gate the AI entry points call: false with ERROR set when TARGET is not allowed.
bool checkSendAllowed(const AiSettings& settings, const SendTarget& target, std::string& error);

} // namespace sf::ai
