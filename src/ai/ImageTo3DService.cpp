// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ImageTo3DService.h"
#include "ai/AiPolicy.h"

#include "ai/ComfyUI.h"
#include "ai/GradioClient.h"
#include "ai/Http.h"
#include "core/Image.h"
#include "core/ImageTo3D.h"
#include "io/GltfIO.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace sf::ai {

namespace {

constexpr int kPollSeconds = 3;
constexpr int kMaxMinutes = 20;

std::string apiError(const HttpResponse& r) {
    Json j;
    std::string err;
    std::string detail;
    if (Json::parse(r.body, j, err)) {
        if (j["detail"].isString()) detail = j["detail"].asString();
        else if (j["detail"].isArray() && j["detail"][0]["msg"].isString()) detail = j["detail"][0]["msg"].asString();
        else if (j["message"].isString()) detail = j["message"].asString();
        else if (j["error"].isString()) detail = j["error"].asString();
        else if (j["error"]["message"].isString()) detail = j["error"]["message"].asString();
    }
    if (detail.empty()) detail = r.body.substr(0, 300);
    switch (r.status) {
    case 401:
    case 403: return "The API key was rejected (" + detail + ")";
    case 402: return "Out of credits on this account (" + detail + ")";
    case 404: return "Model or endpoint not found - check the model id (" + detail + ")";
    case 429: return "Rate limited - wait a moment and try again (" + detail + ")";
    default: return "HTTP " + std::to_string(r.status) + ": " + detail;
    }
}

// Sleeps for `seconds`, returning false early if cancelled.
bool wait(GenerationProgress* p, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) {
        if (p && p->cancel) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !(p && p->cancel);
}

void status(GenerationProgress* p, const std::string& text, int percent = -1) {
    if (!p) return;
    p->setStatus(text);
    p->percent = percent;
}

bool download(const std::string& url, const std::string& outputStem, const std::string& extension,
              GeneratorResult& result, GenerationProgress* p) {
    status(p, "Downloading model...");
    HttpResponse r = httpRequest("GET", url, {}, {}, 300);
    if (!r.error.empty() || r.status < 200 || r.status >= 300 || r.body.empty()) {
        result.error = "Download failed: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    const std::string path = outputStem + extension;
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f || !f.write(r.body.data(), std::streamsize(r.body.size()))) {
        result.error = "Cannot write " + path;
        return false;
    }
    result.ok = true;
    result.outputPath = path;
    return true;
}

GeneratorResult runFalRequest(const AiSettings& s, const std::string& model, const std::string& body,
                              const std::string& outputStem, GenerationProgress* p);

GeneratorResult runFal(const AiSettings& s, const std::string& imagePath, const std::string& outputStem,
                       GenerationProgress* p) {
    GeneratorResult result;
    if (s.resolvedFalKey().empty()) {
        result.error = "Add your fal.ai API key (fal.ai/dashboard/keys) or set FAL_KEY";
        return result;
    }
    std::string uri;
    if (!prepareImageDataUri(imagePath, uri, result.error)) return result;
    return runFalRequest(s, s.falModel, falSubmitBody(s.falModel, uri), outputStem, p);
}

GeneratorResult runFalRequest(const AiSettings& s, const std::string& model, const std::string& body,
                              const std::string& outputStem, GenerationProgress* p) {
    GeneratorResult result;
    const std::string key = s.resolvedFalKey();
    if (key.empty()) {
        result.error = "Add your fal.ai API key (fal.ai/dashboard/keys) or set FAL_KEY";
        return result;
    }
    const std::vector<std::string> headers = {"Authorization: Key " + key, "Content-Type: application/json"};
    std::string endpoint = s.falEndpoint;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();

    status(p, "Sending to " + model + "...");
    HttpResponse r = httpPostJson(endpoint + "/" + model, headers, body, 120);
    if (!r.error.empty()) {
        result.error = "Could not reach fal.ai: " + r.error;
        return result;
    }
    if (r.status < 200 || r.status >= 300) {
        result.error = apiError(r);
        return result;
    }
    Json submitted;
    std::string err;
    if (!Json::parse(r.body, submitted, err) || !submitted["request_id"].isString()) {
        result.error = "Unexpected reply from fal.ai: " + r.body.substr(0, 200);
        return result;
    }
    // Status/result URLs use only the owner/app part of the model id; prefer the ones returned.
    const std::string requestId = submitted["request_id"].asString();
    const std::string app = model.substr(0, model.find('/', model.find('/') + 1));
    const std::string statusUrl = submitted["status_url"].isString() ? submitted["status_url"].asString()
                                                                       : endpoint + "/" + app + "/requests/" + requestId + "/status";
    const std::string resultUrl = submitted["response_url"].isString() ? submitted["response_url"].asString()
                                                                         : endpoint + "/" + app + "/requests/" + requestId;

    const auto start = std::chrono::steady_clock::now();
    while (true) {
        if (!wait(p, kPollSeconds)) {
            result.error = "Cancelled";
            return result;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::minutes(kMaxMinutes)) {
            result.error = "Timed out waiting for fal.ai";
            return result;
        }
        HttpResponse st = httpRequest("GET", statusUrl, headers, {}, 60);
        if (!st.error.empty()) continue; // transient network trouble: keep polling
        if (st.status < 200 || st.status >= 300) {
            result.error = apiError(st);
            return result;
        }
        Json js;
        if (!Json::parse(st.body, js, err)) continue;
        const std::string state = js["status"].asString();
        const int secs = int(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
        if (state == "IN_QUEUE")
            status(p, "Waiting in queue" + (js["queue_position"].isNumber()
                                                  ? " (position " + std::to_string(int(js["queue_position"].asNumber())) + ")"
                                                  : std::string()) + "... " + std::to_string(secs) + "s");
        else if (state == "IN_PROGRESS") status(p, "Reconstructing 3D model... " + std::to_string(secs) + "s");
        else if (state == "COMPLETED") break;
    }

    HttpResponse res = httpRequest("GET", resultUrl, headers, {}, 120);
    if (!res.error.empty() || res.status < 200 || res.status >= 300) {
        result.error = res.error.empty() ? apiError(res) : "Could not fetch the result: " + res.error;
        return result;
    }
    Json out;
    if (!Json::parse(res.body, out, err)) {
        result.error = "Unreadable result from fal.ai";
        return result;
    }
    std::string extension;
    const std::string url = findModelUrl(out, &extension);
    if (url.empty()) {
        result.error = "The result contained no model file: " + res.body.substr(0, 200);
        return result;
    }
    download(url, outputStem, extension, result, p);
    return result;
}

GeneratorResult runMeshyRequest(const AiSettings& s, const std::string& taskPath, const std::string& body,
                                const std::string& outputStem, GenerationProgress* p);

GeneratorResult runMeshy(const AiSettings& s, const std::string& imagePath, const std::string& outputStem,
                         GenerationProgress* p) {
    GeneratorResult result;
    if (s.resolvedMeshyKey().empty()) {
        result.error = "Add your Meshy API key (meshy.ai > API) or set MESHY_API_KEY";
        return result;
    }
    std::string uri;
    if (!prepareImageDataUri(imagePath, uri, result.error)) return result;
    return runMeshyRequest(s, "/openapi/v1/image-to-3d", meshySubmitBody(s.meshyModel, uri), outputStem, p);
}

GeneratorResult runMeshyRequest(const AiSettings& s, const std::string& taskPath, const std::string& body,
                                const std::string& outputStem, GenerationProgress* p) {
    GeneratorResult result;
    const std::string key = s.resolvedMeshyKey();
    if (key.empty()) {
        result.error = "Add your Meshy API key (meshy.ai > API) or set MESHY_API_KEY";
        return result;
    }
    const std::vector<std::string> headers = {"Authorization: Bearer " + key, "Content-Type: application/json"};
    std::string endpoint = s.meshyEndpoint;
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    const std::string tasks = endpoint + taskPath;

    status(p, "Sending to Meshy...");
    HttpResponse r = httpPostJson(tasks, headers, body, 120);
    if (!r.error.empty()) {
        result.error = "Could not reach Meshy: " + r.error;
        return result;
    }
    if (r.status < 200 || r.status >= 300) {
        result.error = apiError(r);
        return result;
    }
    Json submitted;
    std::string err;
    if (!Json::parse(r.body, submitted, err) || !submitted["result"].isString()) {
        result.error = "Unexpected reply from Meshy: " + r.body.substr(0, 200);
        return result;
    }
    const std::string taskUrl = tasks + "/" + submitted["result"].asString();
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        if (!wait(p, kPollSeconds)) {
            result.error = "Cancelled (the Meshy task keeps running on their side)";
            return result;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::minutes(kMaxMinutes)) {
            result.error = "Timed out waiting for Meshy";
            return result;
        }
        HttpResponse st = httpRequest("GET", taskUrl, headers, {}, 60);
        if (!st.error.empty()) continue;
        if (st.status < 200 || st.status >= 300) {
            result.error = apiError(st);
            return result;
        }
        Json task;
        if (!Json::parse(st.body, task, err)) continue;
        const std::string state = task["status"].asString();
        const int pct = int(task["progress"].asNumber(-1));
        if (state == "SUCCEEDED") {
            std::string extension;
            const std::string url = findModelUrl(task["model_urls"], &extension);
            if (url.empty()) {
                result.error = "Meshy finished but returned no GLB";
                return result;
            }
            download(url, outputStem, extension, result, p);
            return result;
        }
        if (state == "FAILED" || state == "CANCELED") {
            const std::string msg = task["task_error"]["message"].asString();
            result.error = "Meshy " + state + (msg.empty() ? std::string() : ": " + msg);
            return result;
        }
        status(p, state == "PENDING" ? "Waiting in queue..." : "Reconstructing 3D model... " + std::to_string(std::max(pct, 0)) + "%",
               pct);
    }
}

// `imagePath` or `prompt` (text to 3D) - exactly one is used.
GeneratorResult runHuggingFace(const AiSettings& s, const std::string& imagePath, const std::string& prompt,
                               const std::string& outputStem, GenerationProgress* p) {
    GeneratorResult result;
    const bool text = !prompt.empty();
    const HfPreset* preset = &resolveHfPreset(s, text, &result.note);
    if (!result.note.empty()) status(p, result.note);
    status(p, "Connecting to " + std::string(preset->label) + "...");
    GradioClient client;
    if (!client.connect(preset->space, s.resolvedHfToken(), result.error)) return result;
    Json image;
    if (!text) {
        std::string png;
        if (!prepareImagePng(imagePath, png, result.error)) return result;
        if (!client.upload(png, "input.png", image, result.error)) return result;
    }

    std::vector<Json> firstOutputs;
    for (const auto& step : preset->steps) {
        if (p && p->cancel) {
            result.error = "Cancelled";
            return result;
        }
        std::vector<Json> args;
        for (const auto& a : step.args) {
            if (a == "$IMG") {
                args.push_back(image);
            } else if (a == "$PROMPT") {
                args.push_back(Json(prompt));
            } else if (a.rfind("$OUT", 0) == 0) {
                const size_t n = size_t(std::stoi(a.substr(4)));
                args.push_back(n < firstOutputs.size() ? firstOutputs[n] : Json());
            } else {
                Json v;
                std::string err;
                Json::parse(a, v, err);
                args.push_back(v);
            }
        }
        std::vector<Json> outputs;
        if (!client.call(step.api, args, outputs, result.error, [&](const std::string& text) { status(p, text); }))
            return result;
        Json first = outputs.empty() ? Json() : outputs[0];
        if (first.isObject() && first["value"].isObject()) first = first["value"]; // unwrap component updates
        firstOutputs.push_back(first);
    }
    const std::string url = client.lastModelUrl();
    if (url.empty()) {
        result.error = "The Space finished but returned no 3D model";
        return result;
    }
    status(p, "Downloading model...");
    std::string bytes;
    if (!client.download(url, bytes, result.error)) return result;
    std::string lower = url.substr(0, url.find('?'));
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const std::string ext = lower.ends_with(".obj") ? ".obj" : lower.ends_with(".ply") ? ".ply" : ".glb";
    const std::string path = outputStem + ext;
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f || !f.write(bytes.data(), std::streamsize(bytes.size()))) {
        result.error = "Cannot write " + path;
        return result;
    }
    result.ok = true;
    result.outputPath = path;
    return result;
}

