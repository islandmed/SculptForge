// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The first-use notice for third-party AI hosts (ai/Consent.h): nothing is sent to a service on someone else's
// computer before the artist allows it; Allow is kept in ai.json and Revoke takes it back; this computer never asks;
// a studio's policy pre-allows what it lists and its "no" wins over an Allow.
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/ComfyUI.h"
#include "ai/Consent.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "ai/Providers.h"
#include "core/ManagedPolicy.h"

using namespace sf;
using namespace sf::ai;

namespace {

bool notAllowed(const std::string& error) { return error.rfind(kSendNotAllowed, 0) == 0; }

struct ConsentState {
    ConsentState() {
        policy::resetPolicyForTests();
        setAiEnabled(true);
    }
    ~ConsentState() {
        policy::resetPolicyForTests();
        setGeneratorStubForTests({});
        setAiEnabled(true);
    }
};

AiSettings freeSettings() { return AiSettings::defaults(AiSettings::Provider::Free); }

ChatMessage ask() { return composeUserMessage("sharp wrinkles that fade out", nullptr); }

} // namespace

TEST(ai_consent_nothing_sent_before_allow) {
    ConsentState restore;
    const uint64_t before = httpRequestCount();
    const std::string out = tempPath("sf_consent_out");
    AiSettings s = freeSettings();

    BrushSuggestion b = requestBrush(s, {ask()}, nullptr);
    CHECK(!b.ok && notAllowed(b.error));
    CHECK(b.error.find("text.pollinations.ai") != std::string::npos); // says where it would have gone
    std::string err;
    CHECK(listModels(withPreset(s, "openrouter"), err).empty() && notAllowed(err));

    GeneratorResult g = generate3D(s, "missing.png", out); // Hugging Face, the default
    CHECK(!g.ok && notAllowed(g.error));
    CHECK(notAllowed(generate3DFromText(s, "a dragon head", out).error));
    AiSettings paid = s;
    paid.falKey = "fal-test";
    paid.generatorService = "fal";
    CHECK(notAllowed(generate3D(paid, "missing.png", out).error));
    paid.generatorService = "meshy";
    paid.meshyKey = "msy-test";
    CHECK(notAllowed(generate3DFromText(paid, "a fox", out).error));
    paid.generatorService = "hyper3d";
    paid.hyper3dKey = "h3d-test";
    CHECK(notAllowed(generate3D(paid, "missing.png", out).error));
    AiSettings remote = s;
    remote.generatorService = "comfyui";
    remote.comfyUrl = "http://10.0.0.5:8188"; // a studio GPU server: someone else's computer
    CHECK(notAllowed(generate3D(remote, "missing.png", out).error));

    CommandRegistry reg;
    registerAiCommands(reg, [] { return AiSettings::defaults(AiSettings::Provider::Free); });
    Document doc;
    CHECK(notAllowed(reg.execute(doc, "brush_ai", {{"prompt", std::string("bark")}}).message));
    CHECK(notAllowed(reg.execute(doc, "brush_ai", {{"prompt", std::string("bark")}, {"provider", std::string("free")}}).message));
    CHECK(notAllowed(reg.execute(doc, "comfyui_check", {{"url", std::string("http://10.0.0.5:8188")}}).message));
    CHECK(httpRequestCount() == before); // not one request
}

TEST(ai_consent_a_missing_key_comes_first) {
    ConsentState restore;
    AiSettings s = freeSettings();
    s.generatorService = "hyper3d"; // no key (keychain only): "add your key", not the notice
    if (s.resolvedHyper3dKey().empty()) {
        CHECK(generateKeyMissing(s));
        const GeneratorResult g = generate3D(s, "missing.png", tempPath("sf_consent_nokey"));
        CHECK(!g.ok && !notAllowed(g.error) && g.error.find("key") != std::string::npos);
    }
    s.generatorService = "huggingface";
    CHECK(!generateKeyMissing(s)); // free: no key needed
}

