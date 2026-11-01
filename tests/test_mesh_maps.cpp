// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mesh Maps (core/MeshMaps.h) and the bake's new maps (core/Bake.h): the mean curvature estimate, AO in a trench,
// thickness of thin and thick shapes, position and world normal, live updates matching a full bake texel for
// texel, UDIM tiles, and baking a Part's top subdivision level onto its lowest.
#include "TestHarness.h"

#include "core/Bake.h"
#include "core/MeshMaps.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Texture.h"

#include <chrono>
#include <cmath>

using namespace sf;

namespace {

void planarUvs(Mesh& m, int axisA = 0, int axisB = 2) {
    const AABB b = m.bounds();
    const glm::vec3 e = glm::max(b.extent(), glm::vec3(1e-6f));
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2((p[axisA] - b.min[axisA]) / e[axisA], (p[axisB] - b.min[axisB]) / e[axisB]) * 0.96f + 0.02f;
        }
}

// Spherical UVs with the seam column duplicated per triangle (good enough for maps on a sphere).
void sphereUvs(Mesh& m) {
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        glm::vec2 uv[3];
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = glm::normalize(m.positions[m.triangles[t][c]]);
            uv[c] = glm::vec2(0.5f + std::atan2(p.z, p.x) / 6.2831853f, 0.5f + std::asin(std::clamp(p.y, -1.0f, 1.0f)) / 3.1415927f);
        }
        for (int c = 1; c < 3; ++c) // (keep a triangle on one side of the seam)
            if (uv[c].x - uv[0].x > 0.5f) uv[c].x -= 1.0f;
            else if (uv[0].x - uv[c].x > 0.5f) uv[c].x += 1.0f;
        for (int c = 0; c < 3; ++c) m.uvs[t * 3 + size_t(c)] = glm::vec2(std::clamp(uv[c].x, 0.0f, 1.0f), uv[c].y);
    }
}

// A plane with a V trench along z in the middle.
Mesh trench() {
    Mesh m = makePlane(64);
    for (glm::vec3& p : m.positions) p.y = -0.6f * std::max(0.0f, 0.3f - std::abs(p.x));
    m.recomputeAllNormals();
    planarUvs(m);
    return m;
}

// The mean value of a scalar map over texels whose texel-map triangle exists and satisfies `where`.
template <class Where>
double meanWhere(const Mesh& m, const MeshMapSet& s, MeshMapKind k, Where where) {
    double sum = 0;
    size_t n = 0;
    const int tilesX = (s.width + 63) / 64, tilesY = (s.height + 63) / 64;
    for (int ty = 0; ty < tilesY; ++ty)
        for (int tx = 0; tx < tilesX; ++tx) {
            TexelTile tt;
            if (!texelTile(m, s.width, s.height, uint32_t(ty * tilesX + tx), tt)) continue;
            for (int li = 0; li < 64 * 64; ++li) {
                const uint32_t t = tt.tri[size_t(li)];
                if (t == kNoTexelTriangle) continue;
                const glm::vec2 b = tt.bary[size_t(li)];
                const glm::uvec3 tv = m.triangles[t];
                const glm::vec3 p = m.positions[tv.x] * (1 - b.x - b.y) + m.positions[tv.y] * b.x + m.positions[tv.z] * b.y;
                if (!where(p)) continue;
                sum += s.value(k, tx * 64 + li % 64, ty * 64 + li / 64);
                ++n;
            }
        }
    return n ? sum / double(n) : -1.0;
}

} // namespace

TEST(mesh_maps_mean_curvature_is_one_over_radius) {
    for (int sub : {3, 5})
        for (float radius : {1.0f, 2.5f}) {
            const Mesh m = makeIcosphere(sub, radius);
            const std::vector<float> h = meshMeanCurvature(m);
            double mean = 0;
            for (float v : h) mean += v;
            mean /= double(h.size());
            CHECK(std::abs(mean * radius - 1.0) < 0.03); // density-independent, convex > 0
        }
    // A flat plane has none.
    const std::vector<float> flat = meshMeanCurvature(makePlane(16));
    float worst = 0;
    for (float v : flat) worst = std::max(worst, std::abs(v));
    CHECK(worst < 1e-3f);
}