GeneratorResult runInflate(const std::string& imagePath, const std::string& outputStem, GenerationProgress* p) {
    GeneratorResult result;
    status(p, "Inflating silhouette...");
    Image img;
    if (!Image::load(imagePath, img, result.error)) return result;
    InflateOptions o;
    o.subdivisions = 2;
    Mesh m = inflateSilhouette(img, o, &result.error);
    if (m.triangleCount() == 0) return result;
    const std::string path = outputStem + ".glb";
    if (!saveGlb(path, m, "Inflated", result.error)) return result;
    result.ok = true;
    result.outputPath = path;
    return result;
}

} // namespace

const std::vector<ServiceModel>& falModels() {
    static const std::vector<ServiceModel> models = {
        {"fal-ai/trellis-2", "TRELLIS.2 (Microsoft) - detailed, open model"},
        {"fal-ai/hunyuan-3d/v3.1/pro/image-to-3d", "Hunyuan3D 3.1 Pro (Tencent)"},
        {"fal-ai/hunyuan-3d/v3.1/rapid/image-to-3d", "Hunyuan3D 3.1 Rapid (Tencent) - fast"},
        {"fal-ai/meshy/v6/image-to-3d", "Meshy 6"},
    };
    return models;
}

bool hunyuanModel(std::string_view id) {
    for (size_t i = 0; i + 7 <= id.size(); ++i) {
        std::string word(id.substr(i, 7));
        for (auto& c : word) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (word == "hunyuan") return true;
    }
    return false;
}

