// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AI provider presets (Preferences > AI > Provider): one click fills the endpoint and a model for a known service, so
// an artist or a studio's IT picks "LiteLLM proxy" instead of typing URLs. Every preset is the Free tier, Anthropic's
// Messages API or an OpenAI-compatible chat endpoint (ai/BrushAssistant.h); the endpoint can be edited after.
// Bedrock, Vertex AI and Azure OpenAI go through a LiteLLM proxy (docs/guide/studios.md "AI providers").
// The preset id is what a studio's policy lists in ai.providers (core/ManagedPolicy.h).
#pragma once

#include "ai/BrushAssistant.h"

#include <span>
#include <string>
#include <string_view>

namespace sf::ai {

struct ProviderPreset {
    const char* id;          // "litellm" (ai.json "preset", the policy's ai.providers, the keychain account)
    const char* label;       // "LiteLLM proxy (studio gateway)"
    AiSettings::Provider kind;
    const char* endpoint;    // "" = the artist types it
    const char* model;       // a sensible first model ("" = pick with Find models)
    const char* keyEnv;      // the environment variable read when no key is saved ("" = none)
    bool needsKey;           // a key is required (local servers usually take none)
    bool local;              // runs on this computer by default
    const char* note;        // one line under the picker
};

std::span<const ProviderPreset> providerPresets();
// The preset with this id, or nullptr.
const ProviderPreset* findProviderPreset(std::string_view id);
// The preset id of SETTINGS: its saved preset, else the one its provider kind and endpoint match, else
// "openai-compatible" / "anthropic" / "free".
std::string providerId(const AiSettings& settings);
// SETTINGS switched to preset ID (provider, endpoint, model; the rest is kept).
AiSettings withPreset(const AiSettings& settings, std::string_view id);

} // namespace sf::ai
