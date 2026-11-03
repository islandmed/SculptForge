// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The AI switch (ai/AiPolicy.h, Preferences > AI): off, no AI command is registered and no AI entry point
// starts a network request; SF_AI wins over the saved choice; ai.json keeps it.
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/ComfyUI.h"
#include "ai/GradioClient.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"

#include <cstdlib>
#include <filesystem>

using namespace sf;
using namespace sf::ai;

namespace {

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// Restores the switch and SF_AI, whatever a test left.
struct AiState {
    std::string savedEnv;
    bool hadEnv = false;
    AiState() {
        if (const char* v = std::getenv("SF_AI")) savedEnv = v, hadEnv = true;
        setEnv("SF_AI", nullptr);
    }
    ~AiState() {
        setEnv("SF_AI", hadEnv ? savedEnv.c_str() : nullptr);
        setAiForcedOff(false);
        setAiEnabled(true);
    }
};

CommandRegistry registryWithAi() {
    CommandRegistry r;
    registerAiCommands(r, [] { return AiSettings::defaults(AiSettings::Provider::Free); });
    return r;
}

} // namespace

TEST(ai_switch_saved_in_ai_json) {
    CHECK(AiSettings::fromJson(Json::object()).enabled); // older ai.json files: on, as before
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    CHECK(s.enabled);
    s.enabled = false;
    CHECK(!AiSettings::fromJson(s.toJson()).enabled);
    const std::string path = tempPath("sf_ai_switch_test.json");
    std::string err;
    CHECK(s.save(path, err));
    CHECK(!AiSettings::load(path).enabled);
    std::filesystem::remove(path);
}

TEST(ai_switch_environment_wins) {
    AiState restore;
    CHECK(aiFeaturesAllowed(true));
    CHECK(!aiFeaturesAllowed(false));
    CHECK(!aiPolicyLocked());
    for (const char* off : {"off", "OFF", "0", "false", "no"}) {
        setEnv("SF_AI", off);
        CHECK(!aiFeaturesAllowed(true));
        CHECK(aiPolicyLocked());
    }
    setEnv("SF_AI", "on");
    CHECK(aiFeaturesAllowed(false));
    setEnv("SF_AI", "maybe"); // unknown: the saved choice
    CHECK(aiFeaturesAllowed(true));
    CHECK(!aiFeaturesAllowed(false));
    CHECK(!aiPolicyLocked());
    setEnv("SF_AI", nullptr);
    setAiForcedOff(true); // --no-ai
    CHECK(!aiFeaturesAllowed(true));
    CHECK(aiPolicyLocked());
    CHECK(aiOffError().find("SF_AI") != std::string::npos);
}

TEST(ai_switch_off_registers_no_ai_command) {
    AiState restore;
    setAiEnabled(false);
    CommandRegistry r = registryWithAi();
    for (const std::string& name : aiCommandNames()) {
        CHECK(!r.find(name));
        CHECK(r.toolSchemaJson().find("\"" + name + "\"") == std::string::npos); // the MCP / LLM tool list
    }
    CHECK(r.find("brush_gallery_search")); // not AI: they stay
    CHECK(r.find("unreal_send"));
    Document doc;
    CHECK(!r.execute(doc, "brush_ai", {{"prompt", std::string("sharp")}}).ok);

    setAiEnabled(true); // turned back on at run time: each once
    addAiFeatureCommands(r, [] { return AiSettings::defaults(AiSettings::Provider::Free); });
    addAiFeatureCommands(r, [] { return AiSettings::defaults(AiSettings::Provider::Free); });
    for (const std::string& name : aiCommandNames()) {
        int n = 0;
        for (const Command& c : r.all()) n += c.name == name;
        CHECK(n == 1);
    }
    removeAiFeatureCommands(r);
    for (const std::string& name : aiCommandNames()) CHECK(!r.find(name));
    CHECK(r.find("unreal_send"));

    CommandRegistry on = registryWithAi();
    for (const std::string& name : aiCommandNames()) CHECK(on.find(name));
}

TEST(ai_switch_off_sends_nothing) {
    AiState restore;
    setAiEnabled(false);
    const uint64_t before = httpRequestCount();
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    const std::string out = tempPath("sf_ai_switch_out");
    auto refused = [](const std::string& error) { return error.find("AI features") != std::string::npos; };

    BrushSuggestion b = requestBrush(s, {composeUserMessage("sharp wrinkles", nullptr)}, nullptr);
    CHECK(!b.ok);
    CHECK(refused(b.error));
    std::string err;
    CHECK(listModels(AiSettings::defaults(AiSettings::Provider::OpenAICompatible), err).empty());
    CHECK(refused(err));
    err.clear();
    CHECK(smallestOllamaModel(err).empty());
    CHECK(refused(err));
    for (const char* service : {"huggingface", "fal", "meshy", "command", "inflate"}) {
        s.generatorService = service;
        GeneratorResult g = generate3D(s, "missing.png", out);
        CHECK(!g.ok);
        CHECK(refused(g.error));
    }
    for (const char* service : {"huggingface", "fal", "meshy"}) {
        s.generatorService = service;
        CHECK(refused(generate3DFromText(s, "a dragon head", out).error));
    }
    CHECK(refused(comfy::run(s, {}, "missing.png", {}, out, nullptr).error));
    comfy::ServerInfo info;
    err.clear();
    CHECK(!comfy::checkServer("http://127.0.0.1:9", info, err, 1));
    CHECK(refused(err));
    GradioClient gradio;
    err.clear();
    CHECK(!gradio.connect("https://example.hf.space", {}, err));
    CHECK(refused(err));
    CHECK(httpRequestCount() == before); // nothing was sent

    setAiEnabled(true); // the counter does see requests: nothing listens on port 9
    err.clear();
    CHECK(!comfy::checkServer("http://127.0.0.1:9", info, err, 1));
    CHECK(httpRequestCount() > before);
}
