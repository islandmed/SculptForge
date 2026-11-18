// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/RenderBridge.h"

#include "core/BrushLibrary.h"
#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "core/Parts.h"
#include "core/Sha256.h"
#include "io/UsdIO.h"

#include "tinyexr.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>

namespace sf::renderbridge {

namespace fs = std::filesystem;

namespace {

fs::path u8path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool readFile(const std::string& path, std::string& out, size_t limit = size_t(-1)) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    if (limit == size_t(-1)) {
        ss << f.rdbuf();
        out = ss.str();
    } else {
        out.resize(limit);
        f.read(out.data(), std::streamsize(limit));
        out.resize(size_t(f.gcount()));
    }
    return true;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// Numbers as USD text: short, and never "nan" (a broken camera is clamped before this).
std::string num(double v) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}
std::string tuple(const glm::vec4& v) {
    return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ", " + num(v.w) + ")";
}
// USD writes matrices row by row with row vectors: its rows are glm's columns.
std::string matrix(const glm::mat4& m) {
    return "( " + tuple(m[0]) + ", " + tuple(m[1]) + ", " + tuple(m[2]) + ", " + tuple(m[3]) + " )";
}

// A light at `cameraToWorld`'s origin shining along `travel` (world), as a transform whose -Z is that direction.
glm::mat4 aimed(const glm::vec3& position, glm::vec3 travel) {
    travel = glm::normalize(travel);
    const glm::vec3 z = -travel;
    glm::vec3 up = std::abs(z.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 x = glm::normalize(glm::cross(up, z));
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(position, 1.0f);
    return m;
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

} // namespace

// --- View ---

View framedView(const glm::vec3& min, const glm::vec3& max, float aspect) {
    View v;
    const glm::vec3 centre = (min + max) * 0.5f;
    const float radius = std::max(glm::length(max - min) * 0.5f, 1e-3f);
    const float fovX = 2.0f * std::atan(std::tan(v.fovY * 0.5f) * std::max(aspect, 1e-3f));
    const float distance = radius / std::sin(std::min(v.fovY, fovX) * 0.5f) * 1.05f;
    const glm::vec3 dir = glm::normalize(glm::vec3(0.45f, 0.25f, 1.0f)); // from the front, a little right and above
    const glm::mat4 view = glm::lookAt(centre + dir * distance, centre, glm::vec3(0, 1, 0));
    v.cameraToWorld = glm::inverse(view);
    v.orthoHalfHeight = radius;
    v.nearClip = distance * 0.01f;
    v.farClip = distance * 100.0f;
    return v;
}

// --- The stage ---

std::string stageText(const Request& r, const std::string& sceneFile) {
    const int w = std::clamp(r.width, 16, 16384), h = std::clamp(r.height, 16, 16384);
    const double aspect = double(w) / double(h);
    const View& v = r.view;
    // Apertures and focal length in USD's camera units (tenths of a scene unit); only their ratios matter for a
    // perspective camera, the apertures are the frame's size for an orthographic one.
    const double focal = 50.0;
    const double fov = std::clamp(double(v.fovY), 0.01, 3.0);
    const double vAperture = v.ortho ? 20.0 * std::max(double(v.orthoHalfHeight), 1e-4) : 2.0 * focal * std::tan(fov * 0.5);
    const double hAperture = vAperture * aspect;
    // The viewport's lens shift moves the image by `shift` in clip space; the aperture window moves the other way.
    const double hOffset = -double(v.shift.x) * hAperture * 0.5, vOffset = -double(v.shift.y) * vAperture * 0.5;
    const double nearClip = std::max(double(v.nearClip), 1e-5), farClip = std::max(double(v.farClip), nearClip * 2.0);

    std::string s;
    s += "#usda 1.0\n(\n";
    s += "    defaultPrim = \"SculptForgeRender\"\n";
    s += "    doc = \"SculptForge render bridge\"\n";
    s += "    metersPerUnit = 1\n";
    s += "    upAxis = \"Y\"\n";
    s += "    subLayers = [\n        @./" + sceneFile + "@\n    ]\n";
    s += ")\n\n";
    s += "def Xform \"SculptForgeRender\"\n{\n";
    s += "    def Camera \"Camera\"\n    {\n";
    s += "        matrix4d xformOp:transform = " + matrix(v.cameraToWorld) + "\n";
    s += "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
    s += std::string("        token projection = \"") + (v.ortho ? "orthographic" : "perspective") + "\"\n";
    s += "        float focalLength = " + num(focal) + "\n";
    s += "        float horizontalAperture = " + num(hAperture) + "\n";
    s += "        float verticalAperture = " + num(vAperture) + "\n";
    s += "        float horizontalApertureOffset = " + num(hOffset) + "\n";
    s += "        float verticalApertureOffset = " + num(vOffset) + "\n";
    s += "        float2 clippingRange = (" + num(nearClip) + ", " + num(farClip) + ")\n";
    s += "    }\n";
    if (r.studioLight) {
        // Three lights that turn with the camera (as the viewport's studio light does) and a soft white dome.
        const glm::mat3 rot(v.cameraToWorld);
        const glm::vec3 eye(v.cameraToWorld[3]);
        struct L { const char* name; glm::vec3 from; double intensity; glm::vec3 colour; };
        const L lights[] = {{"Key", {-0.8f, 0.9f, 1.0f}, 2.2, {1.0f, 0.97f, 0.92f}},
                            {"Fill", {1.0f, 0.1f, 0.8f}, 0.6, {0.9f, 0.94f, 1.0f}},
                            {"Rim", {0.3f, 0.8f, -1.0f}, 1.4, {1.0f, 1.0f, 1.0f}}};
        for (const L& l : lights) {
            const glm::vec3 travel = rot * -glm::normalize(l.from); // camera space -> world
            s += std::string("\n    def DistantLight \"") + l.name + "\"\n    {\n";
            s += "        matrix4d xformOp:transform = " + matrix(aimed(eye, travel)) + "\n";
            s += "        uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
            s += "        float inputs:intensity = " + num(l.intensity) + "\n";
            s += "        float inputs:angle = 2\n";
            s += "        color3f inputs:color = (" + num(l.colour.x) + ", " + num(l.colour.y) + ", " + num(l.colour.z) + ")\n";
            s += "    }\n";
        }
        s += "\n    def DomeLight \"Sky\"\n    {\n";
        s += "        float inputs:intensity = 0.35\n";
        s += "        color3f inputs:color = (1, 1, 1)\n";
        s += "    }\n";
    }
    s += "\n    def RenderSettings \"Settings\"\n    {\n";
    s += std::string("        rel camera = <") + kCameraPath + ">\n";
    s += "        uniform int2 resolution = (" + std::to_string(w) + ", " + std::to_string(h) + ")\n";
    s += "        uniform float pixelAspectRatio = 1\n";
    s += "        uniform token aspectRatioConformPolicy = \"expandAperture\"\n";
    s += "        uniform token[] includedPurposes = [\"default\", \"render\"]\n";
    s += "    }\n";
    s += "}\n";
    return s;
}

// --- The helper ---

HelperInfo parseHelp(const std::string& help) {
    HelperInfo info;
    // argparse prints "--renderer {Storm,Embree}" (in the usage line and the option list; either will do).
    for (const char* flag : {"--renderer {", "-r {"}) {
        const size_t at = help.find(flag);
        if (at == std::string::npos) continue;
        const size_t open = at + std::char_traits<char>::length(flag);
        const size_t close = help.find('}', open);
        if (close == std::string::npos) continue;
        std::stringstream list(help.substr(open, close - open));
        std::string name;
        while (std::getline(list, name, ',')) {
            name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
            if (validRendererName(name) && std::find(info.renderers.begin(), info.renderers.end(), name) == info.renderers.end())
                info.renderers.push_back(name);
        }
        break;
    }
    info.renderSettings = contains(help, "--renderSettingsPrimPath");
    info.disableCameraLight = contains(help, "--disableCameraLight");
    info.ok = contains(help, "--imageWidth") && contains(help, "--camera");
    if (!info.ok) info.error = "This program doesn't take usdrecord's options (--camera, --imageWidth)";
    return info;
}

bool validRendererName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '.' || c == '-')) return false;
    return name.front() != '-'; // (never an option)
}

