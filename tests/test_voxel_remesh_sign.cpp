// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voxel Remesh of a decimated surface with deep, narrow creases (gh#240: face wrinkles, eye bags, nasolabial folds): the
// walls of a crease are closer than a few cells, and the remesh must follow the crease, not fill it (the wall closing
// seals a gap under about 4 cells and the sign pass used to give up 2 cells below its mouth, leaving speckled sheets).
// The repro is our own: a closed slab whose top has V-grooves 1.5-16 cells wide at the top and 12-24 cells deep, in
// long thin triangles (what decimation leaves along a fold).
#include "TestHarness.h"

#include "core/Mesh.h"
#include "core/SurfaceQuery.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

using namespace sf;

#define CHECK_MSG(cond, msg)                                                       \
    do {                                                                           \
        const bool ok_ = (cond);                                                   \
        CHECK(ok_);                                                                \
        if (!ok_) std::printf("    %s\n", std::string(msg).c_str());             \
    } while (0)

namespace {

constexpr float kSize = 2.0f; // the slab is kSize x kSize, 0.5 thick before the grooves

// Groove profile along x: a V centred on each of `centres`, `width` wide at the top and `depth` deep; flat elsewhere.
float profile(float x, const std::vector<float>& centres, float width, float depth) {
    float h = 0.0f;
    for (float c : centres) {
        const float t = 1.0f - std::abs(x - c) / (0.5f * width);
        if (t > 0.0f) h = std::min(h, -depth * t);
    }
    return h;
}

// A closed slab: the top is a grid with samples dense across x (the grooves) and coarse along y (long thin triangles),
// walls and a flat bottom.
Mesh creasedSlab(float width, float depth, bool wobble, float phase) {
    const std::vector<float> centres = {-0.55f + phase, -0.1f + phase, 0.4f + phase};
    std::vector<float> xs;
    for (float x = -1.0f; x <= 1.0001f; x += 0.05f) xs.push_back(x);
    for (float c : centres)
        for (float d : {-0.5f * width, -0.25f * width, 0.0f, 0.25f * width, 0.5f * width}) xs.push_back(c + d);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [](float a, float b) { return std::abs(a - b) < 1e-5f; }), xs.end());
    const std::vector<float> ys = {-1.0f, -0.6f, -0.2f, 0.3f, 0.7f, 1.0f}; // (6 rows over 2 units: 0.4-long triangles)
    const float top = 0.25f, bottom = -0.25f;
    Mesh m;
    const size_t nx = xs.size(), ny = ys.size();
    for (size_t j = 0; j < ny; ++j)
        for (size_t i = 0; i < nx; ++i) {
            // (a slanted crease: each row shifted along x, so the grooves drift with y and their walls aren't axis aligned)
            const float shift = wobble ? 0.08f * std::sin(ys[j] * 2.0f) : 0.0f;
            m.positions.push_back({xs[i] + shift, ys[j], top + profile(xs[i], centres, width, depth)});
        }
    auto top_at = [&](size_t i, size_t j) { return uint32_t(j * nx + i); };
    for (size_t j = 0; j + 1 < ny; ++j)
        for (size_t i = 0; i + 1 < nx; ++i) {
            const uint32_t a = top_at(i, j), b = top_at(i + 1, j), c = top_at(i + 1, j + 1), d = top_at(i, j + 1);
            m.triangles.push_back({a, b, c});
            m.triangles.push_back({a, c, d});
        }
    // Bottom and walls: a loop of border vertices around the top grid, mirrored at the bottom.
    std::vector<uint32_t> border;
    for (size_t i = 0; i < nx; ++i) border.push_back(top_at(i, 0));
    for (size_t j = 1; j < ny; ++j) border.push_back(top_at(nx - 1, j));
    for (size_t i = nx - 1; i-- > 0;) border.push_back(top_at(i, ny - 1));
    for (size_t j = ny - 1; j-- > 1;) border.push_back(top_at(0, j));
    std::vector<uint32_t> low;
    for (uint32_t v : border) {
        glm::vec3 p = m.positions[v];
        p.z = bottom;
        low.push_back(uint32_t(m.positions.size()));
        m.positions.push_back(p);
    }
    const size_t nb = border.size();
    for (size_t k = 0; k < nb; ++k) { // walls, facing out (the border runs counter-clockwise seen from +z)
        const size_t n = (k + 1) % nb;
        m.triangles.push_back({border[k], low[k], low[n]});
        m.triangles.push_back({border[k], low[n], border[n]});
    }
    const uint32_t centre = uint32_t(m.positions.size());
    m.positions.push_back({0.0f, 0.0f, bottom});
    for (size_t k = 0; k < nb; ++k) m.triangles.push_back({centre, low[(k + 1) % nb], low[k]});
    m.rebuildTopology();
    return m;
}

float signedVolume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(glm::dvec3(m.positions[t.x]), glm::cross(glm::dvec3(m.positions[t.y]), glm::dvec3(m.positions[t.z])));
    return float(v / 6.0);
}

