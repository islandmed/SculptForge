// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/BrushAssistant.h"
#include "ai/AiPolicy.h"

#include "ai/Http.h"
#include "core/BrushLibrary.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iterator>
#include <thread>

namespace sf::ai {

namespace {

constexpr const char* kAnthropicUrl = "https://api.anthropic.com/v1/messages";
constexpr const char* kAnthropicModel = "claude-opus-5-5";
constexpr const char* kOllamaUrl = "http://localhost:11434/v1/chat/completions";
constexpr const char* kOllamaModel = "qwen3:4b-instruct"; // 2.5 GB, fine on CPU; see docs/local-ai.md
// Free tier: anonymous, rate limited to roughly one request every few seconds per IP.
constexpr const char* kFreeUrl = "https://text.pollinations.ai/openai";
constexpr const char* kFreeModel = "openai";

std::filesystem::path utf8Path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

Json responseSchema() {
    Json props = Json::object();
    Json reply = Json::object();
    reply.set("type", "string");
    reply.set("description", "1-3 friendly sentences to the artist: what the brush does and one tweak to try.");
    props.set("reply", reply);
    props.set("brush", recipeJsonSchema());
    Json required = Json::array();
    required.push("reply");
    required.push("brush");
    Json schema = Json::object();
    schema.set("type", "object");
    schema.set("properties", props);
    schema.set("required", required);
    schema.set("additionalProperties", false);
    return schema;
}

// Pulls the JSON object out of model text that may be wrapped in prose or code fences.
bool extractJsonObject(const std::string& text, Json& out, std::string& error) {
    size_t first = text.find('{');
    size_t last = text.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last < first) {
        error = "The model did not return a brush recipe";
        return false;
    }
    if (!Json::parse(text.substr(first, last - first + 1), out, error)) {
        error = "The model returned malformed JSON: " + error;
        return false;
    }
    return true;
}

std::string apiErrorMessage(const Json& j) {
    const Json& e = j["error"];
    if (e.isObject() && e["message"].isString()) return e["message"].asString();
    if (e.isString()) return e.asString();
    return {};
}

} // namespace

AiSettings AiSettings::defaults(Provider provider) {
    AiSettings s;
    s.provider = provider;
    if (provider == Provider::Free) {
        s.endpoint = kFreeUrl;
        s.model = kFreeModel;
    } else if (provider == Provider::Anthropic) {
        s.endpoint = kAnthropicUrl;
        s.model = kAnthropicModel;
    } else {
        s.endpoint = kOllamaUrl;
        s.model = kOllamaModel;
    }
    return s;
}

AiSettings::Provider AiSettings::providerFromName(const std::string& name) {
    if (name == "anthropic") return Provider::Anthropic;
    if (name == "openai-compatible") return Provider::OpenAICompatible;
    return Provider::Free;
}

const char* AiSettings::providerName(Provider provider) {
    return provider == Provider::Anthropic          ? "anthropic"
           : provider == Provider::OpenAICompatible ? "openai-compatible"
                                                    : "free";
}

