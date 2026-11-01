// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UV Editor (core/Uv.h): UV topology, the unwrap solvers (LSCM, least stretch, polygroups, planar, unfold), cut
// graphs for closed pieces, packing into one tile and across UDIM tiles, texel density, trim sheets, seams,
// weld / split / straighten, the uv_* commands with undo, painted textures following the UVs, and the cost on
// a million triangles.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/Uv.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sf;

namespace {

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// Every UV inside [lo, hi] (with a little slack).
bool uvsWithin(const Mesh& m, glm::vec2 lo, glm::vec2 hi) {
    for (const glm::vec2& u : m.uvs)
        if (u.x < lo.x - 1e-4f || u.y < lo.y - 1e-4f || u.x > hi.x + 1e-4f || u.y > hi.y + 1e-4f) return false;
    return true;
}

// True when no two islands' UV bounding boxes overlap by more than `slack`.
bool islandBoxesDisjoint(const Mesh& m, float slack = 1e-5f) {
    const UvTopology topo = buildUvTopology(m);
    std::vector<glm::vec2> lo(topo.islandCount, glm::vec2(1e30f)), hi(topo.islandCount, glm::vec2(-1e30f));
    for (uint32_t c = 0; c < m.uvs.size(); ++c) {
        const uint32_t i = topo.island[c / 3];
        lo[i] = glm::min(lo[i], m.uvs[c]);
        hi[i] = glm::max(hi[i], m.uvs[c]);
    }
    for (uint32_t a = 0; a < topo.islandCount; ++a)
        for (uint32_t b = a + 1; b < topo.islandCount; ++b) {
            const glm::vec2 o = glm::min(hi[a], hi[b]) - glm::max(lo[a], lo[b]);
            if (o.x > slack && o.y > slack) return false;
        }
    return true;
}

size_t flippedTriangles(const Mesh& m) {
    std::vector<float> s;
    uvTriangleStretch(m, s);
    return size_t(std::count(s.begin(), s.end(), -1.0f));
}

// A cube whose six sides are polygroups 1-6 (makeCube's), without UVs.
Mesh cube(int res = 6) {
    Mesh m = makeCube(res);
    m.uvs.clear();
    return m;
}

CommandResult run(Document& doc, const char* name, Args args = {}) {
    static CommandRegistry reg;
    return reg.execute(doc, name, args);
}

} // namespace

TEST(uv_corner_twins_on_a_plane) {
    const Mesh m = makePlane(4);
    const std::vector<uint32_t> twin = cornerTwins(m);
    size_t open = 0;
    for (uint32_t c = 0; c < twin.size(); ++c) {
        if (twin[c] == kNoCorner) {
            ++open;
            continue;
        }
        CHECK(twin[twin[c]] == c);
        // The twin runs the same edge the other way.
        CHECK(m.triangles[c / 3][c % 3] == m.triangles[twin[c] / 3][(twin[c] % 3 + 1) % 3]);
    }
    CHECK(open == 16); // 4 x 4 cells: 16 border edges
}

TEST(uv_topology_islands_and_seams) {
    Mesh m = makePlane(4);
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (uint32_t j = 0; j < 3; ++j) {
            const glm::vec3 p = m.positions[m.triangles[t][j]];
            m.uvs[t * 3 + j] = glm::vec2(p.x, p.z) * 0.5f + 0.5f;
        }
    UvTopology topo = buildUvTopology(m);
    CHECK(topo.islandCount == 1);
    CHECK(topo.uvVertexCount == m.vertexCount());
    // A seam right across splits it in two.
    std::vector<uint32_t> cut;
    for (uint32_t c = 0; c < topo.twin.size(); ++c) {
        if (topo.twin[c] == kNoCorner) continue;
        const glm::vec3 a = m.positions[m.triangles[c / 3][c % 3]], b = m.positions[m.triangles[c / 3][(c % 3 + 1) % 3]];
        if (std::abs(a.x) < 1e-5f && std::abs(b.x) < 1e-5f) cut.push_back(c);
    }
    CHECK(!cut.empty());
    uvSetSeams(m, topo.twin, cut, true);
    CHECK(uvSeamCount(m) == 4);
    topo = buildUvTopology(m);
    CHECK(topo.islandCount == 2);
    CHECK(topo.uvVertexCount == m.vertexCount() + 5); // the 5 vertices on the cut, once more
    uvClearSeams(m);
    CHECK(m.uvFlags.empty());
    CHECK(buildUvTopology(m).islandCount == 1);
}

