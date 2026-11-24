// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extract > Make Shell on irregular mask islands over a dense curved surface: the rim (the shell's outline) must
// follow a smooth outline and never stand proud of the shell's own offset (a member's vest plates came out with
// raised, ridged, jagged rims even with Edge Smooth up).
#include "TestHarness.h"

#include "core/ExtractMirror.h"
#include "core/Primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace sf;

namespace {

// A sphere of radius 1 whose vertices are masked in irregular islands (a few overlapping sine fields thresholded).
Mesh islandSphere(int subdivisions, bool soft) {
    Mesh m = makeIcosphere(subdivisions, 1.0f);
    m.mask.assign(m.vertexCount(), 0.0f);
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        const glm::vec3 p = m.positions[v];
        const float f = std::sin(5.1f * p.x + 1.3f) * std::sin(4.3f * p.y + 0.4f) + 0.6f * std::sin(7.7f * p.z + 2.0f * p.x) +
                        0.3f * std::sin(11.0f * p.y - 6.0f * p.z);
        const float s = f - 0.15f;
        m.mask[v] = soft ? glm::clamp(0.5f + s * 6.0f, 0.0f, 1.0f) : (s > 0.0f ? 1.0f : 0.0f);
    }
    return m;
}

struct RimStats {
    size_t loops = 0, verts = 0; // outline vertices (loops of 16 or more)
    float maxTurn = 0.0f;     // degrees, the sharpest turn of the outer outline between two consecutive edges
    float meanTurn = 0.0f;
    size_t steps = 0;         // outline vertices turning more than 45 degrees
    float maxRise = 0.0f;     // how far a rim vertex stands above the shell's offset (radius R + t), in edge lengths
    float maxNormalTilt = 0.0f; // degrees between a rim vertex's normal and the sphere's radial
    float minRimAngle = 180.0f; // the smallest triangle angle in the rim quads' triangles (degrees)
    size_t foldedOuter = 0;   // outer-surface triangles turned more than 60 degrees from the sphere's radial (folds)
    size_t foldedRim = 0;     // rim triangles whose normal is not within 30 degrees of horizontal (not perpendicular to the radial)
    float maxSink = 0.0f;     // how far a rim vertex sits inside the offset sphere, in edge lengths
};

RimStats measure(const Mesh& s, float offsetRadius, float edge) {
    RimStats r;
    // Outer-layer boundary: edges of face group 1 used by one group-1 triangle only.
    std::map<std::pair<uint32_t, uint32_t>, int> use;
    for (uint32_t t = 0; t < s.triangleCount(); ++t)
        if (s.facegroupAt(t) == 1)
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = s.triangles[t][k], b = s.triangles[t][(k + 1) % 3];
                ++use[{std::min(a, b), std::max(a, b)}];
            }
    std::map<uint32_t, std::vector<uint32_t>> adj;
    for (const auto& [e, n] : use)
        if (n == 1) adj[e.first].push_back(e.second), adj[e.second].push_back(e.first);
    // Loops under 16 vertices (specks of a few triangles) turn sharply by nature: leave them out of the outline numbers.
    std::map<uint32_t, uint32_t> loopOf;
    std::vector<uint32_t> loopSize;
    for (const auto& [v, nb] : adj) {
        if (loopOf.count(v)) continue;
        std::vector<uint32_t> stack{v};
        loopOf[v] = uint32_t(loopSize.size());
        loopSize.push_back(0);
        while (!stack.empty()) {
            const uint32_t u = stack.back();
            stack.pop_back();
            ++loopSize.back();
            for (uint32_t w : adj[u])
                if (!loopOf.count(w)) loopOf[w] = loopOf[v], stack.push_back(w);
        }
    }
    r.loops = loopSize.size();
    float turnSum = 0.0f;
    for (const auto& [v, nb] : adj) {
        if (loopSize[loopOf[v]] < 16) continue;
        ++r.verts;
        r.maxRise = std::max(r.maxRise, (glm::length(s.positions[v]) - offsetRadius) / edge);
        r.maxSink = std::max(r.maxSink, (offsetRadius - glm::length(s.positions[v])) / edge);
        const glm::vec3 radial = glm::normalize(s.positions[v]);
        r.maxNormalTilt = std::max(r.maxNormalTilt, glm::degrees(std::acos(glm::clamp(glm::dot(s.normals[v], radial), -1.0f, 1.0f))));
        if (nb.size() != 2) continue;
        const glm::vec3 a = glm::normalize(s.positions[v] - s.positions[nb[0]]), b = glm::normalize(s.positions[nb[1]] - s.positions[v]);
        const float turn = glm::degrees(std::acos(glm::clamp(glm::dot(a, b), -1.0f, 1.0f)));
        r.maxTurn = std::max(r.maxTurn, turn);
        turnSum += turn;
        r.steps += turn > 45.0f;
    }
    r.meanTurn = r.verts ? turnSum / float(r.verts) : 0.0f;
    for (uint32_t t = 0; t < s.triangleCount(); ++t) {
        const glm::uvec3 c = s.triangles[t];
        if (s.facegroupAt(t) == 1 || s.facegroupAt(t) == 3) {
            const glm::vec3 n = glm::cross(s.positions[c.y] - s.positions[c.x], s.positions[c.z] - s.positions[c.x]);
            const glm::vec3 mid = (s.positions[c.x] + s.positions[c.y] + s.positions[c.z]) / 3.0f;
            const float cs = glm::length(n) > 0.0f ? glm::dot(glm::normalize(n), glm::normalize(mid)) : 0.0f;
            if (s.facegroupAt(t) == 1) r.foldedOuter += cs < 0.5f;
            else r.foldedRim += std::abs(cs) > 0.5f;
        }
        if (s.facegroupAt(t) != 3) continue;
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 e1 = s.positions[c[(k + 1) % 3]] - s.positions[c[k]], e2 = s.positions[c[(k + 2) % 3]] - s.positions[c[k]];
            const float l = glm::length(e1) * glm::length(e2);
            if (l > 0.0f) r.minRimAngle = std::min(r.minRimAngle, glm::degrees(std::acos(glm::clamp(glm::dot(e1, e2) / l, -1.0f, 1.0f))));
        }
    }
    return r;
}

} // namespace