std::string AiSettings::resolvedKey() const {
    if (provider == Provider::Free) return {};
    if (!apiKey.empty()) return apiKey;
    const char* env = std::getenv(provider == Provider::Anthropic ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY");
    return env ? env : "";
}

std::string AiSettings::resolvedFalKey() const {
    if (!falKey.empty()) return falKey;
    const char* env = std::getenv("FAL_KEY");
    return env ? env : "";
}

std::string AiSettings::resolvedHfToken() const {
    if (!hfToken.empty()) return hfToken;
    for (const char* name : {"HF_TOKEN", "HUGGING_FACE_HUB_TOKEN"})
        if (const char* env = std::getenv(name)) return env;
    return {};
}

std::string AiSettings::resolvedMeshyKey() const {
    if (!meshyKey.empty()) return meshyKey;
    const char* env = std::getenv("MESHY_API_KEY");
    return env ? env : "";
}

Json AiSettings::toJson() const {
    Json j = Json::object();
    j.set("provider", providerName(provider));
    j.set("endpoint", endpoint);
    j.set("model", model);
    j.set("apiKey", apiKey);
    j.set("effort", effort);
    j.set("generatorService", generatorService);
    j.set("generatorCommand", generatorCommand);
    j.set("hfToken", hfToken);
    j.set("hfModel", hfModel);
    j.set("hfModelChosen", hfModelChosen);
    j.set("hunyuanNoticeShown", hunyuanNoticeShown);
    j.set("falKey", falKey);
    j.set("falModel", falModel);
    j.set("falEndpoint", falEndpoint);
    j.set("meshyKey", meshyKey);
    j.set("meshyModel", meshyModel);
    j.set("meshyEndpoint", meshyEndpoint);
    j.set("comfyUrl", comfyUrl);
    j.set("comfyImageWorkflow", comfyImageWorkflow);
    j.set("comfyTextWorkflow", comfyTextWorkflow);
    j.set("enabled", enabled);
    return j;
}

AiSettings AiSettings::fromJson(const Json& j) {
    // Settings saved before the free tier existed defaulted to Anthropic; without
    // a key they never worked, so those move to Free.
    Provider p = providerFromName(j["provider"].asString());
    if (p == Provider::Anthropic && j["apiKey"].asString().empty() && !std::getenv("ANTHROPIC_API_KEY") &&
        j["endpoint"].asString() == kAnthropicUrl)
        p = Provider::Free;
    AiSettings s = defaults(p);
    const bool keepEndpoint = p == providerFromName(j["provider"].asString());
    if (keepEndpoint && !j["endpoint"].asString().empty()) s.endpoint = j["endpoint"].asString();
    if (keepEndpoint && !j["model"].asString().empty()) s.model = j["model"].asString();
    s.apiKey = j["apiKey"].asString();
    if (!j["effort"].asString().empty()) s.effort = j["effort"].asString();
    s.generatorCommand = j["generatorCommand"].asString();
    auto text = [&](const char* key, std::string& field) {
        if (!j[key].asString().empty()) field = j[key].asString();
    };
    text("generatorService", s.generatorService);
    text("hfToken", s.hfToken);
    // A model the user never chose follows the default. Files from before hfModelChosen: TRELLIS.2 was the
    // default then, so it counts as chosen only with a saved token (it needs one); any other model was picked.
    const std::string savedModel = j["hfModel"].asString();
    s.hfModelChosen = j["hfModelChosen"].isBool() ? j["hfModelChosen"].asBool()
                                                  : !savedModel.empty() && (savedModel != "trellis-2" || !s.hfToken.empty());
    if (s.hfModelChosen && !savedModel.empty()) s.hfModel = savedModel;
    s.hunyuanNoticeShown = j["hunyuanNoticeShown"].asBool();
    text("falKey", s.falKey);
    text("falModel", s.falModel);
    text("falEndpoint", s.falEndpoint);
    text("meshyKey", s.meshyKey);
    text("meshyModel", s.meshyModel);
    text("meshyEndpoint", s.meshyEndpoint);
    text("comfyUrl", s.comfyUrl);
    s.comfyImageWorkflow = j["comfyImageWorkflow"].asString();
    s.comfyTextWorkflow = j["comfyTextWorkflow"].asString();
    s.enabled = j["enabled"].asBool(true);
    return s;
}

bool AiSettings::save(const std::string& path, std::string& error) const {
    const auto p = utf8Path(path);
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "Cannot write " + path;
            return false;
        }
        f << toJson().dump(true) << "\n";
    }
    std::error_code ec;
    std::filesystem::permissions(p, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    return true;
}

AiSettings AiSettings::load(const std::string& path) {
    // Settings from before the rename (OpenSculpt) move over on first use.
    if (const size_t at = path.find("SculptForge"); at != std::string::npos) {
        std::string legacy = path;
        for (size_t p = legacy.find("SculptForge"); p != std::string::npos; p = legacy.find("SculptForge", p + 10))
            legacy.replace(p, 11, "OpenSculpt");
        std::error_code ec;
        if (!std::filesystem::exists(utf8Path(path), ec) && std::filesystem::exists(utf8Path(legacy), ec)) {
            std::filesystem::create_directories(utf8Path(path).parent_path(), ec);
            std::filesystem::copy_file(utf8Path(legacy), utf8Path(path), ec);
        }
    }
    std::ifstream f(utf8Path(path), std::ios::binary);
    if (!f) return defaults(Provider::Free);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Json j;
    std::string err;
    if (!Json::parse(text, j, err)) return defaults(Provider::Free);
    return fromJson(j);
}

std::string defaultAiSettingsPath() {
    const std::string dir = appDataDir(); // shared with the brush library (core/BrushLibrary.h)
#if defined(_WIN32)
    return dir.empty() ? "ai.json" : dir + "\\ai.json";
#else
    return dir + "/ai.json";
#endif
}