const char* kHunyuanLicenceNote =
    "Hunyuan3D models are under the Tencent Hunyuan Community License, which doesn't cover use in the EU, UK or "
    "South Korea. There, choose TRELLIS.2 (free token) or another model.";

const char* kTrellisNoTokenNote =
    "TRELLIS.2 needs a free Hugging Face token (Preferences > AI); used Hunyuan3D 2.1 instead.";

const HfPreset& resolveHfPreset(const AiSettings& s, bool text, std::string* note) {
    const auto& presets = text ? hfTextPresets() : hfPresets();
    auto find = [&](std::string_view id) -> const HfPreset* {
        for (const auto& h : presets)
            if (id == h.id) return &h;
        return nullptr;
    };
    if (!text && s.hfModel == "trellis-2" && s.resolvedHfToken().empty()) {
        if (note) *note = kTrellisNoTokenNote;
        return *find("hunyuan3d-2.1");
    }
    const HfPreset* preset = find(s.hfModel);
    return preset ? *preset : presets[0];
}

std::string base64Encode(const uint8_t* data, size_t size) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = uint32_t(data[i]) << 16 | (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0u) |
                           (i + 2 < size ? uint32_t(data[i + 2]) : 0u);
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += i + 1 < size ? chars[(n >> 6) & 63] : '=';
        out += i + 2 < size ? chars[n & 63] : '=';
    }
    return out;
}

