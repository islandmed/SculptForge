// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Studio controls (docs/guide/studios.md "IT deployment"): the managed policy file (core/ManagedPolicy.h), the AI
// audit log (core/AuditLog.h), provider presets and keys in the keychain (ai/Providers.h, ai/Secrets.h) and the
// studio sign-in against a mock identity provider (ai/Oidc.h).
#include "TestHarness.h"

#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "ai/Oidc.h"
#include "ai/Providers.h"
#include "ai/Secrets.h"
#include "cli/McpServer.h"
#include "core/AuditLog.h"
#include "core/Keychain.h"
#include "core/ManagedPolicy.h"
#include "core/StudioProfile.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace sf;
namespace fs = std::filesystem;

namespace {

std::string tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("sf_studio_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d.string();
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    for (std::string l; std::getline(ss, l);)
        if (!l.empty()) out.push_back(l);
    return out;
}

policy::Policy parsed(const std::string& text) {
    policy::Policy p;
    std::string error;
    CHECK(policy::parsePolicy(text, "test", p, error));
    if (!error.empty()) std::printf("    policy error: %s\n", error.c_str());
    return p;
}

std::string parseError(const std::string& text) {
    policy::Policy p;
    std::string error;
    CHECK(!policy::parsePolicy(text, "test", p, error));
    return error;
}

// The policy, the audit log path, the keychain and the AI switch back as they were, whatever a test did.
struct StudioState {
    std::string auditDir;
    explicit StudioState(const char* name) : auditDir(tempDir(name)) {
        policy::resetPolicyForTests();
        audit::setPathForTests(auditDir + "/audit.jsonl");
        keychain::useMemoryForTests(true);
        ai::secrets::clearCacheForTests();
        ai::setAiEnabled(true);
    }
    ~StudioState() {
        policy::resetPolicyForTests();
        audit::setPathForTests({});
        audit::setClient("app");
        ai::secrets::clearCacheForTests();
        ai::oidc::setTransportForTests({});
        ai::oidc::setSleepForTests({});
        ai::oidc::setClockForTests({});
        ai::setAiEnabled(true);
    }
    std::string auditPath() const { return auditDir + "/audit.jsonl"; }
};

constexpr const char* kFullPolicy = R"({
  "format": "sculptforge-policy", "version": 1,
  "organization": "Acme Animation",
  "ai": {"enabled": true, "providers": ["litellm", "comfyui"], "endpoints": ["https://llm.acme.internal/"], "requireSignIn": true},
  "network": {"enabled": false, "allow": ["https://sw.acme.internal/"], "localNetwork": true},
  "mcp": {"enabled": true, "live": "never", "categories": ["info", "edit"]},
  "audit": {"path": "/mnt/audit/{user}@{host}.jsonl", "maxMB": 20, "keep": 10, "required": true},
  "signIn": {"issuer": "https://acme.okta.com/oauth2/default", "clientId": "0oa1abc", "scopes": ["openid", "email"],
             "useFor": ["https://llm.acme.internal/"]},
  "telemetry": "none",
  "updates": {"check": true, "channel": "stable", "feed": "https://sw.acme.internal/sculptforge/latest.json"}
})";

} // namespace

TEST(studio_policy_parses_every_key) {
    const policy::Policy p = parsed(kFullPolicy);
    CHECK(p.present && !p.broken && p.organization == "Acme Animation");
    CHECK(p.ai == policy::Lock::On && p.aiProviders.size() == 2 && p.aiEndpoints.size() == 1 && p.requireSignIn);
    CHECK(p.network == policy::Lock::Off && p.networkAllow.size() == 1 && p.localNetwork);
    CHECK(p.mcp == policy::Lock::On && !p.mcpLiveAllowed && p.mcpCategories.size() == 2);
    CHECK(p.auditPath == "/mnt/audit/{user}@{host}.jsonl" && p.auditMaxMB == 20 && p.auditKeep == 10 && p.auditRequired);
    CHECK(p.signInConfigured() && p.clientId == "0oa1abc" && p.scopes.size() == 2 && p.signInUseFor.size() == 1);
    CHECK(p.updateCheck == policy::Lock::On && p.updateChannel == "stable" && !p.updateFeed.empty());
    // Everything is optional: the smallest policy manages nothing.
    const policy::Policy empty = parsed(R"({"format": "sculptforge-policy", "version": 1})");
    CHECK(empty.ai == policy::Lock::Unset && empty.network == policy::Lock::Unset && empty.mcpLiveAllowed);
    CHECK(policy::describe(p).find("telemetry none") != std::string::npos);
}