std::vector<std::string> helperArgs(const Request& r, const HelperInfo& info, const std::string& stage,
                                    const std::string& out) {
    std::vector<std::string> a = {"--camera", kCameraPath, "--imageWidth", std::to_string(std::clamp(r.width, 16, 16384))};
    if (!r.renderer.empty()) a.insert(a.end(), {"--renderer", r.renderer});
    if (info.renderSettings) a.insert(a.end(), {"--renderSettingsPrimPath", kSettingsPath});
    if (r.studioLight && info.disableCameraLight) a.push_back("--disableCameraLight"); // our rig replaces the headlight
    a.push_back(stage);
    a.push_back(out);
    return a;
}

bool launchFor(const std::string& helper, std::string& program, std::vector<std::string>& prefixArgs, std::string& error) {
    prefixArgs.clear();
    std::error_code ec;
    const fs::path p = u8path(helper);
    if (helper.empty() || !p.is_absolute() || !fs::is_regular_file(p, ec)) {
        error = helper.empty() ? "No render helper is set up" : "The render helper is missing: " + helper;
        return false;
    }
    std::string ext = pathString(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".cmd" || ext == ".bat") {
        error = "Batch files can't be render helpers (choose the usdrecord script or a program instead)";
        return false;
    }
    program = helper;
#if defined(_WIN32)
    if (ext != ".exe" && ext != ".com") {
        std::string head;
        readFile(helper, head, 256);
        if (head.rfind("#!", 0) != 0 || head.find("python") == std::string::npos) {
            error = "The render helper must be a program (.exe) or a Python script such as usdrecord";
            return false;
        }
        // The Python next to the script (an OpenUSD install's bin folder, its parent or the prebuilt downloads'
        // <root>\python), else the one on PATH.
        for (const fs::path& candidate : {p.parent_path() / "python.exe", p.parent_path().parent_path() / "python.exe",
                                          p.parent_path().parent_path() / "python" / "python.exe"})
            if (fs::is_regular_file(candidate, ec)) {
                program = pathString(candidate);
                break;
            }
        if (program == helper) program = ai::findOnPath("python");
        if (program.empty()) {
            error = "The render helper is a Python script and no python.exe was found next to it or on PATH";
            return false;
        }
        prefixArgs.push_back(helper);
    }
#endif
    return true;
}