TEST(uv_lscm_flattens_a_plane_exactly) {
    // A flat, irregular piece flattens to a similarity of itself: no stretch at all.
    Mesh m = makePlane(12);
    for (glm::vec3& p : m.positions) p += glm::vec3(0.05f * std::sin(7.0f * p.z), 0.0f, 0.04f * std::cos(5.0f * p.x));
    std::vector<glm::vec2> uv;
    CHECK(flattenConformal(m.positions, m.triangles, uv));
    m.uvs.resize(m.triangleCount() * 3);
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (uint32_t j = 0; j < 3; ++j) m.uvs[t * 3 + j] = uv[m.triangles[t][j]];
    const UvStretchStats st = uvStretchStats(m);
    CHECK(st.mean < 1e-3f);
    CHECK(st.flipped == 0.0f);
}

TEST(uv_least_stretch_beats_conformal_on_a_curved_piece) {
    // A dome (a hemisphere's top): conformal keeps angles but shrinks the rim; least stretch keeps lengths better.
    Mesh m = makeIcosphere(4);
    std::vector<uint32_t> keep;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3 tri = m.triangles[t];
        if (m.positions[tri.x].y > 0.5f && m.positions[tri.y].y > 0.5f && m.positions[tri.z].y > 0.5f) keep.push_back(t);
    }
    std::vector<glm::uvec3> tris;
    for (uint32_t t : keep) tris.push_back(m.triangles[t]);
    // Compact the vertices.
    std::vector<uint32_t> remap(m.vertexCount(), ~0u);
    std::vector<glm::vec3> pos;
    for (glm::uvec3& t : tris)
        for (int j = 0; j < 3; ++j) {
            if (remap[t[j]] == ~0u) remap[t[j]] = uint32_t(pos.size()), pos.push_back(m.positions[t[j]]);
            t[j] = remap[t[j]];
        }
    auto stretchOf = [&](const std::vector<glm::vec2>& uv) {
        Mesh d;
        d.positions = pos;
        d.triangles = tris;
        d.uvs.resize(tris.size() * 3);
        for (size_t t = 0; t < tris.size(); ++t)
            for (int j = 0; j < 3; ++j) d.uvs[t * 3 + size_t(j)] = uv[tris[t][j]];
        return uvStretchStats(d);
    };
    std::vector<glm::vec2> conformal, isometric;
    CHECK(flattenConformal(pos, tris, conformal));
    CHECK(flattenIsometric(pos, tris, isometric, 10));
    const UvStretchStats a = stretchOf(conformal), b = stretchOf(isometric);
    std::printf("  dome: conformal stretch %.4f, least stretch %.4f\n", double(a.mean), double(b.mean));
    CHECK(a.flipped == 0.0f);
    CHECK(b.flipped == 0.0f);
    CHECK(b.mean < a.mean);
    CHECK(b.mean < 0.1f);
}

TEST(uv_unwrap_closed_sphere_gets_cut_automatically) {
    Document doc = makeDoc(makeIcosphere(4));
    doc.mesh.uvs.clear();
    const CommandResult r = run(doc, "uv_unwrap", {{"method", std::string("isometric")}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(doc.mesh.hasUvs());
    CHECK(r.message.find("cut automatically") != std::string::npos);
    CHECK(buildUvTopology(doc.mesh).islandCount == 2); // split across the middle: two domes
    CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(1.0f)));
    CHECK(flippedTriangles(doc.mesh) == 0);
    CHECK(uvStretchStats(doc.mesh).mean < 0.3f);
}

TEST(uv_unwrap_by_polygroups_gives_one_flat_island_per_side) {
    Document doc = makeDoc(cube());
    const CommandResult r = run(doc, "uv_unwrap", {{"method", std::string("polygroups")}});
    CHECK(r.ok);
    const UvTopology topo = buildUvTopology(doc.mesh);
    CHECK(topo.islandCount == 6);
    const UvStretchStats st = uvStretchStats(doc.mesh);
    CHECK(st.mean < 1e-3f); // flat sides: no stretch
    CHECK(st.flipped == 0.0f);
    CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(1.0f)));
    CHECK(islandBoxesDisjoint(doc.mesh));
    // Equal texel density across the sides.
    const std::vector<float> d = uvIslandDensity(doc.mesh, topo, 2048);
    for (float x : d) CHECK(std::abs(x - d[0]) < d[0] * 0.01f);
}

