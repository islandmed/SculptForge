// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hyper3D Rodin direct (ai/Hyper3D.h): off without the user's own key (keychain only, no environment variable), one
// pinned API host, the key sent nowhere else, polling backs off, the file is capped and checked before the importer
// sees it and lives in a private folder, the studio policy and the AI switch stop it before anything is sent, one
// audit line per generation with neither the key nor the image. No test talks to Hyper3D: the transport is faked.
#include "TestHarness.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/Hyper3D.h"
#include "ai/ImageTo3DService.h"
#include "ai/Secrets.h"
#include "core/AuditLog.h"
#include "core/Commands.h"
#include "core/Image.h"
#include "core/Keychain.h"
#include "core/ManagedPolicy.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;
using namespace sf::ai;
namespace fs = std::filesystem;

namespace {

constexpr const char* kKey = "h3d-test-key-123";

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

std::string readFile(const std::string& path) {
    std::ifstream f(fs::path(path), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A minimal well-formed GLB around JSON (padded to 4 bytes) and an optional BIN chunk.
std::string glb(std::string json, const std::string& bin = {}) {
    while (json.size() % 4) json += ' ';
    auto u32 = [](std::string& s, uint32_t v) {
        for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xff);
    };
    std::string out = "glTF";
    u32(out, 2);
    const size_t total = 12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size());
    u32(out, uint32_t(total));
    u32(out, uint32_t(json.size()));
    u32(out, 0x4E4F534Au);
    out += json;
    if (!bin.empty()) {
        u32(out, uint32_t(bin.size()));
        u32(out, 0x004E4942u);
        out += bin;
    }
    return out;
}

// Everything a test touches, back as it was.
struct State {
    std::string auditDir;
    std::vector<int> waits;
    State() {
        auditDir = (fs::temp_directory_path() / "sf_hyper3d_audit").string();
        std::error_code ec;
        fs::remove_all(auditDir, ec);
        fs::create_directories(auditDir, ec);
        policy::resetPolicyForTests();
        audit::setPathForTests(auditDir + "/audit.jsonl");
        keychain::useMemoryForTests(true);
        secrets::clearCacheForTests();
        setAiEnabled(true);
    }
    ~State() {
        hyper3d::setTransportForTests({});
        hyper3d::setMaxDownloadBytesForTests(0);
        policy::resetPolicyForTests();
        audit::setPathForTests({});
        secrets::put(secrets::kHyper3d, "");
        secrets::clearCacheForTests();
        setAiEnabled(true);
        setEnv("HYPER3D_API_KEY", nullptr);
    }
    std::string audit() const { return readFile(auditDir + "/audit.jsonl"); }
};

AiSettings hyper3dSettings() {
    AiSettings s = AiSettings::defaults(AiSettings::Provider::Free);
    s.generatorService = "hyper3d";
    return s;
}

// A Hyper3D that answers like the documented API; records what it was sent.
struct FakeHyper3d {
    struct Call {
        std::string method, url, body;
        std::vector<std::string> headers;
    };
    std::vector<Call> calls;
    int pendingPolls = 2;
    std::string fileUrl = "https://file.hyper3d.com/task/model.glb";
    std::string file;
    long fileStatus = 200;

    HttpResponse operator()(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body) {
        calls.push_back({method, url, body, headers});
        HttpResponse r;
        r.status = 201;
        if (method == "GET") {
            r.status = fileStatus;
            r.body = file;
        } else if (url.ends_with("/rodin")) {
            r.body = R"({"message":"Submitted.","uuid":"task-1","jobs":{"uuids":["job-1"],"subscription_key":"sub-1"},"consumed":0.5})";
        } else if (url.ends_with("/status")) {
            r.body = pendingPolls-- > 0 ? R"({"jobs":[{"uuid":"job-1","status":"Generating"}]})"
                                        : R"({"jobs":[{"uuid":"job-1","status":"Done"}]})";
        } else if (url.ends_with("/download")) {
            r.body = R"({"list":[{"name":"preview.webp","url":"https://file.hyper3d.com/p.webp"},{"name":"base_basic_pbr.glb","url":")" +
                     fileUrl + R"("}]})";
        }
        return r;
    }
};

void useFake(FakeHyper3d& fake, State& st) {
    hyper3d::setTransportForTests([&fake](const std::string& m, const std::string& u, const std::vector<std::string>& h,
                                          const std::string& b) { return fake(m, u, h, b); },
                                  &st.waits);
}

