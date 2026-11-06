// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/AiPolicy.h"

#include "core/ManagedPolicy.h"
#include "core/NetworkPolicy.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>

namespace sf::ai {

namespace {
std::atomic<bool> gEnabled{true};
std::atomic<bool> gForcedOff{false};
std::atomic<bool> gStudioAllows{false};
// Studio mode turns AI off and locks it, unless the studio's profile asks for it ("ai": true).
// A managed policy (core/ManagedPolicy.h) that turns AI on allows it in studio mode too (its endpoints are reachable).
bool studioOff() { return net::offlineMode() && !gStudioAllows && policy::current().ai != policy::Lock::On; }
bool managedOff() { return policy::current().ai == policy::Lock::Off; }
bool managedOn() { return policy::current().ai == policy::Lock::On; }
} // namespace

void setStudioAiAllowed(bool on) { gStudioAllows = on; }

AiOverride aiEnvOverride() {
    const char* v = std::getenv("SF_AI");
    if (!v) return AiOverride::None;
    std::string s(v);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (s == "off" || s == "0" || s == "false" || s == "no") return AiOverride::Off;
    if (s == "on" || s == "1" || s == "true" || s == "yes") return AiOverride::On;
    return AiOverride::None;
}

void setAiForcedOff(bool off) { gForcedOff = off; }

bool aiPolicyLocked() {
    return gForcedOff || studioOff() || managedOff() || managedOn() || aiEnvOverride() != AiOverride::None;
}

bool aiManaged() { return managedOff() || managedOn(); }

bool aiFeaturesAllowed(bool saved) {
    if (gForcedOff || studioOff() || managedOff()) return false;
    if (managedOn()) return true; // the studio's policy wins over SF_AI and the saved choice
    switch (aiEnvOverride()) {
    case AiOverride::Off: return false;
    case AiOverride::On: return true;
    case AiOverride::None: break;
    }
    return saved;
}

void setAiEnabled(bool on) { gEnabled = on; }
bool aiEnabled() { return gEnabled; }

std::string aiOffError() {
    if (managedOff()) return "AI features are turned off by your studio's policy";
    if (studioOff()) return "AI features are off in this studio build";
    return aiPolicyLocked() ? "AI features are turned off on this computer (SF_AI=off or --no-ai)"
                            : "AI features are off (Preferences > AI > Enable AI features)";
}

const std::vector<std::string>& aiCommandNames() {
    static const std::vector<std::string> names = {"brush_ai", "image_to_3d", "text_to_3d", "comfyui_check"};
    return names;
}

bool isAiCommand(std::string_view name) {
    const auto& n = aiCommandNames();
    return std::find(n.begin(), n.end(), name) != n.end();
}

} // namespace sf::ai
