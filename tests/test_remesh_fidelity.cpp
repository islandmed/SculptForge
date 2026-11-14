// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel Remesh keeps the sculpt (#193): Clay-stroked areas used to come out grainy. On a Clay-stroked sphere (and, when
// SF_REMESH_FIXTURE names a folder with pre.obj, a real artist sculpt) the remesh has no holes, stays within 0.5% of the
// bounding box of the source surface, adds no high-frequency noise (mean |curvature| of the stroked area, measured at
// the cell scale, against the source), and carries mask, vertex paint and face groups.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"
#include "core/Voxel.h"
#include "io/ObjIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace sf;

namespace {

#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                           \
        const bool ok_ = (cond);                                                   \
        CHECK(ok_);                                                                \
        if (!ok_) std::printf("    %s\n", std::string(msg).c_str());             \
    } while (0)

// Edges that don't have exactly two triangles (boundary: one; non-manifold: three or more).
size_t openEdgeCount(const Mesh& m, size_t* boundary = nullptr) {
    std::vector<uint64_t> edges;
    edges.reserve(m.triangles.size() * 3);
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) edges.push_back(uint64_t(std::min(t[k], t[(k + 1) % 3])) << 32 | std::max(t[k], t[(k + 1) % 3]));
    std::sort(edges.begin(), edges.end());
    size_t open = 0;
    if (boundary) *boundary = 0;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j] == edges[i]) ++j;
        if (j - i != 2) ++open;
        if (j - i == 1 && boundary) ++*boundary;
        i = j;
    }
    return open;
}

std::vector<glm::vec3> vertexNormals(const Mesh& m) {
    std::vector<glm::vec3> n(m.positions.size(), glm::vec3(0.0f));
    for (const glm::uvec3& t : m.triangles) {
        const glm::vec3 fn = glm::cross(m.positions[t.y] - m.positions[t.x], m.positions[t.z] - m.positions[t.x]);
        n[t.x] += fn, n[t.y] += fn, n[t.z] += fn;
    }
    for (auto& v : n) v = glm::length(v) > 1e-20f ? glm::normalize(v) : glm::vec3(0, 1, 0);
    return n;
}

// |curvature| at each vertex, at scale `radius`: the mean over the vertices within it of (n_v - n_u).(p_v - p_u) / |p_v - p_u|^2
// (1/r on a sphere of radius r; independent of the mesh's own resolution). Taken at the cell of the remesh, grain shows up as extra.
std::vector<float> curvatureAt(const Mesh& m, float radius, const std::vector<glm::vec3>* normals = nullptr) {
    const std::vector<glm::vec3> n = normals ? *normals : vertexNormals(m);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    auto cellOf = [&](const glm::vec3& p) { return glm::ivec3(glm::floor(p / radius)); };
    auto key = [](const glm::ivec3& c) {
        return (uint64_t(uint32_t(c.x + (1 << 20)) & 0x1fffffu) << 42) | (uint64_t(uint32_t(c.y + (1 << 20)) & 0x1fffffu) << 21) |
               uint64_t(uint32_t(c.z + (1 << 20)) & 0x1fffffu);
    };
    for (uint32_t v = 0; v < m.positions.size(); ++v) grid[key(cellOf(m.positions[v]))].push_back(v);
    std::vector<float> k(m.positions.size(), 0.0f);
    for (uint32_t v = 0; v < m.positions.size(); ++v) {
        const glm::ivec3 c = cellOf(m.positions[v]);
        double sum = 0.0;
        int cnt = 0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    auto it = grid.find(key(c + glm::ivec3(dx, dy, dz)));
                    if (it == grid.end()) continue;
                    for (uint32_t u : it->second) {
                        if (u == v) continue;
                        const glm::vec3 d = m.positions[v] - m.positions[u];
                        const float d2 = glm::dot(d, d);
                        if (d2 > radius * radius || d2 < radius * radius * 0.01f) continue;
                        sum += double(glm::dot(n[v] - n[u], d)) / double(d2);
                        ++cnt;
                    }
                }
        k[v] = cnt ? float(std::abs(sum / cnt)) : 0.0f;
    }
    return k;
}

