// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fine creases through Voxel Remesh (gh#240): a sphere with carved V-grooves (wrinkles) and one V-ridge, narrower than
// two voxels at the default Resolution, built as a decimated sculpt is (long thin triangles along each crease: rings at
// the shoulders and the bottom), then tilted so no crease runs along a grid axis. The measures sample the source along
// each crease (its flanks and its bottom, a quarter voxel apart) and look at the remeshed surface there:
//   serrationDeg   RMS of the angle between the result's normals at neighbouring flank samples (a clean flank turns
//                  well under a degree per sample; stair-steps swing it back and forth)
//   normalErrDeg   RMS angle between the result's and the source's normal on the flanks
//   hausdorff/mean distance between source and result near the creases (both ways), in voxels
//   bottomGap      mean distance from the source's crease bottom to the result (a groove filled in), in voxels
//   wiggleVox      the stair-step itself: cross-sections of the result every quarter voxel along each crease (radial rays);
//                  the RMS of its crease line's sideways position about its own 8-voxel running mean, in voxels
//   depthCv        how much the result's crease depth varies along the crease (std / mean): a groove that comes and goes
//   profileErr     RMS radial difference between the result's and the source's cross-sections, in voxels
// Used by tests/test_voxel_crease.cpp and sf_remesh_bench --shape creased.
#pragma once

#include "core/Mesh.h"
#include "core/SurfaceQuery.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sf::crease {

struct Groove {
    float latitude; // radians
    float halfWidth; // radians (= scene units on the unit sphere)
    float depth;     // scene units; negative: a ridge
};

struct Fixture {
    Mesh mesh;
    std::vector<Groove> grooves;
    glm::mat3 rotation{1.0f}; // sphere frame -> scene
};

// The unit sphere's creases: `halfWidth` and `depth` in scene units (the default Resolution 200 has voxels of 0.01 here).
inline Fixture makeCreasedSphere(float halfWidth = 0.008f, float depth = 0.012f, int segments = 1024, float ringStep = 0.008f) {
    Fixture f;
    f.grooves = {{0.26f, halfWidth, depth}, {0.61f, halfWidth, depth}, {0.96f, halfWidth, depth}, {-0.44f, halfWidth, depth},
                 {-0.83f, halfWidth, -depth}};
    f.rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))));
    struct Ring {
        float lat, r;
    };
    std::vector<Ring> rings;
    const float pi = 3.14159265f;
    for (float lat = -pi / 2 + ringStep; lat < pi / 2 - ringStep * 0.5f; lat += ringStep) {
        bool inGroove = false;
        for (const Groove& g : f.grooves) inGroove |= std::abs(lat - g.latitude) < g.halfWidth * 1.5f;
        if (!inGroove) rings.push_back({lat, 1.0f});
    }
    for (const Groove& g : f.grooves) { // shoulders and bottom
        rings.push_back({g.latitude - g.halfWidth, 1.0f});
        rings.push_back({g.latitude, 1.0f - g.depth});
        rings.push_back({g.latitude + g.halfWidth, 1.0f});
    }
    std::sort(rings.begin(), rings.end(), [](const Ring& a, const Ring& b) { return a.lat < b.lat; });
    Mesh& m = f.mesh;
    m.positions.push_back(f.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    for (const Ring& ring : rings)
        for (int s = 0; s < segments; ++s) {
            const float th = 2.0f * pi * float(s) / float(segments);
            m.positions.push_back(f.rotation * (ring.r * glm::vec3(std::cos(ring.lat) * std::cos(th), std::cos(ring.lat) * std::sin(th),
                                                                    std::sin(ring.lat))));
        }
    m.positions.push_back(f.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
    const uint32_t S = uint32_t(segments), top = uint32_t(m.positions.size() - 1);
    auto at = [&](size_t ring, uint32_t s) { return 1u + uint32_t(ring) * S + s % S; };
    for (uint32_t s = 0; s < S; ++s) m.triangles.push_back({0u, at(0, s + 1), at(0, s)});
    for (size_t j = 0; j + 1 < rings.size(); ++j)
        for (uint32_t s = 0; s < S; ++s) {
            m.triangles.push_back({at(j, s), at(j, s + 1), at(j + 1, s + 1)});
            m.triangles.push_back({at(j, s), at(j + 1, s + 1), at(j + 1, s)});
        }
    for (uint32_t s = 0; s < S; ++s) m.triangles.push_back({top, at(rings.size() - 1, s), at(rings.size() - 1, s + 1)});
    m.rebuildTopology();
    m.recomputeAllNormals();
    return f;
}

struct Measure {
    double serrationDeg = 0.0, normalErrDeg = 0.0, hausdorff = 0.0, mean = 0.0, bottomGap = 0.0;
    double wiggleVox = 0.0, depthCv = 0.0, profileErr = 0.0;
    size_t samples = 0;
};

inline glm::vec3 faceNormal(const Mesh& m, uint32_t t) {
    const glm::uvec3 tri = m.triangles[t];
    const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
    const float l = glm::length(n);
    return l > 0.0f ? n / l : glm::vec3(0.0f);
}

inline double angleDeg(const glm::vec3& a, const glm::vec3& b) {
    return double(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f))) * 57.29577951308232;
}

