// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SurfaceCursor.h"

#include <cmath>

namespace sf {

namespace {

// Two unit vectors spanning the plane with normal `n`.
void tangentBasis(const glm::vec3& n, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 a = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    u = glm::normalize(glm::cross(n, a));
    v = glm::cross(n, u);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-12f && std::isfinite(len) ? v / len : fallback;
}

// Presses `p` onto the surface along -n, looking from `reach` above the plane down to `reach` below it.
bool pressOnto(const Mesh& mesh, const BVH& bvh, const glm::vec3& p, const glm::vec3& n, float reach, RayHit& hit) {
    if (!bvh.raycast(mesh, p + n * reach, -n, hit)) return false;
    return hit.t <= 2.0f * reach;
}

} // namespace

glm::vec3 interpolatedNormal(const Mesh& mesh, const RayHit& hit) {
    if (hit.triangle >= mesh.triangles.size()) return hit.normal;
    const glm::uvec3 t = mesh.triangles[hit.triangle];
    if (mesh.normals.size() != mesh.positions.size() || t.x >= mesh.normals.size() || t.y >= mesh.normals.size() ||
        t.z >= mesh.normals.size())
        return hit.normal;
    // Barycentric coordinates of the hit point.
    const glm::vec3 a = mesh.positions[t.x], b = mesh.positions[t.y], c = mesh.positions[t.z];
    const glm::vec3 e0 = b - a, e1 = c - a, ep = hit.position - a;
    const float d00 = glm::dot(e0, e0), d01 = glm::dot(e0, e1), d11 = glm::dot(e1, e1);
    const float d20 = glm::dot(ep, e0), d21 = glm::dot(ep, e1);
    const float den = d00 * d11 - d01 * d01;
    if (!(std::abs(den) > 1e-30f)) return hit.normal;
    const float v = (d11 * d20 - d01 * d21) / den;
    const float w = (d00 * d21 - d01 * d20) / den;
    const float u = 1.0f - v - w;
    glm::vec3 n = mesh.normals[t.x] * u + mesh.normals[t.y] * v + mesh.normals[t.z] * w;
    n = safeNormalize(n, hit.normal);
    // Vertex normals wound the other way from the triangle (a flipped import): keep the side the ray came from.
    return glm::dot(n, hit.normal) < -0.2f ? hit.normal : n;
}

glm::vec3 surfaceCursorNormal(const Mesh& mesh, const BVH& bvh, const RayHit& hit, float radius, int samples) {
    const glm::vec3 n0 = interpolatedNormal(mesh, hit);
    if (!(radius > 0.0f) || samples <= 0) return n0;
    // The plane through the surface points around the cursor (two rings, pressed onto the mesh along `axis`): the
    // area vector of each ring's polygon about the centre (Newell). Its tilt comes from how high the points sit, so
    // a bump tilts it by about its height over the brush radius, where averaging the points' normals would follow
    // every steep little slope (#88).
    auto fit = [&](const glm::vec3& axis) {
        glm::vec3 u, v;
        tangentBasis(axis, u, v);
        glm::vec3 sum(0.0f);
        glm::vec3 points[64];
        for (const float r : {radius * 0.5f, radius}) {
            int count = 0;
            for (int i = 0; i < samples && count < 64; ++i) {
                const float a = 6.28318531f * float(i) / float(samples);
                RayHit h;
                if (pressOnto(mesh, bvh, hit.position + (u * std::cos(a) + v * std::sin(a)) * r, axis, r, h))
                    points[count++] = h.position - hit.position;
            }
            if (count < 3) continue; // (over a hole or off an edge: too little to fit)
            for (int i = 0; i < count; ++i) sum += glm::cross(points[i], points[(i + 1) % count]);
        }
        return glm::dot(sum, axis) > 0.0f ? safeNormalize(sum, axis) : axis;
    };
    // Twice: the second fit samples the rings in the plane the first one found, square to the form.
    const glm::vec3 n = fit(fit(n0));
    return glm::dot(n, n0) > -0.2f ? n : n0;
}

void surfaceCursorCircle(const glm::vec3& center, const glm::vec3& normal, float radius, int count,
                         std::vector<glm::vec3>& out) {
    out.clear();
    if (count <= 0 || !(radius > 0.0f)) return;
    glm::vec3 u, v;
    tangentBasis(safeNormalize(normal, glm::vec3(0, 0, 1)), u, v);
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const float a = 6.28318531f * float(i) / float(count);
        out.push_back(center + (u * std::cos(a) + v * std::sin(a)) * radius);
    }
}

} // namespace sf