std::vector<std::string> cleanEnvironment(const std::vector<std::string>& envIn) {
    std::vector<std::string> out;
    for (const std::string& e : envIn) {
        const size_t eq = e.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string name = e.substr(0, eq);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        const bool ours = name.rfind("SF_", 0) == 0 || name.rfind("SCULPTFORGE", 0) == 0;
        const bool secret = contains(name, "TOKEN") || contains(name, "SECRET") || contains(name, "PASSWORD") ||
                            contains(name, "PASSWD") || contains(name, "CREDENTIAL") ||
                            (name.size() > 4 && name.compare(name.size() - 4, 4, "_KEY") == 0) || contains(name, "API_KEY");
        if (!ours && !secret) out.push_back(e);
    }
    return out;
}

std::vector<std::string> installEnvironment(const std::vector<std::string>& envIn, const std::string& helper) {
    std::error_code ec;
    const fs::path bin = u8path(helper).parent_path();
    const fs::path root = bin.parent_path();
    if (pathString(bin.filename()) != "bin" || !fs::is_directory(root / "lib" / "python" / "pxr", ec)) return envIn;
#if defined(_WIN32)
    const char sep = ';';
    auto same = [](const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; };
#else
    const char sep = ':';
    auto same = [](const std::string& a, const char* b) { return a == b; };
#endif
    auto join = [&](std::initializer_list<fs::path> dirs) {
        std::string s;
        for (const fs::path& d : dirs)
            if (fs::is_directory(d, ec)) s += (s.empty() ? "" : std::string(1, sep)) + pathString(d);
        return s;
    };
    const std::pair<const char*, std::string> front[] = {
        {"PATH", join({root / "python", root / "python" / "bin", root / "pip-packages" / "bin", root / "lib",
                       root / "plugin" / "usd", bin})},
        {"PYTHONPATH", join({root / "lib" / "python", root / "pip-packages"})},
#if defined(__linux__)
        {"LD_LIBRARY_PATH", join({root / "lib"})},
#endif
    };
    std::vector<std::string> env = envIn;
    for (const auto& [name, dirs] : front) {
        bool found = false;
        for (std::string& e : env) {
            const size_t eq = e.find('=');
            if (eq == std::string::npos || !same(e.substr(0, eq), name)) continue;
            const std::string rest = e.substr(eq + 1);
            e = e.substr(0, eq + 1) + dirs + (rest.empty() ? "" : std::string(1, sep) + rest);
            found = true;
            break;
        }
        if (!found) env.push_back(std::string(name) + "=" + dirs);
    }
    const fs::path mtlx = root / "libraries";
    bool hasMtlx = false;
    for (const std::string& e : env)
        if (e.rfind("PXR_MTLX_STDLIB_SEARCH_PATHS=", 0) == 0) hasMtlx = true;
    if (!hasMtlx && fs::is_directory(mtlx, ec)) env.push_back("PXR_MTLX_STDLIB_SEARCH_PATHS=" + pathString(mtlx));
    return env;
}

