// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ComfyUI.h"
#include "ai/AiPolicy.h"

#include "ai/Http.h"
#include "ai/ImageTo3DService.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <set>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace sf::ai::comfy {

namespace {

constexpr int kMaxMinutes = 60; // a studio queue can be long
constexpr int kGiveUpSeconds = 30; // unreachable, or the prompt vanished from queue and history

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::filesystem::path u8path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string pathString(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string titleOf(const Json& node) { return lower(trim(node["_meta"]["title"].asString())); }

std::string nodeLabel(const Json& workflow, const std::string& id) {
    const Json& node = workflow[id];
    std::string label = "node " + id;
    const std::string title = trim(node["_meta"]["title"].asString());
    const std::string type = node["class_type"].asString();
    // Titles that only restate the node's display name ("Save Image" for SaveImage) are noise.
    std::string squashed;
    for (char c : title)
        if (c != ' ') squashed += c;
    if (!title.empty() && lower(squashed) != lower(type)) label += " '" + title + "'";
    if (!type.empty()) label += " (" + type + ")";
    return label;
}

// A literal input value, not a link ([node_id, output_index]).
bool isLink(const Json& v) { return v.isArray() && v.size() == 2 && v[0].isString(); }

void setInput(Json& workflow, const std::string& id, const std::string& key, Json value) {
    Json node = workflow[id];
    Json inputs = node["inputs"].isObject() ? node["inputs"] : Json::object();
    inputs.set(key, std::move(value));
    node.set("inputs", inputs);
    workflow.set(id, node);
}

std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

const char* const kMeshExtensions[] = {".glb", ".gltf", ".obj", ".ply", ".stl", ".fbx", ".usdz", ".usda", ".usd"};

bool hasMeshExtension(const std::string& name) {
    const std::string l = lower(name);
    for (const char* e : kMeshExtensions)
        if (l.ends_with(e)) return true;
    return false;
}

// A file named by a plain string: "mesh/out_00001_.glb [output]", or an absolute path inside ComfyUI's folders.
std::optional<OutputFile> fileFromString(std::string s) {
    std::string type = "output";
    if (const size_t b = s.rfind(" ["); b != std::string::npos && s.ends_with("]")) {
        type = s.substr(b + 2, s.size() - b - 3);
        s = s.substr(0, b);
    }
    if (!hasMeshExtension(s)) return std::nullopt;
    std::replace(s.begin(), s.end(), '\\', '/');
    const bool absolute = s.starts_with("/") || (s.size() > 2 && s[1] == ':');
    if (absolute) {
        bool inFolder = false;
        for (const char* folder : {"output", "temp", "input"}) {
            const std::string marker = std::string("/") + folder + "/";
            if (const size_t at = s.rfind(marker); at != std::string::npos) {
                type = folder;
                s = s.substr(at + marker.size());
                inFolder = true;
                break;
            }
        }
        if (!inFolder) s = s.substr(s.rfind('/') + 1);
    }
    OutputFile f;
    f.type = type;
    const size_t slash = s.rfind('/');
    f.filename = slash == std::string::npos ? s : s.substr(slash + 1);
    f.subfolder = slash == std::string::npos ? std::string() : s.substr(0, slash);
    return f;
}

void collectFiles(const Json& v, const std::string& nodeId, const std::string& key, std::vector<OutputFile>& out) {
    if (v.isObject() && v["filename"].isString()) {
        OutputFile f;
        f.nodeId = nodeId;
        f.key = key;
        f.filename = v["filename"].asString();
        f.subfolder = v["subfolder"].asString();
        if (v["type"].isString() && !v["type"].asString().empty()) f.type = v["type"].asString();
        out.push_back(f);
    } else if (v.isString()) {
        if (auto f = fileFromString(v.asString())) {
            f->nodeId = nodeId;
            f->key = key;
            out.push_back(*f);
        }
    } else if (v.isArray()) {
        for (const auto& item : v.items()) collectFiles(item, nodeId, key, out);
    } else if (v.isObject()) {
        for (const auto& [k, item] : v.members()) collectFiles(item, nodeId, key, out);
    }
}

void status(GenerationProgress* p, const std::string& text, int percent = -1) {
    if (!p) return;
    p->setStatus(text);
    p->percent = percent;
}

bool wait(GenerationProgress* p, int milliseconds) {
    for (int i = 0; i < milliseconds / 50; ++i) {
        if (p && p->cancel) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !(p && p->cancel);
}

std::string unreachable(const std::string& url, const std::string& transportError) {
    return "Nothing is answering at " + url + " (" + transportError +
           "). Start ComfyUI there, or check the address and port; for another computer start ComfyUI with --listen";
}

std::string randomId() {
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string id;
    for (int i = 0; i < 16; ++i) id += "0123456789abcdef"[rng() % 16];
    return id;
}

uint64_t randomSeed() {
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd());
    return rng() % 2147483647u; // within every seed widget's range
}

std::string executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    return pathString(std::filesystem::path(std::wstring(buf, n)).parent_path());
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(buf, ec);
    return pathString((ec ? std::filesystem::path(buf) : p).parent_path());
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : pathString(p.parent_path());
#endif
}

} // namespace

