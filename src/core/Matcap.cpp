// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Matcap.h"

#include "core/Mesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sf {

namespace {

// A small lighting model evaluated once per matcap pixel: wrapped diffuse with
// a subsurface tint at the terminator, Blinn specular, rim, and for metals a
// studio environment (sky, horizon, ground, softbox) seen by reflection.
struct MatcapStyle {
    const char* name;
    glm::vec3 albedo;     // linear
    float wrap;           // diffuse wrap (soft, waxy materials)
    glm::vec3 subsurface; // colour bleeding into the shadow side
    float specular, shininess;
    glm::vec3 rim;
    float metal; // 0 = dielectric, 1 = pure environment reflection
    float roughness; // blurs the environment for metals
    // The shared model (shade), or a light of its own: shadeSculpt (readability first) or shadeSoftClay.
    enum class Light { Shared, Sculpt, SoftClay } light = Light::Shared;
    MatcapCavity cavity{};
};

const MatcapStyle kStyles[] = {
    {"Sculpt", {0.54f, 0.46f, 0.39f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
     MatcapStyle::Light::Sculpt, {0.8f, 0.35f}},
    {"Red Wax", {0.50f, 0.045f, 0.03f}, 0.55f, {0.55f, 0.06f, 0.03f}, 0.55f, 70.0f, {0.25f, 0.08f, 0.06f}, 0.0f, 0.0f},
    {"Clay", {0.50f, 0.27f, 0.17f}, 0.25f, {0.05f, 0.03f, 0.02f}, 0.06f, 10.0f, {0.10f, 0.10f, 0.11f}, 0.0f, 0.0f},
    {"Skin", {0.68f, 0.38f, 0.28f}, 0.6f, {0.6f, 0.16f, 0.08f}, 0.16f, 28.0f, {0.22f, 0.16f, 0.15f}, 0.0f, 0.0f},
    {"Chrome", {0.95f, 0.96f, 1.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 1.0f, 0.05f},
    {"White", {0.78f, 0.78f, 0.76f}, 0.3f, {0.04f, 0.04f, 0.05f}, 0.3f, 45.0f, {0.12f, 0.13f, 0.15f}, 0.0f, 0.0f},
    {"Bronze", {0.80f, 0.50f, 0.25f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, {0.0f, 0.0f, 0.0f}, 1.0f, 0.35f},
    // The default (defaultMatcap()). Last, so the indices of the others stay put.
    {"Soft Clay", {0.50f, 0.455f, 0.41f}, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f,
     MatcapStyle::Light::SoftClay, {0.3f, 0.08f}},
};
constexpr int kStyleCount = int(sizeof kStyles / sizeof kStyles[0]);

const glm::vec3 kKey = glm::normalize(glm::vec3(-0.45f, 0.6f, 0.65f));
const glm::vec3 kFill = glm::normalize(glm::vec3(0.75f, -0.15f, 0.45f));

// Studio environment radiance in direction `r` (y up, z towards the viewer).
glm::vec3 environment(const glm::vec3& r, float roughness) {
    const float soft = 0.04f + roughness * 0.5f;
    const float up = r.y;
    const glm::vec3 sky = glm::mix(glm::vec3(0.75f, 0.8f, 0.85f), glm::vec3(0.22f, 0.26f, 0.32f), std::sqrt(std::max(up, 0.0f)));
    const glm::vec3 ground = glm::mix(glm::vec3(0.12f, 0.11f, 0.1f), glm::vec3(0.03f, 0.03f, 0.03f), std::sqrt(std::max(-up, 0.0f)));
    const float t = std::clamp(up / soft * 0.5f + 0.5f, 0.0f, 1.0f); // horizon line
    glm::vec3 c = glm::mix(ground, sky, t * t * (3.0f - 2.0f * t));
    // Soft box behind the camera's upper left, and a dimmer strip on the right.
    const float box = std::pow(std::max(glm::dot(r, kKey), 0.0f), 30.0f / (1.0f + 20.0f * roughness));
    const float strip = std::pow(std::max(glm::dot(r, kFill), 0.0f), 60.0f / (1.0f + 20.0f * roughness));
    c += glm::vec3(3.0f) * box + glm::vec3(0.6f, 0.65f, 0.7f) * strip;
    return c;
}

// Built for reading the surface while sculpting, after ZBrush's default material and Blender's sculpt
// studio light: a strong key from the upper left, little ambient so forms don't flatten, a crisp
// highlight that catches every undulation, only a hint of rim and no fresnel glow. Creases get the
// strongest cavity darkening of the materials (kStyles' cavity, applied by the mesh shader).
glm::vec3 shadeSculpt(const MatcapStyle& m, const glm::vec3& n) {
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const glm::vec3 key = glm::normalize(glm::vec3(-0.4f, 0.65f, 0.65f));
    const glm::vec3 fill = glm::normalize(glm::vec3(0.75f, -0.3f, 0.6f));
    const float diff = std::max((glm::dot(n, key) + 0.1f) / 1.1f, 0.0f);
    const float fd = std::max(glm::dot(n, fill), 0.0f);
    const float hemi = 0.04f + 0.04f * (0.5f + 0.5f * n.y);
    // A steep falloff on both lights: brightness changes quickly with the slope everywhere in view.
    const glm::vec3 light = 1.6f * std::pow(diff, 2.5f) + 0.5f * fd * fd * glm::vec3(0.85f, 0.9f, 1.0f) + glm::vec3(hemi);
    // A tight highlight over a broad sheen, plus a faint one from the fill, so strokes catch light
    // on the shadow side too.
    const float nh = std::max(glm::dot(n, glm::normalize(key + v)), 0.0f);
    const float spec = (0.3f * std::pow(nh, 80.0f) + 0.25f * std::pow(nh, 12.0f)) * std::min(1.0f, diff * 4.0f);
    const float fillSpec = 0.07f * std::pow(std::max(glm::dot(n, glm::normalize(fill + v)), 0.0f), 40.0f);
    const float rim = std::pow(1.0f - std::max(n.z, 0.0f), 4.0f) * 0.1f;
    return m.albedo * light + glm::vec3(spec) + glm::vec3(0.8f, 0.85f, 0.95f) * (fillSpec + rim);
}

// The default: matte studio clay, a warm light grey, built so a sculpted stroke reads at once. One key from the
// upper left with little wrap and a steep falloff (brightness turns quickly with the slope, so the two sides of a
// ridge part clearly), a form shadow on the lower right that goes well down without going black, a cool fill and
// sky/ground ambient, a broad low sheen that runs along ridges (no hot spot), and a cool rim strongest on the
// lower right so the silhouette holds against the charcoal background where the key doesn't reach.
glm::vec3 shadeSoftClay(const MatcapStyle& m, const glm::vec3& n) {
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const glm::vec3 key = glm::normalize(glm::vec3(-0.55f, 0.68f, 0.55f));
    const glm::vec3 fill = glm::normalize(glm::vec3(0.85f, -0.2f, 0.35f));
    const float k = std::max(glm::dot(n, key) * 0.92f + 0.08f, 0.0f);
    const float diff = std::pow(k, 2.4f);
    // A real fill from the right, about a third of the key: the shadow side stays a mid value, and slopes there
    // turn towards or away from it, so strokes read on that side too.
    const float fd = std::max((glm::dot(n, fill) + 0.25f) / 1.25f, 0.0f);
    // Sky above, a warm bounce from the ground below: the lower left, where neither light reaches, stays clay.
    const float up = 0.5f + 0.5f * n.y;
    const glm::vec3 ambient = glm::mix(glm::vec3(0.13f, 0.115f, 0.1f), glm::vec3(0.07f, 0.075f, 0.085f), std::sqrt(up));
    const glm::vec3 light = 1.3f * diff * glm::vec3(1.0f, 0.97f, 0.93f) + 0.3f * fd * fd * glm::vec3(0.9f, 0.93f, 1.0f) + ambient;
    const float nh = std::max(glm::dot(n, glm::normalize(key + v)), 0.0f);
    const float sheen = (0.03f * std::pow(nh, 8.0f) + 0.09f * std::pow(nh, 30.0f)) * std::min(1.0f, k * 3.0f);
    // Rim: grazing normals, weighted towards the lower right (the side the key leaves dark).
    const float grazing = std::pow(1.0f - std::max(n.z, 0.0f), 3.0f);
    const float len = std::sqrt(n.x * n.x + n.y * n.y);
    const float side = len > 1e-4f ? std::clamp((n.x * 0.8f - n.y * 0.6f) / len, 0.0f, 1.0f) : 0.0f;
    const glm::vec3 rim = (0.05f + 0.2f * side) * grazing * glm::vec3(0.82f, 0.88f, 1.0f);
    return m.albedo * light + glm::vec3(sheen) + rim;
}

glm::vec3 shade(const MatcapStyle& m, const glm::vec3& n) {
    if (m.light == MatcapStyle::Light::Sculpt) return shadeSculpt(m, n);
    if (m.light == MatcapStyle::Light::SoftClay) return shadeSoftClay(m, n);
    const glm::vec3 v(0.0f, 0.0f, 1.0f);
    const float nv = std::max(n.z, 0.0f);
    if (m.metal > 0.0f) {
        const glm::vec3 r = 2.0f * n.z * n - v; // reflect(-v, n)
        // Schlick-style brightening towards grazing angles.
        const float fresnel = std::pow(1.0f - nv, 5.0f);
        const glm::vec3 f0 = m.albedo;
        return environment(r, m.roughness) * (f0 + (glm::vec3(1.0f) - f0) * fresnel);
    }
    const float ndl = glm::dot(n, kKey);
    const float diff = std::max((ndl + m.wrap) / (1.0f + m.wrap), 0.0f);
    const float terminator = std::max(0.0f, 1.0f - std::abs(ndl - 0.05f) * 2.5f); // band around the shadow edge
    const float fill = std::max(glm::dot(n, kFill), 0.0f) * 0.2f;
    const float hemi = 0.06f + 0.06f * (0.5f + 0.5f * n.y);
    const glm::vec3 h = glm::normalize(kKey + v);
    const float spec = m.specular * std::pow(std::max(glm::dot(n, h), 0.0f), m.shininess);
    const float rim = std::pow(1.0f - nv, 3.0f);
    glm::vec3 c = m.albedo * (diff * diff * 1.05f + fill + hemi);
    c += m.subsurface * terminator * 0.35f;
    c += glm::vec3(spec) + m.rim * rim;
    return c;
}

std::string squash(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (!std::isspace(c)) out += char(std::tolower(c));
    return out;
}

} // namespace

int matcapCount() { return kStyleCount; }

int defaultMatcap() {
    static const int index = findMatcap("Soft Clay");
    return index;
}

int matcapInListOrder(int k) {
    if (k < 0 || k >= kStyleCount) return -1;
    const int d = defaultMatcap();
    return k == 0 ? d : k <= d ? k - 1 : k;
}

const char* matcapName(int index) { return index >= 0 && index < kStyleCount ? kStyles[index].name : ""; }

MatcapCavity matcapCavity(int index) { return index >= 0 && index < kStyleCount ? kStyles[index].cavity : MatcapCavity{}; }

int findMatcap(const std::string& name) {
    const std::string key = squash(name);
    for (int i = 0; i < kStyleCount; ++i)
        if (squash(kStyles[i].name) == key) return i;
    return -1;
}

std::vector<uint8_t> generateMatcap(int index, int size) {
    if (index < 0 || index >= kStyleCount || size <= 0) return {};
    const MatcapStyle& m = kStyles[index];
    std::vector<uint8_t> rgba(size_t(size) * size_t(size) * 4);
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            float x = (float(i) + 0.5f) / float(size) * 2.0f - 1.0f;
            float y = (float(j) + 0.5f) / float(size) * 2.0f - 1.0f;
            float r2 = x * x + y * y;
            if (r2 > 0.998f) { // outside the disk: repeat the rim so filtering at the edge stays clean
                const float k = std::sqrt(0.998f / r2);
                x *= k;
                y *= k;
                r2 = 0.998f;
            }
            const glm::vec3 n(x, y, std::sqrt(1.0f - r2));
            const glm::vec3 c = glm::clamp(linearToSrgb(glm::max(shade(m, n), glm::vec3(0.0f))), 0.0f, 1.0f);
            uint8_t* px = rgba.data() + (size_t(j) * size_t(size) + size_t(i)) * 4;
            for (int k = 0; k < 3; ++k) px[k] = uint8_t(std::lround(c[k] * 255.0f));
            px[3] = 255;
        }
    return rgba;
}

} // namespace sf