std::string fileFingerprint(const std::string& path) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return {};
    std::error_code ec;
    const auto size = fs::file_size(u8path(path), ec);
    if (ec) return {};
    Sha256 h;
    std::vector<char> buf(1 << 20);
    uint64_t limit = size > (512ull << 20) ? (64ull << 20) : size;
    if (limit != size) {
        const std::string s = "size:" + std::to_string(size) + ";";
        h.update(s.data(), s.size());
    }
    while (limit > 0 && f) {
        f.read(buf.data(), std::streamsize(std::min<uint64_t>(limit, buf.size())));
        const auto n = f.gcount();
        if (n <= 0) break;
        h.update(buf.data(), size_t(n));
        limit -= uint64_t(n);
    }
    return h.hex();
}

std::string jobRoot() {
    if (const std::string o = env("SF_RENDER_BRIDGE_DIR"); !o.empty()) return o;
    fs::path base;
#if defined(_WIN32)
    base = u8path(env("LOCALAPPDATA")); // (the per-user, non-roaming folder: its ACL is the user's alone)
    if (base.empty()) return {};
    base /= "SculptForge";
#elif defined(__APPLE__)
    if (env("HOME").empty()) return {};
    base = u8path(env("HOME")) / "Library" / "Caches" / "SculptForge";
#else
    if (const std::string x = env("XDG_CACHE_HOME"); !x.empty()) base = u8path(x);
    else if (!env("HOME").empty()) base = u8path(env("HOME")) / ".cache";
    else return {};
    base /= "SculptForge";
#endif
    return pathString(base / "render-bridge");
}

std::string foundHelper() {
#if defined(_WIN32)
    // OpenUSD's Windows installs put usdrecord (the script) and usdrecord.cmd in bin: the script is what we run.
    const std::string cmd = ai::findOnPath("usdrecord.cmd");
    if (!cmd.empty()) {
        const fs::path script = u8path(cmd).parent_path() / "usdrecord";
        std::error_code ec;
        if (fs::is_regular_file(script, ec)) return pathString(script);
    }
    const std::string exe = ai::findOnPath("usdrecord.exe");
    return exe;
#else
    const std::string p = ai::findOnPath("usdrecord");
    if (p.empty()) return {};
    std::error_code ec;
    const fs::path abs = fs::absolute(u8path(p), ec);
    return ec ? p : pathString(abs);
#endif
}

bool loadImage(const std::string& path, Image& out, std::string& error) {
    std::string ext = pathString(u8path(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    constexpr int kMaxSide = 16384;
    if (ext != ".exr") {
        std::string bytes;
        if (!readFile(path, bytes)) return (error = "Can't read " + path, false);
        int w = 0, h = 0;
        if (Image::dimensions(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), w, h) &&
            (w > kMaxSide || h > kMaxSide))
            return (error = "The render is larger than 16384 pixels a side", false);
        return Image::decode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), out, error);
    }
    std::string bytes;
    if (!readFile(path, bytes)) return (error = "Can't read " + path, false);
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    EXRVersion version;
    if (ParseEXRVersionFromMemory(&version, data, bytes.size()) != TINYEXR_SUCCESS || version.multipart)
        return (error = "Not a single-part OpenEXR image: " + path, false);
    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    if (ParseEXRHeaderFromMemory(&header, &version, data, bytes.size(), &err) != TINYEXR_SUCCESS) {
        error = std::string("Can't read the EXR header: ") + (err ? err : "unknown error");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    const long long w = (long long)header.data_window.max_x - header.data_window.min_x + 1;
    const long long h = (long long)header.data_window.max_y - header.data_window.min_y + 1;
    FreeEXRHeader(&header);
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) return (error = "The EXR's size is out of range", false);
    float* rgba = nullptr;
    int iw = 0, ih = 0;
    if (LoadEXRFromMemory(&rgba, &iw, &ih, data, bytes.size(), &err) != TINYEXR_SUCCESS) {
        error = std::string("Can't read the EXR: ") + (err ? err : "unknown error");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    out.width = iw;
    out.height = ih;
    out.rgba.resize(size_t(iw) * size_t(ih) * 4);
    auto srgb = [](float c) {
        c = std::isfinite(c) ? std::clamp(c, 0.0f, 1.0f) : 0.0f;
        const float s = c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return uint8_t(std::lround(s * 255.0f));
    };
    for (size_t i = 0; i < size_t(iw) * size_t(ih); ++i) {
        for (int c = 0; c < 3; ++c) out.rgba[i * 4 + c] = srgb(rgba[i * 4 + c]);
        const float a = std::isfinite(rgba[i * 4 + 3]) ? std::clamp(rgba[i * 4 + 3], 0.0f, 1.0f) : 1.0f;
        out.rgba[i * 4 + 3] = uint8_t(std::lround(a * 255.0f));
    }
    std::free(rgba);
    return true;
}

