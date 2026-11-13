// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The direct provider added with Generate 3D's Recommended list: Tripo (ai/Tripo.h) is off without the user's own
// key (keychain only), pinned to one host, sends the key nowhere else,
// check the file before the importer sees it, give every failure a plain message, and fall back to the free models
// only for what another provider would not share. The feed's Space health (ai/SpaceHealth.h) greys a dead Space and
// moves a run to one that is up. No test talks to a provider: the transports are faked.
#include "TestHarness.h"

#include "ai/AiPolicy.h"
#include "ai/Consent.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "ai/Secrets.h"
#include "ai/SpaceHealth.h"
#include "ai/Tripo.h"
#include "core/AuditLog.h"
#include "core/Image.h"
#include "core/Keychain.h"
#include "core/ManagedPolicy.h"
#include "core/UpdateCheck.h"

#include <filesystem>
#include <fstream>

using namespace sf;
using namespace sf::ai;
namespace fs = std::filesystem;

namespace {

constexpr const char* kKey = "tripo-test-key-123";

std::string glb(std::string json) {
    while (json.size() % 4) json += ' ';
    auto u32 = [](std::string& s, uint32_t v) {
        for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
    };
    std::string out = "glTF";
    u32(out, 2);
    u32(out, uint32_t(12 + 8 + json.size()));
    u32(out, uint32_t(json.size()));
    u32(out, 0x4E4F534Au);
    return out + json;
}
const std::string kGoodGlb = glb(R"({"asset":{"version":"2.0"}})");

struct State {
    std::string auditDir;
    std::vector<int> waits;
    State() {
        auditDir = (fs::temp_directory_path() / "sf_tripo_audit").string();
        std::error_code ec;
        fs::remove_all(auditDir, ec);
        fs::create_directories(auditDir, ec);
        policy::resetPolicyForTests();
        audit::setPathForTests(auditDir + "/audit.jsonl");
        keychain::useMemoryForTests(true);
        secrets::clearCacheForTests();
        health::set({});
        setAiEnabled(true);
    }
    ~State() {
        tripo::setTransportForTests({});
        tripo::setTimeoutSecondsForTests(0);
        policy::resetPolicyForTests();
        audit::setPathForTests({});
        secrets::put(secrets::kTripo, "");
        secrets::clearCacheForTests();
        health::set({});
        setAiEnabled(true);
    }
    std::string audit() const {
        std::ifstream f(auditDir + "/audit.jsonl", std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
};

AiSettings tripoSettings() {
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    s.generatorService = "tripo";
    s.tripoKey = kKey;
    allowSend(s, generateTarget(s, false).id);
    return s;
}
std::string pngFile(const char* name) {
    Image img;
    img.width = img.height = 64;
    img.rgba.assign(64 * 64 * 4, 255);
    const std::string path = (fs::temp_directory_path() / name).string();
    std::string err;
    img.savePng(path, err);
    return path;
}

bool hasKeyHeader(const std::vector<std::string>& headers, const char* key) {
    for (const auto& h : headers)
        if (h.find(key) != std::string::npos) return true;
    return false;
}

// A Tripo that answers like the documented v3 API; records what it was sent.
struct FakeTripo {
    struct Call {
        std::string method, url, body;
        std::vector<std::string> headers;
    };
    std::vector<Call> calls;
    int pendingPolls = 2;
    std::string fileUrl = "https://cdn.tripo3d.ai/output/model.glb";
    std::string file = kGoodGlb;
    std::string createAnswer = R"({"code":0,"data":{"task_id":"task-1"}})";
    long createStatus = 200;
    long pollStatus = 200;      // every status check answers this when not 200
    std::string finalStatus = "success";

    HttpResponse operator()(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body) {
        calls.push_back({method, url, body, headers});
        HttpResponse r;
        r.status = 200;
        if (url == fileUrl) r.body = file;
        else if (url.ends_with("/files")) r.body = R"({"code":0,"data":{"file_token":"file_abc"}})";
        else if (url.find("/generation/") != std::string::npos) {
            r.status = createStatus;
            r.body = createAnswer;
        } else if (url.find("/tasks/") != std::string::npos) {
            if (pollStatus != 200) {
                r.status = pollStatus;
                return r;
            }
            if (pendingPolls-- > 0) r.body = R"({"code":0,"data":{"task_id":"task-1","status":"running","progress":40}})";
            else if (finalStatus == "success")
                r.body = R"({"code":0,"data":{"task_id":"task-1","status":"success","progress":100,"output":{"model_url":")" +
                         fileUrl + R"("}}})";
            else r.body = R"({"code":0,"data":{"task_id":"task-1","status":")" + finalStatus + R"(","error_message":"bad input"}})";
        }
        return r;
    }
};

