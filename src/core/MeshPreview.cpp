// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MeshPreview.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sf {

namespace {

constexpr uint8_t kSilhouetteBackground = 236; // light grey: the outline reads at a glance
constexpr uint8_t kShadedBackground = 46;
constexpr float kDarkLimit = 128.0f;

} // namespace

Image renderMeshPreview(const Mesh& mesh, int size, PreviewStyle style, float yawDegrees, float pitchDegrees) {
    const int n = std::clamp(size, 8, 1024) * 2; // supersampled, then halved
    Image img;
    img.width = img.height = n;
    const uint8_t bg = style == PreviewStyle::Silhouette ? kSilhouetteBackground : kShadedBackground;
    img.rgba.assign(size_t(n) * size_t(n) * 4, 255);
    for (size_t i = 0; i < img.rgba.size(); i += 4) img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = bg;

    const glm::mat3 turn = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(pitchDegrees), glm::vec3(1, 0, 0)) *
                                     glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0, 1, 0)));
    std::vector<glm::vec3> p(mesh.positions.size());
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i < p.size(); ++i) {
        p[i] = turn * mesh.positions[i];
        lo = glm::min(lo, p[i]);
        hi = glm::max(hi, p[i]);
    }
    if (mesh.triangles.empty() || !(lo.x <= hi.x)) return img.downscaled(n / 2);
    const glm::vec2 centre = glm::vec2(lo + hi) * 0.5f;
    const float extent = std::max({hi.x - lo.x, hi.y - lo.y, 1e-6f});
    const float scale = float(n) * 0.88f / extent; // a margin all round
    for (auto& q : p) q = glm::vec3((q.x - centre.x) * scale + float(n) * 0.5f, float(n) * 0.5f - (q.y - centre.y) * scale, q.z);

    std::vector<float> depth(size_t(n) * size_t(n), std::numeric_limits<float>::lowest());
    const glm::vec3 light = glm::normalize(glm::vec3(-0.45f, 0.6f, 0.65f)); // upper left, toward the viewer
    const bool anyHidden = mesh.hidden.size() == mesh.triangles.size();
    for (size_t t = 0; t < mesh.triangles.size(); ++t) {
        if (anyHidden && mesh.hidden[t]) continue;
        const glm::uvec3 tri = mesh.triangles[t];
        if (tri.x >= p.size() || tri.y >= p.size() || tri.z >= p.size()) continue;
        const glm::vec3 a = p[tri.x], b = p[tri.y], c = p[tri.z];
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-12f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(n - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(n - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        uint8_t shade = 0;
        if (style == PreviewStyle::Shaded) { // clay, lit from the upper left; both sides lit (AI meshes flip faces)
            const glm::vec3 wa = turn * mesh.positions[tri.x], wb = turn * mesh.positions[tri.y], wc = turn * mesh.positions[tri.z];
            glm::vec3 nrm = glm::cross(wb - wa, wc - wa);
            const float len = glm::length(nrm);
            nrm = len > 0.0f ? nrm / len : glm::vec3(0, 0, 1);
            if (nrm.z < 0.0f) nrm = -nrm;
            const float lit = 0.22f + 0.78f * std::max(0.0f, glm::dot(nrm, light));
            shade = uint8_t(std::clamp(lit * 214.0f, 0.0f, 255.0f));
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                float& d = depth[size_t(y) * size_t(n) + size_t(x)];
                if (z <= d) continue;
                d = z;
                uint8_t* px8 = &img.rgba[(size_t(y) * size_t(n) + size_t(x)) * 4];
                if (style == PreviewStyle::Silhouette) px8[0] = px8[1] = px8[2] = 0;
                else {
                    px8[0] = shade;
                    px8[1] = uint8_t(float(shade) * 0.97f);
                    px8[2] = uint8_t(float(shade) * 0.92f);
                }
            }
    }
    return img.downscaled(n / 2);
}

float silhouetteCoverage(const Image& s) {
    if (s.empty()) return 0.0f;
    size_t dark = 0;
    for (size_t i = 0; i + 3 < s.rgba.size(); i += 4) dark += float(s.rgba[i]) < kDarkLimit;
    return float(dark) / float(size_t(s.width) * size_t(s.height));
}

float silhouetteDifference(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.empty()) return 1.0f;
    size_t either = 0, one = 0;
    for (size_t i = 0; i + 3 < a.rgba.size() && i + 3 < b.rgba.size(); i += 4) {
        const bool da = float(a.rgba[i]) < kDarkLimit, db = float(b.rgba[i]) < kDarkLimit;
        either += da || db;
        one += da != db;
    }
    return either ? float(one) / float(either) : 0.0f;
}

} // namespace sf