TEST(studio_policy_rejects_mistakes) {
    // A typo never quietly drops a lock.
    CHECK(parseError(R"({"format": "sculptforge-policy", "version": 1, "AI": {}})").find("unknown key") != std::string::npos);
    CHECK(parseError(R"({"format": "sculptforge-policy", "version": 1, "ai": {"enable": false}})").find("ai.enable") !=
          std::string::npos);
    CHECK(!parseError(R"({"format": "sculptforge-studio", "version": 1})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 2})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "ai": {"enabled": "no"}})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "ai": {"providers": ["skynet"]}})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "ai": {"endpoints": ["ftp://x/"]}})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "mcp": {"categories": ["everything"]}})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "mcp": {"live": "sometimes"}})").empty());
    // There is no telemetry to turn on.
    CHECK(parseError(R"({"format": "sculptforge-policy", "version": 1, "telemetry": true})").find("no telemetry") !=
          std::string::npos);
    CHECK(parsed(R"({"format": "sculptforge-policy", "version": 1, "telemetry": false})").present);
    // Sign-in required needs an identity provider; the issuer must be https.
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "ai": {"requireSignIn": true}})").empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "signIn": {"issuer": "http://idp.acme.com", "clientId": "x"}})")
               .empty());
    CHECK(!parseError(R"({"format": "sculptforge-policy", "version": 1, "updates": {"channel": "nightly"}})").empty());
    CHECK(!parseError("not json").empty());
}

TEST(studio_policy_fails_closed) {
    const std::string dir = tempDir("failclosed");
    std::ofstream(dir + "/policy.json") << R"({"format": "sculptforge-policy", "version": 1, "ai": {"enabeld": true}})";
    std::vector<std::string> messages;
    const policy::Policy broken = policy::loadPolicyFile(dir + "/policy.json", "test", &messages);
    CHECK(broken.present && broken.broken && !messages.empty());
    CHECK(broken.ai == policy::Lock::Off && broken.network == policy::Lock::Off && broken.mcp == policy::Lock::Off);
    CHECK(!broken.mcpLiveAllowed && broken.updateCheck == policy::Lock::Off);
    const policy::Policy missing = policy::loadPolicyFile(dir + "/none.json", "test");
    CHECK(missing.broken && missing.ai == policy::Lock::Off);
    std::ofstream(dir + "/good.json") << kFullPolicy;
    CHECK(!policy::loadPolicyFile(dir + "/good.json", "test").broken);
}

TEST(studio_policy_locks_the_ai_switch) {
    StudioState state("ai_switch");
    CHECK(!ai::aiManaged());
    policy::Policy off = parsed(R"({"format": "sculptforge-policy", "version": 1, "ai": {"enabled": false}})");
    policy::setPolicyForTests(off);
    CHECK(ai::aiManaged() && ai::aiPolicyLocked() && !ai::aiFeaturesAllowed(true));
    CHECK(ai::aiOffError().find("studio") != std::string::npos);
    policy::setPolicyForTests(parsed(R"({"format": "sculptforge-policy", "version": 1, "ai": {"enabled": true}})"));
    CHECK(ai::aiManaged() && ai::aiPolicyLocked() && ai::aiFeaturesAllowed(false));
    policy::resetPolicyForTests();
    CHECK(!ai::aiFeaturesAllowed(false) && ai::aiFeaturesAllowed(true));
}