std::string logTail(const std::string& path, int lines) {
    std::string text;
    if (!readFile(path, text)) return {};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    size_t at = text.size();
    for (int n = 0; n < lines && at != std::string::npos && at > 0; ++n) at = text.rfind('\n', at - 1);
    std::string tail = at == std::string::npos ? text : text.substr(at + 1);
    if (tail.size() > 1200) tail = "..." + tail.substr(tail.size() - 1200);
    return tail;
}

// --- Settings ---

std::string defaultSettingsPath() { return pathString(u8path(appDataDir()) / "render_bridge.json"); }

std::string defaultRendersDir() {
    fs::path pictures;
#if defined(_WIN32)
    if (!env("USERPROFILE").empty()) pictures = u8path(env("USERPROFILE")) / "Pictures";
#else
    if (!env("HOME").empty()) pictures = u8path(env("HOME")) / "Pictures";
#endif
    std::error_code ec;
    if (!pictures.empty() && fs::is_directory(pictures, ec)) return pathString(pictures / "SculptForge Renders");
    return pathString(u8path(appDataDir()) / "Renders");
}

Settings Settings::load(const std::string& path) {
    Settings s;
    std::string text, error;
    Json j;
    if (!readFile(path, text) || !Json::parse(text, j, error) || !j.isObject()) return s;
    auto str = [&](const char* key, std::string& v) {
        if (j[key].isString()) v = j[key].asString();
    };
    str("helper", s.helper);
    str("helper_sha256", s.helperSha256);
    str("editor", s.editor);
    str("renders_dir", s.rendersDir);
    str("renderer", s.renderer);
    str("format", s.format);
    if (s.format != "png" && s.format != "exr") s.format = "png";
    if (!validRendererName(s.renderer)) s.renderer.clear();
    s.width = std::clamp(int(j["width"].asNumber(s.width)), 16, 16384);
    s.height = std::clamp(int(j["height"].asNumber(s.height)), 16, 16384);
    s.studioLight = j["studio_light"].asBool(true);
    s.openInEditor = j["open_in_editor"].asBool(false);
    s.matchView = j["match_view"].asBool(true);
    return s;
}

bool Settings::save(const std::string& path) const {
    Json j = Json::object();
    j.set("helper", helper);
    j.set("helper_sha256", helperSha256);
    j.set("editor", editor);
    j.set("renders_dir", rendersDir);
    j.set("renderer", renderer);
    j.set("width", double(width));
    j.set("height", double(height));
    j.set("format", format);
    j.set("studio_light", studioLight);
    j.set("open_in_editor", openInEditor);
    j.set("match_view", matchView);
    std::error_code ec;
    fs::create_directories(u8path(path).parent_path(), ec);
    std::ofstream f(u8path(path), std::ios::binary);
    f << j.dump(true);
    return bool(f);
}

bool Settings::helperTrusted() const {
    return !helper.empty() && !helperSha256.empty() && fileFingerprint(helper) == helperSha256;
}

// --- Running it ---