std::string brushSystemPrompt() {
    std::string p =
        "You are the brush designer inside SculptForge, a digital sculpting app used by character, creature and "
        "environment artists. Artists describe the brush they want in their own words - the feel, the material, "
        "the mark it should leave, sometimes a reference like 'like a palette knife' or 'dry cracked mud'. You "
        "translate that into a brush recipe: a set of numeric and named parameters the sculpting engine "
        "understands. You never write code.\n\n"
        "Think like a sculptor: what does the tool do to the clay? Does it add or carve, cut crisp lines or soft "
        "forms, leave texture, follow the stroke? Choose parameters that produce that mark, keep everything else "
        "neutral (0 or 'none'), and prefer subtle values - artists can turn things up.\n\n"
        "When the artist asks for a change to the current brush ('sharper', 'less buildup', 'grittier'), start "
        "from the current recipe they send and change only what they asked for. Keep the same name unless they "
        "want a new brush.\n\n"
        "Parameters:\n";
    for (const auto& rp : recipeParams()) {
        p += "- ";
        p += rp.key;
        p += ": ";
        p += rp.help;
        p += "\n";
    }
    p += "\nExamples of good recipes:\n";
    auto examples = builtinBrushRecipes();
    for (size_t i = 0; i < 3 && i < examples.size(); ++i) p += examples[i].toJson().dump() + "\n";
    p += "\nRespond with only a JSON object of the form {\"reply\": string, \"brush\": recipe}. 'reply' is 1-3 "
         "friendly sentences to the artist describing the brush and one thing they could tweak. 'brush' contains "
         "every parameter listed above plus 'name' and 'description'.";
    return p;
}

ChatMessage composeUserMessage(const std::string& request, const BrushRecipe* current) {
    ChatMessage m{ChatMessage::Role::User, {}, request};
    if (current)
        m.text = "Current brush recipe (edit it if I'm asking for a change):\n" + current->toJson().dump() +
                 "\n\nMy request: " + request;
    else
        m.text = request;
    return m;
}

std::string requestUrl(const AiSettings& s) { return s.endpoint; }

std::vector<std::string> requestHeaders(const AiSettings& s) {
    std::vector<std::string> h = {"Content-Type: application/json"};
    const std::string key = s.resolvedKey();
    if (s.provider == AiSettings::Provider::Anthropic) {
        h.push_back("x-api-key: " + key);
        h.push_back("anthropic-version: 2023-06-01");
        h.push_back("anthropic-beta: server-side-fallback-2026-07-01");
    } else if (!key.empty()) {
        h.push_back("Authorization: Bearer " + key);
    }
    return h;
}

std::string buildRequestBody(const AiSettings& s, const std::vector<ChatMessage>& history) {
    Json body = Json::object();
    body.set("model", s.model);
    Json messages = Json::array();
    if (s.provider == AiSettings::Provider::Anthropic) {
        body.set("max_tokens", 16000);
        body.set("system", brushSystemPrompt());
        body.set("fallbacks", "default");
        Json format = Json::object();
        format.set("type", "json_schema");
        format.set("schema", responseSchema());
        Json outputConfig = Json::object();
        outputConfig.set("effort", s.effort.empty() ? "low" : s.effort);
        outputConfig.set("format", format);
        body.set("output_config", outputConfig);
    } else {
        if (s.provider == AiSettings::Provider::Free) {
            Json format = Json::object();
            format.set("type", "json_object");
            body.set("response_format", format);
            body.set("referrer", "sculptforge");
        }
        Json sys = Json::object();
        sys.set("role", "system");
        sys.set("content", brushSystemPrompt());
        messages.push(sys);
    }
    for (const auto& m : history) {
        Json msg = Json::object();
        msg.set("role", m.role == ChatMessage::Role::User ? "user" : "assistant");
        msg.set("content", m.text);
        messages.push(msg);
    }
    body.set("messages", messages);
    return body.dump();
}

BrushSuggestion parseResponse(const AiSettings& s, long httpStatus, const std::string& body,
                              std::string* rawAssistantJson) {
    BrushSuggestion out;
    Json j;
    std::string err;
    const bool parsed = Json::parse(body, j, err);

    if (httpStatus < 200 || httpStatus >= 300) {
        std::string msg = parsed ? apiErrorMessage(j) : std::string();
        if (httpStatus == 401 || httpStatus == 403) msg = "The API key was rejected. " + msg;
        else if (httpStatus == 429) msg = "Rate limited by the provider - wait a moment and try again. " + msg;
        else if (httpStatus == 404) msg = "Endpoint or model not found - check the AI settings. " + msg;
        out.error = "HTTP " + std::to_string(httpStatus) + ": " + (msg.empty() ? body.substr(0, 300) : msg);
        return out;
    }
    if (!parsed) {
        out.error = "Unreadable response from the provider: " + err;
        return out;
    }

    std::string text;
    if (s.provider == AiSettings::Provider::Anthropic) {
        if (j["stop_reason"].asString() == "refusal") {
            out.error = "The model declined this request. Try describing the brush differently.";
            return out;
        }
        for (const auto& block : j["content"].items())
            if (block["type"].asString() == "text") text += block["text"].asString();
        if (j["stop_reason"].asString() == "max_tokens" && text.empty()) {
            out.error = "The response was cut off before a brush was produced.";
            return out;
        }
    } else {
        text = j["choices"][0]["message"]["content"].asString();
    }

    Json result;
    if (!extractJsonObject(text, result, out.error)) return out;
    // Accept either {"reply", "brush"} or a bare recipe from less obedient local models.
    const Json& recipeJson = result["brush"].isObject() ? result["brush"] : result;
    out.recipe = BrushRecipe::fromJson(recipeJson);
    out.reply = result["reply"].isString() ? result["reply"].asString() : out.recipe.description;
    out.ok = true;
    if (rawAssistantJson) *rawAssistantJson = text;
    return out;
}