TEST(studio_policy_gates_ai_providers_and_endpoints) {
    StudioState state("gate");
    policy::setPolicyForTests(parsed(R"({"format": "sculptforge-policy", "version": 1,
        "ai": {"providers": ["litellm", "ollama"], "endpoints": ["https://llm.acme.internal/v1/", "http://localhost:11434/"]}})"));
    std::string error;
    CHECK(ai::aiRequestAllowed("litellm", "https://llm.acme.internal/v1/chat/completions", error));
    CHECK(!ai::aiRequestAllowed("anthropic", "https://llm.acme.internal/v1/chat/completions", error) &&
          error.find("provider") != std::string::npos);
    CHECK(!ai::aiRequestAllowed("litellm", "https://llm.acme.internal.evil.com/v1/chat", error));
    CHECK(!ai::aiRequestAllowed("litellm", "https://llm.acme.internal/v10/chat", error)); // a '/' boundary
    // Nothing is sent for a refused request: Brush Lab with the free tier, Generate 3D with Hugging Face.
    const uint64_t before = ai::httpRequestCount();
    ai::AiSettings free = ai::AiSettings::defaults(ai::AiSettings::Provider::Free);
    ai::ChatMessage m{ai::ChatMessage::Role::User, "a clay brush", "a clay brush"};
    const ai::BrushSuggestion s = ai::requestBrush(free, {m}, nullptr);
    CHECK(!s.ok && s.error.find("policy") != std::string::npos);
    ai::AiSettings anthropic = ai::withPreset(free, "anthropic");
    anthropic.apiKey = "sk-test";
    CHECK(!ai::requestBrush(anthropic, {m}, nullptr).ok);
    CHECK(ai::httpRequestCount() == before);
    // Sign-in required: refused until signed in.
    policy::Policy signIn = parsed(R"({"format": "sculptforge-policy", "version": 1, "ai": {"requireSignIn": true},
        "signIn": {"issuer": "https://idp.acme.test", "clientId": "sf"}})");
    policy::setPolicyForTests(signIn);
    ai::oidc::setTransportForTests({}); // (starts signed out)
    CHECK(!ai::aiRequestAllowed("free", "https://text.pollinations.ai/openai", error) && error.find("Sign in") != std::string::npos);
}

