// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/GradioClient.h"

#include "ai/AiPolicy.h"

#include "ai/Http.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>

namespace sf::ai {

namespace {

std::string randomHash() {
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string h;
    for (int i = 0; i < 11; ++i) h += chars[rng() % 36];
    return h;
}

bool isFileData(const Json& v) { return v.isObject() && (v["path"].isString() || v["url"].isString()); }

// Gradio may wrap returned values as {"__type__": "update", "value": ...}.
const Json& unwrap(const Json& v) { return v.isObject() && v["value"].isObject() ? v["value"] : v; }

bool modelName(std::string s) {
    s = s.substr(0, s.find('?'));
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const char* ext : {".glb", ".gltf", ".obj", ".ply"})
        if (s.ends_with(ext)) return true;
    return false;
}

// The first file in a returned value of any shape: a FileData object, a bare path or URL string to a model, a list of
// either, or a component update wrapping them.
bool firstFile(const Json& v, bool anyFile, Json& found, int depth = 0) {
    if (depth > 4) return false;
    if (v.isString()) {
        const std::string& str = v.asString();
        if (!modelName(str) || (str.rfind("http", 0) != 0 && str.rfind('/', 0) != 0)) return false;
        found = Json::object();
        found.set(str.rfind("http", 0) == 0 ? "url" : "path", str);
        return true;
    }
    if (isFileData(v)) {
        const std::string name = v["url"].isString() ? v["url"].asString() : v["path"].asString();
        if (!anyFile && !modelName(v["orig_name"].isString() ? v["orig_name"].asString() : name) && !modelName(name)) return false;
        found = v;
        return true;
    }
    if (v.isArray()) {
        for (const auto& item : v.items())
            if (firstFile(item, anyFile, found, depth + 1)) return true;
    } else if (v.isObject() && v.has("value")) {
        return firstFile(v["value"], anyFile, found, depth + 1);
    }
    return false;
}

} // namespace

bool gradioCompleted(const Json& m, std::vector<Json>& outputs, std::string& error) {
    if (m["success"].asBool(false)) {
        outputs = m["output"]["data"].items();
        return true;
    }
    const Json& o = m["output"];
    error = friendlyGradioError(o["error"].isString() ? o["error"].asString()
                                : m["title"].isString() ? m["title"].asString()
                                                        : std::string("The Space reported an error"));
    return false;
}

std::string gradioModelUrl(const Json& config, const std::string& apiName, const std::vector<Json>& outputs,
                           const std::string& base) {
    auto urlOf = [&](const Json& file) {
        if (file["url"].isString() && !file["url"].asString().empty()) return file["url"].asString();
        return base + "/file=" + file["path"].asString();
    };
    const Json& deps = config["dependencies"];
    const Json* dep = nullptr;
    for (size_t i = 0; i < deps.size(); ++i)
        if (deps[i]["api_name"].asString() == apiName) dep = &deps[i];
    auto typeOf = [&](double id) {
        for (const auto& c : config["components"].items())
            if (c["id"].asNumber() == id) return c["type"].asString();
        return std::string();
    };
    Json found;
    // 3D model outputs first, then file outputs, then anything that holds a model file.
    for (int pass = 0; pass < 3; ++pass)
        for (size_t i = 0; i < outputs.size(); ++i) {
            const std::string type = dep && i < (*dep)["outputs"].size() ? typeOf((*dep)["outputs"][i].asNumber()) : std::string();
            const bool modelType = type == "model3d" || type == "litmodel3d";
            if (pass == 0 && !modelType) continue;
            if (pass == 1 && type != "file") continue;
            if (firstFile(outputs[i], pass < 2, found)) return urlOf(found);
        }
    return {};
}

std::string friendlyGradioError(const std::string& raw) {
    if (raw.find("Text to 3D is disable") != std::string::npos)
        return "This free Hugging Face Space has switched Text to 3D off. Use Image to 3D (or another provider) for now.";
    if (raw.find("ZeroGPU") != std::string::npos || raw.find("GPU quota") != std::string::npos)
        return "Hugging Face's free GPU time is used up for now. Add a free Hugging Face token "
               "(huggingface.co/settings/tokens) for a bigger daily allowance, wait for it to refill, or pick another model. (" +
               raw.substr(0, 160) + ")";
    if (raw.find("503") != std::string::npos || raw.find("Space is") != std::string::npos)
        return "The Hugging Face Space is asleep or restarting - try again in a minute, or pick another model. (" +
               raw.substr(0, 160) + ")";
    return raw;
}