std::string modelsUrl(const std::string& chatEndpoint) {
    std::string base = chatEndpoint;
    const std::string suffix = "/chat/completions";
    if (base.size() >= suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
        base.resize(base.size() - suffix.size());
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/models";
}

std::vector<std::string> listModels(const AiSettings& s, std::string& error) {
    std::vector<std::string> out;
    if (!aiEnabled()) {
        error = aiOffError();
        return out;
    }
    HttpResponse r = httpRequest("GET", modelsUrl(s.endpoint), requestHeaders(s), {}, 5);
    if (!r.error.empty()) {
        error = "Nothing is answering at " + s.endpoint + " - is Ollama or LM Studio running? (" + r.error + ")";
        return out;
    }
    Json j;
    if (r.status != 200 || !Json::parse(r.body, j, error)) {
        error = "HTTP " + std::to_string(r.status) + " from " + modelsUrl(s.endpoint);
        return out;
    }
    for (const auto& m : j["data"].items())
        if (m["id"].isString()) out.push_back(m["id"].asString());
    if (out.empty()) error = "The server is running but has no models yet - try: ollama pull " + std::string(kOllamaModel);
    return out;
}

std::string smallestOllamaModel(std::string& error) {
    if (!aiEnabled()) {
        error = aiOffError();
        return {};
    }
    HttpResponse r = httpRequest("GET", "http://localhost:11434/api/tags", {}, {}, 2);
    Json j;
    if (r.status != 200 || !Json::parse(r.body, j, error)) return {};
    std::string best;
    double bestSize = 0;
    for (const auto& m : j["models"].items()) {
        const double size = m["size"].asNumber();
        if (m["name"].isString() && (best.empty() || size < bestSize)) {
            best = m["name"].asString();
            bestSize = size;
        }
    }
    return best;
}

BrushSuggestion requestBrush(const AiSettings& s, const std::vector<ChatMessage>& history,
                             std::string* rawAssistantJson) {
    if (!aiEnabled()) {
        BrushSuggestion out;
        out.error = aiOffError();
        return out;
    }
    if (s.provider == AiSettings::Provider::Anthropic && s.resolvedKey().empty()) {
        BrushSuggestion out;
        out.error = "No Anthropic API key - add one in AI settings or set ANTHROPIC_API_KEY, or switch to Free.";
        return out;
    }
    const std::string body = buildRequestBody(s, history);
    HttpResponse r = httpPostJson(requestUrl(s), requestHeaders(s), body);
    if (s.provider == AiSettings::Provider::Free) {
        // The anonymous tier allows a request every few seconds; one patient retry covers a quick second ask.
        if (r.status == 429) {
            std::this_thread::sleep_for(std::chrono::seconds(6));
            r = httpPostJson(requestUrl(s), requestHeaders(s), body);
        }
        BrushSuggestion out = r.error.empty() ? parseResponse(s, r.status, r.body, rawAssistantJson) : BrushSuggestion{};
        if (out.ok) return out;
        // Offline or the free service is down: use a local Ollama if one is running.
        std::string err;
        const std::string localModel = smallestOllamaModel(err);
        if (!localModel.empty()) {
            AiSettings local = AiSettings::defaults(AiSettings::Provider::OpenAICompatible);
            local.model = localModel;
            HttpResponse lr = httpPostJson(requestUrl(local), requestHeaders(local), buildRequestBody(local, history));
            if (lr.error.empty()) {
                BrushSuggestion localOut = parseResponse(local, lr.status, lr.body, rawAssistantJson);
                if (localOut.ok) return localOut;
            }
        }
        if (!r.error.empty()) out.error = "Could not reach the free AI service: " + r.error;
        out.error += " (Free tier: try again in a moment, run Ollama locally, or add your own provider.)";
        return out;
    }
    if (!r.error.empty()) {
        BrushSuggestion out;
        out.error = "Could not reach " + s.endpoint + ": " + r.error;
        return out;
    }
    return parseResponse(s, r.status, r.body, rawAssistantJson);
}

} // namespace sf::ai