double meanOver(const Mesh& m, const std::vector<float>& k, const std::vector<glm::vec3>& region, float reach) {
    // Mean of k over the vertices of `m` within `reach` of any point of `region`.
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    auto cellOf = [&](const glm::vec3& p) { return glm::ivec3(glm::floor(p / reach)); };
    auto key = [](const glm::ivec3& c) {
        return (uint64_t(uint32_t(c.x + (1 << 20)) & 0x1fffffu) << 42) | (uint64_t(uint32_t(c.y + (1 << 20)) & 0x1fffffu) << 21) |
               uint64_t(uint32_t(c.z + (1 << 20)) & 0x1fffffu);
    };
    for (uint32_t i = 0; i < region.size(); ++i) grid[key(cellOf(region[i]))].push_back(i);
    double sum = 0.0;
    size_t cnt = 0;
    for (uint32_t v = 0; v < m.positions.size(); ++v) {
        const glm::ivec3 c = cellOf(m.positions[v]);
        bool near = false;
        for (int dz = -1; dz <= 1 && !near; ++dz)
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1 && !near; ++dx) {
                    auto it = grid.find(key(c + glm::ivec3(dx, dy, dz)));
                    if (it == grid.end()) continue;
                    for (uint32_t i : it->second)
                        if (glm::distance(region[i], m.positions[v]) <= reach) {
                            near = true;
                            break;
                        }
                }
        if (near) sum += k[v], ++cnt;
    }
    return cnt ? sum / double(cnt) : 0.0;
}

// Largest distance from the vertices of `a` to the surface of `b`.
float oneWayHausdorff(const Mesh& a, const TriangleBVH& b, float reach, double* meanOut = nullptr) {
    float worst = 0.0f;
    double sum = 0.0;
    for (const glm::vec3& p : a.positions) {
        SurfaceHit h;
        const float d = b.closestPoint(p, reach, h) ? h.distance : reach;
        worst = std::max(worst, d);
        sum += d;
    }
    if (meanOut) *meanOut = sum / double(std::max<size_t>(1, a.positions.size()));
    return worst;
}

struct Fidelity {
    size_t openEdges = 0, boundaryEdges = 0;
    float hausdorffPct = 0.0f; // of the longest side of the source's bounding box
    double curvSource = 0.0, curvResult = 0.0; // mean |curvature| in the stroked area (1/mm)
    double cell = 0.0;
    double volumePct = 0.0; // result volume against the source's
};

Fidelity measure(const Mesh& source, const Mesh& result, int resolution, const std::vector<glm::vec3>& region) {
    Fidelity f;
    f.openEdges = openEdgeCount(result, &f.boundaryEdges);
    const AABB box = source.bounds();
    const glm::vec3 ext = box.extent();
    const float longest = std::max({ext.x, ext.y, ext.z});
    f.cell = longest / resolution;
    TriangleBVH sBvh, rBvh;
    sBvh.build(source);
    rBvh.build(result);
    const float h = std::max(oneWayHausdorff(result, sBvh, longest), oneWayHausdorff(source, rBvh, longest));
    f.hausdorffPct = 100.0f * h / longest;
    // Curvature at the scale of the new surface's own edges (1.3 cells), where grain shows: the result's own (from its
    // face normals) against the source's smooth-shaded surface sampled at the same vertices (its normals interpolated), so
    // both use the same neighbours; the surface is what the artist sees, the faceting of its triangles is not.
    const std::vector<glm::vec3> sn = vertexNormals(source);
    Mesh ref = result;
    std::vector<glm::vec3> refNormals(result.positions.size(), glm::vec3(0, 1, 0));
    for (size_t v = 0; v < result.positions.size(); ++v) {
        SurfaceHit h;
        if (!sBvh.closestPoint(result.positions[v], float(f.cell) * 2.0f, h)) continue;
        const glm::uvec3 t = source.triangles[h.prim];
        refNormals[v] = glm::normalize(sn[t.x] * h.bary.x + sn[t.y] * h.bary.y + sn[t.z] * h.bary.z);
        ref.positions[v] = h.position;
    }
    const float R = float(f.cell) * 1.3f;
    f.curvSource = meanOver(ref, curvatureAt(ref, R, &refNormals), region, R);
    f.curvResult = meanOver(result, curvatureAt(result, R), region, R);
    double v0 = 0.0, v1 = 0.0;
    for (const glm::uvec3& t : source.triangles) v0 += glm::dot(source.positions[t.x], glm::cross(source.positions[t.y], source.positions[t.z])) / 6.0;
    for (const glm::uvec3& t : result.triangles) v1 += glm::dot(result.positions[t.x], glm::cross(result.positions[t.y], result.positions[t.z])) / 6.0;
    f.volumePct = 100.0 * (v1 - v0) / v0;
    return f;
}