std::string normalizeUrl(const std::string& url) {
    std::string u = trim(url);
    if (u.empty()) u = kDefaultUrl;
    if (u.find("://") == std::string::npos) u = "http://" + u;
    while (u.size() > 8 && u.back() == '/') u.pop_back();
    return u;
}

// --- Server -----------------------------------------------------------------

std::string ServerInfo::summary() const {
    std::string s = comfyVersion.empty() ? "ComfyUI" : "ComfyUI " + comfyVersion;
    for (size_t i = 0; i < devices.size(); ++i) s += (i ? ", " : " - ") + devices[i];
    return s;
}

bool parseSystemStats(const Json& stats, ServerInfo& info) {
    const Json& system = stats["system"];
    if (!system.isObject()) return false;
    info = {};
    info.comfyVersion = system["comfyui_version"].asString();
    info.pythonVersion = trim(system["python_version"].asString().substr(0, system["python_version"].asString().find(' ')));
    info.pytorchVersion = system["pytorch_version"].asString();
    for (const auto& d : stats["devices"].items()) {
        // "cuda:0 NVIDIA GeForce RTX 4090 : cudaMallocAsync" -> "NVIDIA GeForce RTX 4090"
        std::string name = d["name"].asString();
        if (const size_t colon = name.find(" : "); colon != std::string::npos) name = name.substr(0, colon);
        const std::string type = d["type"].asString();
        if (!type.empty() && type != "cpu" && name.starts_with(type + ":")) name = name.substr(name.find(' ') + 1);
        name = trim(name);
        if (name.empty()) name = type;
        const double vram = d["vram_total"].asNumber(0);
        if (type != "cpu" && vram > 0) {
            char buf[32];
            std::snprintf(buf, sizeof buf, " (%.1f GB)", vram / (1024.0 * 1024.0 * 1024.0));
            name += buf;
        }
        if (!name.empty()) info.devices.push_back(name);
    }
    return true;
}

bool checkServer(const std::string& url, ServerInfo& info, std::string& error, int timeoutSeconds) {
    if (!aiEnabled()) {
        error = aiOffError();
        return false;
    }
    const std::string base = normalizeUrl(url);
    HttpResponse r = httpRequest("GET", base + "/system_stats", {}, {}, timeoutSeconds);
    if (!r.error.empty()) {
        error = unreachable(base, r.error);
        return false;
    }
    if (r.status == 401 || r.status == 403) {
        error = base + " refused the request (HTTP " + std::to_string(r.status) +
                ") - the server needs a login SculptForge cannot provide; ask for direct access to ComfyUI's port";
        return false;
    }
    Json stats;
    std::string err;
    if (r.status != 200 || !Json::parse(r.body, stats, err) || !parseSystemStats(stats, info)) {
        error = base + " answered, but not like ComfyUI (HTTP " + std::to_string(r.status) +
                ") - check the address and port (ComfyUI's default is 8188)";
        return false;
    }
    return true;
}

// --- Workflows ---------------------------------------------------------------

bool isUiFormat(const Json& workflow) { return workflow.isObject() && workflow["nodes"].isArray(); }