TEST(studio_policy_mcp_categories) {
    StudioState state("mcp_categories");
    CHECK(policy::mcpCategories("mesh_info", {}) == std::vector<std::string>{"info"});
    CHECK(policy::mcpCategories("viewport_image", {}) == std::vector<std::string>{"info"});
    CHECK(policy::mcpCategories("stroke_screen", {"points"}) == std::vector<std::string>{"edit"});
    CHECK(policy::mcpCategories("remesh", {"resolution"}) == std::vector<std::string>{"edit"});
    CHECK(policy::mcpCategories("export_glb", {"path"}) == std::vector<std::string>{"files"});
    CHECK(policy::mcpCategories("macro_run", {"name"}) == std::vector<std::string>{"scripts"});
    const auto ai = policy::mcpCategories("image_to_3d", {"path"});
    CHECK(std::set<std::string>(ai.begin(), ai.end()) == std::set<std::string>({"ai", "network", "files"}));
    for (const auto& c : {"info", "edit", "files", "scripts", "network", "ai"})
        CHECK(std::find(policy::mcpCategoryNames().begin(), policy::mcpCategoryNames().end(), c) != policy::mcpCategoryNames().end());

    policy::setPolicyForTests(parsed(R"({"format": "sculptforge-policy", "version": 1, "mcp": {"categories": ["info", "edit"]}})"));
    CommandRegistry registry;
    Document doc;
    // tools/list leaves out what the policy doesn't allow.
    Json list;
    std::string err;
    CHECK(Json::parse(handleMcpMessage(registry, doc, R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"), list, err));
    std::set<std::string> names;
    for (const auto& t : list["result"]["tools"].items()) names.insert(t["name"].asString());
    CHECK(names.count("new_sphere") && names.count("mesh_info") && !names.count("export_glb") && !names.count("open"));
    // tools/call refuses it, says why, and the refusal is in the audit log.
    Json call;
    CHECK(Json::parse(handleMcpMessage(registry, doc,
                                       R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"export_glb","arguments":{"path":"/tmp/x.glb"}}})"),
                      call, err));
    CHECK(call["result"]["isError"].asBool() && call["result"]["content"][0]["text"].asString().find("policy") != std::string::npos);
    CHECK(readFile(state.auditPath()).find("\"export_glb\"") != std::string::npos);
    // MCP off: the server doesn't start.
    policy::setPolicyForTests(parsed(R"({"format": "sculptforge-policy", "version": 1, "mcp": {"enabled": false}})"));
    std::istringstream in(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    std::ostringstream out;
    CHECK(runMcpServer(registry, in, out) == 3 && out.str().empty());
    CHECK(!policy::mcpLiveAllowed());
    policy::resetPolicyForTests();
    CHECK(policy::mcpLiveAllowed() && policy::mcpToolAllowed("export_glb", {"path"}));
}

TEST(studio_audit_log_records_mcp_calls) {
    StudioState state("audit_mcp");
    CommandRegistry registry;
    Document doc;
    handleMcpMessage(registry, doc,
                     R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"clientInfo":{"name":"claude-code","version":"2.1.0"}}})");
    handleMcpMessage(registry, doc,
                     R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"new_sphere","arguments":{"subdivisions":3}}})");
    handleMcpMessage(registry, doc,
                     R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"brush_install","arguments":{"code":"SFB1-0123456789abcdef","api_key":"sk-secret"}}})");
    const auto l = lines(readFile(state.auditPath()));
    CHECK(l.size() == 2);
    Json first, second;
    std::string err;
    CHECK(Json::parse(l[0], first, err) && Json::parse(l[1], second, err));
    CHECK(first["client"].asString() == "mcp:claude-code 2.1.0" && first["tool"].asString() == "mcp");
    CHECK(first["command"].asString() == "new_sphere" && first["args"]["subdivisions"].asNumber() == 3);
    CHECK(first["result"].asString() == "ok" && first["time"].asString().size() == 24 && first["ms"].isNumber());
    CHECK(!first["user"].isNull() && !first["host"].asString().empty());
    // Secrets and artwork never reach the log.
    CHECK(second["args"]["api_key"].asString() == "<redacted>");
    CHECK(second["args"]["code"].asString().find("bytes") != std::string::npos);
    CHECK(l[1].find("sk-secret") == std::string::npos && l[1].find("0123456789abcdef") == std::string::npos);
}

TEST(studio_audit_log_rotates_and_can_be_required) {
    StudioState state("audit_rotate");
    policy::Policy p = parsed(R"({"format": "sculptforge-policy", "version": 1, "audit": {"maxMB": 0.1, "keep": 2}})");
    policy::setPolicyForTests(p);
    audit::Entry e;
    e.tool = "ai";
    e.command = "brush_ai";
    e.message = std::string(400, 'x');
    for (int i = 0; i < 1200; ++i) CHECK(audit::record(e));
    CHECK(fs::exists(state.auditPath()) && fs::exists(state.auditPath() + ".1") && fs::exists(state.auditPath() + ".2"));
    CHECK(!fs::exists(state.auditPath() + ".3"));
    CHECK(fs::file_size(state.auditPath()) <= uint64_t(0.1 * 1024 * 1024) + 1024);
    // Required and unwritable: the AI tool's call is refused before it runs.
    p.auditRequired = true;
    policy::setPolicyForTests(p);
    const std::string blocker = state.auditDir + "/file";
    std::ofstream(blocker) << "x";
    audit::setPathForTests(blocker + "/audit.jsonl"); // (a folder under a file can't exist)
    CommandRegistry registry;
    Document doc;
    Json call;
    std::string err;
    CHECK(Json::parse(handleMcpMessage(registry, doc,
                                       R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"new_sphere","arguments":{}}})"),
                      call, err));
    CHECK(call["result"]["isError"].asBool() && call["result"]["content"][0]["text"].asString().find("audit log") != std::string::npos);
    // The path template: user and host names can't climb out of the folder.
    CHECK(policy::expandPathTemplate("/a/{user}/x").find("..") == std::string::npos);
    CHECK(policy::expandPathTemplate("/a/b.jsonl") == "/a/b.jsonl");
}