bool prepareImagePng(const std::string& imagePath, std::string& png, std::string& error) {
    Image img;
    if (!Image::load(imagePath, img, error)) return false;
    if (img.width < 16 || img.height < 16) {
        error = "The image is too small";
        return false;
    }
    // Reconstruction models work best with the subject centred and filling most of a square frame.
    Image framed = img.framedSubject(0.08f).downscaled(1024);
    std::vector<uint8_t> bytes;
    if (!framed.encodePng(bytes)) {
        error = "Could not encode the image";
        return false;
    }
    png.assign(bytes.begin(), bytes.end());
    return true;
}

bool prepareImageDataUri(const std::string& imagePath, std::string& dataUri, std::string& error) {
    std::string png;
    if (!prepareImagePng(imagePath, png, error)) return false;
    dataUri = "data:image/png;base64," + base64Encode(reinterpret_cast<const uint8_t*>(png.data()), png.size());
    return true;
}

const std::vector<HfPreset>& hfTextPresets() {
    static const std::vector<HfPreset> presets = {
        {"hunyuan3d-2-turbo", "Hunyuan3D 2 mini Turbo (Tencent) - free, fast", "https://tencent-hunyuan3d-2mini-turbo.hf.space",
         {{"shape_generation", {"$PROMPT"}}}},
        {"hunyuan3d-2", "Hunyuan3D 2 (Tencent) - free", "https://tencent-hunyuan3d-2.hf.space", {{"shape_generation", {"$PROMPT"}}}},
    };
    return presets;
}