bool parseWorkflow(const std::string& text, Json& workflow, std::string& error) {
    Json j;
    std::string err;
    if (!Json::parse(text, j, err)) {
        error = "The workflow is not valid JSON (" + err + ")";
        return false;
    }
    if (isUiFormat(j)) {
        error = "This workflow was saved in ComfyUI's editor format, which can't be queued. Open it in ComfyUI and use "
                "Workflow > Export (API) (older versions: enable 'Dev mode' in the settings, then 'Save (API Format)'), "
                "then pick that file";
        return false;
    }
    if (j["prompt"].isObject() && !j["prompt"]["class_type"].isString()) j = Json(j["prompt"]); // {"prompt": {...}} as sent to /prompt
    if (!j.isObject() || j.members().empty()) {
        error = "Not a ComfyUI API workflow (expected {node_id: {class_type, inputs}})";
        return false;
    }
    for (const auto& [id, node] : j.members()) {
        if (!node.isObject() || !node["class_type"].isString()) {
            error = "Not a ComfyUI API workflow: '" + id + "' has no class_type (export it with Workflow > Export (API))";
            return false;
        }
    }
    workflow = std::move(j);
    return true;
}

bool loadWorkflow(const std::string& path, Json& workflow, std::string& error) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) {
        error = "Cannot open the workflow " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parseWorkflow(ss.str(), workflow, error)) {
        error = pathString(u8path(path).filename()) + ": " + error;
        return false;
    }
    return true;
}

bool applyContract(Json& workflow, const ContractInputs& in, std::string& error, ContractReport* report) {
    ContractReport local;
    ContractReport& rep = report ? *report : local;
    rep = {};
    std::vector<std::string> ids;
    for (const auto& [id, node] : workflow.members()) ids.push_back(id);

    if (!in.image.empty()) {
        for (const auto& id : ids)
            if (titleOf(workflow[id]) == "sf_image") rep.imageNodes.push_back(id);
        if (rep.imageNodes.empty()) {
            // Fallback: the first Load Image node (the stock one first, then look-alikes from node packs).
            for (const auto& id : ids)
                if (workflow[id]["class_type"].asString() == "LoadImage") {
                    rep.imageNodes.push_back(id);
                    break;
                }
            if (rep.imageNodes.empty())
                for (const auto& id : ids)
                    if (workflow[id]["class_type"].asString().find("LoadImage") != std::string::npos &&
                        workflow[id]["inputs"].has("image")) {
                        rep.imageNodes.push_back(id);
                        break;
                    }
        }
        if (rep.imageNodes.empty()) {
            error = "The workflow has no Load Image node for the picture - add one (title it sf_image), or pick an "
                    "image-to-3D workflow";
            return false;
        }
        for (const auto& id : rep.imageNodes) setInput(workflow, id, "image", in.image);
    }

    if (!in.prompt.empty()) {
        auto promptKey = [&](const std::string& id) -> std::string {
            const Json& inputs = workflow[id]["inputs"];
            for (const char* key : {"text", "prompt", "string", "value"})
                if (inputs.has(key) && (std::string(key) != "value" || inputs[key].isString())) return key;
            return {};
        };
        std::vector<std::pair<std::string, std::string>> targets; // node id, input name
        for (const auto& id : ids) {
            if (titleOf(workflow[id]) != "sf_prompt") continue;
            const std::string key = promptKey(id);
            if (key.empty()) {
                error = "The " + nodeLabel(workflow, id) + " is titled sf_prompt but has no text, prompt or string input";
                return false;
            }
            targets.emplace_back(id, key);
        }
        if (targets.empty()) {
            // Fallback: the first text encoder that isn't the negative prompt.
            for (const auto& id : ids) {
                const Json& node = workflow[id];
                if (node["class_type"].asString().find("TextEncode") == std::string::npos) continue;
                if (titleOf(node).find("negative") != std::string::npos) continue;
                const std::string key = promptKey(id);
                if (key.empty() || isLink(node["inputs"][key])) continue;
                targets.emplace_back(id, key);
                break;
            }
        }
        if (targets.empty()) {
            error = "The workflow has no node for the description - title the node that takes the text sf_prompt "
                    "(in ComfyUI: right-click it > Title)";
            return false;
        }
        for (const auto& [id, key] : targets) {
            setInput(workflow, id, key, in.prompt);
            rep.promptNodes.push_back(id);
        }
    }

    if (in.seed) {
        const double seed = double(*in.seed);
        for (const auto& id : ids) {
            // The keys are chosen first and set after: setInput replaces the node, so a reference into its
            // inputs doesn't survive it.
            std::vector<const char*> keys;
            {
                const Json& inputs = workflow[id]["inputs"];
                for (const char* key : {"seed", "noise_seed"})
                    if (inputs[key].isNumber()) keys.push_back(key);
                if (keys.empty() && titleOf(workflow[id]) == "sf_seed")
                    for (const char* key : {"value", "int", "number"})
                        if (inputs[key].isNumber()) {
                            keys.push_back(key);
                            break;
                        }
            }
            for (const char* key : keys) setInput(workflow, id, key, seed);
            if (!keys.empty()) rep.seedNodes.push_back(id);
        }
    }
    return true;
}

