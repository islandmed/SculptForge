// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Providers.h"

#include <iterator>

namespace sf::ai {

namespace {

using P = AiSettings::Provider;

constexpr ProviderPreset kPresets[] = {
    {"free", "Free - no account or key", P::Free, "https://text.pollinations.ai/openai", "openai", "", false, false,
     "An open model hosted by Pollinations.ai. Only your brush description and settings are sent."},
    {"anthropic", "Anthropic (Claude)", P::Anthropic, "https://api.anthropic.com/v1/messages", "claude-opus-5-5",
     "ANTHROPIC_API_KEY", true, false, "Your Anthropic API key (console.anthropic.com)."},
    {"openai", "OpenAI", P::OpenAICompatible, "https://api.openai.com/v1/chat/completions", "", "OPENAI_API_KEY",
     true, false, "Your OpenAI API key (platform.openai.com); then Find models."},
    {"ollama", "Ollama (this computer)", P::OpenAICompatible, "http://localhost:11434/v1/chat/completions",
     "qwen3:4b-instruct", "", false, true, "Private: the model runs on this computer. No key."},
    {"lmstudio", "LM Studio (this computer)", P::OpenAICompatible, "http://localhost:1234/v1/chat/completions", "", "",
     false, true, "Private: start LM Studio's server (Developer tab), then Find models. No key."},
    {"vllm", "vLLM server", P::OpenAICompatible, "http://localhost:8000/v1/chat/completions", "", "OPENAI_API_KEY", false,
     true, "A vLLM server (vllm serve <model>): this computer or a GPU server at your studio."},
    {"llamacpp", "llama.cpp server", P::OpenAICompatible, "http://localhost:8080/v1/chat/completions", "", "", false, true,
     "llama-server -m model.gguf: this computer or a server at your studio. No key."},
    {"litellm", "LiteLLM proxy (studio gateway)", P::OpenAICompatible, "http://localhost:4000/v1/chat/completions", "",
     "OPENAI_API_KEY", false, false,
     "Your studio's gateway: one address for every model your studio allows (Bedrock, Vertex AI, Azure OpenAI and "
     "others behind it). A virtual key, or your studio sign-in."},
    {"openrouter", "OpenRouter", P::OpenAICompatible, "https://openrouter.ai/api/v1/chat/completions", "openai/gpt-oss-20b",
     "OPENROUTER_API_KEY", true, false, "Many models through one key (openrouter.ai/keys)."},
    {"openai-compatible", "Other OpenAI-compatible server", P::OpenAICompatible, "", "", "OPENAI_API_KEY", false, false,
     "Any server with an OpenAI-style /v1/chat/completions endpoint."},
};

} // namespace

std::span<const ProviderPreset> providerPresets() { return kPresets; }

const ProviderPreset* findProviderPreset(std::string_view id) {
    for (const ProviderPreset& p : kPresets)
        if (id == p.id) return &p;
    return nullptr;
}

std::string providerId(const AiSettings& s) {
    if (const ProviderPreset* p = findProviderPreset(s.preset); p && p->kind == s.provider) return p->id;
    if (s.provider == P::Free) return "free";
    if (s.provider == P::Anthropic) return "anthropic";
    for (const ProviderPreset& p : kPresets) // a file from before presets: its endpoint names the server
        if (p.kind == P::OpenAICompatible && *p.endpoint && s.endpoint == p.endpoint) return p.id;
    return "openai-compatible";
}

AiSettings withPreset(const AiSettings& settings, std::string_view id) {
    AiSettings s = settings;
    const ProviderPreset* p = findProviderPreset(id);
    if (!p) return s;
    s.preset = p->id;
    s.provider = p->kind;
    if (*p->endpoint || p->kind != P::OpenAICompatible) s.endpoint = p->endpoint;
    s.model = p->model;
    s.apiKey.clear(); // each preset has its own key (ai/Secrets.h)
    return s;
}

} // namespace sf::ai