TEST(studio_provider_presets) {
    std::set<std::string> ids;
    for (const ai::ProviderPreset& p : ai::providerPresets()) {
        CHECK(ids.insert(p.id).second); // unique
        // Every preset is one a studio's policy can name.
        CHECK(std::find(policy::aiProviderIds().begin(), policy::aiProviderIds().end(), p.id) != policy::aiProviderIds().end());
        CHECK(*p.label && *p.note);
    }
    for (const char* id : {"anthropic", "ollama", "lmstudio", "vllm", "llamacpp", "litellm", "openrouter"}) CHECK(ids.count(id));
    ai::AiSettings s = ai::withPreset(ai::AiSettings::defaults(ai::AiSettings::Provider::Free), "litellm");
    CHECK(s.provider == ai::AiSettings::Provider::OpenAICompatible && ai::providerId(s) == "litellm");
    s.endpoint = "https://llm.acme.internal/v1/chat/completions"; // the studio's own address keeps the preset
    CHECK(ai::providerId(ai::AiSettings::fromJson(s.toJson())) == "litellm");
    // An ai.json from before presets: its endpoint names the server.
    ai::AiSettings old = ai::AiSettings::defaults(ai::AiSettings::Provider::OpenAICompatible);
    CHECK(ai::providerId(old) == "ollama");
    old.endpoint = "http://gpu7:9000/v1/chat/completions";
    CHECK(ai::providerId(old) == "openai-compatible");
}

TEST(studio_keys_live_in_the_keychain) {
    StudioState state("keys");
    ai::AiSettings s = ai::withPreset(ai::AiSettings::defaults(ai::AiSettings::Provider::Free), "openrouter");
    s.apiKey = "sk-or-test";
    s.hfToken = "hf_test";
    s.falKey = "fal-test";
    s.meshyKey = "msy-test";
    s.hyper3dKey = "hyper3d-test";
    const std::string json = s.toJson().dump();
    for (const char* secret : {"sk-or-test", "hf_test", "fal-test", "msy-test", "hyper3d-test"}) CHECK(json.find(secret) == std::string::npos);
    CHECK(ai::secrets::storeKeys(s));
    std::string v;
    CHECK(keychain::load("ai/openrouter", v) && v == "sk-or-test");
    CHECK(keychain::load(ai::secrets::kHuggingFace, v) && v == "hf_test");
    // A fresh load gets them from the keychain.
    ai::secrets::clearCacheForTests();
    const ai::AiSettings loaded = ai::AiSettings::fromJson(s.toJson());
    CHECK(loaded.apiKey.empty() && loaded.resolvedKey() == "sk-or-test" && loaded.resolvedHfToken() == "hf_test");
    CHECK(loaded.resolvedFalKey() == "fal-test" && loaded.resolvedMeshyKey() == "msy-test");
    CHECK(loaded.resolvedHyper3dKey() == "hyper3d-test");
    // An older ai.json with keys in it: moved to the keychain, and out of the file.
    keychain::useMemoryForTests(true);
    ai::secrets::clearCacheForTests();
    const std::string path = state.auditDir + "/ai.json";
    Json legacy = s.toJson();
    legacy.set("apiKey", "sk-or-old");
    legacy.set("hfToken", "hf_old");
    legacy.set("hyper3dKey", "hyper3d-old");
    std::ofstream(path) << legacy.dump();
    std::string message;
    CHECK(ai::secrets::migrateLegacyKeys(path, message) && message.find("keychain") != std::string::npos);
    const std::string after = readFile(path);
    CHECK(after.find("sk-or-old") == std::string::npos && after.find("hf_old") == std::string::npos);
    CHECK(keychain::load("ai/openrouter", v) && v == "sk-or-old");
    CHECK(ai::AiSettings::load(path).resolvedHfToken() == "hf_old");
    CHECK(after.find("hyper3d-old") == std::string::npos);
    CHECK(ai::AiSettings::load(path).resolvedHyper3dKey() == "hyper3d-old");
    // Nothing to move: the file is left alone.
    const auto stamp = fs::last_write_time(path);
    CHECK(ai::secrets::migrateLegacyKeys(path, message) && message.empty() && fs::last_write_time(path) == stamp);
}