// Vertices of `m` that moved (or sit) away from `reference`'s surface by more than `eps`: where the strokes landed.
std::vector<glm::vec3> strokedVertices(const Mesh& m, const Mesh& reference, float eps) {
    TriangleBVH bvh;
    bvh.build(reference);
    std::vector<glm::vec3> out;
    for (const glm::vec3& p : m.positions) {
        SurfaceHit h;
        if (!bvh.closestPoint(p, eps * 4.0f, h) || h.distance > eps) out.push_back(p);
    }
    return out;
}


void report(const char* what, const Mesh& source, const Mesh& result, const Fidelity& f) {
    std::printf("  %s: %zu tris -> %zu, open edges %zu (%zu boundary), Hausdorff %.3f%% of the longest side, volume %+.4f%%, mean |curvature| in the stroked area at the cell scale: smooth source %.4f, remesh %.4f (%+.1f%%)\n",
                what, source.triangleCount(), result.triangleCount(), f.openEdges, f.boundaryEdges, f.hausdorffPct, f.volumePct, f.curvSource, f.curvResult,
                100.0 * (f.curvResult / std::max(f.curvSource, 1e-12) - 1.0));
}

} // namespace

// A sphere with overlapping Clay strokes (dense and curved: the sculpt the remesh must keep).
TEST(remesh_invariant_clay_sphere) {
    CommandRegistry reg;
    const Mesh base = makeIcosphere(5, 1.0f); // the sphere the strokes start from (dynamic topology refines it as it goes)
    Document doc = test::makeDoc(makeIcosphere(5, 1.0f));
    CHECK(reg.execute(doc, "dyntopo", {{"enabled", true}, {"detail", 50.0}}).ok); // detail follows the brush, as in the session
    const char* paths[] = {"-0.5,0.2,1 -0.1,0.35,1 0.3,0.25,1 0.55,0.1,1", "-0.4,-0.1,1 0,0.0,1 0.45,-0.15,1",
                           "-0.3,0.45,1 0.1,0.5,1 0.4,0.4,1"};
    int seed = 1;
    for (const char* p : paths)
        CHECK(reg.execute(doc, "stroke",
                          {{"points", std::string(p)}, {"brush", std::string("Clay")}, {"radius", 0.18}, {"strength", 0.6}, {"seed", double(seed++)}})
                  .ok);
    const Mesh source = doc.mesh;
    const std::vector<glm::vec3> stroked = strokedVertices(source, base, 0.004f);
    CHECK(stroked.size() > 100);
    const int resolution = 200;
    std::string err;
    setVoxelDenoise(false);
    const Mesh plain = voxelRemesh(source, resolution, &err);
    setVoxelDenoise(true);
    const Fidelity before = measure(source, plain, resolution, stroked);
    report("clay sphere without the low-pass", source, plain, before);
    const Mesh result = voxelRemesh(source, resolution, &err);
    CHECK_MSG(result.triangleCount() > 0, err);
    const Fidelity f = measure(source, result, resolution, stroked);
    report("clay sphere", source, result, f);
    CHECK(f.boundaryEdges == 0);
    CHECK(f.hausdorffPct < 0.5f);
    CHECK(std::abs(f.volumePct) < 0.02);
    CHECK(f.curvResult < f.curvSource * 1.10); // no added high-frequency noise
    CHECK(f.curvResult < before.curvResult);   // and the field low-pass is what takes it out
}