std::string workflowKind(const Json& workflow) {
    bool text = false;
    for (const auto& [id, node] : workflow.members()) {
        const std::string title = titleOf(node), type = node["class_type"].asString();
        if (title == "sf_image" || type == "LoadImage") return "image";
        if (title == "sf_prompt" || type.find("TextEncode") != std::string::npos) text = true;
    }
    return text ? "text" : "";
}

// --- Results -----------------------------------------------------------------

bool OutputFile::isMesh() const { return hasMeshExtension(filename); }

HistoryEntry parseHistory(const Json& history, const std::string& promptId) {
    HistoryEntry h;
    const Json& e = history[promptId];
    if (!e.isObject()) return h;
    h.found = true;
    const Json& st = e["status"];
    for (const auto& [nodeId, outputs] : e["outputs"].members())
        for (const auto& [key, value] : outputs.members()) collectFiles(value, nodeId, key, h.files);
    for (const auto& m : st["messages"].items()) {
        const std::string kind = m[0].asString();
        const Json& d = m[1];
        if (kind == "execution_error") {
            std::string where = d["node_type"].asString();
            if (d["node_id"].isString()) where += (where.empty() ? "node " : " (node ") + d["node_id"].asString() + (where.empty() ? "" : ")");
            h.error = (where.empty() ? std::string() : where + " failed: ") + trim(d["exception_message"].asString());
            if (d["exception_type"].isString() && d["exception_type"].asString().find("OutOfMemory") != std::string::npos)
                h.error += " - the GPU ran out of memory; try a smaller model or resolution";
        } else if (kind == "execution_interrupted" && h.error.empty()) {
            h.error = "The workflow was interrupted on the ComfyUI server";
        }
    }
    const std::string s = st["status_str"].asString();
    h.failed = s == "error" || !h.error.empty();
    if (h.failed && h.error.empty()) h.error = "The workflow failed on the ComfyUI server (see its console)";
    // Old servers have no status block: being in the history means it finished.
    h.completed = !h.failed && (st["completed"].asBool() || !st.isObject() || s == "success");
    return h;
}

const OutputFile* firstMesh(const std::vector<OutputFile>& files) {
    for (const auto& f : files)
        if (f.isMesh()) return &f;
    return nullptr;
}

std::string describeOutputs(const Json& history, const std::string& promptId) {
    const Json& e = history[promptId];
    // e.prompt = [number, id, workflow, extra, outputs to execute]: names the nodes.
    const Json& workflow = e["prompt"][2];
    std::string out;
    for (const auto& [nodeId, outputs] : e["outputs"].members()) {
        for (const auto& [key, value] : outputs.members()) {
            std::vector<std::string> names;
            std::function<void(const Json&)> walk = [&](const Json& v) {
                if (v.isObject() && v["filename"].isString()) names.push_back(v["filename"].asString());
                else if (v.isString() && v.asString().size() < 120) names.push_back(v.asString());
                else if (v.isArray())
                    for (const auto& item : v.items()) walk(item);
            };
            walk(value);
            std::string item = (workflow.isObject() ? nodeLabel(workflow, nodeId) : "node " + nodeId) + " '" + key + "'";
            for (size_t i = 0; i < names.size() && i < 3; ++i) item += (i ? ", " : ": ") + names[i];
            if (names.size() > 3) item += " (+" + std::to_string(names.size() - 3) + " more)";
            out += (out.empty() ? "" : "; ") + item;
        }
    }
    return out.empty() ? "none" : out;
}

std::string viewUrl(const std::string& baseUrl, const OutputFile& file) {
    return normalizeUrl(baseUrl) + "/view?filename=" + urlEncode(file.filename) + "&subfolder=" + urlEncode(file.subfolder) +
           "&type=" + urlEncode(file.type);
}