bool hasKeyHeader(const std::vector<std::string>& headers) {
    for (const auto& h : headers)
        if (h.find(kKey) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(hyper3d_key_only_from_user_keychain_never_env_or_file) {
    State st;
    setEnv("HYPER3D_API_KEY", "from-env");
    AiSettings s = hyper3dSettings();
    CHECK(s.resolvedHyper3dKey().empty()); // no environment variable, no built-in or trial key
    s.hyper3dKey = kKey;
    CHECK(s.resolvedHyper3dKey() == kKey);
    // ai.json never holds it, even when an old or hand-edited file has one
    CHECK(s.toJson().dump().find(kKey) == std::string::npos);
    Json j = s.toJson();
    j.set("hyper3dKey", "planted");
    CHECK(AiSettings::fromJson(j).resolvedHyper3dKey().empty());
    // saved: the keychain account "ai/hyper3d"
    CHECK(secrets::storeKeys(s));
    std::string v;
    CHECK(keychain::load(secrets::kHyper3d, v) && v == kKey);
    secrets::clearCacheForTests();
    CHECK(hyper3dSettings().resolvedHyper3dKey() == kKey);
    bool listed = false;
    for (const auto& a : secrets::allAccounts()) listed = listed || a == secrets::kHyper3d;
    CHECK(listed);
}

TEST(hyper3d_without_key_sends_nothing) {
    State st;
    FakeHyper3d fake;
    useFake(fake, st);
    const uint64_t before = httpRequestCount();
    GeneratorResult r = generate3DFromText(hyper3dSettings(), "a dragon head", tempPath("sf_h3d_nokey"));
    CHECK(!r.ok && r.error.find("Hyper3D API key") != std::string::npos);
    CHECK(fake.calls.empty() && httpRequestCount() == before);
}

TEST(hyper3d_ai_switch_off_sends_nothing) {
    State st;
    FakeHyper3d fake;
    useFake(fake, st);
    AiSettings s = hyper3dSettings();
    s.hyper3dKey = kKey;
    setAiEnabled(false);
    const uint64_t before = httpRequestCount();
    CHECK(!generate3DFromText(s, "a dragon head", tempPath("sf_h3d_off")).ok);
    CHECK(!hyper3d::run(s, {}, "a dragon head", nullptr).ok); // the module's own gate too
    CHECK(fake.calls.empty() && httpRequestCount() == before);
}

TEST(hyper3d_studio_policy_controls_it) {
    State st;
    FakeHyper3d fake;
    useFake(fake, st);
    AiSettings s = hyper3dSettings();
    s.hyper3dKey = kKey;
    bool known = false;
    for (const auto& id : policy::aiProviderIds()) known = known || id == "hyper3d";
    CHECK(known); // a provider id the policy's allow-list names
    policy::Policy p;
    p.present = true;
    p.aiProviders = {"fal"};
    policy::setPolicyForTests(p);
    GeneratorResult r = generate3DFromText(s, "a dragon head", tempPath("sf_h3d_policy"));
    CHECK(!r.ok && fake.calls.empty());
    CHECK(!hyper3d::run(s, {}, "a dragon head", nullptr).ok && fake.calls.empty());
    // the endpoint allow-list applies too (the host is pinned, the studio can still refuse it)
    p.aiProviders = {"hyper3d"};
    p.aiEndpoints = {"https://llm.studio.internal/"};
    policy::setPolicyForTests(p);
    CHECK(!generate3DFromText(s, "a dragon head", tempPath("sf_h3d_policy")).ok && fake.calls.empty());
    // audit.required with a log that can't be written: refused before anything is sent
    p.aiEndpoints.clear();
    p.auditRequired = true;
    policy::setPolicyForTests(p);
    { std::ofstream(st.auditDir + "/file") << "x"; }
    audit::setPathForTests(st.auditDir + "/file/audit.jsonl");
    CHECK(!hyper3d::run(s, {}, "a dragon head", nullptr).ok && fake.calls.empty());
}

TEST(hyper3d_api_host_is_pinned) {
    CHECK(hyper3d::apiUrl("https://api.hyper3d.com/api/v2/rodin"));
    CHECK(hyper3d::apiUrl("https://api.hyper3d.com/api/v2/status"));
    CHECK(!hyper3d::apiUrl("http://api.hyper3d.com/api/v2/rodin"));
    CHECK(!hyper3d::apiUrl("https://api.hyper3d.com:8443/api/v2/rodin"));
    CHECK(!hyper3d::apiUrl("https://api.hyper3d.com.evil.example/api/v2/rodin"));
    CHECK(!hyper3d::apiUrl("https://user@api.hyper3d.com/api/v2/rodin"));
    CHECK(!hyper3d::apiUrl("https://evil.example/api/v2/rodin"));
    CHECK(!hyper3d::apiUrl("https://api.hyper3d.com/api/v2/../../x"));
    // the file: https on Hyper3D's own domains only
    CHECK(hyper3d::downloadUrlAllowed("https://file.hyper3d.com/a/model.glb"));
    CHECK(hyper3d::downloadUrlAllowed("https://cdn.hyper3d.ai/model.glb"));
    CHECK(hyper3d::downloadUrlAllowed("https://files.deemos.com/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("http://file.hyper3d.com/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("https://hyper3d.com.evil.example/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("https://evilhyper3d.com/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("https://x@file.hyper3d.com/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("https://127.0.0.1/model.glb"));
    CHECK(!hyper3d::downloadUrlAllowed("file:///etc/passwd"));
}

TEST(hyper3d_request_pieces) {
    std::string ct;
    const std::string body = hyper3d::submitBody("Gen-2.5-High", "PNGDATA", {}, ct);
    CHECK(ct.rfind("Content-Type: multipart/form-data; boundary=", 0) == 0);
    CHECK(body.find("name=\"images\"; filename=\"input.png\"") != std::string::npos && body.find("PNGDATA") != std::string::npos);
    CHECK(body.find("Gen-2.5-High") != std::string::npos && body.find("name=\"prompt\"") == std::string::npos);
    CHECK(body.find("geometry_file_format\"\r\n\r\nglb") != std::string::npos);
    const std::string text = hyper3d::submitBody("bogus", {}, "a vase", ct);
    CHECK(text.find("name=\"prompt\"\r\n\r\na vase") != std::string::npos && text.find("name=\"images\"") == std::string::npos);
    CHECK(text.find(hyper3d::kDefaultTier) != std::string::npos); // an unknown tier runs the default
    CHECK(hyper3d::tiers().size() == 5 && hyper3d::resolvedTier("Gen-2.5-Extreme-High") == "Gen-2.5-Extreme-High");

    std::string uuid, sub, err;
    CHECK(hyper3d::parseSubmit(R"({"uuid":"u","jobs":{"uuids":["j"],"subscription_key":"k"}})", uuid, sub, err) && uuid == "u" && sub == "k");
    CHECK(!hyper3d::parseSubmit(R"({"error":"API_INSUFFICIENT_FUNDS","message":"no"})", uuid, sub, err) &&
          err.find("credits") != std::string::npos);
    CHECK(!hyper3d::parseSubmit("<html>", uuid, sub, err));
    hyper3d::JobState state;
    int queue = 0;
    CHECK(hyper3d::parseStatus(R"({"jobs":[{"status":"Done"},{"status":"Generating"}]})", state, queue, err) &&
          state == hyper3d::JobState::Generating);
    CHECK(hyper3d::parseStatus(R"({"jobs":[{"status":"Done"},{"status":"Failed"}]})", state, queue, err) &&
          state == hyper3d::JobState::Failed);
    CHECK(hyper3d::parseStatus(R"({"jobs":[{"status":"Waiting","queue_length":3}]})", state, queue, err) &&
          state == hyper3d::JobState::Waiting && queue == 3);
    CHECK(!hyper3d::parseStatus(R"({"jobs":[]})", state, queue, err));
    std::string url;
    CHECK(hyper3d::parseDownload(R"({"list":[{"name":"a.webp","url":"x"},{"name":"B.GLB","url":"https://f/b.glb"}]})", url, err) &&
          url == "https://f/b.glb");
    CHECK(!hyper3d::parseDownload(R"({"list":[{"name":"a.fbx","url":"x"}]})", url, err));
    // back-off: at least 5 s first, growing, capped at 30 s
    CHECK(hyper3d::pollDelaySeconds(0) == 5);
    for (int i = 1; i < 100; ++i) CHECK(hyper3d::pollDelaySeconds(i) >= hyper3d::pollDelaySeconds(i - 1) && hyper3d::pollDelaySeconds(i) <= 30);
    CHECK(hyper3d::pollDelaySeconds(99) == 30);
}

TEST(hyper3d_glb_checked_before_parse) {
    std::string err;
    CHECK(hyper3d::checkGlb(glb(R"({"asset":{"version":"2.0"}})"), err));
    CHECK(hyper3d::checkGlb(glb(R"({"asset":{"version":"2.0"}})", std::string(8, '\0')), err));
    CHECK(hyper3d::checkGlb(readFile(SF_TEST_DATA_DIR "/marble-bust.glb"), err)); // a real file
    std::string g = glb(R"({"asset":{"version":"2.0"}})");
    CHECK(!hyper3d::checkGlb("", err));
    CHECK(!hyper3d::checkGlb("<html>not a model</html> padding padding", err));
    std::string bad = g;
    bad[4] = 1; // version 1
    CHECK(!hyper3d::checkGlb(bad, err));
    CHECK(!hyper3d::checkGlb(g + "xxxx", err));              // length field disagrees
    CHECK(!hyper3d::checkGlb(g.substr(0, g.size() - 4), err)); // truncated
    CHECK(!hyper3d::checkGlb(glb(R"([1,2,3])"), err));         // JSON chunk without an asset
    CHECK(!hyper3d::checkGlb(glb(R"({"asset":)"), err));       // broken JSON
    std::string lying = g;
    lying[12] = char(0xff), lying[13] = char(0xff); // chunk longer than the file
    CHECK(!hyper3d::checkGlb(lying, err));
}

TEST(hyper3d_private_dir) {
    std::string err;
    const std::string dir = hyper3d::makePrivateDir(err);
    CHECK(!dir.empty() && fs::is_directory(dir));
#if !defined(_WIN32)
    const auto perms = fs::status(dir).permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    const std::string other = hyper3d::makePrivateDir(err);
    CHECK(other != dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove_all(other, ec);
}

TEST(hyper3d_full_flow_with_fake_service) {
    State st;
    FakeHyper3d fake;
    fake.file = readFile(SF_TEST_DATA_DIR "/marble-bust.glb");
    useFake(fake, st);
    AiSettings s = hyper3dSettings();
    s.hyper3dKey = kKey;
    s.hyper3dTier = "Gen-2.5-Medium";
    GenerationProgress progress;
    const uint64_t before = httpRequestCount();
    GeneratorResult r = generate3DFromText(s, "a stylised dragon head", tempPath("sf_h3d_flow"), &progress);
    CHECK(r.ok);
    CHECK(httpRequestCount() == before); // (all through the fake)
    // submit, two pending polls, done, download list, file
    CHECK(fake.calls.size() == 6);
    for (size_t i = 0; i + 1 < fake.calls.size(); ++i) {
        CHECK(fake.calls[i].method == "POST" && hyper3d::apiUrl(fake.calls[i].url));
        CHECK(hasKeyHeader(fake.calls[i].headers));
    }
    CHECK(fake.calls[0].body.find("Gen-2.5-Medium") != std::string::npos);
    CHECK(fake.calls[1].body.find("sub-1") != std::string::npos);  // status: the subscription key
    CHECK(fake.calls[4].body.find("task-1") != std::string::npos);  // download: the task uuid
    CHECK(fake.calls[5].method == "GET" && fake.calls[5].url == fake.fileUrl);
    CHECK(!hasKeyHeader(fake.calls[5].headers)); // the key never goes to the file host
    CHECK(st.waits.size() == 3 && st.waits[0] >= 5);
    // the file sits in a private folder, readable by the existing glTF importer
    CHECK(!r.privateDir.empty() && fs::path(r.outputPath).parent_path() == fs::path(r.privateDir));
    Document doc;
    CHECK(CommandRegistry().execute(doc, "open", {{"path", r.outputPath}}).ok && doc.mesh.vertexCount() > 0);
    std::error_code ec;
    fs::remove_all(r.privateDir, ec);
    // one audit line: service, tier, endpoint; never the key or the words
    const std::string log = st.audit();
    CHECK(log.find("hyper3d") != std::string::npos && log.find("api.hyper3d.com") != std::string::npos);
    CHECK(log.find("Gen-2.5-Medium") != std::string::npos);
    CHECK(log.find(kKey) == std::string::npos && log.find("dragon") == std::string::npos);
}

TEST(hyper3d_image_is_sent_as_png_and_never_logged) {
    State st;
    FakeHyper3d fake;
    fake.file = glb(R"({"asset":{"version":"2.0"}})");
    useFake(fake, st);
    Image img;
    img.width = img.height = 32;
    img.rgba.assign(32 * 32 * 4, 255);
    const std::string path = tempPath("sf_h3d_input.png");
    std::string err;
    CHECK(img.savePng(path, err));
    AiSettings s = hyper3dSettings();
    s.hyper3dKey = kKey;
    GeneratorResult r = generate3D(s, path, tempPath("sf_h3d_img"));
    CHECK(r.ok && !fake.calls.empty());
    CHECK(fake.calls[0].body.find("name=\"images\"; filename=\"input.png\"") != std::string::npos);
    CHECK(fake.calls[0].body.find("\x89PNG") != std::string::npos);
    CHECK(st.audit().find("image_to_3d") != std::string::npos && st.audit().find("PNG") == std::string::npos);
    std::error_code ec;
    fs::remove_all(r.privateDir, ec);
    fs::remove(path, ec);
}

TEST(hyper3d_bad_downloads_refused) {
    State st;
    AiSettings s = hyper3dSettings();
    s.hyper3dKey = kKey;
    auto attempt = [&](FakeHyper3d& fake) {
        fake.calls.clear();
        useFake(fake, st);
        return hyper3d::run(s, {}, "a vase", nullptr);
    };
    {
        FakeHyper3d fake; // a file host outside Hyper3D's domains: not fetched at all
        fake.fileUrl = "https://evil.example/model.glb";
        fake.file = glb(R"({"asset":{"version":"2.0"}})");
        GeneratorResult r = attempt(fake);
        CHECK(!r.ok && r.error.find("evil.example") != std::string::npos && fake.calls.back().method == "POST");
        // the paid result isn't lost: the task id and where to find it
        CHECK(r.error.find("task task-1") != std::string::npos && r.error.find("hyper3d.ai dashboard") != std::string::npos);
        CHECK(r.error.find(kKey) == std::string::npos);
    }
    {
        FakeHyper3d fake; // plain http
        fake.fileUrl = "http://file.hyper3d.com/model.glb";
        GeneratorResult r = attempt(fake);
        CHECK(!r.ok && fake.calls.back().method == "POST" && r.error.find("task task-1") != std::string::npos);
    }
    {
        FakeHyper3d fake; // a redirect off Hyper3D's servers (as httpGetRestricted reports it)
        hyper3d::setTransportForTests([&fake](const std::string& m, const std::string& u, const std::vector<std::string>& h,
                                              const std::string& b) {
            HttpResponse r = fake(m, u, h, b);
            if (m == "GET") r = {0, {}, "refused to download from https://evil.example/x.glb"};
            return r;
        });
        GeneratorResult r = hyper3d::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.error.find("evil.example") != std::string::npos && r.error.find("task task-1") != std::string::npos);
    }
    {
        FakeHyper3d fake; // not a GLB
        fake.file = "<html>hello</html>";
        GeneratorResult r = attempt(fake);
        CHECK(!r.ok && r.error.find("not a valid GLB") != std::string::npos && r.privateDir.empty());
    }
    {
        FakeHyper3d fake; // over the size cap (200 MB; 1 KB here)
        CHECK(hyper3d::kMaxDownloadBytes == size_t(200) << 20);
        hyper3d::setMaxDownloadBytesForTests(1024);
        fake.file = glb(R"({"asset":{"version":"2.0"}})", std::string(2048, '\0'));
        GeneratorResult r = attempt(fake);
        hyper3d::setMaxDownloadBytesForTests(0);
        CHECK(!r.ok && r.error.find("200 MB") != std::string::npos);
    }
    {
        FakeHyper3d fake; // a failed job
        fake.pendingPolls = 0;
        hyper3d::setTransportForTests([&fake](const std::string& m, const std::string& u, const std::vector<std::string>& h,
                                              const std::string& b) {
            HttpResponse r = fake(m, u, h, b);
            if (u.ends_with("/status")) r.body = R"({"jobs":[{"uuid":"job-1","status":"Failed"}]})";
            return r;
        });
        CHECK(!hyper3d::run(s, {}, "a vase", nullptr).ok && fake.calls.size() == 2);
    }
    {
        FakeHyper3d fake; // a rejected key: stops at the submit
        hyper3d::setTransportForTests([&fake](const std::string& m, const std::string& u, const std::vector<std::string>& h,
                                              const std::string& b) {
            fake.calls.push_back({m, u, b, h});
            HttpResponse r;
            r.status = 401;
            return r;
        });
        GeneratorResult r = hyper3d::run(s, {}, "a vase", nullptr);
        CHECK(!r.ok && r.error.find("rejected") != std::string::npos && fake.calls.size() == 1);
        CHECK(r.error.find(kKey) == std::string::npos);
    }
    CHECK(st.audit().find(kKey) == std::string::npos);
}