namespace {

float meanEdge(const Mesh& m) {
    float sum = 0.0f;
    for (uint32_t w : m.neighbors(0)) sum += glm::distance(m.positions[0], m.positions[w]);
    return sum / float(m.neighbors(0).size());
}

} // namespace

// Edge Smooth on irregular islands of a dense sphere (a painted mask: soft; and a hard-edged one): the rim follows a smooth
// outline (a few degrees of turn per edge on average, no hairpin), never stands above the shell's offset, and no triangle
// of the surface turns over (a turned-over one lights black and the offset along its normal spikes).
TEST(extract_shell_rim_follows_a_smooth_outline_without_spikes) {
    for (bool soft : {true, false}) {
        const Mesh src = islandSphere(7, soft); // 327k triangles
        const float edge = meanEdge(src);
        ExtractOptions o; // a member's settings: Edge Smooth 5, Thickness 0.015, Both Sides off, Corners / Border off
        std::string err;
        o.smooth = 0;
        const RimStats raw = measure(extractShell(src, o, err), 1.0f + o.thickness * 2.0f, edge);
        CHECK(raw.verts > 1000);
        for (int smooth : {5, 10, 50}) {
            o.smooth = smooth;
            const Mesh shell = extractShell(src, o, err);
            CHECK(shell.triangleCount() > 0);
            const RimStats r = measure(shell, 1.0f + o.thickness * 2.0f, edge);
            std::printf("rim soft=%d smooth=%2d: turn mean %.1f (raw %.1f) max %.1f, >45deg %zu of %zu (raw %zu), rise %.3f sink %.3f edges, "
                        "turned over %zu + %zu, smallest rim angle %.1f\n",
                        int(soft), smooth, r.meanTurn, raw.meanTurn, r.maxTurn, r.steps, r.verts, raw.steps, r.maxRise, r.maxSink,
                        r.foldedOuter, r.foldedRim, r.minRimAngle);
            CHECK(r.verts == raw.verts); // the same outline, smoothed
            CHECK(r.foldedOuter == 0 && r.foldedRim == 0);
            CHECK(r.maxRise < 0.1f && r.maxSink < 0.05f); // no rim vertex above the offset (0.1 of an edge: Taubin's overshoot)
            CHECK(r.maxTurn < 100.0f);
            if (soft) {
                CHECK(r.meanTurn < 12.0f && r.meanTurn < 0.25f * raw.meanTurn);
                CHECK(r.steps * 10 < r.verts);
            } else {
                CHECK(r.meanTurn < 0.5f * raw.meanTurn);
            }
        }
    }
}