std::vector<std::string> missingNodeTypes(const Json& workflow, const Json& objectInfo) {
    std::vector<std::string> missing;
    for (const auto& [id, node] : workflow.members()) {
        const std::string type = node["class_type"].asString();
        if (!objectInfo.has(type) && std::find(missing.begin(), missing.end(), type) == missing.end()) missing.push_back(type);
    }
    return missing;
}

std::string promptError(const std::string& body, const Json& workflow) {
    Json j;
    std::string err;
    if (!Json::parse(body, j, err) || !j.isObject()) return "ComfyUI rejected the workflow: " + trim(body.substr(0, 300));
    const Json& e = j["error"];
    const std::string type = e["type"].asString();
    if (type == "missing_node_type") {
        const Json& x = e["extra_info"];
        std::string node = x["class_type"].asString();
        if (x["node_id"].isString() && workflow.has(x["node_id"].asString())) node = nodeLabel(workflow, x["node_id"].asString());
        return "This ComfyUI does not have the node " + (node.empty() ? e["message"].asString() : node) +
               ". Install the custom node pack that provides it (ComfyUI Manager > Install Missing Custom Nodes) and "
               "restart ComfyUI";
    }
    if (type == "prompt_no_outputs")
        return "The workflow has no output node - end it with a node that saves the mesh (Save GLB, or SculptForge's "
               "Save Mesh)";
    std::vector<std::string> problems;
    bool missingModel = false;
    for (const auto& [id, info] : j["node_errors"].members()) {
        for (const auto& ne : info["errors"].items()) {
            std::string msg = trim(ne["message"].asString());
            std::string details = trim(ne["details"].asString());
            // "ckpt_name: 'hunyuan3d-dit-v2.safetensors' not in [...]": a model the server doesn't have.
            const std::string input = lower(details.substr(0, details.find(':')));
            const std::string d = lower(details);
            if (ne["type"].asString() == "value_not_in_list" &&
                (input.ends_with("_name") || input.find("model") != std::string::npos ||
                 d.find(".safetensors") != std::string::npos || d.find(".ckpt") != std::string::npos ||
                 d.find(".gguf") != std::string::npos || d.find(".pth") != std::string::npos))
                missingModel = true;
            if (details.size() > 160) details = details.substr(0, 157) + "...";
            if (!details.empty()) msg += ": " + details;
            std::string label = workflow.has(id) ? nodeLabel(workflow, id) : "node " + id;
            problems.push_back(label + " - " + msg);
        }
    }
    if (!problems.empty()) {
        std::string out = "ComfyUI rejected the workflow: ";
        for (size_t i = 0; i < problems.size() && i < 4; ++i) out += (i ? "; " : "") + problems[i];
        if (problems.size() > 4) out += " (+" + std::to_string(problems.size() - 4) + " more)";
        if (missingModel) out += ". The workflow names a model file this server doesn't have - download it into ComfyUI's models folder";
        return out;
    }
    std::string msg = trim(e["message"].asString());
    if (msg.empty() && e.isString()) msg = e.asString();
    if (e["details"].isString() && !e["details"].asString().empty()) msg += " (" + trim(e["details"].asString()) + ")";
    return "ComfyUI rejected the workflow: " + (msg.empty() ? trim(body.substr(0, 300)) : msg);
}

int queuePosition(const Json& queue, const std::string& promptId) {
    for (const auto& item : queue["queue_running"].items())
        if (item[1].asString() == promptId) return 0;
    double ours = -1;
    for (const auto& item : queue["queue_pending"].items())
        if (item[1].asString() == promptId) ours = item[0].asNumber();
    if (ours < 0) return -1;
    int position = 1;
    for (const auto& item : queue["queue_pending"].items())
        if (item[0].asNumber() < ours) ++position;
    return position;
}

// --- Running -----------------------------------------------------------------