TEST(mesh_maps_ao_curvature_and_normal_on_a_trench) {
    const Mesh m = trench();
    MeshMapSet s;
    std::string err;
    MeshMapSettings st;
    st.aoRays = 48;
    st.aoDistance = 0.2f;
    CHECK(bakeMeshMaps(m, 256, 256, kAllMeshMaps, st, s, err));
    CHECK(s.kinds == kAllMeshMaps && s.texelsBaked > 50000);
    const double floorAo = meanWhere(m, s, MeshMapKind::AmbientOcclusion, [](glm::vec3 p) { return std::abs(p.x) < 0.04f; });
    const double flatAo = meanWhere(m, s, MeshMapKind::AmbientOcclusion, [](glm::vec3 p) { return std::abs(p.x) > 0.8f; });
    std::printf("  trench AO %.3f, flat AO %.3f\n", floorAo, flatAo);
    CHECK(flatAo > 0.97);
    CHECK(floorAo < flatAo - 0.15);
    // The trench's floor is concave (dark curvature), its rims convex (bright), the flat mid-grey.
    const double floorCurv = meanWhere(m, s, MeshMapKind::Curvature, [](glm::vec3 p) { return std::abs(p.x) < 0.02f; });
    const double rimCurv = meanWhere(m, s, MeshMapKind::Curvature, [](glm::vec3 p) { return std::abs(std::abs(p.x) - 0.3f) < 0.02f; });
    const double flatCurv = meanWhere(m, s, MeshMapKind::Curvature, [](glm::vec3 p) { return std::abs(p.x) > 0.8f; });
    std::printf("  curvature floor %.3f rim %.3f flat %.3f\n", floorCurv, rimCurv, flatCurv);
    CHECK(std::abs(flatCurv - 0.5) < 0.01);
    CHECK(floorCurv < 0.45 && rimCurv > 0.55);
    // World normal on the flat part points up; position spans the box.
    const int x = 250, y = 128; // near +x edge
    CHECK(s.vector(MeshMapKind::WorldNormal, x, y).y > 0.99f);
    CHECK(s.vector(MeshMapKind::Position, x, y).x > 0.95f);
    CHECK(s.vector(MeshMapKind::Position, 5, y).x < 0.05f);
}

TEST(mesh_maps_thickness_tells_thin_from_thick) {
    Mesh ball = makeIcosphere(4, 1.0f);
    sphereUvs(ball);
    Mesh slab = makeCube(8, 2.0f);
    for (glm::vec3& p : slab.positions) p.y *= 0.03f;
    slab.recomputeAllNormals();
    planarUvs(slab);
    MeshMapSettings st;
    st.thicknessRays = 24;
    st.thicknessDistance = 0.1f;
    MeshMapSet a, b;
    std::string err;
    CHECK(bakeMeshMaps(ball, 128, 128, meshMapBit(MeshMapKind::Thickness), st, a, err));
    CHECK(bakeMeshMaps(slab, 128, 128, meshMapBit(MeshMapKind::Thickness), st, b, err));
    const double thick = meanWhere(ball, a, MeshMapKind::Thickness, [](glm::vec3) { return true; });
    const double thin = meanWhere(slab, b, MeshMapKind::Thickness, [](glm::vec3 p) { return std::abs(p.x) < 0.8f && std::abs(p.z) < 0.8f; });
    std::printf("  thickness sphere %.3f slab %.3f\n", thick, thin);
    CHECK(thick > 0.9 && thin < 0.5);
}

TEST(mesh_maps_live_update_matches_a_full_bake) {
    Mesh m = trench();
    MeshMapSettings st;
    st.aoRays = 16;
    st.thicknessRays = 8;
    auto set = std::make_shared<MeshMapSet>();
    std::string err;
    CHECK(bakeMeshMaps(m, 256, 256, kAllMeshMaps, st, *set, err));
    // Sculpt: a dent near one corner of the plane (inside the bounding box, which sets the maps' ray distances).
    const glm::vec3 c(-0.6f, 0.0f, -0.6f);
    for (glm::vec3& p : m.positions) {
        const float d = glm::distance(glm::vec2(p.x, p.z), glm::vec2(c.x, c.z));
        if (d < 0.15f) p.y -= 0.1f * (0.5f + 0.5f * std::cos(d / 0.15f * 3.14159265f));
    }
    m.recomputeAllNormals();
    std::vector<uint32_t> moved;
    const AABB region = changedRegion(*set, m, &moved);
    CHECK(region.valid() && !moved.empty());
    MeshMapUpdate up;
    CHECK(prepareMeshMapUpdate(m, set, region, moved, up));
    const size_t allTiles = size_t(4 * 4);
    std::printf("  update: %zu of %zu tiles, %zu local triangles of %zu\n", up.tiles.size(), allTiles, up.local.triangleCount(),
                m.triangleCount());
    CHECK(up.tiles.size() < allTiles);
    CHECK(up.local.triangleCount() < m.triangleCount());
    runMeshMapUpdate(up);
    CHECK(up.ok);
    const std::shared_ptr<MeshMapSet> updated = applyMeshMapUpdate(up);
    CHECK(updated && updated->revision == set->revision + 1);
    CHECK(!changedRegion(*updated, m).valid()); // the positions it remembers are the sculpted ones
    MeshMapSet full;
    CHECK(bakeMeshMaps(m, 256, 256, kAllMeshMaps, st, full, err));
    // (Position keeps the box of the first bake, so procedural patterns don't slide as the Part grows: compare the
    // other maps.)
    size_t differ = 0, total = 0;
    for (int k = 0; k < kMeshMapCount; ++k) {
        if (MeshMapKind(k) == MeshMapKind::Position) continue;
        size_t before = differ;
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                float a[4] = {}, b[4] = {};
                updated->images[size_t(k)].values(x, y, a);
                full.images[size_t(k)].values(x, y, b);
                ++total;
                for (int i = 0; i < 3; ++i)
                    if (std::abs(a[i] - b[i]) > 1.0f / 255.0f) {
                        ++differ;
                        break;
                    }
            }
        if (differ > before) std::printf("  %s: %zu texels differ\n", meshMapId(MeshMapKind(k)), differ - before);
    }
    std::printf("  %zu of %zu texels differ from a full bake\n", differ, total);
    CHECK(differ == 0);
}