TEST(uv_unwrap_planar_and_conformal_methods) {
    for (const char* method : {"planar", "conformal", "auto"}) {
        Document doc = makeDoc(cube(4));
        uvSeamsFromPolygroups(doc.mesh, cornerTwins(doc.mesh));
        const CommandResult r = run(doc, "uv_unwrap", {{"method", std::string(method)}});
        CHECK(r.ok);
        CHECK(doc.mesh.hasUvs());
        CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(1.0f)));
        CHECK(flippedTriangles(doc.mesh) == 0);
        if (std::string(method) != "auto") CHECK(buildUvTopology(doc.mesh).islandCount == 6);
    }
}

TEST(uv_unfold_keeps_pins_and_layout) {
    Document doc = makeDoc(cube(4));
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    const std::vector<glm::vec2> before = doc.mesh.uvs;
    // Pin one corner after nudging it; Unfold keeps it there.
    const UvTopology topo = buildUvTopology(doc.mesh);
    std::vector<uint32_t> corners;
    for (uint32_t c = 0; c < topo.uvVertex.size(); ++c)
        if (topo.uvVertex[c] == topo.uvVertex[0]) corners.push_back(c);
    for (uint32_t c : corners) doc.mesh.uvs[c] += glm::vec2(0.01f, 0.0f);
    uvSetPins(doc.mesh, corners, true);
    const glm::vec2 pinned = doc.mesh.uvs[0];
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("unfold")}}).ok);
    CHECK(glm::distance(doc.mesh.uvs[0], pinned) < 1e-5f);
    // The unpinned islands are fitted back where they were.
    double moved = 0.0;
    for (size_t c = 0; c < before.size(); ++c) moved = std::max(moved, double(glm::distance(before[c], doc.mesh.uvs[c])));
    CHECK(moved < 0.05);
}

TEST(uv_pack_one_tile_and_udim_tiles) {
    Document doc = makeDoc(cube());
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    // Across four UDIM tiles: six equal squares fit best four to a tile, so two tiles (1001, 1002) hold them
    // at twice the size one tile would give.
    CommandResult r = run(doc, "uv_pack", {{"tiles", 4.0}, {"margin", 16.0}});
    CHECK(r.ok);
    CHECK(r.message.find("2 UDIM tiles") != std::string::npos);
    CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(4.0f, 1.0f)));
    std::vector<int> tiles;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t)
        tiles.push_back(udimTile((doc.mesh.uvs[t * 3] + doc.mesh.uvs[t * 3 + 1] + doc.mesh.uvs[t * 3 + 2]) / 3.0f));
    std::sort(tiles.begin(), tiles.end());
    tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    CHECK(tiles.size() == 2);
    CHECK(tiles.front() == 1001 && tiles.back() == 1002);
    CHECK(islandBoxesDisjoint(doc.mesh));
    // A fixed texel density spills into as many tiles as it needs.
    r = run(doc, "uv_pack", {{"density", 256.0}, {"resolution", 1024.0}, {"margin", 4.0}});
    CHECK(r.ok);
    const float d = uvMeshDensity(doc.mesh, 1024);
    CHECK(std::abs(d - 256.0f) < 256.0f * 0.01f);
    CHECK(r.message.find("UDIM tiles") != std::string::npos);
    // Back to one tile, with a margin: the islands keep 16 texels apart.
    r = run(doc, "uv_pack", {{"margin", 16.0}, {"resolution", 1024.0}});
    CHECK(r.ok);
    CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(1.0f)));
    CHECK(islandBoxesDisjoint(doc.mesh, -15.0f / 1024.0f)); // boxes at least ~15 texels apart
}

TEST(uv_pack_rectangles) {
    // Sixteen quarter-tile squares fill one tile; a seventeenth needs a second; an oversized one never fits.
    std::vector<glm::vec2> sizes(16, glm::vec2(0.25f)), pos;
    std::vector<uint8_t> turned;
    std::vector<int> tile;
    CHECK(packRectangles(sizes, 1, false, 0.0f, pos, turned, tile) == 1);
    sizes.push_back(glm::vec2(0.25f));
    CHECK(packRectangles(sizes, 1, false, 0.0f, pos, turned, tile) == 0);
    CHECK(packRectangles(sizes, 2, false, 0.0f, pos, turned, tile) == 2);
    CHECK(packRectangles({glm::vec2(1.5f, 0.1f)}, 4, true, 0.0f, pos, turned, tile) == 0);
    // A tall strip lies down when turning is allowed.
    CHECK(packRectangles({glm::vec2(0.1f, 0.9f), glm::vec2(0.9f, 0.9f)}, 1, true, 0.0f, pos, turned, tile) == 1);
}