void useFake(FakeTripo& fake, State& st) {
    tripo::setTransportForTests([&fake](const std::string& m, const std::string& u, const std::vector<std::string>& h,
                                        const std::string& b) { return fake(m, u, h, b); },
                                &st.waits);
}

} // namespace

TEST(tripo_key_only_from_the_keychain) {
    State st;
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    CHECK(s.resolvedTripoKey().empty()); // no environment variable, no built-in key
    s.tripoKey = kKey;
    const std::string json = s.toJson().dump();
    CHECK(json.find(kKey) == std::string::npos); // never in ai.json
    CHECK(secrets::storeKeys(s));
    secrets::clearCacheForTests();
    CHECK(AiSettings::defaults(AiSettings::Provider::Free).resolvedTripoKey() == kKey);
    int listed = 0;
    for (const auto& a : secrets::allAccounts()) listed += a == secrets::kTripo;
    CHECK(listed == 1);
    Json j = s.toJson();
    j.set("tripoKey", "planted");
    CHECK(AiSettings::fromJson(j).resolvedTripoKey() == kKey || AiSettings::fromJson(j).resolvedTripoKey().empty());
}

TEST(tripo_without_key_sends_nothing) {
    State st;
    FakeTripo fake;
    useFake(fake, st);
    AiSettings t = AiSettings::defaults(AiSettings::Provider::Free);
    t.generatorService = "tripo";
    GeneratorResult r = generate3DFromText(t, "a dragon head", (fs::temp_directory_path() / "sf_tripo_nokey").string());
    CHECK(!r.ok && r.error.find("Tripo API key") != std::string::npos && fake.calls.empty());
    // the AI switch and the studio policy stop it before anything is sent
    t.tripoKey = kKey;
    setAiEnabled(false);
    CHECK(!tripo::run(t, {}, "a vase", nullptr).ok);
    setAiEnabled(true);
    policy::Policy p;
    p.present = true;
    p.aiProviders = {"fal"};
    policy::setPolicyForTests(p);
    CHECK(!tripo::run(t, {}, "a vase", nullptr).ok);
    CHECK(fake.calls.empty());
    bool known = false;
    for (const auto& id : policy::aiProviderIds()) known = known || id == "tripo";
    CHECK(known);
}

TEST(tripo_hosts_are_pinned) {
    CHECK(tripo::apiUrl("https://openapi.tripo3d.ai/v3/files"));
    CHECK(tripo::apiUrl("https://openapi.tripo3d.ai/v3/tasks/abc"));
    CHECK(!tripo::apiUrl("https://openapi.tripo3d.com/v3/files")); // the China-region host: a global key is never sent there
    CHECK(!tripo::apiUrl("http://openapi.tripo3d.ai/v3/files"));
    CHECK(!tripo::apiUrl("https://openapi.tripo3d.ai:8443/v3/files"));
    CHECK(!tripo::apiUrl("https://openapi.tripo3d.ai.evil.example/v3/files"));
    CHECK(!tripo::apiUrl("https://x@openapi.tripo3d.ai/v3/files"));
    CHECK(!tripo::apiUrl("https://openapi.tripo3d.ai/v2/openapi/task"));
    CHECK(!tripo::apiUrl("https://openapi.tripo3d.ai/v3/../x"));
    CHECK(!tripo::apiUrl("https://evil.example/v3/files"));
    CHECK(tripo::downloadUrlAllowed("https://cdn.tripo3d.ai/output/model.glb"));
    CHECK(tripo::downloadUrlAllowed("https://tripo-data.rg1.data.tripo3d.com/x.glb"));
    CHECK(!tripo::downloadUrlAllowed("http://cdn.tripo3d.ai/model.glb"));
    CHECK(!tripo::downloadUrlAllowed("https://tripo3d.ai.evil.example/model.glb"));
    CHECK(!tripo::downloadUrlAllowed("https://eviltripo3d.ai/model.glb"));
    CHECK(!tripo::downloadUrlAllowed("https://127.0.0.1/model.glb"));
}