// `voxel`: the voxel size in scene units (distances come out in voxels).
inline Measure measure(const Fixture& f, const Mesh& result, float voxel) {
    Measure out;
    TriangleBVH src, res;
    src.build(f.mesh);
    res.build(result);
    const float pi = 3.14159265f, search = 20.0f * voxel;
    double serr2 = 0.0, nerr2 = 0.0, dsum = 0.0, gap = 0.0;
    size_t serrN = 0, nerrN = 0, dN = 0, gapN = 0;
    for (const Groove& g : f.grooves) {
        const float c = std::cos(g.latitude), step = 0.25f * voxel / c;
        const int n = int(2.0f * pi / step);
        for (float side : {-0.5f, 0.0f, 0.5f}) {
            const float lat = g.latitude + side * g.halfWidth, r = 1.0f - g.depth * (1.0f - std::abs(side));
            glm::vec3 prev(0.0f);
            for (int k = 0; k <= n; ++k) {
                const float th = float(k) * step;
                const glm::vec3 p = f.rotation * (r * glm::vec3(std::cos(lat) * std::cos(th), std::cos(lat) * std::sin(th), std::sin(lat)));
                SurfaceHit hs, hr;
                if (!src.closestPoint(p, search, hs) || !res.closestPoint(hs.position, search, hr)) {
                    prev = glm::vec3(0.0f);
                    continue;
                }
                const double d = double(hr.distance) / voxel;
                out.hausdorff = std::max(out.hausdorff, d), dsum += d, ++dN;
                if (side == 0.0f) {
                    gap += d, ++gapN;
                    continue;
                }
                const glm::vec3 nr = faceNormal(result, hr.prim), ns = faceNormal(f.mesh, hs.prim);
                const double e = angleDeg(nr, ns);
                nerr2 += e * e, ++nerrN;
                if (prev != glm::vec3(0.0f)) {
                    const double s = angleDeg(nr, prev);
                    serr2 += s * s, ++serrN;
                }
                prev = nr;
            }
        }
    }
    // Cross-sections: radial rays across each crease (out to two widths a side), the extreme radius and where it is.
    double wig2 = 0.0, prof2 = 0.0;
    size_t wigN = 0, profN = 0;
    std::vector<double> depthRatios;
    for (const Groove& g : f.grooves) {
        const float c = std::cos(g.latitude), step = 0.25f * voxel / c;
        const int n = int(2.0f * pi / step), across = 41;
        const float sign = g.depth > 0.0f ? 1.0f : -1.0f;
        std::vector<double> where(size_t(n), 0.0), depth(size_t(n), 0.0);
        std::vector<uint8_t> ok(size_t(n), 0);
        for (int k = 0; k < n; ++k) {
            const float th = float(k) * step;
            double best = 1e30, bestLat = 0.0;
            bool all = true;
            for (int j = 0; j < across; ++j) {
                const float lat = g.latitude + g.halfWidth * 2.0f * (float(j) / float(across - 1) * 2.0f - 1.0f);
                const glm::vec3 dir = f.rotation * glm::vec3(std::cos(lat) * std::cos(th), std::cos(lat) * std::sin(th), std::sin(lat));
                SurfaceHit h;
                if (!res.raycast(dir * (1.0f + 10.0f * voxel), -dir, 20.0f * voxel, h)) {
                    all = false;
                    break;
                }
                const double r = 1.0 + 10.0 * voxel - double(h.distance);
                const float off = std::abs(lat - g.latitude);
                const double rs = off < g.halfWidth ? 1.0 - g.depth * (1.0 - off / g.halfWidth) : 1.0;
                prof2 += (r - rs) * (r - rs) / (double(voxel) * voxel), ++profN;
                if (sign * r < best) best = sign * r, bestLat = lat;
            }
            if (!all) continue;
            ok[size_t(k)] = 1, where[size_t(k)] = (bestLat - g.latitude) / voxel, depth[size_t(k)] = sign * (1.0 - sign * best);
        }
        for (int k = 0; k < n; ++k) {
            if (!ok[size_t(k)]) continue;
            double sum = 0.0;
            int cnt = 0;
            for (int d = -16; d <= 16; ++d) {
                const int i = (k + d + n) % n;
                if (ok[size_t(i)]) sum += where[size_t(i)], ++cnt;
            }
            const double e = where[size_t(k)] - sum / cnt;
            wig2 += e * e, ++wigN;
            depthRatios.push_back(depth[size_t(k)] / std::abs(double(g.depth)));
        }
    }
    out.wiggleVox = wigN ? std::sqrt(wig2 / double(wigN)) : 0.0;
    out.profileErr = profN ? std::sqrt(prof2 / double(profN)) : 0.0;
    if (!depthRatios.empty()) {
        double m = 0.0, v = 0.0;
        for (double d : depthRatios) m += d;
        m /= double(depthRatios.size());
        for (double d : depthRatios) v += (d - m) * (d - m);
        out.depthCv = m > 0.0 ? std::sqrt(v / double(depthRatios.size())) / m : 0.0;
    }
    // The other way: result vertices near a crease, to the source.
    const glm::mat3 back = glm::transpose(f.rotation);
    for (const glm::vec3& q : result.positions) {
        const glm::vec3 p = back * q;
        const float lat = std::asin(std::clamp(p.z / std::max(glm::length(p), 1e-6f), -1.0f, 1.0f));
        bool near = false;
        for (const Groove& g : f.grooves) near |= std::abs(lat - g.latitude) < 2.0f * g.halfWidth;
        SurfaceHit h;
        if (!near || !src.closestPoint(q, search, h)) continue;
        const double d = double(h.distance) / voxel;
        out.hausdorff = std::max(out.hausdorff, d), dsum += d, ++dN;
    }
    out.samples = dN;
    out.mean = dN ? dsum / double(dN) : 0.0;
    out.serrationDeg = serrN ? std::sqrt(serr2 / double(serrN)) : 0.0;
    out.normalErrDeg = nerrN ? std::sqrt(nerr2 / double(nerrN)) : 0.0;
    out.bottomGap = gapN ? gap / double(gapN) : 0.0;
    return out;
}