TEST(uv_texel_density_set_and_match) {
    Document doc = makeDoc(cube());
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    // Make the islands differ, then match them.
    UvTopology topo = buildUvTopology(doc.mesh);
    std::vector<uint8_t> one(topo.islandCount, 0);
    one[0] = 1;
    uvSetDensity(doc.mesh, topo, one, 100.0f, 1024);
    CHECK(std::abs(uvIslandDensity(doc.mesh, topo, 1024)[0] - 100.0f) < 0.5f);
    CHECK(run(doc, "uv_density", {{"op", std::string("match")}, {"resolution", 1024.0}}).ok);
    topo = buildUvTopology(doc.mesh);
    const std::vector<float> d = uvIslandDensity(doc.mesh, topo, 1024);
    for (float x : d) CHECK(std::abs(x - d[0]) < d[0] * 0.005f);
    CHECK(run(doc, "uv_density", {{"op", std::string("set")}, {"value", 512.0}, {"resolution", 1024.0}}).ok);
    CHECK(std::abs(uvMeshDensity(doc.mesh, 1024) - 512.0f) < 2.0f);
    CHECK(run(doc, "uv_density", {{"op", std::string("info")}}).message.find("texels per unit") != std::string::npos);
}

TEST(uv_density_and_stretch_measures) {
    // A unit square face mapped to the whole 0-1 tile: 1024 texels per unit at 1K, no stretch; squashed 2:1 it
    // stretches by log2(sqrt 2)... per axis, the worst: half a step.
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    m.triangles = {{0, 1, 2}, {0, 2, 3}};
    m.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
    std::vector<float> d, s;
    uvTriangleDensity(m, 1024, d);
    CHECK(std::abs(d[0] - 1024.0f) < 1e-2f);
    uvTriangleStretch(m, s);
    CHECK(std::abs(s[0]) < 1e-5f);
    for (glm::vec2& u : m.uvs) u.y *= 0.5f;
    uvTriangleStretch(m, s);
    CHECK(std::abs(s[0] - 0.5f) < 1e-3f);
    // Folding one triangle over the other (its own corner moved across the shared diagonal): flipped.
    m.uvs[5] = glm::vec2(0.9f, 0.1f);
    uvTriangleStretch(m, s);
    CHECK(s[1] == -1.0f);
    CHECK(s[0] >= 0.0f);
    CHECK(uvHeatColor(-1.0f).z > uvHeatColor(-1.0f).x);
    CHECK(uvHeatColor(1.0f).x > uvHeatColor(1.0f).y);
}

TEST(uv_trim_sheet_strips) {
    std::vector<TrimStrip> strips;
    std::string err;
    CHECK(parseTrimStrips("4", strips, err) && strips.size() == 4);
    CHECK(std::abs(strips[0].v1 - 1.0f) < 1e-6f && std::abs(strips[0].v0 - 0.75f) < 1e-6f);
    CHECK(parseTrimStrips("0.5,0.25,0.25", strips, err) && strips.size() == 3 && std::abs(strips[0].v0 - 0.5f) < 1e-6f);
    CHECK(parseTrimStrips("0-0.5;0.5-0.75", strips, err) && strips.size() == 2 && std::abs(strips[1].v1 - 0.75f) < 1e-6f);
    CHECK(!parseTrimStrips("banana", strips, err));

    Document doc = makeDoc(cube());
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    CHECK(run(doc, "uv_select", {{"op", std::string("polygroup")}, {"group", 1.0}, {"mode", std::string("island")}}).ok);
    CommandResult r = run(doc, "uv_trim", {{"strip", 2.0}, {"strips", std::string("4")}, {"fit_width", true}});
    CHECK(r.ok);
    // The selected island sits in strip 2 (v 0.5 - 0.75) across u 0-1.
    const UvTopology topo = buildUvTopology(doc.mesh);
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t) {
        if (doc.mesh.polygroupAt(t) != 1) continue;
        for (uint32_t j = 0; j < 3; ++j) lo = glm::min(lo, doc.mesh.uvs[t * 3 + j]), hi = glm::max(hi, doc.mesh.uvs[t * 3 + j]);
    }
    CHECK(lo.y >= 0.5f - 1e-4f && hi.y <= 0.75f + 1e-4f);
    CHECK(std::abs(lo.x) < 1e-4f && std::abs(hi.x - 1.0f) < 1e-4f);
    CHECK(hi.y - lo.y > 0.2f); // filled the strip's height (less the margin)
}