namespace {

// A mock identity provider: discovery, device authorization and a token endpoint that answers "pending" a few
// times, then "slow_down" once, then tokens. It records what it was sent.
struct MockIdp {
    std::string issuer = "https://idp.acme.test/realms/art";
    int pendingPolls = 2;
    bool slowDownOnce = true;
    bool deny = false;
    bool advertiseDevice = true;
    std::string discoveredIssuer;
    std::vector<std::string> posts;
    int64_t clock = 1'800'000'000;
    int slept = 0;
    int refreshes = 0;

    static std::string idToken() {
        // {"sub":"u1","email":"ana@acme.test","name":"Ana Ruiz"} base64url, unsigned (display only)
        return "eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImVtYWlsIjoiYW5hQGFjbWUudGVzdCIsIm5hbWUiOiJBbmEgUnVpeiJ9.";
    }
    void install() {
        ai::oidc::setTransportForTests(
            {[this](const std::string& url) {
                 ai::HttpResponse r;
                 if (url != issuer + "/.well-known/openid-configuration") return r.status = 404, r;
                 Json d = Json::object();
                 d.set("issuer", discoveredIssuer.empty() ? issuer : discoveredIssuer);
                 if (advertiseDevice) d.set("device_authorization_endpoint", issuer + "/device");
                 d.set("token_endpoint", issuer + "/token");
                 r.status = 200;
                 r.body = d.dump();
                 return r;
             },
             [this](const std::string& url, const std::string& body) {
                 posts.push_back(url + " " + body);
                 ai::HttpResponse r;
                 r.status = 200;
                 if (url == issuer + "/device") {
                     r.body = R"({"device_code":"dev-123","user_code":"WDJB-MJHT","verification_uri":"https://idp.acme.test/device",
                                 "verification_uri_complete":"https://idp.acme.test/device?code=WDJB-MJHT","interval":2,"expires_in":300})";
                 } else if (body.find("grant_type=refresh_token") != std::string::npos) {
                     ++refreshes;
                     r.body = R"({"access_token":"at-2","token_type":"Bearer","expires_in":3600})";
                 } else if (deny) {
                     r.status = 400, r.body = R"({"error":"access_denied"})";
                 } else if (pendingPolls > 0) {
                     --pendingPolls;
                     r.status = 400, r.body = R"({"error":"authorization_pending"})";
                 } else if (slowDownOnce) {
                     slowDownOnce = false;
                     r.status = 400, r.body = R"({"error":"slow_down"})";
                 } else {
                     Json t = Json::object();
                     t.set("access_token", "at-1");
                     t.set("token_type", "Bearer");
                     t.set("expires_in", 600);
                     t.set("refresh_token", "rt-1");
                     t.set("id_token", idToken());
                     r.body = t.dump();
                 }
                 return r;
             }});
        ai::oidc::setSleepForTests([this](int s) {
            slept += s;
            clock += s;
        });
        ai::oidc::setClockForTests([this] { return clock; });
    }
    policy::Policy policyFor() const {
        return parsed(R"({"format": "sculptforge-policy", "version": 1, "ai": {"endpoints": ["https://llm.acme.test/"]},
            "signIn": {"issuer": ")" + issuer + R"(", "clientId": "sculptforge-desktop", "audience": "sf-gateway"}})");
    }
};

} // namespace

