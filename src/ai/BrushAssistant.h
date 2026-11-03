// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bring-your-own-AI brush designer: turns an artist's description ("sharp
// wrinkles that fade out", "make it grittier") into a BrushRecipe. Free out of
// the box (a hosted open model, no account or key), or bring your own:
// Anthropic's API or any OpenAI-compatible endpoint (OpenAI, Ollama, LM
// Studio, OpenRouter, ...). The model only ever returns recipe data.
#pragma once

#include "core/BrushRecipe.h"

#include <string>
#include <vector>

namespace sf::ai {

struct AiSettings {
    // Preferences > AI > Enable AI features (ai/AiPolicy.h: SF_AI=off wins over it).
    bool enabled = true;
    // Free: Pollinations' anonymous OpenAI-compatible endpoint (open models, no
    // key), falling back to a local Ollama if it is running.
    enum class Provider { Free, Anthropic, OpenAICompatible };
    Provider provider = Provider::Free;
    std::string endpoint;
    std::string model;
    std::string apiKey; // Free: unused; empty = use ANTHROPIC_API_KEY / OPENAI_API_KEY from the environment
    std::string effort = "low";
    // Image to 3D. service: "huggingface" (free Spaces), "fal", "meshy",
    // "comfyui" (your ComfyUI server), "command" (generatorCommand with
    // {input}/{output}) or "inflate" (offline).
    std::string generatorService = "huggingface";
    std::string hfToken;  // empty = HF_TOKEN from the environment (optional; raises the free quota)
    std::string hfModel = "hunyuan3d-2.1"; // the default (hfPresets()[0]): free, no token
    bool hfModelChosen = false;   // the user picked hfModel; otherwise loading resets it to the current default
    bool hunyuanNoticeShown = false; // the one-time Hunyuan3D licence notice
    std::string generatorCommand;
    std::string falKey;   // empty = FAL_KEY from the environment
    std::string falModel = "fal-ai/trellis-2";
    std::string falEndpoint = "https://queue.fal.run";
    std::string meshyKey; // empty = MESHY_API_KEY from the environment
    std::string meshyModel = "latest";
    std::string meshyEndpoint = "https://api.meshy.ai";
    std::string comfyUrl = "http://127.0.0.1:8188";
    std::string comfyImageWorkflow; // API-format workflow files; empty = the bundled default
    std::string comfyTextWorkflow;
    std::string resolvedFalKey() const;
    std::string resolvedHfToken() const;
    std::string resolvedMeshyKey() const;

    static AiSettings defaults(Provider provider);
    static Provider providerFromName(const std::string& name); // "free", "anthropic", "openai-compatible"
    static const char* providerName(Provider provider);
    std::string resolvedKey() const;

    Json toJson() const;
    static AiSettings fromJson(const Json& j);
    // Stored in the user's config directory with owner-only permissions.
    bool save(const std::string& path, std::string& error) const;
    static AiSettings load(const std::string& path);
};

struct ChatMessage {
    enum class Role { User, Assistant } role;
    std::string text;    // exactly what is sent to the model (history is append-only)
    std::string display; // what the chat panel shows
};

struct BrushSuggestion {
    bool ok = false;
    std::string reply; // short artist-facing explanation
    BrushRecipe recipe;
    std::string error;
};

// Where the app keeps ai.json (same location as SDL_GetPrefPath("SculptForge", "SculptForge")).
std::string defaultAiSettingsPath();

std::string brushSystemPrompt();
// The user turn to send: the artist's words plus the brush currently being edited.
ChatMessage composeUserMessage(const std::string& request, const BrushRecipe* current);

// Request pieces (exposed for tests).
std::string requestUrl(const AiSettings& s);
std::vector<std::string> requestHeaders(const AiSettings& s);
std::string buildRequestBody(const AiSettings& s, const std::vector<ChatMessage>& history);
// `rawAssistantJson` receives the model's JSON text, to append to the history.
BrushSuggestion parseResponse(const AiSettings& s, long httpStatus, const std::string& body,
                              std::string* rawAssistantJson = nullptr);

// Local model servers (blocking). `chatEndpoint` is an OpenAI-compatible
// .../v1/chat/completions URL; the model list comes from .../v1/models.
std::string modelsUrl(const std::string& chatEndpoint);
std::vector<std::string> listModels(const AiSettings& s, std::string& error);
// The smallest model a local Ollama has installed, or empty if none is running.
std::string smallestOllamaModel(std::string& error);

// Blocking (run on a worker thread): sends the conversation, whose last
// message must be the user's, and returns a recipe.
BrushSuggestion requestBrush(const AiSettings& s, const std::vector<ChatMessage>& history,
                             std::string* rawAssistantJson);

} // namespace sf::ai
