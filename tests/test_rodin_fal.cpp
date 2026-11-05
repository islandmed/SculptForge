// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hyper3D Rodin 2.5 through fal.ai (#123): request bodies, the fal queue flow against a local fake fal.ai with
// Rodin's documented replies, and nothing sent while AI is off. No real service is contacted.
#include "TestHarness.h"

#include "ai/AiPolicy.h"
#include "ai/Http.h"
#include "ai/ImageTo3DService.h"
#include "link/HttpServer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>

using namespace sf;

namespace {

Json parsed(const std::string& text) {
    Json j;
    std::string err;
    CHECK(Json::parse(text, j, err));
    return j;
}

} // namespace

TEST(rodin_fal_request_bodies) {
    CHECK(ai::rodinModel("fal-ai/hyper3d/rodin/v2.5") && ai::rodinModel("fal-ai/hyper3d/rodin/v2.5/fast"));
    CHECK(!ai::rodinModel("fal-ai/trellis-2") && !ai::hunyuanModel("fal-ai/hyper3d/rodin/v2.5"));
    bool listed = false;
    for (const auto& m : ai::falModels()) listed = listed || std::string(m.id) == "fal-ai/hyper3d/rodin/v2.5";
    CHECK(listed);

    // Image: a list of images (the prepared data URI), GLB, no material: the shape is what we sculpt.
    Json body = parsed(ai::falSubmitBody("fal-ai/hyper3d/rodin/v2.5", "data:image/png;base64,AAAA"));
    CHECK(body["image_urls"].isArray() && body["image_urls"][0].asString() == "data:image/png;base64,AAAA");
    CHECK(!body["image_url"].isString());
    CHECK(body["geometry_file_format"].asString() == "glb");
    CHECK(body["material"].asString() == "None");

    // Text: Rodin takes the prompt on the same endpoint; other fal models keep Hunyuan3D 3.1 Rapid's text endpoint.
    CHECK(ai::falTextModel("fal-ai/hyper3d/rodin/v2.5/fast") == "fal-ai/hyper3d/rodin/v2.5/fast");
    CHECK(ai::falTextModel("fal-ai/trellis-2") == "fal-ai/hunyuan-3d/v3.1/rapid/text-to-3d");
    body = parsed(ai::falTextBody(std::string(300, 'x'), "fal-ai/hyper3d/rodin/v2.5"));
    CHECK(body["prompt"].asString().size() == 300 && !body["image_urls"].isArray());
    CHECK(body["geometry_file_format"].asString() == "glb" && body["material"].asString() == "None");
    CHECK(!body["enable_geometry"].isBool()); // Hunyuan's field, not Rodin's

    // Rodin's reply: model_mesh is the GLB; the textures (PNG) are not the model.
    std::string ext;
    Json reply = parsed(R"({"model_mesh":{"url":"https://v3.fal.media/files/x/base_basic_shaded.glb",
        "content_type":"model/gltf-binary","file_name":"base_basic_shaded.glb","file_size":1234},
        "seed":42,"textures":[{"url":"https://v3.fal.media/files/x/texture.png","content_type":"image/png"}]})");
    CHECK(ai::findModelUrl(reply, &ext) == "https://v3.fal.media/files/x/base_basic_shaded.glb" && ext == ".glb");
}

TEST(rodin_fal_text_to_3d_mocked) {
    if (!ai::httpSupported()) return; // a build without libcurl
    // A fake fal.ai queue: submit, one status poll, the result, then the file download, all on 127.0.0.1.
    link::HttpServer server;
    link::HttpServer::Options o;
    o.bindAddress = "127.0.0.1";
    std::mutex mutex;
    std::string submittedPath, submittedBody, authorization;
    std::string base;
    const std::string glb = "glTF fake mesh bytes";
    std::string err;
    CHECK(server.start(o, [&](const link::HttpRequest& r) {
        std::lock_guard lock(mutex);
        if (r.method == "POST") {
            submittedPath = r.path;
            submittedBody = r.body;
            authorization = r.header("authorization");
            return link::HttpResponse::json(200, R"({"request_id":"req1","status_url":")" + base +
                                                     R"(/fal-ai/hyper3d/requests/req1/status","response_url":")" + base +
                                                     R"(/fal-ai/hyper3d/requests/req1"})");
        }
        if (r.path.ends_with("/status")) return link::HttpResponse::json(200, R"({"status":"COMPLETED"})");
        if (r.path == "/fal-ai/hyper3d/requests/req1")
            return link::HttpResponse::json(200, R"({"model_mesh":{"url":")" + base + R"(/files/mesh.glb"},"seed":7,)"
                                                 R"("textures":[{"url":")" + base + R"(/files/texture.png"}]})");
        if (r.path == "/files/mesh.glb") return link::HttpResponse::bytes("model/gltf-binary", glb);
        return link::HttpResponse::text(404, "not found");
    }, err));
    base = "http://127.0.0.1:" + std::to_string(server.port());

    ai::AiSettings s = ai::AiSettings::defaults(ai::AiSettings::Provider::Free);
    s.generatorService = "fal";
    s.falModel = "fal-ai/hyper3d/rodin/v2.5";
    s.falKey = "test-key";
    s.falEndpoint = base + "/";
    const std::string stem = tempPath("sf_rodin_text");
    ai::GeneratorResult r = ai::generate3DFromText(s, "a stylised dragon head", stem);
    server.stop();
    if (!r.ok) std::fprintf(stderr, "  rodin mock: %s\n", r.error.c_str());
    CHECK(r.ok);
    CHECK(r.outputPath == stem + ".glb");
    std::ifstream f(r.outputPath, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(f), {}) == glb);
    f.close();
    std::filesystem::remove(r.outputPath);
    CHECK(submittedPath == "/fal-ai/hyper3d/rodin/v2.5");
    CHECK(authorization == "Key test-key");
    Json body = parsed(submittedBody);
    CHECK(body["prompt"].asString() == "a stylised dragon head" && body["material"].asString() == "None");
}

TEST(rodin_fal_ai_off_sends_nothing) {
    const bool was = ai::aiEnabled();
    ai::setAiEnabled(false);
    const uint64_t before = ai::httpRequestCount();
    ai::AiSettings s = ai::AiSettings::defaults(ai::AiSettings::Provider::Free);
    s.generatorService = "fal";
    s.falKey = "test-key";
    for (const char* model : {"fal-ai/hyper3d/rodin/v2.5", "fal-ai/hyper3d/rodin/v2.5/fast"}) {
        s.falModel = model;
        CHECK(ai::generate3D(s, "missing.png", tempPath("sf_rodin_off")).error.find("AI features") != std::string::npos);
        CHECK(ai::generate3DFromText(s, "a fox", tempPath("sf_rodin_off")).error.find("AI features") != std::string::npos);
    }
    CHECK(ai::httpRequestCount() == before);
    ai::setAiEnabled(was);
}