TEST(studio_sign_in_device_flow_with_a_mock_idp) {
    StudioState state("oidc");
    MockIdp idp;
    policy::setPolicyForTests(idp.policyFor());
    idp.install();
    CHECK(ai::oidc::state() == ai::oidc::State::SignedOut);
    std::string shownCode, error;
    const bool ok = ai::oidc::signIn(
        [&](const ai::oidc::DeviceCode& c) {
            shownCode = c.userCode;
            CHECK(c.verificationUriComplete.find("WDJB-MJHT") != std::string::npos);
            return true;
        },
        {}, error);
    CHECK(ok && error.empty() && shownCode == "WDJB-MJHT");
    // Pending twice (2 s each), slow_down (+5 s), then the tokens: the client waited as RFC 8628 asks.
    CHECK(idp.slept == 2 + 2 + 2 + 7);
    CHECK(idp.posts.front().find("client_id=sculptforge-desktop") != std::string::npos);
    CHECK(idp.posts.front().find("scope=openid+profile+email+offline_access") != std::string::npos);
    CHECK(idp.posts.front().find("audience=sf-gateway") != std::string::npos);
    CHECK(idp.posts.back().find("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code") != std::string::npos);
    CHECK(ai::oidc::state() == ai::oidc::State::SignedIn && ai::oidc::signedInAs() == "Ana Ruiz");
    // The tokens are in the keychain, not a file.
    std::string secret;
    CHECK(keychain::load("sso/sculptforge-desktop", secret) && secret.find("at-1") != std::string::npos);
    // The bearer goes to the studio gateway only.
    CHECK(ai::oidc::bearerHeaderFor("https://llm.acme.test/v1/chat/completions") == "Authorization: Bearer at-1");
    CHECK(ai::oidc::bearerHeaderFor("https://api.openai.com/v1/chat/completions").empty());
    ai::AiSettings gw = ai::withPreset(ai::AiSettings::defaults(ai::AiSettings::Provider::Free), "litellm");
    gw.endpoint = "https://llm.acme.test/v1/chat/completions";
    gw.apiKey = "should-not-be-sent";
    const auto headers = ai::requestHeaders(gw);
    CHECK(std::find(headers.begin(), headers.end(), "Authorization: Bearer at-1") != headers.end());
    for (const auto& h : headers) CHECK(h.find("should-not-be-sent") == std::string::npos);
    // Expired: refreshed with the refresh token.
    idp.clock += 3600;
    CHECK(ai::oidc::accessToken() == "at-2" && idp.refreshes == 1);
    // Signing out forgets them.
    ai::oidc::signOut();
    CHECK(ai::oidc::state() == ai::oidc::State::SignedOut && !keychain::load("sso/sculptforge-desktop", secret));
    CHECK(ai::oidc::bearerHeaderFor("https://llm.acme.test/v1/chat/completions").empty());
}

