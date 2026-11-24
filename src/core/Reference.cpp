// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Reference.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace sf {

namespace {

std::string fileStem(const std::string& path) {
    const auto stem = std::filesystem::path(std::u8string(path.begin(), path.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

const char* modeName(ReferenceMode m) { return m == ReferenceMode::Scene ? "scene" : "behind"; }
const char* planeName(ReferencePlane p) {
    return p == ReferencePlane::Side ? "side" : p == ReferencePlane::Top ? "top" : "front";
}
bool parseMode(const std::string& s, ReferenceMode& m) {
    if (s == "behind") m = ReferenceMode::Behind;
    else if (s == "scene") m = ReferenceMode::Scene;
    else return false;
    return true;
}
bool parsePlane(const std::string& s, ReferencePlane& p) {
    if (s == "front") p = ReferencePlane::Front;
    else if (s == "side") p = ReferencePlane::Side;
    else if (s == "top") p = ReferencePlane::Top;
    else return false;
    return true;
}

// A decoded image kept to the size cap; `scaled` says whether it had to shrink.
// The header is checked first: a shared project's image can claim huge dimensions in a tiny file, and the decoder
// would allocate for them before any later check. Every reference decode (file, project entry, reference_add) comes
// through here.
bool decodeCapped(const unsigned char* bytes, size_t size, Image& out, bool& scaled, std::string& error) {
    int w = 0, h = 0;
    if (!Image::dimensions(bytes, size, w, h)) {
        error = "Not a PNG or JPEG image";
        return false;
    }
    if (w <= 0 || h <= 0 || w > kReferenceMaxHeaderSide || h > kReferenceMaxHeaderSide ||
        size_t(w) * size_t(h) > kReferenceMaxPixels) {
        error = "Reference images are limited to " + std::to_string(kReferenceMaxHeaderSide) + " pixels a side and " +
                std::to_string(kReferenceMaxPixels >> 20) + " megapixels";
        return false;
    }
    if (!Image::decode(bytes, size, out, error)) return false;
    scaled = std::max(out.width, out.height) > kReferenceMaxSide;
    if (scaled) out = out.downscaled(kReferenceMaxSide);
    return true;
}

AABB boundsOr(const AABB& b) {
    if (b.valid()) return b;
    AABB unit;
    unit.expand(glm::vec3(-1.0f));
    unit.expand(glm::vec3(1.0f));
    return unit;
}

} // namespace

void ReferenceImage::axes(glm::vec3& u, glm::vec3& v, glm::vec3& n) const {
    switch (plane) {
    case ReferencePlane::Side: u = {0, 0, -1}; v = {0, -1, 0}; n = {1, 0, 0}; break; // (as View > Right sees it)
    case ReferencePlane::Top: u = {1, 0, 0}; v = {0, 0, 1}; n = {0, 1, 0}; break;
    default: u = {1, 0, 0}; v = {0, -1, 0}; n = {0, 0, 1}; break;
    }
}

std::array<glm::vec3, 4> ReferenceImage::corners(float canvasAspect) const {
    static constexpr glm::vec2 kSigns[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    std::array<glm::vec3, 4> c{};
    if (mode == ReferenceMode::Behind) {
        const float hh = 0.5f * height, hw = 0.5f * height * aspect() * stretch / std::max(canvasAspect, 1e-4f);
        for (int i = 0; i < 4; ++i) c[size_t(i)] = glm::vec3(center + kSigns[i] * glm::vec2(hw, hh), 0.0f);
        return c;
    }
    glm::vec3 u, v, n;
    axes(u, v, n);
    const float hh = 0.5f * sceneHeight, hw = hh * aspect() * stretch;
    for (int i = 0; i < 4; ++i) c[size_t(i)] = origin + u * (kSigns[i].x * hw) + v * (kSigns[i].y * hh);
    return c;
}

glm::vec2 ReferenceImage::cornerUv(int i) const {
    static constexpr glm::vec2 kUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    glm::vec2 uv = kUv[i & 3];
    if (flipX) uv.x = 1.0f - uv.x;
    if (flipY) uv.y = 1.0f - uv.y;
    return uv;
}

void ReferenceImage::placeInScene(const AABB& sceneBounds) {
    const AABB b = boundsOr(sceneBounds);
    const glm::vec3 e = b.extent(), c = b.center();
    const float margin = 0.05f * std::max({e.x, e.y, e.z, 1e-3f});
    origin = c;
    switch (plane) {
    case ReferencePlane::Side: origin.x = b.min.x - margin; sceneHeight = e.y; break;
    case ReferencePlane::Top: origin.y = b.min.y - margin; sceneHeight = e.z; break;
    default: origin.z = b.min.z - margin; sceneHeight = e.y; break;
    }
    sceneHeight = std::max(sceneHeight * 1.1f, 2.0f * margin);
}

void ReferenceImage::resetPlacement(const AABB& bounds) {
    stretch = 1.0f;
    if (mode == ReferenceMode::Behind) {
        center = {0.5f, 0.5f};
        height = 0.8f;
    } else {
        placeInScene(bounds);
    }
}

float ReferenceImage::facing(const glm::mat4& view, bool ortho) const {
    if (mode == ReferenceMode::Behind) return 1.0f;
    glm::vec3 u, v, n;
    axes(u, v, n);
    const glm::vec3 nv = glm::normalize(glm::mat3(view) * n);
    glm::vec3 look(0.0f, 0.0f, -1.0f);
    if (!ortho) {
        const glm::vec3 p = glm::vec3(view * glm::vec4(origin, 1.0f));
        if (glm::length(p) > 1e-6f) look = glm::normalize(p);
    }
    const float c = std::abs(glm::dot(nv, look));
    const float t = std::clamp((c - 0.12f) / (0.3f - 0.12f), 0.0f, 1.0f); // gone below ~7 degrees, whole past ~17
    return t * t * (3.0f - 2.0f * t);
}

bool isLocalReferencePath(const std::string& path) {
    const auto sep = [](char c) { return c == '/' || c == '\\'; };
    size_t b = 0;
    while (b < path.size() && (path[b] == ' ' || path[b] == '\t')) ++b; // (Windows would still take " \\host\x")
    const std::string_view p(path.data() + b, path.size() - b);
    if (p.empty() || p.find('\0') != std::string_view::npos) return false;
    if (p.size() >= 2 && sep(p[0]) && sep(p[1])) return false; // \\host, //host, \\?\UNC, \\.\device
    if (p.find("://") != std::string_view::npos) return false;
    size_t i = 0; // a scheme: letters then ':' (a lone letter is a drive)
    if (std::isalpha(static_cast<unsigned char>(p[0])))
        while (i < p.size() && (std::isalnum(static_cast<unsigned char>(p[i])) || p[i] == '+' || p[i] == '-' || p[i] == '.')) ++i;
    return !(i > 1 && i < p.size() && p[i] == ':');
}

bool loadReferenceFile(const std::string& path, ReferenceImage& out, std::string& error) {
    if (!isLocalReferencePath(path)) {
        error = "Reference images must be files on this computer";
        return false;
    }
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec) {
        error = "Cannot open " + path;
        return false;
    }
    if (size == 0 || size > kReferenceMaxFileBytes) {
        error = "Reference images are limited to " + std::to_string(kReferenceMaxFileBytes >> 20) + " MB: " + path;
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    auto bytes = std::make_shared<std::vector<uint8_t>>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f && !f.eof()) {
        error = "Cannot read " + path;
        return false;
    }
    Image img;
    bool scaled = false;
    if (!decodeCapped(bytes->data(), bytes->size(), img, scaled, error)) return false;
    out.image = std::make_shared<const Image>(std::move(img));
    out.fileBytes = scaled || bytes->size() > kReferenceMaxEmbedBytes ? nullptr : std::move(bytes);
    out.name = fileStem(path);
    out.path = path;
    return true;
}

int referenceHandleAt(const std::array<glm::vec2, 4>& c, glm::vec2 p, float grab) {
    for (int i = 0; i < 4; ++i)
        if (glm::distance(c[size_t(i)], p) <= grab) return i + 1;
    auto cross = [](glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; };
    int pos = 0, neg = 0;
    for (size_t i = 0; i < 4; ++i) {
        const float s = cross(c[(i + 1) % 4] - c[i], p - c[i]);
        pos += s > 0.0f;
        neg += s < 0.0f;
    }
    return pos == 0 || neg == 0 ? 0 : -1; // (inside a convex quad: on one side of every edge)
}

size_t referenceGpuBytes(const std::vector<ReferenceImage>& refs) {
    std::vector<const Image*> seen;
    size_t bytes = 0;
    for (const ReferenceImage& r : refs) {
        if (!r.shown() || std::find(seen.begin(), seen.end(), r.image.get()) != seen.end()) continue;
        seen.push_back(r.image.get());
        bytes += size_t(r.image->width) * size_t(r.image->height) * 4 * 4 / 3; // (+ a third for the mip levels)
    }
    return bytes;
}

Json referenceToJson(const ReferenceImage& r) {
    Json j = Json::object();
    j.set("name", r.name);
    j.set("path", r.path);
    j.set("mode", std::string(modeName(r.mode)));
    j.set("plane", std::string(planeName(r.plane)));
    Json c = Json::array();
    c.push(double(r.center.x));
    c.push(double(r.center.y));
    j.set("center", std::move(c));
    Json o = Json::array();
    for (int k = 0; k < 3; ++k) o.push(double(r.origin[k]));
    j.set("origin", std::move(o));
    j.set("height", double(r.height));
    j.set("scene_height", double(r.sceneHeight));
    j.set("stretch", double(r.stretch));
    j.set("opacity", double(r.opacity));
    j.set("flip_x", r.flipX);
    j.set("flip_y", r.flipY);
    j.set("locked", r.locked);
    j.set("visible", r.visible);
    j.set("in_renders", r.inRenders);
    j.set("embed", r.embed);
    return j;
}

bool referenceFromJson(const Json& j, const std::vector<uint8_t>* embedded, ReferenceImage& out) {
    if (!j.isObject()) return false;
    ReferenceImage r;
    r.name = j["name"].asString();
    r.path = j["path"].asString();
    if (r.name.size() > 200) r.name.resize(200);
    parseMode(j["mode"].asString(), r.mode);
    parsePlane(j["plane"].asString(), r.plane);
    auto num = [&](const Json& v, float fallback, float lo, float hi) {
        const double d = v.asNumber(fallback);
        return std::isfinite(d) ? std::clamp(float(d), lo, hi) : fallback;
    };
    if (j["center"].size() == 2) r.center = {num(j["center"][0], 0.5f, -10.0f, 11.0f), num(j["center"][1], 0.5f, -10.0f, 11.0f)};
    if (j["origin"].size() == 3)
        for (int k = 0; k < 3; ++k) r.origin[k] = num(j["origin"][size_t(k)], 0.0f, -1e6f, 1e6f);
    r.height = num(j["height"], r.height, 0.01f, 20.0f);
    r.sceneHeight = num(j["scene_height"], 0.0f, 0.0f, 1e6f);
    r.stretch = num(j["stretch"], 1.0f, 0.05f, 20.0f);
    r.opacity = num(j["opacity"], r.opacity, 0.0f, 1.0f);
    r.flipX = j["flip_x"].asBool(false);
    r.flipY = j["flip_y"].asBool(false);
    r.locked = j["locked"].asBool(false);
    r.visible = j["visible"].asBool(true);
    r.inRenders = j["in_renders"].asBool(false);
    r.embed = j["embed"].asBool(true);
    std::string err;
    if (embedded && !embedded->empty()) {
        Image img;
        bool scaled = false;
        if (decodeCapped(embedded->data(), embedded->size(), img, scaled, err)) {
            r.image = std::make_shared<const Image>(std::move(img));
            if (!scaled) r.fileBytes = std::make_shared<const std::vector<uint8_t>>(*embedded);
        } else
            r.invalid = true;
    } else if (!r.path.empty() && !isLocalReferencePath(r.path)) { // a network or URL path in a project: never opened, never kept
        r.path.clear();
        r.embed = false;
        r.invalid = true;
    } else if (!r.path.empty()) { // by path: a file that moved leaves the entry without its image (the panel says so)
        // The path names a file on this computer, chosen by whoever made the project: never embed it on the next save
        // unless the user ticks Embed (else a shared project could pull a private picture into the file sent back).
        r.embed = false;
        ReferenceImage loaded;
        if (loadReferenceFile(r.path, loaded, err)) {
            r.image = std::move(loaded.image);
            r.fileBytes = std::move(loaded.fileBytes);
        } else {
            std::error_code ec;
            r.invalid = std::filesystem::exists(std::filesystem::path(std::u8string(r.path.begin(), r.path.end())), ec); // there, but not an image we take
        }
    }
    out = std::move(r);
    return true;
}

bool referenceEmbedBytes(const ReferenceImage& r, std::vector<uint8_t>& out) {
    if (r.fileBytes && !r.fileBytes->empty()) {
        out = *r.fileBytes;
        return true;
    }
    return r.image && !r.image->empty() && r.image->encodePng(out);
}

void registerReferenceCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    auto place = [](Document& doc, ReferenceImage& r, const Args& a) -> std::string {
        if (a.contains("mode") && !parseMode(argString(a, "mode"), r.mode)) return "mode is behind or scene";
        if (a.contains("plane") && !parsePlane(argString(a, "plane"), r.plane)) return "plane is front, side or top";
        if (r.mode == ReferenceMode::Scene && (r.sceneHeight <= 0.0f || a.contains("plane"))) r.placeInScene(doc.visibleBounds());
        if (argBool(a, "reset", false)) r.resetPlacement(doc.visibleBounds());
        auto num = [&](const char* k, float& v, float lo, float hi) {
            if (!a.contains(k)) return true;
            const double d = argNumber(a, k, v);
            if (!std::isfinite(d)) return false;
            v = std::clamp(float(d), lo, hi);
            return true;
        };
        if (!num("x", r.center.x, -10.0f, 11.0f) || !num("y", r.center.y, -10.0f, 11.0f) ||
            !num("height", r.height, 0.01f, 20.0f) || !num("stretch", r.stretch, 0.05f, 20.0f) ||
            !num("opacity", r.opacity, 0.0f, 1.0f) || !num("origin_x", r.origin.x, -1e6f, 1e6f) ||
            !num("origin_y", r.origin.y, -1e6f, 1e6f) || !num("origin_z", r.origin.z, -1e6f, 1e6f) ||
            !num("scene_height", r.sceneHeight, 1e-6f, 1e6f))
            return "numbers only";
        for (auto [key, flag] : {std::pair{"flip_x", &r.flipX}, std::pair{"flip_y", &r.flipY}, std::pair{"locked", &r.locked},
                                 std::pair{"visible", &r.visible}, std::pair{"in_renders", &r.inRenders}, std::pair{"embed", &r.embed}})
            if (a.contains(key)) *flag = argBool(a, key, *flag);
        return {};
    };
    const std::vector<ParamSpec> setParams = {
        {"mode", T::String, "behind (a backdrop fixed to the view) or scene (a plane by the model that turns with the view).", false},
        {"plane", T::String, "In the scene: front, side or top (placed just behind the model as that view sees it).", false},
        {"x", T::Number, "Behind: centre across the view, 0 (left) to 1 (right).", false},
        {"y", T::Number, "Behind: centre down the view, 0 (top) to 1 (bottom).", false},
        {"height", T::Number, "Behind: height as a fraction of the view's height.", false},
        {"origin_x", T::Number, "In the scene: the plane's centre, X.", false},
        {"origin_y", T::Number, "In the scene: the plane's centre, Y.", false},
        {"origin_z", T::Number, "In the scene: the plane's centre, Z.", false},
        {"scene_height", T::Number, "In the scene: height in scene units.", false},
        {"stretch", T::Number, "Width against the image's own proportions (1 keeps them).", false},
        {"opacity", T::Number, "How strongly it shows, 0-1.", false},
        {"flip_x", T::Bool, "Mirror it left to right.", false},
        {"flip_y", T::Bool, "Mirror it top to bottom.", false},
        {"locked", T::Bool, "Adjust leaves it alone.", false},
        {"visible", T::Bool, "Show it.", false},
        {"in_renders", T::Bool, "Also show it in renders, exports and the timelapse.", false},
        {"embed", T::Bool, "Save the image inside the project (off: only its path).", false},
        {"reset", T::Bool, "Place it again (before the values above).", false}};

    std::vector<ParamSpec> addParams = {{"path", T::String, "Image file (png, jpg, ...).", true}};
    addParams.insert(addParams.end(), setParams.begin(), setParams.end());
    registry.add({"reference_add",
                  "Add a reference image to the view to sculpt against: behind the model, or as a plane in the scene. "
                  "It never takes strokes, and shows in renders only with in_renders=true.",
                  addParams, [place](Document& doc, const Args& a) -> CommandResult {
                      if (doc.references.size() >= size_t(kMaxReferences))
                          return {false, "At most " + std::to_string(kMaxReferences) + " reference images"};
                      ReferenceImage r;
                      std::string err;
                      if (!loadReferenceFile(argString(a, "path"), r, err)) return {false, err};
                      if (const std::string e = place(doc, r, a); !e.empty()) return {false, "reference_add: " + e};
                      doc.references.push_back(std::move(r));
                      doc.modified = true;
                      const ReferenceImage& added = doc.references.back();
                      return {true, "Reference '" + added.name + "' (" + std::to_string(added.image->width) + "x" +
                                        std::to_string(added.image->height) + ", " + modeName(added.mode) + ")"};
                  }});
    std::vector<ParamSpec> withIndex = {{"index", T::Number, "Which reference, from 0 (default: the last added).", false}};
    withIndex.insert(withIndex.end(), setParams.begin(), setParams.end());
    registry.add({"reference_set", "Place and set up a reference image.", withIndex,
                  [place](Document& doc, const Args& a) -> CommandResult {
                      if (doc.references.empty()) return {false, "No reference images"};
                      const double i = argNumber(a, "index", double(doc.references.size() - 1));
                      if (!(i >= 0.0 && i < double(doc.references.size()))) return {false, "No reference " + std::to_string(int(i))};
                      ReferenceImage& r = doc.references[size_t(i)];
                      if (const std::string e = place(doc, r, a); !e.empty()) return {false, "reference_set: " + e};
                      doc.modified = true;
                      return {true, "Reference '" + r.name + "'"};
                  }});
    registry.add({"reference_remove", "Take a reference image (or all of them) off the view.",
                  {{"index", T::Number, "Which reference, from 0 (default: the last added).", false},
                   {"all", T::Bool, "Remove every reference image.", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      if (doc.references.empty()) return {true, "No reference images"};
                      if (argBool(a, "all", false)) {
                          doc.references.clear();
                      } else {
                          const double i = argNumber(a, "index", double(doc.references.size() - 1));
                          if (!(i >= 0.0 && i < double(doc.references.size()))) return {false, "No reference " + std::to_string(int(i))};
                          doc.references.erase(doc.references.begin() + std::ptrdiff_t(i));
                      }
                      doc.modified = true;
                      return {true, std::to_string(doc.references.size()) + " reference images left"};
                  }});
    registry.add({"reference_pair",
                  "A front and a side reference in one go: both planes in the scene, behind the model, the same height.",
                  {{"front", T::String, "The front image.", true}, {"side", T::String, "The side image.", true},
                   {"opacity", T::Number, "How strongly they show, 0-1 (default 0.5).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      if (doc.references.size() + 2 > size_t(kMaxReferences))
                          return {false, "At most " + std::to_string(kMaxReferences) + " reference images"};
                      ReferenceImage pair[2];
                      const char* keys[2] = {"front", "side"};
                      for (int k = 0; k < 2; ++k) {
                          std::string err;
                          if (!loadReferenceFile(argString(a, keys[k]), pair[k], err)) return {false, err};
                          pair[k].mode = ReferenceMode::Scene;
                          pair[k].plane = k == 0 ? ReferencePlane::Front : ReferencePlane::Side;
                          pair[k].opacity = std::clamp(float(argNumber(a, "opacity", 0.5)), 0.0f, 1.0f);
                          pair[k].placeInScene(doc.visibleBounds());
                      }
                      doc.references.push_back(std::move(pair[0]));
                      doc.references.push_back(std::move(pair[1]));
                      doc.modified = true;
                      return {true, "Front '" + doc.references[doc.references.size() - 2].name + "' and side '" +
                                        doc.references.back().name + "'"};
                  }});
}

} // namespace sf