// The artist session's sculpt (a creature head blocked out with Move, dynamic topology and Clay), when
// SF_REMESH_FIXTURE names a folder with its pre-remesh pre.obj (not in the repository: 3 MB).
TEST(remesh_invariant_artist_sculpt) {
    const char* dir = std::getenv("SF_REMESH_FIXTURE");
    if (!dir) return;
    Mesh source;
    std::string err;
    CHECK_MSG(loadObj((std::filesystem::path(dir) / "pre.obj").string(), source, err), err);
    const int resolution = 200;
    const Mesh result = voxelRemesh(source, resolution, &err);
    CHECK_MSG(result.triangleCount() > 0, err);
    // The stroked snout: the source's vertices off the unit sphere it started as (and below the horns).
    std::vector<glm::vec3> region;
    for (const glm::vec3& p : source.positions) {
        const float r = glm::length(p);
        if (std::abs(r - 1.0f) > 0.002f && r < 1.12f && p.y < 0.9f) region.push_back(p);
    }
    const Fidelity f = measure(source, result, resolution, region);
    report("artist sculpt", source, result, f);
    CHECK(f.boundaryEdges == 0 && f.openEdges <= 1); // (one non-manifold edge where surface nets meets a tip: as before)
    CHECK(f.hausdorffPct < 0.5f);
    CHECK(std::abs(f.volumePct) < 0.02);
    CHECK(f.curvResult < f.curvSource * 1.13); // 1.16 before the low-pass (#193)
}

// Mask, vertex paint and face groups come through the remesh.
TEST(remesh_invariant_carries_attributes) {
    Mesh m = makeIcosphere(5, 1.0f);
    m.mask.assign(m.positions.size(), 0.0f);
    m.colors.assign(m.positions.size(), glm::vec3(1.0f));
    m.facegroups.assign(m.triangles.size(), 0u);
    for (size_t v = 0; v < m.positions.size(); ++v) {
        if (m.positions[v].y > 0.3f) m.mask[v] = 1.0f;
        if (m.positions[v].x > 0.0f) m.colors[v] = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    for (size_t t = 0; t < m.triangles.size(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
        m.facegroups[t] = c.z > 0.0f ? 2u : 1u;
    }
    std::string err;
    const Mesh r = voxelRemesh(m, 100, &err);
    CHECK_MSG(r.triangleCount() > 0, err);
    CHECK(r.facegroups.size() == r.triangles.size());
    CHECK(r.mask.size() == r.positions.size() && r.colors.size() == r.positions.size());
    size_t wrongGroup = 0, wrongMask = 0, wrongColour = 0, quadsKept = 0;
    for (size_t t = 0; t < r.triangles.size(); ++t) {
        const glm::uvec3 tri = r.triangles[t];
        const glm::vec3 c = (r.positions[tri.x] + r.positions[tri.y] + r.positions[tri.z]) / 3.0f;
        if (std::abs(c.z) > 0.04f && r.facegroups[t] != (c.z > 0.0f ? 2u : 1u)) ++wrongGroup;
        if (r.quadPartnerAt(uint32_t(t)) != Mesh::kNoQuad) ++quadsKept;
    }
    for (size_t v = 0; v < r.positions.size(); ++v) {
        if (std::abs(r.positions[v].y - 0.3f) > 0.04f && (r.mask[v] > 0.5f) != (r.positions[v].y > 0.3f)) ++wrongMask;
        if (std::abs(r.positions[v].x) > 0.04f && (r.colors[v].y < 0.5f) != (r.positions[v].x > 0.0f)) ++wrongColour;
    }
    std::printf("  carried: %zu triangles, wrong group %zu, wrong mask %zu, wrong colour %zu, in quads %zu\n", r.triangles.size(), wrongGroup, wrongMask, wrongColour, quadsKept);
    CHECK(wrongGroup == 0 && wrongMask == 0 && wrongColour == 0);
    CHECK(quadsKept == r.triangles.size()); // the result is still all quads
}