TEST(tripo_request_pieces) {
    const Json img = [] { Json j; std::string e; Json::parse(tripo::imageBody("v3.1-20260211", "file_abc"), j, e); return j; }();
    CHECK(img["input"].asString() == "file_abc" && img["model"].asString() == "v3.1-20260211");
    CHECK(img["texture"].isBool() && !img["texture"].asBool() && !img["pbr"].asBool()); // the shape only: cheaper
    CHECK(tripo::imageBody("retired-model", "f").find("v3.1-20260211") != std::string::npos);
    const std::string longText(3000, 'a');
    Json t;
    std::string e;
    CHECK(Json::parse(tripo::textBody("v3.0-20250812", longText), t, e) && t["prompt"].asString().size() == 1024 &&
          t["model"].asString() == "v3.0-20250812");
    std::string ct;
    const std::string up = tripo::uploadBody("PNGDATA", ct);
    CHECK(ct.find("multipart/form-data") != std::string::npos && up.find("name=\"file\"; filename=\"input.png\"") != std::string::npos);
    std::string token, err;
    CHECK(tripo::parseUpload(R"({"code":0,"data":{"file_token":"file_x"}})", token, err) && token == "file_x");
    CHECK(!tripo::parseUpload(R"({"code":2010,"message":"no credit"})", token, err) && err.find("Out of credits") != std::string::npos);
    tripo::TaskStatus ts;
    CHECK(tripo::parseTask(R"({"code":0,"data":{"status":"queued","progress":0}})", ts, err) && ts.state == tripo::TaskState::Queued);
    CHECK(tripo::parseTask(R"({"code":0,"data":{"status":"weird-new-state"}})", ts, err) && ts.state == tripo::TaskState::Queued); // keep waiting
    CHECK(tripo::parseTask(R"({"code":0,"data":{"status":"failed","error_message":"x"}})", ts, err) && ts.state == tripo::TaskState::Failed);
    CHECK(tripo::parseTask(R"({"code":0,"data":{"status":"success","output":{"model_url":"https://cdn.tripo3d.ai/a.glb"}}})", ts, err) &&
          ts.state == tripo::TaskState::Done && ts.modelUrl == "https://cdn.tripo3d.ai/a.glb");
    CHECK(!tripo::parseTask(R"({"code":0,"data":{"status":"success","output":{}}})", ts, err)); // finished with no model
    for (int i = 0; i < 20; ++i) CHECK(tripo::pollDelaySeconds(i) >= 2 && tripo::pollDelaySeconds(i) <= 5);
    bool transient = false;
    CHECK(tripo::errorMessage(403, {}, R"({"code":2010,"message":"x"})", transient).find("Out of credits") == 0 && !transient);
    CHECK(tripo::errorMessage(401, {}, "", transient).find("rejected the API key") != std::string::npos && !transient);
    CHECK(tripo::errorMessage(429, {}, R"({"code":1007})", transient).find("rate limiting") != std::string::npos && transient);
    CHECK(tripo::errorMessage(0, "timeout", "", transient).find("Could not reach Tripo") == 0 && transient);
    CHECK(tripo::errorMessage(503, {}, "", transient).find("HTTP 503") != std::string::npos && transient);
    CHECK(tripo::errorMessage(400, {}, R"({"code":2008})", transient).find("refused the content") != std::string::npos && !transient);
}

TEST(tripo_full_flow_image) {
    State st;
    FakeTripo fake;
    useFake(fake, st);
    const std::string png = pngFile("sf_tripo_in.png");
    AiSettings s = tripoSettings();
    GenerationProgress progress;
    const uint64_t before = httpRequestCount();
    GeneratorResult r = generate3D(s, png, (fs::temp_directory_path() / "sf_tripo_flow").string(), &progress);
    CHECK(r.ok);
    CHECK(httpRequestCount() == before); // (all through the fake)
    // upload, create, two running polls, success, the file
    CHECK(fake.calls.size() == 6);
    CHECK(fake.calls[0].method == "POST" && fake.calls[0].url == "https://openapi.tripo3d.ai/v3/files");
    CHECK(fake.calls[0].body.find("\x89PNG") != std::string::npos);
    CHECK(fake.calls[1].url == "https://openapi.tripo3d.ai/v3/generation/image-to-model");
    CHECK(fake.calls[1].body.find("file_abc") != std::string::npos && fake.calls[1].body.find("v3.1-20260211") != std::string::npos);
    CHECK(fake.calls[2].method == "GET" && fake.calls[2].url == "https://openapi.tripo3d.ai/v3/tasks/task-1");
    for (size_t i = 0; i + 1 < fake.calls.size(); ++i) CHECK(hasKeyHeader(fake.calls[i].headers, kKey));
    CHECK(fake.calls[5].url == fake.fileUrl && !hasKeyHeader(fake.calls[5].headers, kKey)); // the key never goes to the file host
    CHECK(st.waits.size() == 3 && st.waits[0] >= 2);
    CHECK(!r.privateDir.empty() && fs::path(r.outputPath).parent_path() == fs::path(r.privateDir));
    std::error_code ec;
    fs::remove_all(r.privateDir, ec);
    fs::remove(png, ec);
    const std::string log = st.audit();
    CHECK(log.find("tripo") != std::string::npos && log.find("openapi.tripo3d.ai") != std::string::npos);
    CHECK(log.find(kKey) == std::string::npos && log.find("PNG") == std::string::npos);
}

TEST(tripo_text_flow_and_cost_of_a_description) {
    State st;
    FakeTripo fake;
    useFake(fake, st);
    AiSettings s = tripoSettings();
    GeneratorResult r = generate3DFromText(s, "a stylised dragon head", (fs::temp_directory_path() / "sf_tripo_text").string());
    CHECK(r.ok);
    CHECK(fake.calls[0].url == "https://openapi.tripo3d.ai/v3/generation/text-to-model"); // no upload for text
    CHECK(fake.calls[0].body.find("a stylised dragon head") != std::string::npos);
    CHECK(st.audit().find("dragon") == std::string::npos && st.audit().find("promptChars") != std::string::npos);
    std::error_code ec;
    fs::remove_all(r.privateDir, ec);
}

TEST(tripo_failures_say_what_happened_and_never_exit) {
    State st;
    AiSettings s = tripoSettings();
    {   // no credits: reported as it is, no other provider may take over (it would not share it)
        FakeTripo fake;
        fake.createStatus = 403;
        fake.createAnswer = R"({"code":2010,"message":"no credit"})";
        useFake(fake, st);
        const uint64_t before = httpRequestCount();
        GeneratorResult r = generate3DFromText(s, "a vase", (fs::temp_directory_path() / "sf_tripo_f1").string());
        CHECK(!r.ok && r.error == "Out of credits on your Tripo account" && !r.transient);
        CHECK(httpRequestCount() == before); // nothing fell back to the network
    }
    {   // the content policy
        FakeTripo fake;
        fake.createStatus = 400;
        fake.createAnswer = R"({"code":2008,"message":"x"})";
        useFake(fake, st);
        CHECK(tripo::run(s, {}, "a vase", nullptr).error.find("refused the content") != std::string::npos);
    }
    {   // the task fails on Tripo's side
        FakeTripo fake;
        fake.finalStatus = "failed";
        useFake(fake, st);
        GeneratorResult r = tripo::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.error.find("could not make this model") != std::string::npos && r.error.find("bad input") != std::string::npos);
    }
    {   // the status endpoint keeps answering 503: retried a few times, then a clear transient failure
        FakeTripo fake;
        fake.pollStatus = 503;
        useFake(fake, st);
        GeneratorResult r = tripo::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.transient && r.error.find("HTTP 503") != std::string::npos);
        int gets = 0;
        for (const auto& c : fake.calls) gets += c.method == "GET";
        CHECK(gets == 6);
    }
    {   // a model that takes too long: the wait is capped
        FakeTripo fake;
        fake.pendingPolls = 1 << 20;
        tripo::setTimeoutSecondsForTests(-1);
        useFake(fake, st);
        GeneratorResult r = tripo::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.transient && r.error.find("gave up") != std::string::npos && r.error.find("stays in your Tripo account") != std::string::npos);
        tripo::setTimeoutSecondsForTests(0);
    }
    {   // Cancel while waiting
        FakeTripo fake;
        useFake(fake, st);
        GenerationProgress p;
        p.cancel = true;
        GeneratorResult r = tripo::run(s, {}, "a vase", &p);
        CHECK(!r.ok && r.error.find("Cancelled") == 0);
    }
    {   // the file host is not Tripo's: not fetched, the paid result is not lost
        FakeTripo fake;
        fake.fileUrl = "https://evil.example/model.glb";
        useFake(fake, st);
        GeneratorResult r = tripo::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.error.find("evil.example") != std::string::npos && r.error.find("task task-1") != std::string::npos);
        CHECK(fake.calls.back().url.find("/tasks/") != std::string::npos);
    }
    {   // a file that is not a GLB never reaches the importer
        FakeTripo fake;
        fake.file = "<html>not a model</html>";
        useFake(fake, st);
        GeneratorResult r = tripo::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.error.find("not a valid GLB") != std::string::npos);
    }
}