// A grazing-lit close-up (PPM) of `m` looking down on groove `g` at longitude `th`, `span` scene units across: what an
// artist sees of the crease (stair-steps show as a sawtooth highlight).
inline bool renderPatch(const Fixture& f, const Mesh& m, size_t g, float th, float span, const char* path, int px = 480) {
    TriangleBVH bvh;
    bvh.build(m);
    const float lat = f.grooves[g].latitude;
    const glm::vec3 up = f.rotation * glm::vec3(std::cos(lat) * std::cos(th), std::cos(lat) * std::sin(th), std::sin(lat));
    const glm::vec3 east = f.rotation * glm::vec3(-std::sin(th), std::cos(th), 0.0f);
    const glm::vec3 north = glm::cross(up, east);
    const glm::vec3 light = glm::normalize(up * 0.45f + east * 0.3f + north * 0.85f);
    std::vector<unsigned char> img(size_t(px) * size_t(px) * 3, 0);
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const glm::vec3 o = up * 1.2f + east * (span * (float(x) / float(px) - 0.5f)) + north * (span * (0.5f - float(y) / float(px)));
            SurfaceHit h;
            float v = 0.0f;
            if (bvh.raycast(o, -up, 1.0f, h)) v = 0.08f + 0.92f * std::max(0.0f, glm::dot(faceNormal(m, h.prim), light));
            const unsigned char c = (unsigned char)(std::clamp(v, 0.0f, 1.0f) * 255.0f);
            unsigned char* p = &img[(size_t(y) * size_t(px) + size_t(x)) * 3];
            p[0] = c, p[1] = c, p[2] = (unsigned char)(std::min(255, c + 12));
        }
    std::FILE* fo = std::fopen(path, "wb");
    if (!fo) return false;
    std::fprintf(fo, "P6 %d %d 255\n", px, px);
    std::fwrite(img.data(), 1, img.size(), fo);
    std::fclose(fo);
    return true;
}