TEST(mesh_maps_live_update_is_faster_than_a_full_bake) {
    // A denser Part: a 2K map of a 130k-triangle sphere, then a small local change.
    Mesh m = makeIcosphere(6, 1.0f);
    sphereUvs(m);
    MeshMapSettings st;
    auto set = std::make_shared<MeshMapSet>();
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(bakeMeshMaps(m, 2048, 2048, kAllMeshMaps, st, *set, err));
    const double full = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    for (glm::vec3& p : m.positions)
        if (glm::distance(p, glm::vec3(0, 0, 1)) < 0.08f) p *= 1.02f;
    m.recomputeAllNormals();
    std::vector<uint32_t> moved;
    const auto t1 = std::chrono::steady_clock::now();
    const AABB region = changedRegion(*set, m, &moved);
    MeshMapUpdate up;
    CHECK(prepareMeshMapUpdate(m, set, region, moved, up));
    runMeshMapUpdate(up);
    CHECK(applyMeshMapUpdate(up) != nullptr);
    const double live = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("  2K maps of %zu triangles: full bake %.2f s, live update of %zu tiles %.3f s\n", m.triangleCount(), full,
                up.tiles.size(), live);
    CHECK(live < full * 0.5);
}

TEST(bake_new_maps_udim_tiles_and_levels) {
    // A plane with its right half moved to UDIM tile 1002.
    Mesh low = makePlane(16);
    planarUvs(low);
    for (size_t t = 0; t < low.triangleCount(); ++t) {
        const glm::vec2 c = (low.uvs[t * 3] + low.uvs[t * 3 + 1] + low.uvs[t * 3 + 2]) / 3.0f;
        if (c.x > 0.5f)
            for (int i = 0; i < 3; ++i) low.uvs[t * 3 + size_t(i)].x += 1.0f;
    }
    CHECK(udimTiles(low) == std::vector<int>({1001, 1002}));
    Mesh high = makePlane(64);
    for (glm::vec3& p : high.positions) p.y = 0.02f * std::sin(p.x * 20.0f);
    high.recomputeAllNormals();
    BakeSettings st;
    st.size = 128;
    st.maps = bakeBit(BakeMap::Normal) | bakeBit(BakeMap::Curvature) | bakeBit(BakeMap::Thickness) | bakeBit(BakeMap::Position) |
              bakeBit(BakeMap::Id);
    std::vector<BakeResult> results;
    std::string err;
    CHECK(bakeMapsUdim(low, {&high}, st, results, err));
    CHECK(results.size() == 2 && results[0].udim == 1001 && results[1].udim == 1002);
    for (const BakeResult& r : results) {
        CHECK(r.islandTexels > 2000);
        CHECK(r.curvature.size() == 128 * 128 && r.position.size() == 128 * 128 * 3 && r.id.size() == 128 * 128 * 3);
    }
    CHECK(bakeFileName("Low", BakeMap::Curvature, ".png", 1002) == "Low_curvature.1002.png");
    std::vector<std::string> written;
    const std::string dir = tempPath("sf_bake_udim");
    CHECK(saveBakeMaps(results[1], dir, "Low", false, written, err));
    CHECK(written.size() == 5);

    // A Part's own levels: its top subdivision level (with sculpted ripples) baked onto its lowest.
    Mesh part = makeQuadSphere(6);
    sphereUvs(part);
    std::string e2;
    CHECK(subdivDivide(part, true, e2));
    CHECK(subdivDivide(part, true, e2));
    for (glm::vec3& p : part.positions) p *= 1.0f + 0.03f * std::sin(p.y * 30.0f);
    part.recomputeAllNormals();
    Mesh l, h;
    CHECK(bakeLevelSources(part, "subdivision", l, h, e2));
    CHECK(l.triangleCount() * 16 == h.triangleCount());
    BakeSettings ns;
    ns.size = 128;
    ns.maps = bakeBit(BakeMap::Normal);
    BakeResult nr;
    CHECK(bakeMaps(l, {&h}, ns, nr, e2));
    // The ripples show in the normal map: many texels lean away from flat.
    size_t leaning = 0;
    for (size_t i = 0; i < nr.normal.size(); i += 3) leaning += nr.normal[i + 2] < 245;
    std::printf("  level bake: %zu of %zu texels lean\n", leaning, nr.normal.size() / 3);
    CHECK(leaning > 1000);
    Mesh noLevels = makeIcosphere(2);
    CHECK(!bakeLevelSources(noLevels, "subdivision", l, h, e2));
    CHECK(!bakeLevelSources(noLevels, "micro", l, h, e2));
}