TEST(uv_seams_path_polygroups_and_angle) {
    Document doc = makeDoc(cube(4));
    CommandResult r = run(doc, "uv_seam", {{"op", std::string("polygroups")}});
    CHECK(r.ok);
    CHECK(uvSeamCount(doc.mesh) == 12 * 4); // 12 cube edges, 4 segments each
    CHECK(run(doc, "uv_seam", {{"op", std::string("clear_all")}}).ok);
    CHECK(uvSeamCount(doc.mesh) == 0);
    CHECK(run(doc, "uv_seam", {{"op", std::string("angle")}, {"angle", 45.0}}).ok);
    CHECK(uvSeamCount(doc.mesh) == 12 * 4);
    CHECK(run(doc, "uv_seam", {{"op", std::string("clear_all")}}).ok);
    // A path between opposite corners of the cube: at least 3 x 4 edges long.
    uint32_t lo = 0, hi = 0;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        const glm::vec3 p = doc.mesh.positions[v];
        if (p.x + p.y + p.z < doc.mesh.positions[lo].x + doc.mesh.positions[lo].y + doc.mesh.positions[lo].z) lo = v;
        if (p.x + p.y + p.z > doc.mesh.positions[hi].x + doc.mesh.positions[hi].y + doc.mesh.positions[hi].z) hi = v;
    }
    r = run(doc, "uv_seam", {{"op", std::string("path")}, {"from", double(lo)}, {"to", double(hi)}});
    CHECK(r.ok);
    CHECK(uvSeamCount(doc.mesh) >= 8);
    // Undo takes the seams back.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(uvSeamCount(doc.mesh) == 0);
}

TEST(uv_weld_split_straighten) {
    Document doc = makeDoc(cube(4));
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    // Split one side's faces into two islands by its half.
    UvTopology topo = buildUvTopology(doc.mesh);
    const size_t before = topo.islandCount;
    std::vector<uint8_t> faces(doc.mesh.triangleCount(), 0);
    for (uint32_t t = 0; t < faces.size(); ++t) {
        const glm::uvec3 tri = doc.mesh.triangles[t];
        const glm::vec3 c = (doc.mesh.positions[tri.x] + doc.mesh.positions[tri.y] + doc.mesh.positions[tri.z]) / 3.0f;
        faces[t] = doc.mesh.polygroupAt(t) == 1 && c.x + c.y + c.z > 0.0f;
    }
    doc.uvSelection.mode = UvSelectMode::Face;
    doc.uvSelection.faces = faces;
    CHECK(run(doc, "uv_edit", {{"op", std::string("split")}}).ok);
    topo = buildUvTopology(doc.mesh);
    CHECK(topo.islandCount == before + 1);
    // Weld the seam just made: select its edges (vertex mode, both sides) and weld.
    CHECK(run(doc, "uv_select", {{"op", std::string("seams")}}).ok);
    CHECK(run(doc, "uv_select", {{"op", std::string("mode")}, {"mode", std::string("vertex")}}).ok);
    CHECK(run(doc, "uv_edit", {{"op", std::string("weld")}}).ok);
    CHECK(buildUvTopology(doc.mesh).islandCount == before);
    // Straighten: some vertices line up.
    CHECK(run(doc, "uv_select", {{"op", std::string("rect")}, {"u0", 0.0}, {"v0", 0.0}, {"u1", 0.2}, {"v1", 0.2},
                                 {"mode", std::string("vertex")}}).ok);
    if (!doc.uvSelection.empty()) {
        CHECK(run(doc, "uv_edit", {{"op", std::string("straighten")}}).ok);
    }
    // Pins.
    CHECK(run(doc, "uv_edit", {{"op", std::string("pin")}}).ok || doc.uvSelection.empty());
    CHECK(run(doc, "uv_edit", {{"op", std::string("clear_pins")}}).ok);
}