TEST(ai_consent_targets_name_what_where_and_who_pays) {
    AiSettings s = freeSettings();
    SendTarget t = brushTarget(s);
    CHECK(t.host == "text.pollinations.ai" && t.id == "text.pollinations.ai" && !t.local);
    CHECK(t.what.find("brush description") != std::string::npos && t.payer.find("Free") != std::string::npos);

    t = brushTarget(withPreset(s, "openrouter"));
    CHECK(t.host == "openrouter.ai" && t.label == "OpenRouter" && t.payer.find("You") != std::string::npos);
    t = brushTarget(withPreset(s, "anthropic"));
    CHECK(t.host == "api.anthropic.com" && t.policyId == "anthropic");

    // Hugging Face: one notice per Space family (a Space owner), the Space that actually runs.
    t = generateTarget(s, false);
    CHECK(t.id == "hf-tencent" && t.host == "tencent-hunyuan3d-2-1.hf.space" && t.what.find("image") != std::string::npos);
    CHECK(generateTarget(s, true).what.find("description") != std::string::npos);
    AiSettings trellis = s;
    trellis.hfModel = "trellis-2";
    trellis.hfModelChosen = true;
    trellis.hfToken = ""; // no token: Hunyuan3D 2.1 runs instead, so that is what is named
    if (trellis.resolvedHfToken().empty()) CHECK(generateTarget(trellis, false).id == "hf-tencent");
    trellis.hfToken = "hf_test";
    CHECK(generateTarget(trellis, false).id == "hf-microsoft");
    AiSettings sf3d = s;
    sf3d.hfModel = "stable-fast-3d";
    sf3d.hfModelChosen = true;
    CHECK(generateTarget(sf3d, false).id == "hf-stabilityai");

    AiSettings g = s;
    g.generatorService = "fal";
    CHECK(generateTarget(g, false).host == "queue.fal.run" && generateTarget(g, false).payer.find("You") != std::string::npos);
    g.generatorService = "meshy";
    CHECK(generateTarget(g, false).host == "api.meshy.ai");
    g.generatorService = "hyper3d";
    CHECK(generateTarget(g, false).host == "api.hyper3d.com");
}

TEST(ai_consent_this_computer_never_asks) {
    ConsentState restore;
    CHECK(localUrl("http://localhost:11434/v1/chat/completions"));
    CHECK(localUrl("http://127.0.0.1:8188"));
    CHECK(localUrl("http://[::1]:1234/v1/models"));
    CHECK(localUrl("HTTP://LOCALHOST:8188"));
    CHECK(!localUrl("http://127.evil.example/v1")); // a name, not the loopback address
    CHECK(!localUrl("http://localhost@evil.example/"));
    CHECK(!localUrl("http://localhost.evil.example/"));
    CHECK(!localUrl("https://10.0.0.5:8188"));
    CHECK(!localUrl("file:///etc/passwd"));

    AiSettings s = freeSettings();
    for (const char* id : {"ollama", "lmstudio", "llamacpp", "vllm", "litellm"}) CHECK(brushTarget(withPreset(s, id)).local);
    CHECK(sendAllowed(s, brushTarget(withPreset(s, "ollama"))));
    s.generatorService = "comfyui"; // the default address
    CHECK(generateTarget(s, false).local);
    s.generatorService = "inflate";
    CHECK(generateTarget(s, false).local);
    s.generatorService = "command";
    CHECK(generateTarget(s, true).local);

    // A local request goes ahead (nothing listens on port 9, so it fails to connect, not on the notice).
    const uint64_t before = httpRequestCount();
    AiSettings local = withPreset(freeSettings(), "ollama");
    local.endpoint = "http://127.0.0.1:9/v1/chat/completions";
    const BrushSuggestion b = requestBrush(local, {ask()}, nullptr);
    CHECK(!b.ok && !notAllowed(b.error));
    CHECK(httpRequestCount() > before);
}