const std::vector<HfPreset>& hfPresets() {
    static const std::vector<HfPreset> presets = {
        // Hunyuan3D 2.1 first: the default, works with no token. resolveHfPreset() falls back to it.
        {"hunyuan3d-2.1", "Hunyuan3D 2.1 (Tencent) - free, no token", "https://tencent-hunyuan3d-2-1.hf.space",
         {{"shape_generation", {"$IMG"}}}},
        {"trellis-2", "TRELLIS.2 (Microsoft, MIT) - Best detail (needs token)", "https://microsoft-trellis-2.hf.space",
         {{"start_session", {}}, {"preprocess_image", {"$IMG"}}, {"image_to_3d", {"$OUT1"}}, {"extract_glb", {}}}},
        {"hunyuan3d-2-turbo", "Hunyuan3D 2 mini Turbo (Tencent) - free, fastest", "https://tencent-hunyuan3d-2mini-turbo.hf.space",
         {{"shape_generation", {"\"\"", "$IMG"}}}},
        {"hunyuan3d-2", "Hunyuan3D 2 (Tencent) - free", "https://tencent-hunyuan3d-2.hf.space",
         {{"shape_generation", {"\"\"", "$IMG"}}}},
        {"stable-fast-3d", "Stable Fast 3D (Stability AI) - free, quick and simple", "https://stabilityai-stable-fast-3d.hf.space",
         // As its UI does: check the background, remove it (fills the session state), then Run.
         {{"requires_bg_remove", {"$IMG", "0.85"}},
          {"run_button", {"\"Remove Background\"", "$IMG"}},
          {"run_button", {"\"Run\"", "$IMG"}}}},
    };
    return presets;
}

std::string falSubmitBody(const std::string& model, const std::string& dataUri) {
    Json body = Json::object();
    const bool hunyuan = model.find("hunyuan") != std::string::npos;
    body.set(hunyuan ? "input_image_url" : "image_url", dataUri);
    if (model.find("hunyuan-3d/v3.1/rapid") != std::string::npos) body.set("enable_geometry", true); // shape only: we sculpt it
    return body.dump();
}

std::string meshySubmitBody(const std::string& model, const std::string& dataUri) {
    Json body = Json::object();
    body.set("image_url", dataUri);
    body.set("ai_model", model.empty() ? "latest" : model);
    body.set("should_texture", false); // SculptForge sculpts the shape; skip texturing time and credits
    Json formats = Json::array();
    formats.push("glb");
    body.set("target_formats", formats);
    return body.dump();
}