TEST(uv_commands_undo_redo_exactly) {
    Document doc = makeDoc(cube());
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    const std::vector<glm::vec2> a = doc.mesh.uvs;
    CHECK(run(doc, "uv_transform", {{"rotate", 30.0}, {"scale", 0.5}, {"move_u", 0.1}}).ok);
    const std::vector<glm::vec2> b = doc.mesh.uvs;
    CHECK(a != b);
    CHECK(run(doc, "uv_pack", {{"tiles", 2.0}}).ok);
    const std::vector<glm::vec2> c = doc.mesh.uvs;
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.uvs == b);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.uvs == a);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh)); // the unwrap: no UVs again
    CHECK(!doc.mesh.hasUvs());
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.uvs == a);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.uvs == c);
}

TEST(uv_selection_modes_convert) {
    Document doc = makeDoc(cube(4));
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    CHECK(run(doc, "uv_select", {{"op", std::string("face")}, {"face", 0.0}, {"mode", std::string("face")}}).ok);
    CHECK(doc.uvSelection.count() == 1);
    CHECK(run(doc, "uv_select", {{"op", std::string("mode")}, {"mode", std::string("island")}}).ok);
    CHECK(doc.uvSelection.count() == doc.mesh.triangleCount() / 6);
    CHECK(run(doc, "uv_select", {{"op", std::string("mode")}, {"mode", std::string("vertex")}}).ok);
    CHECK(doc.uvSelection.count() > 0);
    CHECK(run(doc, "uv_select", {{"op", std::string("invert")}}).ok);
    CHECK(run(doc, "uv_select", {{"op", std::string("all")}, {"mode", std::string("edge")}}).ok);
    CHECK(doc.uvSelection.count() == doc.mesh.triangleCount() * 3);
    CHECK(run(doc, "uv_select", {{"op", std::string("none")}}).ok);
    CHECK(doc.uvSelection.empty());
    CHECK(run(doc, "uv_info").message.find("6 islands") != std::string::npos);
}

TEST(uv_painted_texture_follows_the_new_layout) {
    // Paint each side of the cube its own colour, then repack: the model looks the same (keep_texture), but
    // with keep_texture=false the colours slide.
    Document doc = makeDoc(cube(4));
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("polygroups")}}).ok);
    CHECK(run(doc, "texture_create", {{"size", std::string("1024")}}).ok);
    Mesh& m = doc.mesh;
    PaintTexture& tex = *m.texture;
    bakeTexels(m, tex, [&](uint32_t t, float, float, float, int x, int y) {
        uint8_t* px = &tex.pixels[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        const uint32_t g = m.polygroupAt(t);
        px[0] = uint8_t(g * 40), px[1] = uint8_t(255 - g * 30), px[2] = uint8_t(g * 20), px[3] = 255;
    }, true);
    tex.markAllDirty();
    auto colourAt = [&](uint32_t t) {
        const glm::uvec3 tri = m.triangles[t];
        const glm::vec3 c = (m.positions[tri.x] + m.positions[tri.y] + m.positions[tri.z]) / 3.0f;
        return sampleTextureAt(m, t, c);
    };
    std::vector<glm::vec3> before;
    for (uint32_t t = 0; t < m.triangleCount(); t += 7) before.push_back(colourAt(t));
    const CommandResult r = run(doc, "uv_transform", {{"rotate", 90.0}, {"scale", 0.8}});
    CHECK(r.ok);
    CHECK(r.message.find("paint moved") != std::string::npos);
    float worst = 0.0f;
    size_t k = 0;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); t += 7) worst = std::max(worst, glm::length(colourAt(t) - before[k++]));
    std::printf("  texture after a UV rotate: worst colour change %.4f\n", double(worst));
    CHECK(worst < 0.03f);
    // Undo brings back both the UVs and the texels.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    k = 0;
    worst = 0.0f;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); t += 7) worst = std::max(worst, glm::length(colourAt(t) - before[k++]));
    CHECK(worst < 1e-4f);
}