TEST(paid_service_falls_back_to_the_free_models_only_when_allowed_and_up) {
    State st;
    AiSettings s = tripoSettings();
    FakeTripo fake;
    fake.createStatus = 503; // Tripo is down: transient
    fake.createAnswer = "";
    useFake(fake, st);
    const std::string stem = (fs::temp_directory_path() / "sf_tripo_fb").string();
    // the artist never allowed the free Spaces: they are not used behind their back
    GeneratorResult r = generate3DFromText(s, "a vase", stem);
    CHECK(!r.ok && r.error.find("HTTP 503") != std::string::npos && r.error.find("free models were not tried") != std::string::npos);
    // allowed, but the feed says they are down too: no second hang, and the reason is said
    AiSettings free = s;
    free.generatorService = "huggingface";
    allowSend(s, generateTarget(free, true).id);
    health::set({{"hunyuan3d-2-turbo", "off", "Switched off by its author"}, {"hunyuan3d-2", "off", "Switched off by its author"}});
    const uint64_t before = httpRequestCount();
    r = generate3DFromText(s, "a vase", stem);
    CHECK(!r.ok && r.error.find("The free models are switched off") != std::string::npos);
    CHECK(httpRequestCount() == before);
    // a refusal is not transient: never retried elsewhere
    fake.createStatus = 403;
    fake.createAnswer = R"({"code":2010})";
    r = generate3DFromText(s, "a vase", stem);
    CHECK(!r.ok && r.error == "Out of credits on your Tripo account");
}