size_t components(const Mesh& m) {
    std::vector<uint32_t> parent(m.vertexCount());
    std::iota(parent.begin(), parent.end(), 0u);
    auto find = [&](uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (const glm::uvec3& t : m.triangles) {
        parent[find(t.y)] = find(t.x);
        parent[find(t.z)] = find(t.x);
    }
    std::vector<uint8_t> used(m.vertexCount(), 0), seen(m.vertexCount(), 0);
    for (const glm::uvec3& t : m.triangles) used[t.x] = used[t.y] = used[t.z] = 1;
    size_t n = 0;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (used[v] && !seen[find(v)]) seen[find(v)] = 1, ++n;
    return n;
}

struct CreaseError {
    float worstOut = 0.0f;   // cells: the farthest a remesh vertex lies from the source (extra voxels, a filled crease)
    float worstIn = 0.0f;    // cells: the farthest a source point in a groove lies from the remesh (a crease lost)
    float volumeRatio = 0.0f;
    size_t pieces = 0;
};

CreaseError measure(const Mesh& source, const Mesh& out, float cell, float grooveTop) {
    CreaseError e;
    TriangleBVH srcBvh, outBvh;
    srcBvh.build(source);
    outBvh.build(out);
    SurfaceHit hit;
    for (const glm::vec3& p : out.positions)
        if (srcBvh.closestPoint(p, 1e9f, hit)) e.worstOut = std::max(e.worstOut, hit.distance / cell);
    // The source's own groove vertices and the middle of each triangle that lies below the slab's flat top.
    auto probe = [&](const glm::vec3& p) {
        if (p.z < grooveTop - 1e-4f && std::abs(p.x) < 0.9f && std::abs(p.y) < 0.9f && outBvh.closestPoint(p, 1e9f, hit))
            e.worstIn = std::max(e.worstIn, hit.distance / cell);
    };
    for (const glm::vec3& p : source.positions) probe(p);
    for (const glm::uvec3& t : source.triangles) probe((source.positions[t.x] + source.positions[t.y] + source.positions[t.z]) / 3.0f);
    e.volumeRatio = std::abs(signedVolume(out)) / std::abs(signedVolume(source));
    e.pieces = components(out);
    return e;
}

} // namespace

// The grooves' x offset (cells' worth of phase against the grid: a crease seen at a different place between samples).
constexpr float kPhases[] = {0.0f, 0.0037f, -0.0061f};

// Creases 1.5, 2.5 and 4 cells wide at the top, 12 cells deep, at Resolution 100 and 200, straight and slanted: the remesh
// is one piece (no stray sheets or one-voxel bits along the crease) and stays near the source.
TEST(voxel_remesh_invariant_creases_stay_one_piece) {
    for (int resolution : {100, 200}) {
        const float cell = kSize / float(resolution);
        for (float cells : {1.5f, 2.5f, 4.0f})
            for (bool wobble : {false, true})
                for (float phase : kPhases) {
                    const Mesh source = creasedSlab(cells * cell, 12.0f * cell, wobble, phase);
                    std::string error;
                    const Mesh out = voxelRemesh(source, resolution, &error);
                    CHECK_MSG(out.triangleCount() > 0, error);
                    if (out.triangleCount() == 0) continue;
                    const CreaseError e = measure(source, out, cell, 0.25f);
                    CHECK_MSG(e.pieces == 1, "res " + std::to_string(resolution) + " width " + std::to_string(cells) + " wobble " +
                                                 std::to_string(int(wobble)) + ": " + std::to_string(e.pieces) + " pieces");
                    CHECK_MSG(e.worstOut < 2.0f, "res " + std::to_string(resolution) + " width " + std::to_string(cells) + " wobble " +
                                                     std::to_string(int(wobble)) + ": " + std::to_string(e.worstOut) + " cells off the source");
                }
    }
}

// How deep the remesh follows a V groove 24 cells deep, by its width at the top: it used to stop where the groove
// narrowed to about 4 cells and fill the rest (a ridge above the groove); now it goes on to where the groove is under a cell wide.
TEST(voxel_remesh_invariant_follows_creases_down_to_the_tip) {
    for (int resolution : {100, 200}) {
        const float cell = kSize / float(resolution);
        for (float cells : {6.0f, 8.0f, 12.0f, 16.0f}) {
            const Mesh source = creasedSlab(cells * cell, 24.0f * cell, false, 0.0037f);
            const Mesh out = voxelRemesh(source, resolution);
            TriangleBVH bvh;
            bvh.build(out);
            SurfaceHit hit;
            const float x = -0.55f + 0.0037f;
            CHECK(bvh.raycast({x, 0.31f, 1.0f}, {0, 0, -1}, 5.0f, hit));
            const float followed = (0.25f - (1.0f - hit.distance)) / cell; // (cells below the top, at the groove's centre)
            // (down to where the groove is about a cell wide: width cells at the top, narrowing to a point 24 cells down)
            CHECK_MSG(followed >= 24.0f * (1.0f - 1.2f / cells), "res " + std::to_string(resolution) + " width " + std::to_string(cells) +
                                                                          ": " + std::to_string(followed) + " of 24 cells");
        }
    }
}

// The brick grid (above Resolution 512 in the app) opens sealed creases as the dense grid does: the same points and triangles.
TEST(voxel_remesh_invariant_creases_bricks_match_dense) {
    const float cell = kSize / 100.0f;
    const Mesh source = creasedSlab(2.5f * cell, 12.0f * cell, true, 0.0037f);
    setVoxelSparseAbove(kVoxelDenseMax);
    const Mesh dense = voxelRemesh(source, 100);
    setVoxelSparseAbove(0);
    const Mesh bricks = voxelRemesh(source, 100);
    setVoxelSparseAbove(kVoxelDenseMax);
    CHECK(dense.triangleCount() > 0);
    CHECK(dense.triangleCount() == bricks.triangleCount());
    CHECK(dense.vertexCount() == bricks.vertexCount());
    if (dense.vertexCount() != bricks.vertexCount()) return;
    float worst = 0.0f;
    for (size_t i = 0; i < dense.vertexCount(); ++i) worst = std::max(worst, glm::distance(dense.positions[i], bricks.positions[i]));
    CHECK(worst < 1e-6f);
}