TEST(uv_project_round_trip_keeps_seams_and_pins) {
    Document doc = makeDoc(cube(4));
    CHECK(run(doc, "uv_seam", {{"op", std::string("polygroups")}}).ok);
    CHECK(run(doc, "uv_unwrap", {{"method", std::string("conformal")}}).ok);
    std::vector<uint32_t> corners = {0, 1, 2};
    uvSetPins(doc.mesh, corners, true);
    const std::string path = tempPath("sf_uv_editor_test.sfproj");
    CommandRegistry reg;
    CHECK(reg.execute(doc, "save_project", {{"path", path}}).ok);
    Document loaded;
    CHECK(reg.execute(loaded, "open_project", {{"path", path}}).ok);
    CHECK(loaded.mesh.uvFlags == doc.mesh.uvFlags);
    CHECK(uvSeamCount(loaded.mesh) == 12 * 4);
    std::filesystem::remove(path);
}

TEST(uv_editor_million_triangles_stay_interactive) {
    // An 8-level icosphere: 1.3M triangles. Topology, measures, a transform and a pack are each well under the
    // time a frame or a click can take; a full unwrap by polygroups (in the background in the app) takes seconds.
    Mesh m = makeIcosphere(8);
    // Eight polygroups by octant, as seams.
    m.ensurePolygroups();
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::vec3 c = m.positions[m.triangles[t].x];
        m.polygroups[t] = (c.x > 0) + 2 * (c.y > 0) + 4 * (c.z > 0);
    }
    std::printf("  %zu triangles\n", m.triangleCount());
    UvUnwrapOptions o;
    o.method = UvUnwrapMethod::Polygroups;
    o.iterations = 4;
    UvUnwrapResult r;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(uvUnwrap(m, o, r, err));
    const double unwrap = secondsSince(t0);
    m.uvs = r.uvs;
    t0 = std::chrono::steady_clock::now();
    const UvTopology topo = buildUvTopology(m);
    const double topoS = secondsSince(t0);
    CHECK(topo.islandCount == 8);
    t0 = std::chrono::steady_clock::now();
    std::vector<float> s;
    uvTriangleStretch(m, s);
    const double stretchS = secondsSince(t0);
    t0 = std::chrono::steady_clock::now();
    std::vector<uint32_t> all(m.uvs.size());
    for (uint32_t c = 0; c < all.size(); ++c) all[c] = c;
    uvTransform(m.uvs, all, glm::mat2(0.9f), glm::vec2(0.5f), glm::vec2(0.0f));
    const double moveS = secondsSince(t0);
    t0 = std::chrono::steady_clock::now();
    UvPackResult pr;
    CHECK(uvPack(m, topo, {}, UvPackOptions(), pr, err));
    const double packS = secondsSince(t0);
    std::printf("  unwrap %.2f s, topology %.3f s, stretch %.3f s, move %.4f s, pack %.3f s; stretch %.3f\n", unwrap, topoS,
                stretchS, moveS, packS, double(uvStretchStats(m).mean));
    CHECK(topoS < 2.0);
    CHECK(stretchS < 2.0);
    CHECK(moveS < 0.2);
    CHECK(packS < 2.0);
    CHECK(unwrap < 60.0);
    CHECK(flippedTriangles(m) < m.triangleCount() / 1000);
}

TEST(uv_unwrap_lion_head_by_sharp_edges) {
    // A real sculpt with holes and thin strands: the islands don't fold, overlap or leave the tile.
    Document doc;
    std::string lion = __FILE__;
    lion = lion.substr(0, lion.rfind("tests")) + "assets/demo/lion-head.glb";
    if (!run(doc, "open", {{"path", lion}}).ok) return;
    CHECK(run(doc, "uv_seam", {{"op", std::string("angle")}, {"angle", 50.0}}).ok);
    for (const char* method : {"isometric", "conformal"}) {
        const CommandResult r = run(doc, "uv_unwrap", {{"method", std::string(method)}});
        CHECK(r.ok);
        const UvStretchStats st = uvStretchStats(doc.mesh);
        const UvTopology topo = buildUvTopology(doc.mesh);
        const size_t folded = flippedTriangles(doc.mesh);
        std::printf("  lion %s: %s; %u islands, %zu folded, stretch %.3f, boxes disjoint %d\n", method, r.message.c_str(),
                    topo.islandCount, folded, double(st.mean), int(islandBoxesDisjoint(doc.mesh)));
        CHECK(folded * 200 < doc.mesh.triangleCount());
        CHECK(islandBoxesDisjoint(doc.mesh));
        CHECK(uvsWithin(doc.mesh, glm::vec2(0.0f), glm::vec2(1.0f)));
        CHECK(st.mean < 0.25f);
    }
}