TEST(space_health_comes_from_the_feed_and_moves_a_run_to_a_space_that_is_up) {
    State st;
    UpdateInfo info;
    std::string err;
    const std::string feed = R"({"version":"0.12.14","download":"https://github.com/x/y/releases",
        "ai3d":{"spaces":{"hunyuan3d-2.1":{"state":"off","note":"Switched off by its author"},
                          "trellis-2":{"state":"slow","note":"Wakes from sleep"},
                          "bad id!":{"state":"off"}, "x":{"state":"weird"}, "stable-fast-3d":{"state":"up"}}}})";
    CHECK(parseUpdateFeed(feed, info, err));
    CHECK(info.spaceHealth.size() == 3); // the odd id and the odd state are dropped
    health::set(info.spaceHealth);
    CHECK(health::of("hunyuan3d-2.1").state == health::State::Off);
    CHECK(health::of("trellis-2").state == health::State::Slow);
    CHECK(health::of("never-heard-of").state == health::State::Up); // unknown = up
    CHECK(health::reason(health::of("hunyuan3d-2.1")) == "switched off: Switched off by its author");
    // the default model is off: the first Space that is up runs instead, said in the note
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    s.hfModel = "hunyuan3d-2.1";
    std::string note;
    const HfPreset& p = resolveHfPreset(s, false, &note);
    CHECK(std::string(p.id) == "hunyuan3d-2-turbo" && note.find("runs instead") != std::string::npos);
    // with every Space off the chosen one stays (the panel disables Generate with the reason)
    health::set({{"hunyuan3d-2.1", "off", ""}, {"trellis-2", "off", ""}, {"hunyuan3d-2-turbo", "off", ""}, {"hunyuan3d-2", "off", ""},
                 {"stable-fast-3d", "off", ""}});
    CHECK(std::string(resolveHfPreset(s, false).id) == "hunyuan3d-2.1");
    // no feed block: nothing known, everything up
    CHECK(parseUpdateFeed(R"({"version":"0.12.14","download":"https://github.com/x/y/releases"})", info, err) && info.spaceHealth.empty());
    health::set(info.spaceHealth);
    CHECK(std::string(resolveHfPreset(s, false).id) == "hunyuan3d-2.1");
}

TEST(recommended_list_has_prices_notes_and_no_referral_tags) {
    const auto& list = recommendedModels();
    CHECK(list.size() >= 5);
    bool tripoFirst = std::string(list[0].service) == "tripo";
    for (const RecommendedModel& m : list) {
        CHECK(*m.label && *m.price && *m.note);
        const std::string url = m.keyUrl;
        CHECK(url.empty() || url.rfind("https://", 0) == 0);
        CHECK(url.find('?') == std::string::npos && url.find("ref") == std::string::npos); // the provider's own page: no affiliate tags
    }
    CHECK(tripoFirst);
}