TEST(ai_consent_allow_is_kept_and_revoke_takes_it_back) {
    ConsentState restore;
    int calls = 0;
    setGeneratorStubForTests([&](int, const std::string& stem) {
        ++calls;
        GeneratorResult r;
        r.ok = true;
        r.outputPath = stem + ".obj";
        return r;
    });
    AiSettings s = freeSettings();
    const SendTarget hf = generateTarget(s, false);
    CHECK(!sendAllowed(s, hf));
    CHECK(!generate3D(s, "missing.png", "stem").ok && calls == 0); // the stub stands in for the service: not reached

    allowSend(s, hf.id);
    allowSend(s, hf.id); // once is enough
    CHECK(s.sendAllowed.size() == 1 && sendRemembered(s, hf.id) && sendAllowed(s, hf));
    CHECK(generate3D(s, "missing.png", "stem").ok && calls == 1);
    CHECK(generate3DFromText(s, "a fox", "stem").ok && calls == 2); // the same Space family
    CHECK(!sendAllowed(s, brushTarget(s))); // one service's Allow is not another's

    // ai.json keeps it (no keys in there, ever).
    const std::string path = tempPath("sf_consent_ai.json");
    std::string err;
    CHECK(s.save(path, err));
    AiSettings loaded = AiSettings::load(path);
    CHECK(sendRemembered(loaded, hf.id));
    CHECK(AiSettings::fromJson(Json::object()).sendAllowed.empty()); // older files: everything asks first

    revokeSend(loaded, hf.id);
    CHECK(!sendAllowed(loaded, hf));
    CHECK(!generate3D(loaded, "missing.png", "stem").ok && calls == 2);
    CHECK(loaded.save(path, err) && AiSettings::load(path).sendAllowed.empty());
}

TEST(ai_consent_studio_policy_forbids_and_pre_allows) {
    ConsentState restore;
    AiSettings s = freeSettings();
    allowSend(s, brushTarget(s).id);
    allowSend(s, generateTarget(s, false).id);
    policy::Policy p;
    p.present = true;
    p.aiProviders = {"ollama", "huggingface"};
    policy::setPolicyForTests(p);
    const uint64_t before = httpRequestCount();
    // The studio's "no" wins over the artist's Allow.
    CHECK(!sendAllowed(s, brushTarget(s)));
    const BrushSuggestion b = requestBrush(s, {ask()}, nullptr);
    CHECK(!b.ok && b.error.find("policy") != std::string::npos);
    CHECK(httpRequestCount() == before);
    // What it lists is its decision: no notice for its artists.
    AiSettings fresh = freeSettings();
    CHECK(policyPreAllowed(generateTarget(fresh, false)) && sendAllowed(fresh, generateTarget(fresh, false)));
    CHECK(!policyPreAllowed(brushTarget(fresh)));
    // An endpoint the policy names counts too (a studio gateway).
    p.aiProviders.clear();
    p.aiEndpoints = {"https://llm.acme.internal/"};
    policy::setPolicyForTests(p);
    AiSettings gateway = withPreset(fresh, "litellm");
    gateway.endpoint = "https://llm.acme.internal/v1/chat/completions";
    CHECK(!brushTarget(gateway).local && sendAllowed(fresh, brushTarget(gateway)));
    CHECK(!sendAllowed(s, brushTarget(s))); // Pollinations isn't under it: refused, Allow or not
    // AI off by policy: never pre-allowed.
    p.ai = policy::Lock::Off;
    policy::setPolicyForTests(p);
    CHECK(!policyPreAllowed(brushTarget(gateway)));
}

TEST(ai_consent_preferences_list_every_online_service) {
    AiSettings s = freeSettings();
    allowSend(s, "llm.example.org"); // allowed earlier for a server these settings no longer use
    const std::vector<SendTarget> all = knownTargets(s);
    auto has = [&](const std::string& id) {
        for (const SendTarget& t : all)
            if (t.id == id) return true;
        return false;
    };
    for (const char* id : {"text.pollinations.ai", "api.anthropic.com", "api.openai.com", "openrouter.ai", "hf-tencent",
                           "hf-microsoft", "hf-stabilityai", "queue.fal.run", "api.meshy.ai", "api.hyper3d.com",
                           "llm.example.org"})
        CHECK(has(id));
    for (const SendTarget& t : all) CHECK(!t.local && !t.label.empty());
}