bool gradioBuildInputs(const Json& config, const std::string& apiName, const std::vector<Json>& userArgs,
                       const std::vector<GradioSlot>& slots, Json& data, int& fnIndex, std::string& error) {
    const Json& deps = config["dependencies"];
    const Json* dep = nullptr;
    for (size_t i = 0; i < deps.size(); ++i)
        if (deps[i]["api_name"].asString() == apiName) {
            dep = &deps[i];
            fnIndex = deps[i]["id"].isNumber() ? int(deps[i]["id"].asNumber()) : int(i);
            break;
        }
    if (!dep) {
        error = "This Space has no '" + apiName + "' endpoint (it may have changed)";
        return false;
    }
    auto component = [&](double id) -> const Json* {
        for (const auto& c : config["components"].items())
            if (c["id"].asNumber() == id) return &c;
        return nullptr;
    };
    auto lower = [](std::string t) {
        for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const auto& inputs = (*dep)["inputs"].items();
    std::vector<const Json*> comps;
    for (const auto& id : inputs) comps.push_back(component(id.asNumber()));
    auto typeOf = [&](size_t i) { return comps[i] ? (*comps[i])["type"].asString() : std::string(); };

    // Slots first: by label, else by type, each input claimed once.
    std::vector<const Json*> claimed(inputs.size(), nullptr);
    std::vector<const GradioSlot*> owner(inputs.size(), nullptr);
    for (const auto& slot : slots) {
        size_t found = inputs.size();
        for (int pass = 0; pass < 2 && found == inputs.size(); ++pass)
            for (size_t i = 0; i < inputs.size(); ++i) {
                if (owner[i] || typeOf(i) == "state") continue;
                const bool hit = pass == 0 ? comps[i] && !slot.label.empty() &&
                                                 lower((*comps[i])["props"]["label"].asString()) == lower(slot.label)
                                           : !slot.type.empty() && typeOf(i) == slot.type;
                if (hit) {
                    found = i;
                    break;
                }
            }
        if (found == inputs.size() && slot.optional) continue;
        if (found == inputs.size()) {
            error = "This Space has no '" + (slot.label.empty() ? slot.type : slot.label) + "' input on '" + apiName + "' (it may have changed)";
            return false;
        }
        owner[found] = &slot;
    }

    data = Json::array();
    size_t next = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const std::string type = typeOf(i);
        const Json fallback = comps[i] ? (*comps[i])["props"]["value"] : Json();
        if (type == "state") data.push(fallback); // hidden from the public API; the server keeps it per session
        else if (owner[i]) data.push(owner[i]->value);
        else if (next < userArgs.size()) data.push(userArgs[next++]);
        else data.push(fallback);
    }
    return true;
}

std::vector<std::string> GradioClient::headers(bool json) const {
    std::vector<std::string> h;
    if (json) h.push_back("Content-Type: application/json");
    if (!token_.empty()) h.push_back("Authorization: Bearer " + token_);
    return h;
}

bool GradioClient::connect(const std::string& spaceUrl, const std::string& token, std::string& error) {
    if (!aiEnabled()) {
        error = aiOffError();
        return false;
    }
    root_ = spaceUrl;
    while (!root_.empty() && root_.back() == '/') root_.pop_back();
    token_ = token;
    sessionHash_ = randomHash();
    HttpResponse r = httpRequest("GET", root_ + "/config", headers(false), {}, 60);
    if (!r.error.empty()) {
        error = "Cannot reach " + root_ + ": " + r.error;
        return false;
    }
    std::string err;
    if (r.status != 200 || !Json::parse(r.body, config_, err) || !config_["dependencies"].isArray()) {
        error = friendlyGradioError("HTTP " + std::to_string(r.status) + " from " + root_ + " (Space is unavailable)");
        return false;
    }
    prefix_ = config_["api_prefix"].isString() ? config_["api_prefix"].asString() : std::string();
    if (prefix_ == "/") prefix_.clear();
    return true;
}