std::string findModelUrl(const Json& result, std::string* extension) {
    auto urlOf = [](const Json& j) -> std::string {
        if (j.isString()) return j.asString();
        if (j.isObject() && j["url"].isString()) return j["url"].asString();
        return {};
    };
    auto extOf = [](const std::string& url, const Json& file) {
        const std::string type = file.isObject() ? file["content_type"].asString() : std::string();
        std::string path = url.substr(0, url.find('?'));
        std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (path.ends_with(".obj") || type.find("obj") != std::string::npos) return std::string(".obj");
        return std::string(".glb");
    };
    // Known field names first (fal TRELLIS/Hunyuan/Meshy, Meshy direct).
    for (const char* key : {"model_glb", "model_mesh", "glb", "model_file", "mesh", "model_obj", "obj"}) {
        const std::string url = urlOf(result[key]);
        if (!url.empty()) {
            if (extension) *extension = extOf(url, result[key]);
            return url;
        }
    }
    if (result["model_urls"].isObject()) {
        if (std::string url = findModelUrl(result["model_urls"], extension); !url.empty()) return url;
    }
    // Otherwise any URL ending in .glb anywhere in the result.
    std::function<std::string(const Json&)> search = [&](const Json& j) -> std::string {
        if (j.isString()) {
            std::string path = j.asString().substr(0, j.asString().find('?'));
            if (path.size() > 4 && (path.ends_with(".glb") || path.ends_with(".GLB"))) return j.asString();
            return {};
        }
        if (j.isArray())
            for (const auto& item : j.items())
                if (auto u = search(item); !u.empty()) return u;
        if (j.isObject())
            for (const auto& [k, v] : j.members())
                if (auto u = search(v); !u.empty()) return u;
        return {};
    };
    const std::string url = search(result);
    if (!url.empty() && extension) *extension = ".glb";
    return url;
}

GeneratorResult generate3D(const AiSettings& s, const std::string& imagePath, const std::string& outputStem,
                           GenerationProgress* progress) {
    if (!aiEnabled()) return {false, {}, aiOffError()};
    if (s.generatorService == "meshy") return runMeshy(s, imagePath, outputStem, progress);
    if (s.generatorService == "command") {
        status(progress, "Running your generator...");
        return runGenerator(s.generatorCommand, imagePath, outputStem + ".glb");
    }
    if (s.generatorService == "inflate") return runInflate(imagePath, outputStem, progress);
    if (s.generatorService == "fal") return runFal(s, imagePath, outputStem, progress);
    if (s.generatorService == "comfyui") return comfy::run(s, {}, imagePath, {}, outputStem, progress);
    return runHuggingFace(s, imagePath, {}, outputStem, progress);
}

std::string meshyTextBody(const std::string& model, const std::string& prompt) {
    Json body = Json::object();
    body.set("mode", "preview"); // untextured shape: what we sculpt
    body.set("prompt", prompt.substr(0, 800));
    body.set("ai_model", model.empty() ? "latest" : model);
    return body.dump();
}

std::string falTextBody(const std::string& prompt) {
    Json body = Json::object();
    body.set("prompt", prompt.substr(0, 200));
    body.set("enable_geometry", true); // shape only
    return body.dump();
}

GeneratorResult generate3DFromText(const AiSettings& s, const std::string& prompt, const std::string& outputStem,
                                   GenerationProgress* progress) {
    if (!aiEnabled()) return {false, {}, aiOffError()};
    GeneratorResult result;
    if (prompt.find_first_not_of(" \t\r\n") == std::string::npos) {
        result.error = "Describe the model you want";
        return result;
    }
    if (s.generatorService == "meshy") return runMeshyRequest(s, "/openapi/v2/text-to-3d", meshyTextBody(s.meshyModel, prompt), outputStem, progress);
    if (s.generatorService == "fal")
        return runFalRequest(s, "fal-ai/hunyuan-3d/v3.1/rapid/text-to-3d", falTextBody(prompt), outputStem, progress);
    if (s.generatorService == "comfyui") return comfy::run(s, {}, {}, prompt, outputStem, progress);
    if (s.generatorService == "command" || s.generatorService == "inflate") {
        result.error = "Text to 3D needs Hugging Face (free), fal.ai, Meshy or ComfyUI - pick one of those services";
        return result;
    }
    return runHuggingFace(s, {}, prompt, outputStem, progress);
}

} // namespace sf::ai