namespace {

bool startHelper(ai::ChildProcess& child, const std::string& helper, const std::vector<std::string>& args,
                 const std::string& log, const std::vector<std::string>& envEntries, std::string& error) {
    std::string program;
    std::vector<std::string> all;
    if (!launchFor(helper, program, all, error)) return false;
    all.insert(all.end(), args.begin(), args.end());
    child.setEnvironment(installEnvironment(cleanEnvironment(envEntries), helper));
    return child.start(program, all, log, {}, error);
}

std::string makeFolder(std::string& error) {
    const std::string root = jobRoot();
    if (root.empty()) return (error = "No folder for render jobs (no home folder)", std::string());
    std::error_code ec;
    fs::create_directories(u8path(root), ec);
#if !defined(_WIN32)
    fs::permissions(u8path(root), fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
    // Keep the newest few jobs (their logs help when a render fails); the rest go.
    std::vector<fs::path> old;
    for (fs::directory_iterator it(u8path(root), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && pathString(it->path().filename()).rfind("job-", 0) == 0) old.push_back(it->path());
    std::sort(old.begin(), old.end());
    for (size_t i = 0; i + 4 < old.size(); ++i) fs::remove_all(old[i], ec);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    for (int k = 0; k < 100; ++k) {
        char name[64];
        std::snprintf(name, sizeof name, "job-%013lld-%02d", (long long)stamp, k);
        const fs::path dir = u8path(root) / name;
        if (fs::create_directory(dir, ec)) {
#if !defined(_WIN32)
            fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
            return pathString(dir);
        }
    }
    error = "Can't create a render job folder in " + root;
    return {};
}

} // namespace

HelperInfo probe(const std::string& helper, const std::vector<std::string>& envEntries) {
    HelperInfo info;
    std::string err;
    const std::string folder = makeFolder(err);
    if (folder.empty()) return (info.error = err, info);
    const std::string log = pathString(u8path(folder) / "help.log");
    ai::ChildProcess child;
    if (!startHelper(child, helper, {"--help"}, log, envEntries, err)) return (info.error = err, info);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (child.running() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (child.running()) {
        child.kill();
        info.error = "The render helper didn't answer --help within 20 seconds";
        return info;
    }
    std::string text;
    readFile(log, text);
    info = parseHelp(text);
    if (!info.ok) {
        const std::string tail = logTail(log, 4);
        info.error = child.exitCode() != 0 && !tail.empty() ? "The render helper failed to start: " + tail : info.error;
    }
    return info;
}

Job::~Job() {
    if (prepare_.valid()) prepare_.wait(); // (the scene file is being written: it ends by itself)
    if (child_) child_->kill();
}

bool Job::start(const std::string& helper, const HelperInfo& info, const Request& r, const std::vector<MeshObjectRef>& meshes,
                 const std::string& name, const std::vector<std::string>& envEntries, std::string& error) {
    if (state_ == State::Running) return (error = "A render is already running", false);
    error_.clear();
    image_.clear();
    if (meshes.empty()) return (error = "Nothing visible to render", false);
    if (!r.renderer.empty() && (!validRendererName(r.renderer) ||
                                std::find(info.renderers.begin(), info.renderers.end(), r.renderer) == info.renderers.end()))
        return (error = "The render helper doesn't list the renderer \"" + r.renderer + "\"", false);
    std::string program;
    std::vector<std::string> prefix;
    if (!launchFor(helper, program, prefix, error)) return false; // (before anything is written)
    folder_ = makeFolder(error);
    if (folder_.empty()) return false;
    const fs::path dir = u8path(folder_);
    image_ = pathString(dir / (r.format == "exr" ? "render.exr" : "render.png"));
    log_ = pathString(dir / "render.log");
    // The scene is written on a worker from copies of the meshes (text USD of a big sculpt takes seconds; the view
    // stays responsive and the sculpt can change meanwhile), then the helper starts there.
    std::vector<MeshObjectRef> copies = meshes;
    for (MeshObjectRef& m : copies)
        if (m.mesh && m.instanceOf < 0) {
            m.owned = std::make_shared<const Mesh>(*m.mesh);
            m.mesh = m.owned.get();
        }
    for (MeshObjectRef& m : copies) // a linked copy points at its source's copy
        if (m.instanceOf >= 0 && size_t(m.instanceOf) < copies.size()) m.mesh = copies[size_t(m.instanceOf)].mesh;
    child_ = std::make_unique<ai::ChildProcess>();
    ai::ChildProcess* child = child_.get();
    const std::vector<std::string> args = helperArgs(r, info, pathString(dir / "render.usda"), image_);
    const std::string stage = stageText(r, "scene.usda"), log = log_;
    prepare_ = std::async(std::launch::async, [=, copies = std::move(copies)]() -> std::string {
        std::string err;
        if (!saveUsd(pathString(dir / "scene.usda"), copies, nullptr, name, err)) return err;
        {
            std::ofstream f(dir / "render.usda", std::ios::binary);
            f << stage;
            if (!f) return "Can't write the render stage in " + pathString(dir);
        }
        if (!startHelper(*child, helper, args, log, envEntries, err)) return err;
        return {};
    });
    started_ = std::chrono::steady_clock::now();
    state_ = State::Running;
    return true;
}

State Job::poll() {
    if (state_ != State::Running || !child_) return state_;
    if (prepare_.valid()) {
        if (prepare_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return state_;
        const std::string err = prepare_.get();
        if (!err.empty()) {
            ended_ = std::chrono::steady_clock::now();
            state_ = State::Failed;
            error_ = err;
            child_.reset();
            return state_;
        }
    }
    if (child_->running()) {
        if (elapsedSeconds() > timeLimitSeconds) {
            child_->kill();
            state_ = State::Failed;
            ended_ = std::chrono::steady_clock::now();
            error_ = "The render took longer than " + std::to_string(int(timeLimitSeconds / 60)) + " minutes and was stopped";
        }
        return state_;
    }
    ended_ = std::chrono::steady_clock::now();
    const int code = child_->exitCode();
    std::error_code ec;
    const bool wrote = fs::is_regular_file(u8path(image_), ec) && fs::file_size(u8path(image_), ec) > 0;
    if (code == 0 && wrote) {
        state_ = State::Done;
    } else {
        state_ = State::Failed;
        const std::string tail = logTail(log_);
        error_ = code != 0 ? "The renderer stopped with exit code " + std::to_string(code) : "The renderer wrote no image";
        if (!tail.empty()) error_ += ":\n" + tail;
    }
    child_.reset();
    return state_;
}

void Job::cancel() {
    if (state_ != State::Running) return;
    if (prepare_.valid()) prepare_.wait(); // (writing the scene: a moment, then the helper it started is stopped)
    if (child_) child_->kill();
    child_.reset();
    ended_ = std::chrono::steady_clock::now();
    state_ = State::Cancelled;
}

double Job::elapsedSeconds() const {
    if (state_ == State::Idle) return 0.0;
    const auto end = state_ == State::Running ? std::chrono::steady_clock::now() : ended_;
    return std::chrono::duration<double>(end - started_).count();
}

std::string keepRender(const std::string& image, const std::string& dir, const std::string& name, std::string& error) {
    std::error_code ec;
    fs::create_directories(u8path(dir), ec);
    if (!fs::is_directory(u8path(dir), ec)) return (error = "Can't create " + dir, std::string());
    std::string base = usdIdentifier(name.empty() ? "render" : name); // (letters, digits and '_' only)
    const std::string ext = pathString(u8path(image).extension());
    for (int k = 1; k < 100000; ++k) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, "_%04d", k);
        const fs::path target = u8path(dir) / u8path(base + suffix + ext);
        if (fs::exists(target, ec)) continue;
        if (!fs::copy_file(u8path(image), target, fs::copy_options::none, ec))
            return (error = "Can't save the render in " + dir + ": " + ec.message(), std::string());
        return pathString(target);
    }
    error = "Too many renders in " + dir;
    return {};
}

// --- sculptforge-cli ---

void registerCommands(CommandRegistry& registry, std::function<Settings()> settings, std::string overrideHelper) {
    using T = ParamSpec::Type;
    // (No helper parameter: commands are also MCP tools, and a tool must never name a program to run.)
    static const char* kNoHelper =
        "No render helper is set up (Preferences > Render bridge in the app, or sculptforge-cli --render-helper PATH)";
    static const char* kChanged = "The render helper changed since it was confirmed: confirm it again in Preferences > "
                                  "Render bridge (or sculptforge-cli --render-helper PATH)";
    registry.add(
        {"render_bridge",
         "Render a still of the visible Parts with a Hydra render delegate through the render helper set up in "
         "Preferences > Render bridge (OpenUSD's usdrecord): a three-quarter view from the front with the studio "
         "light. Runs in the foreground and saves the image to `path` (.png or .exr). The helper runs as its own "
         "program; a studio policy can turn this off.",
         {{"path", T::String, "Output image (.png or .exr).", true},
          {"renderer", T::String, "A renderer the helper lists (render_bridge_renderers). Default: the helper's.", false},
          {"width", T::Number, "Width in pixels. Default 1920.", false},
          {"height", T::Number, "Height in pixels. Default 1080.", false},
          {"light", T::Bool, "The studio light (key, fill, rim, dome). Default true.", false}},
         [settings, overrideHelper](Document& doc, const Args& a) -> CommandResult {
             std::string why;
             if (!policy::renderBridgeAllowed(&why)) return {false, why};
             const Settings s = settings();
             const std::string helper = overrideHelper.empty() ? s.helper : overrideHelper;
             if (helper.empty()) return {false, kNoHelper};
             if (overrideHelper.empty() && !s.helperTrusted()) return {false, kChanged};
             const std::string out = argString(a, "path");
             std::string ext = pathString(u8path(out).extension());
             std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
             if (ext != ".png" && ext != ".exr") return {false, "path must end in .png or .exr"};
             const std::vector<std::string> envEntries = ai::currentEnvironment();
             const HelperInfo info = probe(helper, envEntries);
             if (!info.ok) return {false, info.error};
             Request r;
             r.renderer = argString(a, "renderer", s.renderer);
             if (!r.renderer.empty() && std::find(info.renderers.begin(), info.renderers.end(), r.renderer) == info.renderers.end()) {
                 if (a.count("renderer")) return {false, "The render helper doesn't list the renderer \"" + r.renderer + "\""};
                 r.renderer.clear(); // (a saved choice the helper no longer has: its default)
             }
             r.width = std::clamp(int(argNumber(a, "width", s.width)), 16, 16384);
             r.height = std::clamp(int(argNumber(a, "height", s.height)), 16, 16384);
             r.format = ext == ".exr" ? "exr" : "png";
             r.studioLight = argBool(a, "light", s.studioLight);
             const auto objects = visiblePartObjects(doc, true);
             AABB box;
             for (const MeshObjectRef& o : objects) {
                 const AABB b = o.mesh->bounds();
                 for (int k = 0; k < 8; ++k)
                     box.expand(glm::vec3(o.transform * glm::vec4(k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y,
                                                                  k & 4 ? b.max.z : b.min.z, 1.0f)));
             }
             if (objects.empty()) return {false, "Nothing visible to render"};
             r.view = framedView(box.min, box.max, float(r.width) / float(r.height));
             Job job;
             std::string err;
             if (!job.start(helper, info, r, objects, doc.assetName(), envEntries, err)) return {false, err};
             while (job.poll() == State::Running) std::this_thread::sleep_for(std::chrono::milliseconds(50));
             if (job.state() != State::Done) return {false, job.error()};
             std::error_code ec;
             fs::create_directories(u8path(out).parent_path(), ec);
             fs::copy_file(u8path(job.image()), u8path(out), fs::copy_options::overwrite_existing, ec);
             if (ec) return {false, "Can't write " + out + ": " + ec.message()};
             char secs[32];
             std::snprintf(secs, sizeof secs, "%.1f s", job.elapsedSeconds());
             return {true, "Rendered " + out + " (" + std::to_string(r.width) + " x " + std::to_string(r.height) +
                               (r.renderer.empty() ? "" : ", " + r.renderer) + ", " + secs + ")"};
         }});
    registry.add({"render_bridge_renderers",
                  "List the renderers (Hydra render delegates) the render helper finds through its own plugin paths.",
                  {},
                  [settings, overrideHelper](Document&, const Args&) -> CommandResult {
                      std::string why;
                      if (!policy::renderBridgeAllowed(&why)) return {false, why};
                      const Settings s = settings();
                      const std::string helper = overrideHelper.empty() ? s.helper : overrideHelper;
                      if (helper.empty()) return {false, kNoHelper};
                      if (overrideHelper.empty() && !s.helperTrusted()) return {false, kChanged};
                      const HelperInfo info = probe(helper, ai::currentEnvironment());
                      if (!info.ok) return {false, info.error};
                      std::string list;
                      for (const std::string& n : info.renderers) list += (list.empty() ? "" : ", ") + n;
                      return {true, list.empty() ? "The helper's default renderer only" : list};
                  }});
}

} // namespace sf::renderbridge