// Segment pq against triangle abc (both open: touching at a shared corner doesn't count).
inline bool segmentHitsTriangle(const glm::vec3& p, const glm::vec3& q, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 d = q - p, e1 = b - a, e2 = c - a, h = glm::cross(d, e2);
    const float det = glm::dot(e1, h);
    if (std::abs(det) < 1e-20f) return false;
    const float f = 1.0f / det;
    const glm::vec3 s = p - a;
    const float u = f * glm::dot(s, h);
    if (u <= 1e-6f || u >= 1.0f - 1e-6f) return false;
    const glm::vec3 k = glm::cross(s, e1);
    const float v = f * glm::dot(d, k);
    if (v <= 1e-6f || u + v >= 1.0f - 1e-6f) return false;
    const float t = f * glm::dot(e2, k);
    return t > 1e-6f && t < 1.0f - 1e-6f;
}

// Pairs of triangles that share no corner and cross each other (an edge of one through the other), among the
// triangles with a corner where `only` is set (all when empty).
inline size_t selfIntersections(const Mesh& m, const std::vector<uint8_t>& only = {}) {
    std::vector<AABB> boxes(m.triangleCount());
    for (size_t t = 0; t < boxes.size(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        AABB b;
        b.expand(m.positions[f.x]), b.expand(m.positions[f.y]), b.expand(m.positions[f.z]);
        boxes[t] = b;
    }
    FlatBVH bvh;
    bvh.build(boxes);
    const auto& nodes = bvh.nodes();
    const auto& order = bvh.order();
    auto overlap = [](const AABB& a, const AABB& b) {
        return glm::all(glm::lessThanEqual(a.min, b.max)) && glm::all(glm::lessThanEqual(b.min, a.max));
    };
    size_t count = 0;
    std::vector<uint32_t> stack;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 f = m.triangles[t];
        if (!only.empty() && !(only[f.x] | only[f.y] | only[f.z])) continue;
        stack.assign(1, 0u);
        while (!stack.empty()) {
            const FlatBVH::Node& node = nodes[stack.back()];
            stack.pop_back();
            if (!overlap(node.box, boxes[t])) continue;
            if (node.count == 0) {
                stack.push_back(node.first), stack.push_back(node.first + 1);
                continue;
            }
            for (uint32_t k = node.first; k < node.first + node.count; ++k) {
                const uint32_t u = order[k];
                if (u <= t && (only.empty() || [&] {
                        const glm::uvec3 g = m.triangles[u];
                        return bool(only[g.x] | only[g.y] | only[g.z]);
                    }()))
                    continue; // (each pair once)
                const glm::uvec3 g = m.triangles[u];
                bool shared = false;
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) shared |= f[i] == g[j];
                if (shared || !overlap(boxes[t], boxes[u])) continue;
                const auto& P = m.positions;
                bool hit = false;
                for (int i = 0; i < 3 && !hit; ++i) {
                    hit |= segmentHitsTriangle(P[f[i]], P[f[(i + 1) % 3]], P[g.x], P[g.y], P[g.z]);
                    hit |= segmentHitsTriangle(P[g[i]], P[g[(i + 1) % 3]], P[f.x], P[f.y], P[f.z]);
                }
                count += hit;
            }
        }
    }
    return count;
}

} // namespace sf::crease