GeneratorResult run(const AiSettings& s, const std::string& workflowPath, const std::string& imagePath,
                    const std::string& prompt, const std::string& outputStem, GenerationProgress* p,
                    std::optional<uint64_t> seed) {
    if (!aiEnabled()) return {false, {}, aiOffError()};
    GeneratorResult result;
    const bool text = imagePath.empty();
    const std::string kind = text ? "text" : "image";
    const std::string base = normalizeUrl(s.comfyUrl);

    std::string path = workflowPath;
    if (path.empty()) path = text ? s.comfyTextWorkflow : s.comfyImageWorkflow;
    if (path.empty()) path = defaultWorkflow(kind);
    if (path.empty()) {
        result.error = std::string("Choose a ComfyUI workflow for ") + (text ? "text" : "image") +
                       " to 3D (exported with Workflow > Export (API)) - see docs/comfyui.md";
        return result;
    }
    Json workflow;
    if (!loadWorkflow(path, workflow, result.error)) return result;

    status(p, "Connecting to ComfyUI at " + base + "...");
    ServerInfo info;
    if (!checkServer(base, info, result.error, 10)) return result;

    ContractInputs in;
    in.prompt = prompt;
    in.seed = seed ? seed : std::optional<uint64_t>(randomSeed());
    if (!text) {
        std::string png;
        if (!prepareImagePng(imagePath, png, result.error)) return result;
        status(p, "Uploading the image...");
        std::string contentType;
        const std::string body = multipartBody({{"image", png, "sculptforge_" + randomId() + ".png", "image/png"},
                                                {"type", "input", {}, {}},
                                                {"overwrite", "true", {}, {}}},
                                               contentType);
        HttpResponse r = httpRequest("POST", base + "/upload/image", {contentType}, body, 120);
        Json up;
        std::string err;
        if (!r.error.empty()) {
            result.error = "Image upload to ComfyUI failed: " + r.error;
            return result;
        }
        if (r.status != 200 || !Json::parse(r.body, up, err) || !up["name"].isString()) {
            result.error = "Image upload to ComfyUI failed (HTTP " + std::to_string(r.status) + "): " + trim(r.body.substr(0, 200));
            return result;
        }
        in.image = up["subfolder"].asString().empty() ? up["name"].asString()
                                                      : up["subfolder"].asString() + "/" + up["name"].asString();
    }
    if (p && p->cancel) {
        result.error = "Cancelled";
        return result;
    }
    if (!applyContract(workflow, in, result.error)) return result;

    Json request = Json::object();
    request.set("prompt", workflow);
    request.set("client_id", "sculptforge-" + randomId());
    status(p, "Queueing the workflow...");
    HttpResponse r = httpPostJson(base + "/prompt", {"Content-Type: application/json"}, request.dump(), 60);
    if (!r.error.empty()) {
        result.error = "Lost the connection to ComfyUI: " + r.error;
        return result;
    }
    Json queued;
    std::string err;
    if (r.status != 200 || !Json::parse(r.body, queued, err) || !queued["prompt_id"].isString()) {
        result.error = promptError(r.body, workflow);
        // List every missing node type at once, not only the first one ComfyUI complained about.
        if (r.body.find("missing_node_type") != std::string::npos) {
            HttpResponse oi = httpRequest("GET", base + "/object_info", {}, {}, 60);
            Json objectInfo;
            if (oi.status == 200 && Json::parse(oi.body, objectInfo, err)) {
                const auto missing = missingNodeTypes(workflow, objectInfo);
                if (missing.size() > 1) {
                    std::string list;
                    for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
                    result.error = "This ComfyUI is missing the nodes " + list +
                                   ". Install the custom node packs that provide them (ComfyUI Manager > Install "
                                   "Missing Custom Nodes) and restart ComfyUI";
                }
            }
        }
        return result;
    }
    const std::string id = queued["prompt_id"].asString();

    const auto start = std::chrono::steady_clock::now();
    auto lastSeen = start;
    int lastPosition = -1;
    HistoryEntry done;
    Json history;
    while (true) {
        if (!wait(p, 1000)) {
            // Free the server: drop it from the queue, or stop it if it is the one running.
            Json del = Json::object();
            Json ids = Json::array();
            ids.push(id);
            del.set("delete", ids);
            httpPostJson(base + "/queue", {"Content-Type: application/json"}, del.dump(), 10);
            HttpResponse q = httpRequest("GET", base + "/queue", {}, {}, 10);
            Json queue;
            if (lastPosition == 0 || (Json::parse(q.body, queue, err) && queuePosition(queue, id) == 0)) {
                Json body = Json::object();
                body.set("prompt_id", id);
                httpPostJson(base + "/interrupt", {"Content-Type: application/json"}, body.dump(), 10);
            }
            result.error = "Cancelled";
            return result;
        }
        const auto now = std::chrono::steady_clock::now();
        const int secs = int(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
        if (now - start > std::chrono::minutes(kMaxMinutes)) {
            result.error = "Timed out after " + std::to_string(kMaxMinutes) + " minutes waiting for ComfyUI";
            return result;
        }
        HttpResponse h = httpRequest("GET", base + "/history/" + id, {}, {}, 30);
        if (h.status == 200 && Json::parse(h.body, history, err)) {
            done = parseHistory(history, id);
            if (done.found && (done.completed || done.failed)) break;
        }
        HttpResponse q = httpRequest("GET", base + "/queue", {}, {}, 30);
        Json queue;
        if (q.error.empty() && q.status == 200 && Json::parse(q.body, queue, err)) {
            lastPosition = queuePosition(queue, id);
            if (lastPosition >= 0 || done.found) lastSeen = now;
        }
        if (now - lastSeen > std::chrono::seconds(kGiveUpSeconds)) {
            result.error = !q.error.empty() ? "Lost the connection to ComfyUI at " + base + ": " + q.error
                                            : "The job disappeared from ComfyUI's queue (cancelled there, or the server restarted)";
            return result;
        }
        if (lastPosition == 0) status(p, "Running the workflow on ComfyUI... " + std::to_string(secs) + "s");
        else if (lastPosition > 0)
            status(p, "Waiting in ComfyUI's queue (position " + std::to_string(lastPosition) + ")... " + std::to_string(secs) + "s");
    }
    if (done.failed) {
        result.error = "ComfyUI: " + done.error;
        return result;
    }
    const OutputFile* mesh = firstMesh(done.files);
    if (!mesh) {
        result.error = "The workflow finished but saved no mesh file (.glb, .obj, .ply, .stl, .fbx...). It produced: " +
                       describeOutputs(history, id) + ". End it with a node that saves the mesh, such as Save GLB";
        return result;
    }

    status(p, "Downloading " + mesh->filename + "...");
    HttpResponse d = httpRequest("GET", viewUrl(base, *mesh), {}, {}, 300);
    if (!d.error.empty() || d.status != 200 || d.body.empty()) {
        result.error = "Could not download " + mesh->filename + " from ComfyUI: " +
                       (d.error.empty() ? "HTTP " + std::to_string(d.status) : d.error);
        return result;
    }
    std::string ext = lower(u8path(mesh->filename).extension().string());
    const std::string out = outputStem + ext;
    std::ofstream f(u8path(out), std::ios::binary);
    if (!f || !f.write(d.body.data(), std::streamsize(d.body.size()))) {
        result.error = "Cannot write " + out;
        return result;
    }
    result.ok = true;
    result.outputPath = out;
    return result;
}

std::vector<BundledWorkflow> bundledWorkflows() {
    std::vector<std::filesystem::path> roots;
    if (const std::string exe = executableDir(); !exe.empty()) {
        const auto dir = u8path(exe);
        roots.push_back(dir / ".." / "share" / "sculptforge" / "comfyui");
        roots.push_back(dir / ".." / "Resources" / "comfyui"); // macOS app bundle
        roots.push_back(dir / "comfyui");                      // Windows zip
        roots.push_back(dir / ".." / "integrations" / "comfyui"); // build/ inside a checkout
    }
#ifdef SF_SOURCE_DIR
    roots.push_back(std::filesystem::path(SF_SOURCE_DIR) / "integrations" / "comfyui");
#endif
    std::vector<BundledWorkflow> out;
    std::set<std::string> names;
    for (const auto& root : roots) {
        for (const auto& dir : {root / "workflows", root}) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) continue;
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".json") files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                const std::string name = pathString(file.stem());
                if (names.count(name)) continue;
                Json wf;
                std::string err;
                if (!loadWorkflow(pathString(file), wf, err)) continue; // UI-format copies, notes...
                const std::string kind = workflowKind(wf);
                if (kind.empty()) continue;
                names.insert(name);
                std::error_code cec;
                const auto canonical = std::filesystem::weakly_canonical(file, cec);
                out.push_back({name, pathString(cec ? file : canonical), kind});
            }
        }
    }
    return out;
}

std::string defaultWorkflow(const std::string& kind, const std::vector<std::string>& preferred) {
    const auto all = bundledWorkflows();
    for (const auto& want : preferred)
        for (const auto& w : all)
            if (w.kind == kind && lower(w.name).find(lower(want)) != std::string::npos) return w.path;
    for (const auto& w : all)
        if (w.kind == kind) return w.path;
    return {};
}

} // namespace sf::ai::comfy