bool GradioClient::upload(const std::string& bytes, const std::string& fileName, Json& fileData, std::string& error) {
    const std::string boundary = "----sculptforge" + randomHash();
    std::string body = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"" + fileName +
                       "\"\r\nContent-Type: application/octet-stream\r\n\r\n" + bytes + "\r\n--" + boundary + "--\r\n";
    auto h = headers(false);
    h.push_back("Content-Type: multipart/form-data; boundary=" + boundary);
    HttpResponse r = httpRequest("POST", root_ + prefix_ + "/upload", h, body, 120);
    Json paths;
    std::string err;
    if (!r.error.empty() || r.status != 200 || !Json::parse(r.body, paths, err) || !paths[0].isString()) {
        error = "Upload to " + root_ + " failed: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) + " " + r.body.substr(0, 200) : r.error);
        return false;
    }
    fileData = Json::object();
    fileData.set("path", paths[0].asString());
    Json meta = Json::object();
    meta.set("_type", "gradio.FileData");
    fileData.set("meta", meta);
    fileData.set("orig_name", fileName);
    return true;
}

std::string GradioClient::fileUrl(const Json& value) const {
    const Json& v = unwrap(value);
    if (v["url"].isString() && !v["url"].asString().empty()) return v["url"].asString();
    return root_ + prefix_ + "/file=" + v["path"].asString();
}

bool GradioClient::call(const std::string& apiName, const std::vector<Json>& userArgs, std::vector<Json>& outputs,
                        std::string& error, const std::function<void(const std::string&)>& onStatus,
                        const std::vector<GradioSlot>& slots) {
    Json data;
    int fnIndex = -1;
    if (!gradioBuildInputs(config_, apiName, userArgs, slots, data, fnIndex, error)) return false;

    Json join = Json::object();
    join.set("data", data);
    join.set("event_data", Json());
    join.set("fn_index", fnIndex);
    join.set("trigger_id", Json());
    join.set("session_hash", sessionHash_);
    HttpResponse r = httpRequest("POST", root_ + prefix_ + "/queue/join", headers(true), join.dump(), 60);
    Json joined;
    std::string err;
    if (!r.error.empty() || r.status != 200 || !Json::parse(r.body, joined, err)) {
        error = friendlyGradioError("Could not join the queue: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) + " " + r.body.substr(0, 200) : r.error));
        return false;
    }
    const std::string eventId = joined["event_id"].asString();

    // Read server-sent events until our event completes.
    std::string buffer;
    bool done = false, ok = false;
    auto h = headers(false);
    h.push_back("Accept: text/event-stream");
    HttpResponse s = httpRequestStream(
        "GET", root_ + prefix_ + "/queue/data?session_hash=" + sessionHash_, h, {}, 900, [&](const char* d, size_t n) {
            buffer.append(d, n);
            size_t eol;
            while ((eol = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, eol);
                buffer.erase(0, eol + 1);
                if (line.rfind("data:", 0) != 0) continue;
                Json m;
                if (!Json::parse(line.substr(5), m, err)) continue;
                if (!eventId.empty() && m["event_id"].isString() && m["event_id"].asString() != eventId) continue;
                const std::string msg = m["msg"].asString();
                if (msg == "estimation" && onStatus) {
                    const int rank = int(m["rank"].asNumber(0));
                    onStatus(rank > 0 ? "In the free queue: " + std::to_string(rank) + " ahead of you" : "Starting...");
                } else if (msg == "process_starts" && onStatus) {
                    onStatus("Generating on " + root_.substr(root_.find("//") + 2) + "...");
                } else if (msg == "progress" && onStatus) {
                    const Json& p = m["progress_data"][0];
                    if (p.isObject()) {
                        std::string text = p["desc"].isString() ? p["desc"].asString() : "Working";
                        if (p["index"].isNumber() && p["length"].isNumber())
                            text += " " + std::to_string(int(p["index"].asNumber())) + "/" + std::to_string(int(p["length"].asNumber()));
                        onStatus(text);
                    }
                } else if (msg == "process_completed") {
                    done = true;
                    ok = gradioCompleted(m, outputs, error);
                    return false; // stop reading
                } else if (msg == "close_stream") {
                    return false;
                }
            }
            return true;
        });
    if (!done) {
        error = friendlyGradioError(s.error.empty() ? "The Space closed the connection without a result" : "Connection lost: " + s.error);
        return false;
    }
    if (!ok) return false;

    // Remember the first 3D model (or file) output.
    lastModelUrl_ = gradioModelUrl(config_, apiName, outputs, root_ + prefix_);
    return true;
}

bool GradioClient::download(const std::string& url, std::string& bytes, std::string& error) {
    HttpResponse r = httpRequest("GET", url, headers(false), {}, 300);
    if (!r.error.empty() || r.status != 200 || r.body.empty()) {
        error = "Download failed: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    bytes = std::move(r.body);
    return true;
}

} // namespace sf::ai