TEST(studio_sign_in_refusals) {
    StudioState state("oidc_refusals");
    std::string error;
    {
        MockIdp idp;
        idp.deny = true;
        policy::setPolicyForTests(idp.policyFor());
        idp.install();
        CHECK(!ai::oidc::signIn({}, {}, error) && error.find("declined") != std::string::npos);
        CHECK(ai::oidc::state() == ai::oidc::State::SignedOut);
    }
    {
        MockIdp idp; // another issuer in the discovery document: refused before anything is posted
        idp.discoveredIssuer = "https://evil.test/";
        policy::setPolicyForTests(idp.policyFor());
        idp.install();
        CHECK(!ai::oidc::signIn({}, {}, error) && error.find("issuer") != std::string::npos && idp.posts.empty());
    }
    {
        MockIdp idp;
        idp.advertiseDevice = false;
        policy::setPolicyForTests(idp.policyFor());
        idp.install();
        CHECK(!ai::oidc::signIn({}, {}, error) && error.find("device") != std::string::npos);
    }
    {
        MockIdp idp; // the code runs out while the artist is away
        idp.pendingPolls = 1000;
        policy::setPolicyForTests(idp.policyFor());
        idp.install();
        CHECK(!ai::oidc::signIn({}, {}, error) && error.find("expired") != std::string::npos);
    }
    {
        MockIdp idp; // cancelled from the panel
        idp.pendingPolls = 1000;
        policy::setPolicyForTests(idp.policyFor());
        idp.install();
        int calls = 0;
        CHECK(!ai::oidc::signIn({}, [&] { return ++calls > 3; }, error) && error.find("cancelled") != std::string::npos);
    }
    // Parsers on their own.
    ai::oidc::Tokens t;
    CHECK(ai::oidc::parseTokenReply(400, R"({"error":"expired_token"})", 0, t, error) == ai::oidc::TokenReply::Expired);
    CHECK(ai::oidc::parseTokenReply(200, R"({"access_token":"x","token_type":"mac"})", 0, t, error) == ai::oidc::TokenReply::Error);
    ai::oidc::DeviceCode d;
    CHECK(!ai::oidc::parseDeviceCode(R"({"device_code":"a","user_code":"b","verification_uri":"http://evil.test/"})", 0, d, error));
    CHECK(ai::oidc::parseIdToken("garbage").display().empty());
    policy::resetPolicyForTests();
    CHECK(ai::oidc::state() == ai::oidc::State::NotConfigured && !ai::oidc::signIn({}, {}, error));
}

TEST(studio_policy_network_off_is_studio_mode) {
    const policy::Policy p = parsed(kFullPolicy);
    const net::NetworkPolicy n = withManagedPolicy({}, p);
    CHECK(n.offline && n.localNetwork);
    auto allowed = [&](const std::string& u) { return std::find(n.allow.begin(), n.allow.end(), u) != n.allow.end(); };
    CHECK(allowed("https://sw.acme.internal/") && allowed("https://llm.acme.internal/") && allowed("https://acme.okta.com/"));
    CHECK(allowed("https://sw.acme.internal/sculptforge/latest.json"));
    // Network on (or not managed): unchanged.
    const net::NetworkPolicy open = withManagedPolicy({}, parsed(R"({"format": "sculptforge-policy", "version": 1})"));
    CHECK(!open.offline && open.allow.empty());
}

TEST(hyper3d_respects_studio_policy) {
    StudioState state("hyper3d_policy");
    ai::AiSettings settings;
    settings.generatorService = "hyper3d";
    const uint64_t before = ai::httpRequestCount();
    policy::setPolicyForTests(parsed(R"({"format":"sculptforge-policy","version":1,"ai":{"providers":["fal"]}})"));
    CHECK(!ai::generate3D(settings, "missing.png", "unused").ok);
    CHECK(!ai::generate3DFromText(settings, "a fox", "unused").ok);
    policy::setPolicyForTests(parsed(R"({"format":"sculptforge-policy","version":1,"ai":{"providers":["hyper3d"],"endpoints":["https://internal.example/"]}})"));
    CHECK(!ai::generate3DFromText(settings, "a fox", "unused").ok);
    CHECK(ai::httpRequestCount() == before);
    std::string error;
    policy::setPolicyForTests(parsed(R"({"format":"sculptforge-policy","version":1,"ai":{"providers":["hyper3d"],"endpoints":["https://api.hyper3d.com/"]}})"));
    CHECK(ai::aiRequestAllowed("hyper3d", settings.hyper3dEndpoint, error));
}
