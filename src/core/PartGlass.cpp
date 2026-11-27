// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/PartGlass.h"

#include "core/Mesh.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sf {

const char* glassPresetName(GlassPreset p) {
    switch (p) {
    case GlassPreset::Water: return "water";
    case GlassPreset::Glass: return "glass";
    case GlassPreset::Diamond: return "diamond";
    }
    return "glass";
}

float glassPresetIor(GlassPreset p) {
    switch (p) {
    case GlassPreset::Water: return 1.33f;
    case GlassPreset::Glass: return 1.5f;
    case GlassPreset::Diamond: return 2.42f;
    }
    return 1.5f;
}

bool parseGlassPreset(const std::string& name, GlassPreset& out) {
    std::string lower;
    for (char c : name) lower += char(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < kGlassPresetCount; ++i)
        if (lower == glassPresetName(GlassPreset(i))) {
            out = GlassPreset(i);
            return true;
        }
    return false;
}

PartGlass glassFromPreset(GlassPreset p) {
    PartGlass g;
    g.transmission = 1.0f;
    g.ior = glassPresetIor(p);
    // Water takes a faint blue-green over a long way; glass and diamond stay clear (a tint is the member's to add).
    if (p == GlassPreset::Water) {
        g.tint = srgbToLinear(glm::vec3(0.62f, 0.86f, 0.92f));
        g.depth = 2.0f;
    }
    return g;
}

float glassF0(float ior) {
    const float r = (ior - 1.0f) / (ior + 1.0f);
    return r * r;
}

float glassFresnel(float ior, float cosTheta) {
    const float f0 = glassF0(ior);
    const float k = std::clamp(1.0f - cosTheta, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * k * k * k * k * k;
}

namespace {
float clampOr(float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }
} // namespace

PartGlass clampedGlass(PartGlass g) {
    const PartGlass d;
    g.transmission = clampOr(g.transmission, 0.0f, 1.0f, d.transmission);
    g.ior = clampOr(g.ior, kGlassIorMin, kGlassIorMax, d.ior);
    g.depth = clampOr(g.depth, kGlassDepthMin, kGlassDepthMax, d.depth);
    for (int c = 0; c < 3; ++c) g.tint[c] = clampOr(g.tint[c], 0.0f, 1.0f, 1.0f);
    return g;
}

Json glassToJson(const PartGlass& g) {
    Json j = Json::object();
    j.set("transmission", double(g.transmission));
    j.set("ior", double(g.ior));
    const glm::vec3 s = linearToSrgb(glm::clamp(g.tint, 0.0f, 1.0f));
    Json tint = Json::array();
    for (int c = 0; c < 3; ++c) tint.push(Json(double(s[c])));
    j.set("tint", std::move(tint));
    j.set("depth", double(g.depth));
    return j;
}

bool glassFromJson(const Json& j, PartGlass& out) {
    if (!j.isObject()) return false;
    PartGlass g = out;
    if (j["transmission"].isNumber()) g.transmission = float(j["transmission"].asNumber());
    if (j["ior"].isNumber()) g.ior = float(j["ior"].asNumber());
    if (j["depth"].isNumber()) g.depth = float(j["depth"].asNumber());
    const Json& t = j["tint"];
    if (t.isArray() && t.size() == 3 && t[size_t(0)].isNumber() && t[size_t(1)].isNumber() && t[size_t(2)].isNumber()) {
        const glm::vec3 s(float(t[size_t(0)].asNumber()), float(t[size_t(1)].asNumber()), float(t[size_t(2)].asNumber()));
        g.tint = srgbToLinear(glm::clamp(s, 0.0f, 1.0f));
    }
    out = clampedGlass(g);
    return true;
}

std::vector<size_t> glassDrawOrder(const std::vector<float>& distances) {
    std::vector<size_t> order(distances.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return distances[a] > distances[b]; });
    return order;
}

} // namespace sf
