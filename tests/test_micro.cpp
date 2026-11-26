// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Micro-displacement levels (core/Micro.h): divide, brushes, stitching, undo, picking, shading, bake, maps,
// projects and the commands.
#include "TestHarness.h"

#include "core/Benchmark.h"
#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Micro.h"
#include "core/MicroTiles.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/TierConfig.h"
#include "core/Wavelet.h"
#include "io/Zip.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <array>
#include <map>

using namespace sf;

namespace {

Document microDoc(int level, size_t baseTriangles = 20000) {
    Document doc = makeDoc(makeBenchSphere(baseTriangles));
    std::string err;
    CHECK(microDivide(doc.mesh, level, err));
    return doc;
}

// Every point shared by two base triangles (edges) or more (corners) has the same displacement in each copy, and
// the micro positions there are bit-identical.
bool watertight(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n, S = mm.samples;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (int e = 0; e < 3; ++e) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t u = nb >> 2;
            const int f = int(nb & 3);
            const bool reversed = m.triangles[u][f] != m.triangles[t][e];
            for (uint32_t k = 0; k <= n; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(e, k, n, i, j);
                MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                if (mm.disp[size_t(t) * S + MicroMesh::index(i, j, n)] != mm.disp[size_t(u) * S + MicroMesh::index(i2, j2, n)])
                    return false;
                const glm::vec3 a = microPoint(m, t, i, j), b = microPoint(m, u, i2, j2);
                if (std::memcmp(&a, &b, sizeof a) != 0) return false;
            }
        }
    return true;
}

DabParams dabAt(const glm::vec3& c, float r, float strength = 0.5f) {
    DabParams p;
    p.center = c;
    p.radius = r;
    p.strength = strength;
    p.viewDir = glm::vec3(0, 0, -1);
    return p;
}

// The micro surface's distance from the origin under the point (0, 0, 1) (the sphere's front).
float frontHeight(const Document& doc) {
    RayHit hit;
    CHECK(microRaycast(doc.mesh, doc.bvh, glm::vec3(0.001f, 0.002f, 5.0f), glm::vec3(0, 0, -1), hit));
    return glm::length(hit.position);
}

} // namespace

TEST(micro_divide_counts_and_memory) {
    Document doc = microDoc(3);
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.n == 8);
    CHECK(mm.samples == 45);
    CHECK(microTriangleCount(doc.mesh) == doc.mesh.triangleCount() * 64);
    CHECK(mm.disp.size() == doc.mesh.triangleCount() * 45);
    // 2 bytes a point, about 1.4 points per unique micro vertex at level 3: well under 4 bytes per micro vertex.
    CHECK(double(mm.disp.size() * 2) / double(mm.microVertices()) < 3.0);
    CHECK(microText(doc.mesh).rfind("Micro 3", 0) == 0);
    // Every base edge has a neighbour on a closed sphere.
    for (uint32_t e : mm.edgeNeighbor) CHECK(e != MicroMesh::kNone);
    // d = 0 everywhere: the surface is the smooth Phong surface, close to the unit sphere.
    // (The bench sphere is a subdivided cube: compare with the radius of each base triangle's corners. The flat
    // triangles sag inwards by up to e^2 / 8; Phong tessellation takes most of that back.)
    float worstMicro = 0.0f, worstFlat = 0.0f;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 97) {
        const glm::uvec3 tr = doc.mesh.triangles[t];
        const float r = (glm::length(doc.mesh.positions[tr.x]) + glm::length(doc.mesh.positions[tr.y]) + glm::length(doc.mesh.positions[tr.z])) / 3.0f;
        MicroTriangle T;
        T.set(doc.mesh, t, mm.n);
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) {
                worstMicro = std::max(worstMicro, std::abs(glm::length(microPoint(doc.mesh, t, i, j)) - r));
                worstFlat = std::max(worstFlat, std::abs(glm::length(T.flat(i, j)) - r));
            }
    }
    CHECK(worstMicro < worstFlat * 0.5f);
    CHECK(watertight(doc.mesh));
    std::string err;
    CHECK(!microDivide(doc.mesh, 2, err)); // one micro level per Part
}

// Drawing above the stored level (close-ups, TierBudgets::microDrawAbove): the points of a finer level lie on the
// surface the stored grid defines (stored points unchanged, in-between points within the stored cell's own sag of
// the flat micro triangle), and a shared base edge gives the same bits from both triangles at every drawn point.
TEST(micro_draw_above_stored_level_is_seamless) {
    Document doc = microDoc(3);
    microNoise(doc.mesh, 0.02f, 6.0f, 3); // a bumpy surface: in-between heights really interpolate
    const MicroMesh& mm = *doc.mesh.micro;
    // The renderer's shading grid (the stored normals the pull uses), as MicroRenderer builds it.
    std::vector<uint32_t> shading(size_t(mm.baseTriangles) * mm.samples, 0);
    {
        MicroShader shader;
        std::vector<uint32_t> written;
        shader.shade(doc.mesh, {}, true, written, [&](uint32_t tri) { return shading.data() + size_t(tri) * mm.samples; });
        CHECK(written.size() == mm.baseTriangles);
    }
    const uint32_t* sg = shading.data();
    const int level = mm.level + 2;
    const uint32_t md = 1u << level, sc = md / mm.n;
    size_t checked = 0;
    float worstOff = 0.0f;
    const float microEdge = mm.baseEdge / float(mm.n);
    for (uint32_t t = 0; t < mm.baseTriangles; t += 7) {
        for (uint32_t j = 0; j <= md; j += 3)
            for (uint32_t i = 0; i + j <= md; i += 5) {
                const glm::vec3 p = microDrawPoint(doc.mesh, t, level, i, j, sg), flat = microDrawPoint(doc.mesh, t, level, i, j);
                if (i % sc == 0 && j % sc == 0) CHECK(p == microPoint(doc.mesh, t, i / sc, j / sc)); // stored points: the same bits
                const float wc[3] = {float(md - i - j) / float(md), float(i) / float(md), float(j) / float(md)};
                CHECK(glm::distance(flat, microPointAt(doc.mesh, t, wc)) < 1e-6f); // no pull = the flat micro triangle (the CPU's surface)
                worstOff = std::max(worstOff, glm::distance(p, flat));
            }
        for (int e = 0; e < 3; ++e) { // the shared edge from both sides, at every drawn point
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            CHECK(nb != MicroMesh::kNone);
            if (nb == MicroMesh::kNone) continue;
            const uint32_t t2 = nb >> 2, e2 = nb & 3;
            for (uint32_t k = 0; k <= md; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(e, k, md, i, j);
                MicroMesh::edgePoint(int(e2), md - k, md, i2, j2);
                const glm::vec3 a = microDrawPoint(doc.mesh, t, level, i, j, sg), b = microDrawPoint(doc.mesh, t2, level, i2, j2, sg);
                CHECK(a == b);
                CHECK(microDrawPoint(doc.mesh, t, level, i, j) == microDrawPoint(doc.mesh, t2, level, i2, j2));
                ++checked;
            }
        }
    }
    CHECK(checked > 1000);
    // The pull leaves the flat micro triangle, but by less than a micro edge's worth over a bumpy sphere.
    CHECK(worstOff < microEdge);
    CHECK(worstOff > 0.0f);
}

// --- Adaptive Detail tiles (core/MicroTiles.h; the 10x scoreboard's axis 2) ---
namespace {

// The tile store attached to the level (what SF_MICRO_TILES=1 does at the first stroke).
void attachTiles(Document& doc, size_t residentBudget = 0) {
    MicroMesh& mm = mutableMicro(doc.mesh);
    mm.tiles = std::make_shared<MicroTiles>();
    mm.tiles->resize(mm.baseTriangles);
    mm.tiles->residentBudget = residentBudget;
}

// Base weights of the surface point hit from the front, and its base triangle.
uint32_t frontTriangle(const Document& doc, glm::vec3& w) {
    RayHit hit;
    CHECK(doc.bvh.raycast(doc.mesh, glm::vec3(0.001f, 0.002f, 5.0f), glm::vec3(0, 0, -1), hit));
    const glm::uvec3 tr = doc.mesh.triangles[hit.triangle];
    const glm::vec3 a = doc.mesh.positions[tr.x], b = doc.mesh.positions[tr.y], c = doc.mesh.positions[tr.z], p = hit.position;
    const glm::vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1), d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    w.y = (d11 * d20 - d01 * d21) / den, w.z = (d00 * d21 - d01 * d20) / den, w.x = 1.0f - w.y - w.z;
    return hit.triangle;
}

std::map<uint64_t, std::array<int16_t, MicroTiles::kSamples>> tileSnapshot(const MicroTiles& t) {
    std::map<uint64_t, std::array<int16_t, MicroTiles::kSamples>> out;
    t.forEach([&](uint64_t key, const MicroTiles::Tile& tile) { out[key] = tile.delta; });
    return out;
}

// A stroke of small Standard dabs at the front (finer than the root grid: tiles).
UndoRecord fineStroke(Document& doc, float r = 0.02f, int dabs = 5) {
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < dabs; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0.002f * float(i), 0, 1), r, 1.0f), sym);
    return doc.sculptor.endStroke();
}

} // namespace

// A dab much smaller than the root cell x 48 writes delta tiles at its own depth and leaves the root grid untouched;
// the surface under it still rises; undo frees the tiles and redo brings them back identical.
TEST(micro_tiles_small_dab_writes_tiles_not_root_and_undoes) {
    Document doc = microDoc(4);
    attachTiles(doc);
    const MicroMesh& mm0 = *doc.mesh.micro;
    const float rootCell = mm0.baseEdge / float(mm0.n);
    CHECK(MicroTiles::depthFor(rootCell, 0.02f, 0.0f) >= 2);
    CHECK(MicroTiles::depthFor(rootCell, 0.3f, 0.0f) <= 0);
    CHECK(MicroTiles::depthFor(rootCell, 0.02f, 1.0f / rootCell) <= 2); // seen from afar (the root cell 1 px): capped to 2 levels finer
    CHECK(MicroTiles::depthFor(rootCell, 0.02f, 0.1f / rootCell) <= 0); // (0.1 px: nothing finer than the root is worth storing)
    const std::vector<int16_t> rootBefore = doc.mesh.micro->disp;
    glm::vec3 w;
    const uint32_t tri = frontTriangle(doc, w);
    const float hBefore = doc.mesh.micro->tiles->heightAt(*doc.mesh.micro, tri, w);
    UndoRecord r = fineStroke(doc);
    CHECK(r.micro != nullptr);
    CHECK(!r.micro->tileKeys.empty());
    r.micro->settle(); // (the journal is packed off the release: #184)
    CHECK(r.micro->tris.empty() || r.micro->journal || !r.micro->disp.empty()); // (root tiles were saved by touch; their change is zero)
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.tiles != nullptr);
    CHECK(mm.tiles->alive() > 0);
    CHECK(mm.disp == rootBefore);                      // the root grid: untouched
    CHECK(mm.tiles->heightAt(mm, tri, w, 0) == hBefore); // (the root's own height, the same)
    CHECK(mm.tiles->heightAt(mm, tri, w) > hBefore + 0.5f * 0.02f * 0.06f / mm.step); // raised by at least half a full dab
    CHECK(mm.rangeMax > 0);
    const auto after = tileSnapshot(*mm.tiles);
    const size_t alive = mm.tiles->alive();
    for (uint8_t b : r.micro->tileBorn) CHECK(b == 1); // every tile is new in this stroke
    CHECK(r.micro->tileKeys.size() == alive);
    doc.undo.push(std::move(r));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->tiles->alive() == 0);
    CHECK(doc.mesh.micro->disp == rootBefore);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->tiles->alive() == alive);
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == after);
    // A second stroke over the same tiles: they existed before (not born), undo restores their old deltas.
    UndoRecord r2 = fineStroke(doc);
    CHECK(r2.micro != nullptr && !r2.micro->tileKeys.empty());
    bool anyOld = false;
    for (uint8_t b : r2.micro->tileBorn) anyOld = anyOld || b == 0;
    CHECK(anyOld);
    doc.undo.push(std::move(r2));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == after);
}

// Every tile point on a base edge resolves to the same height from both base triangles (no stitch needed: the
// interpolation parameters are exact and the brush saw the same position and normal).
TEST(micro_tiles_shared_edges_are_bit_identical) {
    Document doc = microDoc(4);
    attachTiles(doc);
    doc.undo.push(fineStroke(doc, 0.03f, 8));
    const MicroMesh& mm = *doc.mesh.micro;
    const MicroTiles& tiles = *mm.tiles;
    CHECK(tiles.alive() > 0);
    size_t checks = 0, edgeTiles = 0;
    std::vector<uint64_t> keys;
    tiles.forEach([&](uint64_t key, const MicroTiles::Tile&) { keys.push_back(key); });
    for (const uint64_t key : keys) {
        const uint32_t t = MicroTiles::keyTri(key);
        const int depth = MicroTiles::keyDepth(key);
        const TileFrame f = TileFrame::of(depth, MicroTiles::keyPath(key));
        const glm::vec3 c[3] = {f.at(0, 0), f.at(1, 0), f.at(0, 1)};
        const glm::uvec3 tt = doc.mesh.triangles[t];
        for (int e = 0; e < 3; ++e) { // the tile's edge from corner e to e + 1: on a base edge when a base weight is 0 at both
            const glm::vec3 a = c[e], b = c[(e + 1) % 3];
            int zero = -1;
            for (int k = 0; k < 3; ++k)
                if (std::abs(a[k]) < 1e-7f && std::abs(b[k]) < 1e-7f) zero = k;
            if (zero < 0) continue;
            const int baseEdge = zero == 2 ? 0 : zero == 0 ? 1 : 2; // base edge e runs from corner e to e + 1: the corner left out is e + 2
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(baseEdge)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t u = nb >> 2;
            const glm::uvec3 ut = doc.mesh.triangles[u];
            ++edgeTiles;
            for (int k = 0; k <= 8; ++k) {
                const glm::vec3 w = a + (b - a) * (float(k) / 8.0f);
                glm::vec3 wu(0.0f);
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        if (ut[j] == tt[i]) wu[j] = w[i];
                const float h1 = tiles.heightAt(mm, t, w), h2 = tiles.heightAt(mm, u, wu);
                CHECK(h1 == h2);
                ++checks;
            }
        }
    }
    CHECK(edgeTiles > 0 && checks > 0);
    CHECK(watertight(doc.mesh)); // the root is as before
}

// A big dab over tiled detail writes the root only: the fine deltas ride along unchanged (the multires promise),
// and no tile is made or touched.
TEST(micro_tiles_coarse_dab_leaves_fine_deltas) {
    Document doc = microDoc(4);
    attachTiles(doc);
    doc.undo.push(fineStroke(doc));
    const auto fine = tileSnapshot(*doc.mesh.micro->tiles);
    const std::vector<int16_t> rootBefore = doc.mesh.micro->disp;
    Symmetry sym;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 4; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(r.micro != nullptr && r.micro->tileKeys.empty());
    CHECK(doc.mesh.micro->disp != rootBefore);
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == fine);
    CHECK(watertight(doc.mesh));
    doc.undo.push(std::move(r));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == rootBefore);
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == fine);
}

// Drawing Adaptive Detail tiles (render/MicroTilePages.h): the renderer adds each tiled triangle's delta page along
// the Phong normal at every drawn level; microDrawPoint(..., pageLevel) is its CPU twin. The pages equal the tiles'
// delta point by point, an untiled triangle draws today's bits, the drawn points stay seamless across tiled/untiled
// and tiled/tiled base edges at every drawn level, and a point shared by two drawn levels is the same (no popping).
TEST(micro_draw_above_stored_level_is_seamless_with_tiles) {
    Document doc = microDoc(4);
    attachTiles(doc);
    doc.undo.push(fineStroke(doc, 0.03f, 8));
    doc.undo.push(fineStroke(doc, 0.015f, 6)); // a second, finer one over it (deeper tiles)
    const MicroMesh& mm = *doc.mesh.micro;
    const MicroTiles& tiles = *mm.tiles;
    CHECK(tiles.alive() > 0);
    std::vector<uint32_t> shading(size_t(mm.baseTriangles) * mm.samples, 0);
    {
        MicroShader shader;
        std::vector<uint32_t> written;
        shader.shade(doc.mesh, {}, true, written, [&](uint32_t tri) { return shading.data() + size_t(tri) * mm.samples; });
    }
    const uint32_t* sg = shading.data();
    const int P = mm.level + 2; // the page level (stored + drawAbove on the floor)
    const uint32_t M = 1u << P;
    // The triangles with tiles and their neighbours.
    std::vector<uint32_t> tiled, near;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        if (tiles.hasTiles(t)) tiled.push_back(t);
    CHECK(!tiled.empty());
    for (uint32_t t : tiled)
        for (int e = 0; e < 3; ++e)
            if (const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)]; nb != MicroMesh::kNone) near.push_back(nb >> 2);
    near.insert(near.end(), tiled.begin(), tiled.end());
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    // 1. Pages == the CPU twin's point value == the rounded delta chain; some of it non-zero.
    std::vector<int16_t> page(size_t(M + 1) * (M + 2) / 2);
    size_t nonZero = 0;
    for (uint32_t t : near) {
        tiles.pageValues(t, M, page.data());
        for (uint32_t j = 0; j <= M; j += 1)
            for (uint32_t i = 0; i + j <= M; ++i) {
                const int16_t v = page[MicroMesh::index(i, j, M)];
                CHECK(v == tiles.pageValue(t, M, i, j));
                CHECK(v == MicroTiles::pageRound(tiles.deltaAt(t, MicroTiles::pageWeights(M, i, j))));
                nonZero += v != 0;
            }
        if (!tiles.hasTiles(t)) CHECK(std::all_of(page.begin(), page.end(), [](int16_t v) { return v == 0; }));
    }
    CHECK(nonZero > 100);
    // 2. Seams and levels: at every drawn level up to P, each base edge of a tiled triangle gives the same bits from
    // both sides; an untiled triangle draws exactly today's points; a coarse point drawn finer is the same point.
    size_t tiledUntiled = 0, tiledTiled = 0, checks = 0, moved = 0;
    for (int level = std::max(1, mm.level - 1); level <= P; ++level) {
        const uint32_t md = 1u << level;
        for (uint32_t t : near) {
            const bool tt = tiles.hasTiles(t);
            for (uint32_t j = 0; j <= md; j += (level >= P ? 3 : 1))
                for (uint32_t i = 0; i + j <= md; i += (level >= P ? 5 : 1)) {
                    const glm::vec3 a = microDrawPoint(doc.mesh, t, level, i, j, sg, P), b = microDrawPoint(doc.mesh, t, level, i, j, sg);
                    if (!tt) CHECK(a == b);
                    moved += a != b;
                    if (level < P) CHECK(a == microDrawPoint(doc.mesh, t, level + 1, i * 2, j * 2, sg, P)); // no popping
                }
            if (!tt) continue;
            for (int e = 0; e < 3; ++e) {
                const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
                if (nb == MicroMesh::kNone) continue;
                const uint32_t t2 = nb >> 2, e2 = nb & 3;
                (tiles.hasTiles(t2) ? tiledTiled : tiledUntiled) += 1;
                for (uint32_t k = 0; k <= md; ++k) {
                    uint32_t i, j, i2, j2;
                    MicroMesh::edgePoint(e, k, md, i, j);
                    MicroMesh::edgePoint(int(e2), md - k, md, i2, j2);
                    CHECK(microDrawPoint(doc.mesh, t, level, i, j, sg, P) == microDrawPoint(doc.mesh, t2, level, i2, j2, sg, P));
                    CHECK(microDrawPoint(doc.mesh, t, level, i, j, nullptr, P) == microDrawPoint(doc.mesh, t2, level, i2, j2, nullptr, P));
                    ++checks;
                }
            }
        }
    }
    CHECK(tiledUntiled > 0);
    CHECK(tiledTiled > 0);
    CHECK(checks > 1000);
    CHECK(moved > 100); // the tiles really draw
    // 3. Undo of both strokes: no tiles, no page values, today's points everywhere.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->tiles->alive() == 0);
    for (uint32_t t : near)
        for (uint32_t k = 0; k <= M; k += 7) CHECK(microDrawPoint(doc.mesh, t, P, k, 0, sg, P) == microDrawPoint(doc.mesh, t, P, k, 0, sg));
}

// The tiles' light (microTileSlopes, render/MicroTilePages.h): the surface gradient a page's slopes give agrees from
// both sides of a shared edge (per-triangle one-sided differences differed by ~20% there: a seam in the light), is 0
// next to a triangle without tiles (it draws no tilt), and follows the delta inside.
TEST(micro_tile_slopes_agree_across_shared_edges) {
    Document doc = microDoc(4);
    attachTiles(doc);
    doc.undo.push(fineStroke(doc, 0.03f, 8));
    doc.undo.push(fineStroke(doc, 0.015f, 6));
    const MicroMesh& mm = *doc.mesh.micro;
    const MicroTiles& tiles = *mm.tiles;
    const uint32_t M = 1u << (mm.level + 2);
    const size_t S = size_t(M + 1) * (M + 2) / 2;
    auto gradient = [&](uint32_t t, const std::vector<float>& sl, uint32_t i, uint32_t j) {
        const glm::uvec3 v = doc.mesh.triangles[t];
        const glm::vec3 c0 = doc.mesh.positions[v[0]];
        const glm::vec3 ei = (doc.mesh.positions[v[1]] - c0) / float(M), ej = (doc.mesh.positions[v[2]] - c0) / float(M);
        const float* s = sl.data() + size_t(MicroMesh::index(i, j, M)) * 2;
        const float hi = s[0] * mm.step, hj = s[1] * mm.step;
        const float a11 = glm::dot(ei, ei), a12 = glm::dot(ei, ej), a22 = glm::dot(ej, ej), det = a11 * a22 - a12 * a12;
        return ei * ((hi * a22 - hj * a12) / det) + ej * ((hj * a11 - hi * a12) / det);
    };
    auto slopes = [&](uint32_t t) {
        std::vector<int16_t> v(S);
        std::vector<float> sl(S * 2);
        tiles.pageValues(t, M, v.data());
        microTileSlopes(doc.mesh, t, M, v.data(), sl.data());
        return sl;
    };
    double maxG = 0.0, maxSeam = 0.0;
    size_t shared = 0, besideUntiled = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        if (!tiles.hasTiles(t)) continue;
        const std::vector<float> sl = slopes(t);
        for (uint32_t j = 0; j <= M; ++j)
            for (uint32_t i = 0; i + j <= M; ++i) maxG = std::max(maxG, double(glm::length(gradient(t, sl, i, j))));
        for (int e = 0; e < 3; ++e) {
            const uint32_t nb = mm.edgeNeighbor[size_t(t) * 3 + size_t(e)];
            if (nb == MicroMesh::kNone) continue;
            const uint32_t t2 = nb >> 2, e2 = nb & 3;
            const bool tiled2 = tiles.hasTiles(t2);
            const std::vector<float> sl2 = tiled2 ? slopes(t2) : std::vector<float>();
            for (uint32_t k = 0; k <= M; ++k) {
                uint32_t i, j, i2, j2;
                MicroMesh::edgePoint(e, k, M, i, j);
                MicroMesh::edgePoint(int(e2), M - k, M, i2, j2);
                const glm::vec3 g = gradient(t, sl, i, j);
                if (!tiled2) {
                    CHECK(g == glm::vec3(0.0f));
                    ++besideUntiled;
                    continue;
                }
                maxSeam = std::max(maxSeam, double(glm::length(g - gradient(t2, sl2, i2, j2))));
                ++shared;
            }
        }
    }
    std::printf("  shared edge points %zu (beside untiled %zu): largest gradient %.4f, largest seam %.6f\n", shared, besideUntiled, maxG, maxSeam);
    CHECK(shared > 100);
    CHECK(besideUntiled > 100);
    CHECK(maxG > 0.01);              // the delta really tilts the light
    // Only the planes' tilt between the sides is left (the average is projected on each: ~1.7 degrees between neighbours
    // of this 20k-triangle sphere, so ~3% of the gradient; the one-sided differences alone were 24%).
    CHECK(maxSeam < maxG * 0.05);
}

// A resident budget: at the end of a stroke the least recently used tiles beyond it are parked (packed), the surface
// reads the same, a later stroke and undo bring them back, and nothing is lost.
TEST(micro_tiles_park_beyond_the_budget_and_come_back) {
    Document doc = microDoc(4);
    attachTiles(doc, 20 * sizeof(MicroTiles::Tile)); // room for 20 resident tiles
    doc.undo.push(fineStroke(doc));
    const auto one = tileSnapshot(*doc.mesh.micro->tiles);
    glm::vec3 w;
    const uint32_t tri = frontTriangle(doc, w);
    const float h1 = doc.mesh.micro->tiles->heightAt(*doc.mesh.micro, tri, w);
    {
        const MicroTiles& t = *doc.mesh.micro->tiles;
        CHECK(t.alive() == one.size());
        CHECK(t.residentCount() <= 20);
        CHECK(t.parkedCount() > 0 && t.parkedCount() + t.residentCount() == t.alive());
        CHECK(t.parkedBytes() < t.parkedCount() * sizeof(MicroTiles::Tile)); // packed smaller than raw
    }
    // the codec round-trips every tile exactly
    {
        std::vector<uint8_t> packed;
        MicroTiles::Tile back;
        for (const auto& [key, delta] : one) {
            MicroTiles::Tile t;
            t.delta = delta;
            packed.clear();
            MicroTiles::packTile(t, packed);
            CHECK(MicroTiles::unpackTile(packed.data(), packed.size(), back) && back.delta == delta);
        }
    }
    // a second stroke a little aside: parked tiles under it come back and take the change; the far ones stay parked
    Symmetry sym;
    sym.x = false;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 5; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0.002f * float(i), 0, 1), 0.02f, 1.0f), sym);
    UndoRecord r2 = doc.sculptor.endStroke();
    CHECK(r2.micro != nullptr && !r2.micro->tileKeys.empty());
    const float h2 = doc.mesh.micro->tiles->heightAt(*doc.mesh.micro, tri, w);
    CHECK(h2 > h1);
    CHECK(doc.mesh.micro->tiles->residentCount() <= 20 || doc.mesh.micro->tiles->parkedCount() == 0);
    doc.undo.push(std::move(r2));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == one); // (through parked and resident tiles alike)
    CHECK(doc.mesh.micro->tiles->heightAt(*doc.mesh.micro, tri, w) == h1);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->tiles->alive() == 0);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(tileSnapshot(*doc.mesh.micro->tiles) == one);
}

// The tile path's promises, measured on the surface the tiles describe (heightAt on a grid of points under the brush):
// a cut (inverted Standard) lowers and never raises; nothing moves outside the brush; the X mirror of a symmetric
// stroke matches; the surface stays closed across base edges (the shared-edge test above). GeomInvariants.h's rules
// for the base cage, applied to the micro surface a tiled dab writes.
TEST(micro_tiles_invariant_cut_only_local_and_symmetric) {
    Document doc = microDoc(4);
    attachTiles(doc);
    const MicroMesh& mm0 = *doc.mesh.micro;
    const float r = 0.02f;
    CHECK(MicroTiles::depthFor(mm0.baseEdge / float(mm0.n), r, 0.0f) >= 2);
    // sample points: rays from the front onto a grid around the dab centre, 3 r wide (inside and outside the brush)
    struct Sample { uint32_t tri; glm::vec3 w; glm::vec3 p; };
    std::vector<Sample> samples;
    for (int y = -12; y <= 12; ++y)
        for (int x = -12; x <= 12; ++x) {
            RayHit hit;
            const glm::vec3 o(float(x) * 0.25f * r, float(y) * 0.25f * r, 5.0f);
            if (!doc.bvh.raycast(doc.mesh, o, glm::vec3(0, 0, -1), hit)) continue;
            const glm::uvec3 tr = doc.mesh.triangles[hit.triangle];
            const glm::vec3 a = doc.mesh.positions[tr.x], b = doc.mesh.positions[tr.y], c = doc.mesh.positions[tr.z];
            const glm::vec3 v0 = b - a, v1 = c - a, v2 = hit.position - a;
            const float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1), d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
            const float den = d00 * d11 - d01 * d01;
            glm::vec3 w;
            w.y = (d11 * d20 - d01 * d21) / den, w.z = (d00 * d21 - d01 * d20) / den, w.x = 1.0f - w.y - w.z;
            samples.push_back({hit.triangle, w, hit.position});
        }
    CHECK(samples.size() > 500);
    auto heights = [&] {
        std::vector<float> h(samples.size());
        const MicroMesh& mm = *doc.mesh.micro;
        for (size_t k = 0; k < samples.size(); ++k) h[k] = mm.tiles->heightAt(mm, samples[k].tri, samples[k].w);
        return h;
    };
    const std::vector<float> before = heights();
    // 1. cut-only: an inverted Standard stroke never raises a point, lowers under the centre, moves nothing outside r
    {
        Symmetry sym;
        sym.x = false;
        CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
        for (int i = 0; i < 4; ++i) {
            DabParams p = dabAt(glm::vec3(0.001f * float(i), 0, 1), r, 1.0f);
            p.invert = true;
            doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, p, sym);
        }
        UndoRecord rec = doc.sculptor.endStroke();
        CHECK(rec.micro != nullptr && !rec.micro->tileKeys.empty());
        const std::vector<float> after = heights();
        float maxRise = 0, maxSink = 0, outside = 0;
        for (size_t k = 0; k < samples.size(); ++k) {
            const float d = after[k] - before[k];
            maxRise = std::max(maxRise, d), maxSink = std::min(maxSink, d);
            if (glm::length(glm::vec2(samples[k].p.x, samples[k].p.y)) > 1.3f * r) outside = std::max(outside, std::abs(d));
        }
        std::printf("tiles cut: max rise %.1f units, max sink %.1f, outside the brush %.1f\n", double(maxRise), double(maxSink), double(outside));
        CHECK(maxRise <= 0.0f);
        CHECK(maxSink < -0.5f * 0.06f * r / doc.mesh.micro->step);
        CHECK(outside == 0.0f);
        doc.undo.push(std::move(rec));
        CHECK(doc.undo.undo(doc.mesh, doc.bvh));
        CHECK(heights() == before);
    }
    // 2. symmetry: with X symmetry the surface at (x, y) and (-x, y) changes alike
    {
        Symmetry sym;
        sym.x = true;
        CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
        for (int i = 0; i < 4; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0.6f * r + 0.001f * float(i), 0.002f * float(i), 1), r, 1.0f), sym);
        doc.undo.push(doc.sculptor.endStroke());
        const std::vector<float> after = heights();
        float worst = 0, biggest = 0;
        size_t pairs = 0;
        for (size_t k = 0; k < samples.size(); ++k) {
            const glm::vec3& p = samples[k].p;
            if (std::abs(p.x) < 1e-6f) continue;
            // the mirror sample: the closest one to (-x, y)
            size_t best = samples.size();
            float bd = 1e30f;
            for (size_t m = 0; m < samples.size(); ++m) {
                const float dd = glm::length(glm::vec2(samples[m].p.x + p.x, samples[m].p.y - p.y));
                if (dd < bd) bd = dd, best = m;
            }
            if (best == samples.size() || bd > 0.02f * r) continue;
            const float d1 = after[k] - before[k], d2 = after[best] - before[best];
            worst = std::max(worst, std::abs(d1 - d2)), biggest = std::max(biggest, std::abs(d1));
            ++pairs;
        }
        std::printf("tiles symmetry: %zu pairs, largest change %.1f units, worst mirror difference %.2f\n", pairs, double(biggest), double(worst));
        CHECK(pairs > 200 && biggest > 10.0f);
        CHECK(worst <= 0.02f * biggest + 1.0f); // the mirror matches to the quantization step and the sample pairing
    }
}

// Without the store (the default), nothing changes: no tiles appear and the step records none.
TEST(micro_tiles_absent_by_default) {
    Document doc = microDoc(4);
    CHECK(doc.mesh.micro->tiles == nullptr);
    UndoRecord r = fineStroke(doc);
    CHECK(r.micro != nullptr && r.micro->tileKeys.empty());
    CHECK(doc.mesh.micro->tiles == nullptr || !MicroTiles::enabled());
}

TEST(micro_divide_needs_top_level) {
    Document doc = makeDoc(makeBenchSphere(20000));
    std::string err;
    CHECK(subdivSetLevel(doc.mesh, 0, err, &doc.bvh));
    CHECK(!microDivide(doc.mesh, 2, err));
    CHECK(err.find("highest") != std::string::npos);
    CHECK(subdivSetLevel(doc.mesh, subdivLevelCount(doc.mesh) - 1, err, &doc.bvh));
    CHECK(microDivide(doc.mesh, 2, err));
    // ... and blocks subdivision changes while it exists.
    CHECK(!subdivSetLevel(doc.mesh, 0, err, &doc.bvh));
    CHECK(err.find("micro") != std::string::npos);
    CHECK(!subdivDivide(doc.mesh, true, err));
}

TEST(micro_standard_brush_raises_and_undoes) {
    Document doc = microDoc(4);
    const std::vector<int16_t> before = doc.mesh.micro->disp;
    const float h0 = frontHeight(doc);
    Symmetry sym;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 10; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(r.micro != nullptr);
    CHECK(!r.empty());
    CHECK(r.micro->tris.size() > 0 && r.micro->tris.size() < doc.mesh.triangleCount());
    doc.undo.push(std::move(r));
    const float h1 = frontHeight(doc);
    CHECK(h1 > h0 + 0.01f);
    CHECK(watertight(doc.mesh));
    // X symmetry: the mirrored side moved too, the back didn't.
    CHECK(doc.mesh.micro->rangeMax > 0);
    const std::vector<int16_t> after = doc.mesh.micro->disp;
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == before);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.micro->disp == after);
}

// #120: a recipe brush on a micro level keeps its surface pattern (Dragon Scales was a plain Standard blob) and its
// depth's sign (Wrinkle carves).
TEST(micro_recipe_brushes_keep_pattern_and_depth) {
    auto recipe = [](const char* name) {
        for (const BrushRecipe& r : builtinBrushRecipes())
            if (r.name == name) return r;
        CHECK(false);
        return BrushRecipe{};
    };
    const BrushRecipe scales = recipe("Dragon Scales"), wrinkle = recipe("Wrinkle");
    Symmetry sym;
    auto sculpt = [&](const BrushRecipe* r) {
        Document doc = microDoc(4);
        CHECK(doc.sculptor.beginStroke(doc.mesh, "Recipe"));
        for (int i = 0; i < 6; ++i) {
            if (r) doc.sculptor.dabRecipe(doc.mesh, doc.bvh, *r, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
            else doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
        }
        doc.undo.push(doc.sculptor.endStroke());
        CHECK(watertight(doc.mesh));
        return doc;
    };
    const float h0 = frontHeight(microDoc(4));
    const Document plain = sculpt(nullptr), scaled = sculpt(&scales), carved = sculpt(&wrinkle);
    CHECK(frontHeight(scaled) > h0 + 0.001f);
    CHECK(frontHeight(carved) < h0 - 0.001f);
    // The pattern: the scales' displacement differs from Standard's smooth bump over many grid points.
    const auto& a = plain.mesh.micro->disp;
    const auto& b = scaled.mesh.micro->disp;
    CHECK(a.size() == b.size());
    size_t differ = 0;
    for (size_t i = 0; i < a.size(); ++i) differ += std::abs(int(a[i]) - int(b[i])) > 2;
    CHECK(differ > 1000);
}

TEST(micro_brushes_each_change_the_surface_and_stay_closed) {
    struct Case {
        BrushType type;
        bool invert;
    };
    for (const Case c : {Case{BrushType::Clay, false}, Case{BrushType::ClayBlock, false}, Case{BrushType::Inflate, false},
                         Case{BrushType::Flatten, false}, Case{BrushType::Crease, false}, Case{BrushType::Pinch, false},
                         Case{BrushType::SharpCrease, false}, Case{BrushType::Layer, false}, Case{BrushType::Trim, false},
                         Case{BrushType::Standard, true}}) {
        Document doc = microDoc(3);
        microNoise(doc.mesh, 0.01f, 12.0f); // something to flatten, trim and pinch
        const std::vector<int16_t> before = doc.mesh.micro->disp;
        Symmetry sym;
        doc.sculptor.beginStroke(doc.mesh, brushName(c.type));
        for (int i = 0; i < 6; ++i) {
            DabParams p = dabAt(glm::normalize(glm::vec3(0.1f * float(i), 0.2f, 1.0f)), 0.25f, 0.8f);
            p.invert = c.invert;
            doc.sculptor.dab(doc.mesh, doc.bvh, c.type, p, sym);
        }
        UndoRecord r = doc.sculptor.endStroke();
        CHECK(doc.mesh.micro->disp != before);
        CHECK(watertight(doc.mesh));
        doc.undo.push(std::move(r));
        doc.undo.undo(doc.mesh, doc.bvh);
        CHECK(doc.mesh.micro->disp == before);
    }
}

TEST(micro_smooth_flattens_noise) {
    Document doc = microDoc(3);
    microNoise(doc.mesh, 0.01f, 30.0f);
    auto roughness = [&] { // spread of the displacement in the triangles near the front
        double sum = 0, sq = 0;
        size_t count = 0;
        const MicroMesh& mm = *doc.mesh.micro;
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const glm::uvec3 tr = doc.mesh.triangles[t];
            if (doc.mesh.positions[tr.x].z < 0.97f) continue;
            for (uint32_t k = 0; k < mm.samples; ++k) {
                const double v = mm.disp[size_t(t) * mm.samples + k];
                sum += v, sq += v * v, ++count;
            }
        }
        const double mean = sum / double(count);
        return sq / double(count) - mean * mean;
    };
    const double r0 = roughness();
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Smooth");
    for (int i = 0; i < 8; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Smooth, dabAt(glm::vec3(0, 0, 1), 0.4f, 1.0f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(roughness() < r0 * 0.5);
    CHECK(watertight(doc.mesh));
}

TEST(micro_smooth_peaks_lowers_the_high_points_only) {
    Document doc = microDoc(3);
    microNoise(doc.mesh, 0.01f, 30.0f);
    const auto before = doc.mesh.micro->disp;
    // The heights under the front, where the dabs land.
    const auto front = [&](const MicroStore& d, int16_t& lo, int16_t& hi) {
        const MicroMesh& mm = *doc.mesh.micro;
        lo = INT16_MAX, hi = INT16_MIN;
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            if (doc.mesh.positions[doc.mesh.triangles[t].x].z < 0.97f) continue;
            for (uint32_t k = 0; k < mm.samples; ++k) {
                const int16_t v = d[size_t(t) * mm.samples + k];
                lo = std::min(lo, v), hi = std::max(hi, v);
            }
        }
    };
    int16_t lo0, hi0, lo1, hi1;
    front(before, lo0, hi0);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "SmoothPeaks");
    for (int i = 0; i < 8; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::SmoothPeaks, dabAt(glm::vec3(0, 0, 1), 0.4f, 1.0f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    const MicroStore& after = doc.mesh.micro->disp;
    front(after, lo1, hi1);
    CHECK(hi1 < hi0);  // the tallest peak came down
    CHECK(lo1 >= lo0); // no hollow was cut deeper
    bool rose = false;
    for (size_t i = 0; i < after.size(); ++i) rose = rose || after[i] > before[i];
    CHECK(!rose); // nothing rose
    CHECK(watertight(doc.mesh));
}

TEST(micro_mask_brush_protects) {
    Document doc = microDoc(3);
    Symmetry sym;
    sym.x = false;
    // Mask the front.
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Mask"));
    DabParams mp = dabAt(glm::vec3(0, 0, 1), 0.5f, 1.0f);
    for (int i = 0; i < 8; ++i) doc.sculptor.dabMask(doc.mesh, doc.bvh, mp, sym);
    UndoRecord mr = doc.sculptor.endStroke();
    CHECK(doc.mesh.micro->hasMask());
    CHECK(mr.micro && mr.micro->maskAdded);
    doc.undo.push(std::move(mr));
    const float h0 = frontHeight(doc);
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    for (int i = 0; i < 5; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(std::abs(frontHeight(doc) - h0) < 1e-4f); // fully masked at the centre
    // Undo both: the mask goes away again (it didn't exist before).
    doc.undo.undo(doc.mesh, doc.bvh);
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(!doc.mesh.micro->hasMask());
    doc.undo.redo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.micro->hasMask());
}

TEST(micro_move_deforms_the_base_cage) {
    // Dual scale: Move reshapes the base below the micro level; the detail (displacements) rides along unchanged.
    Document doc = microDoc(3);
    microNoise(doc.mesh, 0.005f, 20.0f);
    const std::vector<int16_t> detail = doc.mesh.micro->disp;
    const float h0 = frontHeight(doc);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Move");
    doc.sculptor.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.4f, 1.0f), sym);
    for (int i = 0; i < 5; ++i) doc.sculptor.grab(doc.mesh, doc.bvh, glm::vec3(0.03f, 0, 0.02f));
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(!r.positions.empty() && !r.micro);
    doc.undo.push(std::move(r));
    CHECK(doc.mesh.micro->disp == detail);
    CHECK(frontHeight(doc) > h0 + 0.05f);
    CHECK(watertight(doc.mesh));
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(std::abs(frontHeight(doc) - h0) < 1e-4f);
}

TEST(micro_move_brush_moves_along_normals) {
    Document doc = microDoc(3);
    doc.sculptor.microMoveAlongNormals = true;
    const float h0 = frontHeight(doc);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Move");
    doc.sculptor.beginGrab(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.3f, 1.0f), sym);
    for (int i = 0; i < 5; ++i) doc.sculptor.grab(doc.mesh, doc.bvh, glm::vec3(0, 0, 0.02f));
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(frontHeight(doc) > h0 + 0.05f);
    CHECK(watertight(doc.mesh));
}

TEST(micro_range_widens_for_big_strokes) {
    Document doc = microDoc(2);
    doc.sculptor.microBaseDabs = false; // (the grid's range; a brush this big moves the base otherwise)
    const float step0 = doc.mesh.micro->step;
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    for (int i = 0; i < 60; ++i) doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.8f, 1.0f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(doc.mesh.micro->step > step0);
    CHECK(frontHeight(doc) > 1.5f); // nothing clipped at the old range (+/- 8 base edges)
    doc.undo.push(std::move(r));
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(std::abs(frontHeight(doc) - 1.0f) < 0.01f);
}

TEST(micro_raycast_hits_the_micro_surface) {
    Document doc = microDoc(4);
    RayHit base, micro;
    CHECK(doc.bvh.raycast(doc.mesh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), base));
    CHECK(microRaycast(doc.mesh, doc.bvh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), micro));
    CHECK(std::abs(micro.position.z - base.position.z) < 1e-3f); // flat: the Phong surface is near the base
    microNoise(doc.mesh, 0.02f, 8.0f);
    CHECK(surfaceRaycast(doc.mesh, doc.bvh, glm::vec3(0.01f, 0.01f, 5), glm::vec3(0, 0, -1), micro));
    // The hit lies on the micro surface: its point at the hit's base triangle and weights matches.
    const float h = glm::length(micro.position);
    CHECK(std::abs(h - 1.0f) < 0.03f);
    CHECK(std::abs(micro.t - (5.0f - micro.position.z)) < 1e-4f);
}

// microRaycast tests only the grid window a ray can reach on each base triangle: on a coarse base at a high level
// (64 x 64 cells a triangle, a bumpy surface) its nearest hit is the one testing every micro triangle of every base
// triangle finds, for rays from every side and grazing ones (that hit the base).
TEST(micro_raycast_window_matches_every_triangle) {
    Document doc = microDoc(6, 400);
    microNoise(doc.mesh, 0.03f, 9.0f, 5);
    const Mesh& m = doc.mesh;
    const MicroMesh& mm = *m.micro;
    const uint32_t n = mm.n;
    auto rayTri = [](const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
        const glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 1e-12f) return false;
        const float inv = 1.0f / det;
        const glm::vec3 s = o - a;
        const float u = glm::dot(s, p) * inv;
        if (u < 0.0f || u > 1.0f) return false;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) return false;
        t = glm::dot(e2, q) * inv;
        return t > 0.0f;
    };
    std::vector<glm::vec3> all(size_t(mm.baseTriangles) * mm.samples);
    for (uint32_t tri = 0; tri < mm.baseTriangles; ++tri)
        for (uint32_t j = 0; j <= n; ++j)
            for (uint32_t i = 0; i + j <= n; ++i) all[size_t(tri) * mm.samples + MicroMesh::index(i, j, n)] = microPoint(m, tri, i, j);
    auto every = [&](const glm::vec3& o, const glm::vec3& d) {
        float best = std::numeric_limits<float>::max();
        for (uint32_t tri = 0; tri < mm.baseTriangles; ++tri) {
            const glm::vec3* P = all.data() + size_t(tri) * mm.samples;
            for (uint32_t j = 0; j < n; ++j)
                for (uint32_t i = 0; i + j < n; ++i) {
                    const glm::vec3 &a = P[MicroMesh::index(i, j, n)], &b = P[MicroMesh::index(i + 1, j, n)],
                                    &c = P[MicroMesh::index(i, j + 1, n)];
                    float t;
                    if (rayTri(o, d, a, b, c, t)) best = std::min(best, t);
                    if (i + j + 1 < n && rayTri(o, d, b, P[MicroMesh::index(i + 1, j + 1, n)], c, t)) best = std::min(best, t);
                }
        }
        return best;
    };
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    int hits = 0, grazing = 0;
    for (int k = 0; k < 160; ++k) {
        // A point near the surface and a direction towards it from anywhere (every 4th nearly tangent to the sphere).
        const glm::vec3 on = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f));
        glm::vec3 d = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) - on * 1.5f);
        if (k % 4 == 3) {
            d = glm::normalize(glm::cross(on, glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f)) - on * 0.05f);
            ++grazing;
        }
        const glm::vec3 o = on - d * 3.0f;
        const float want = every(o, d);
        RayHit hit;
        // (a ray that grazes a bump beyond the base surface's outline misses the base: picks start from the base hit)
        if (want == std::numeric_limits<float>::max() || !doc.bvh.raycast(m, o, d, hit)) continue;
        CHECK(microRaycast(m, doc.bvh, o, d, hit));
        CHECK(std::abs(hit.t - want) <= 1e-5f * want);
        ++hits;
    }
    CHECK(hits > 100);
    CHECK(grazing == 40);
}

TEST(micro_shading_partial_matches_full) {
    Document doc = microDoc(3, 5000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    const MicroMesh& mm = *doc.mesh.micro;
    std::vector<uint32_t> full(size_t(mm.baseTriangles) * mm.samples, 0), part(full.size(), 0);
    MicroShader shader;
    std::vector<uint32_t> written;
    shader.shade(doc.mesh, {}, true, written, [&](uint32_t t) { return full.data() + size_t(t) * mm.samples; });
    CHECK(written.size() == mm.baseTriangles);
    // A brush changes a few triangles; shading just what changed must agree with shading everything.
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.2f, 1.0f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    std::vector<uint32_t> dirty = r.micro->tris;
    part = full;
    shader.shade(doc.mesh, dirty, false, written, [&](uint32_t t) { return part.data() + size_t(t) * mm.samples; });
    CHECK(written.size() > dirty.size());
    shader.shade(doc.mesh, {}, true, written, [&](uint32_t t) { return full.data() + size_t(t) * mm.samples; });
    CHECK(part == full);
    // Normals point outwards on the sphere.
    int outward = 0, total = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; t += 53) {
        const uint32_t v = full[size_t(t) * mm.samples];
        const float ex = float(v & 255) / 255.0f * 2.0f - 1.0f, ey = float((v >> 8) & 255) / 255.0f * 2.0f - 1.0f;
        glm::vec3 nn(ex, ey, 1.0f - std::abs(ex) - std::abs(ey));
        const float tt = std::max(-nn.z, 0.0f);
        nn.x += nn.x >= 0.0f ? -tt : tt;
        nn.y += nn.y >= 0.0f ? -tt : tt;
        outward += glm::dot(glm::normalize(nn), doc.mesh.positions[doc.mesh.triangles[t].x]) > 0.8f;
        ++total;
    }
    CHECK(outward >= total * 9 / 10);
}

// A stroke's shading writes the changed triangles alone (MicroRenderer: their neighbours wait for the stroke's
// end): those come out exact, the neighbours after them complete it, and until then they're off by the falloff tail.
TEST(micro_shading_alone_then_outer) {
    Document doc = microDoc(3, 5000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    const MicroMesh& mm = *doc.mesh.micro;
    const size_t S = mm.samples;
    std::vector<uint32_t> before(size_t(mm.baseTriangles) * S, 0), full(before.size(), 0);
    MicroShader shader;
    std::vector<uint32_t> written, outer;
    auto into = [&](std::vector<uint32_t>& v) { return [&v, S](uint32_t t) { return v.data() + size_t(t) * S; }; };
    shader.shade(doc.mesh, {}, true, written, into(before));
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.2f, 1.0f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    std::vector<uint32_t> dirty = r.micro->tris;
    std::sort(dirty.begin(), dirty.end());
    shader.shade(doc.mesh, {}, true, written, into(full));
    std::vector<uint32_t> part = before;
    shader.shade(doc.mesh, dirty, false, written, into(part), {}, false, &outer);
    CHECK(written.size() == dirty.size());
    CHECK(!outer.empty());
    for (uint32_t t : dirty) CHECK(std::equal(part.begin() + std::ptrdiff_t(t * S), part.begin() + std::ptrdiff_t((t + 1) * S),
                                              full.begin() + std::ptrdiff_t(t * S)));
    // The stale neighbours: a few points along the shared border, by little.
    int worst = 0;
    size_t off = 0;
    for (uint32_t t : outer)
        for (size_t k = 0; k < S; ++k) {
            const uint32_t a = part[t * S + k], b = full[t * S + k];
            if (a == b) continue;
            ++off;
            for (int c = 0; c < 16; c += 8) worst = std::max(worst, std::abs(int((a >> c) & 255) - int((b >> c) & 255)));
        }
    std::printf("  %zu changed, %zu outer, %zu stale points, worst normal code off by %d\n", dirty.size(), outer.size(), off, worst);
    CHECK(off < outer.size() * 3 * mm.n);
    CHECK(worst <= 8);
    shader.shade(doc.mesh, outer, false, written, into(part), {}, false, nullptr);
    CHECK(part == full);
}

TEST(micro_bake_mesh_is_closed_and_matches) {
    Document doc = microDoc(3, 3000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    Mesh baked;
    std::string err;
    CHECK(microBakeMesh(doc.mesh, 3, baked, err));
    CHECK(baked.triangleCount() == microTriangleCount(doc.mesh));
    CHECK(baked.vertexCount() == doc.mesh.micro->microVertices());
    // Closed: Euler characteristic 2 (V - E + F with E = 3F/2).
    CHECK(long(baked.vertexCount()) - long(baked.triangleCount()) * 3 / 2 + long(baked.triangleCount()) == 2);
    // The base corners are the micro points there.
    const glm::vec3 c = microPoint(doc.mesh, 0, 0, 0);
    CHECK(glm::distance(baked.positions[doc.mesh.triangles[0].x], c) < 1e-6f);
    // Half the level: a quarter of the triangles.
    Mesh half;
    CHECK(microBakeMesh(doc.mesh, 2, half, err));
    CHECK(half.triangleCount() * 4 == baked.triangleCount());
    CHECK(!microBakeMesh(doc.mesh, 3, baked, err, 1000)); // the size limit
}

TEST(micro_displacement_maps) {
    Mesh plane = makePlane(16, 2.0f);
    plane.uvs.resize(plane.triangleCount() * 3);
    for (size_t t = 0; t < plane.triangleCount(); ++t)
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = plane.positions[plane.triangles[t][k]];
            plane.uvs[t * 3 + size_t(k)] = glm::vec2(p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f);
        }
    Document doc = makeDoc(std::move(plane));
    std::string err;
    CHECK(microDivide(doc.mesh, 3, err));
    microNoise(doc.mesh, 0.02f, 6.0f);
    float scale = 0.0f;
    const std::string exr = tempPath("sf_micro_disp.exr"), png = tempPath("sf_micro_disp.png");
    CHECK(microExportDisplacement(doc.mesh, exr, 128, false, "exr", scale, err));
    CHECK(scale > 0.001f && scale < 0.1f);
    CHECK(std::filesystem::file_size(exr) > 128 * 128 * 4);
    CHECK(microExportDisplacement(doc.mesh, png, 64, true, "png", scale, err));
    CHECK(std::filesystem::exists(png));
    std::filesystem::remove(exr);
    std::filesystem::remove(png);
    Document sphere = microDoc(2, 3000);
    CHECK(!microExportDisplacement(sphere.mesh, exr, 64, false, "exr", scale, err)); // no UVs
    CHECK(err.find("UV") != std::string::npos);
}

TEST(micro_project_round_trip) {
    Document doc = microDoc(3, 5000);
    microNoise(doc.mesh, 0.01f, 10.0f);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Mask");
    doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.3f, 1.0f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    const std::string path = tempPath("sf_micro_round_trip.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    auto version = [&] {
        std::vector<ZipEntry> entries;
        Json manifest;
        std::string e;
        if (!readStoredZip(path, entries, e) || entries.empty() ||
            !Json::parse(std::string(entries[0].data.begin(), entries[0].data.end()), manifest, e))
            return -1;
        return int(manifest["version"].asNumber());
    };
    CHECK(version() == 7);
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(loaded.mesh.micro != nullptr);
    if (loaded.mesh.micro) {
        CHECK(loaded.mesh.micro->level == 3);
        CHECK(loaded.mesh.micro->step == doc.mesh.micro->step);
        CHECK(loaded.mesh.micro->disp == doc.mesh.micro->disp);
        CHECK(loaded.mesh.micro->mask == doc.mesh.micro->mask);
        CHECK(loaded.mesh.micro->rangeMax == doc.mesh.micro->rangeMax);
    }
    std::filesystem::remove(path);
    // Without a micro level a project stays format 6 (0.9.7 and older open it).
    Document plain = makeDoc(makeIcosphere(2));
    CHECK(saveProject(path, plain, err));
    CHECK(version() == 6);
    Document again;
    CHECK(loadProject(path, again, err));
    CHECK(again.projectSettings.isObject());
    std::filesystem::remove(path);
}

TEST(micro_commands) {
    CommandRegistry reg;
    Document doc = makeDoc(makeBenchSphere(20000));
    CHECK(reg.execute(doc, "micro_info").ok);
    CommandResult r = reg.execute(doc, "micro_divide", {{"triangles", 1.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.micro != nullptr);
    CHECK(microLevel(doc.mesh) == microLevelFor(doc.mesh, 1000000));
    CHECK(reg.execute(doc, "micro_noise", {{"amplitude", 0.002}}).ok);
    CHECK(reg.execute(doc, "micro_info").message.find("Micro level") != std::string::npos);
    CHECK(reg.execute(doc, "micro_bake", {{"level", 1.0}}).ok);
    CHECK(doc.partCount() == 2);
    CHECK(reg.execute(doc, "micro_delete").ok);
    CHECK(doc.mesh.micro == nullptr);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.micro != nullptr);
    CHECK(reg.execute(doc, "undo").ok); // the noise
    CHECK(reg.execute(doc, "undo").ok); // the divide
    CHECK(doc.mesh.micro == nullptr);
}

// The point of micro levels: memory and dab time on a dense surface. 5M micro triangles here (fast enough for
// every ctest run); --bench-micro measures 33M - 300M. Timing limits only with SF_BENCH_LIMITS=1.
TEST(micro_memory_and_dab_time) {
    Document doc = makeDoc(makeBenchSphere(80000));
    std::string err;
    const size_t rss0 = residentBytes();
    CHECK(microDivide(doc.mesh, 3, err));
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.microTriangles() > 4'000'000);
    const double bytesPerMicroTriangle = double(mm.bytes()) / double(mm.microTriangles());
    CHECK(bytesPerMicroTriangle < 2.0);
    (void)rss0;
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i)
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::normalize(glm::vec3(0.02f * float(i), 0.1f, 1)), 0.12f), sym);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
    doc.undo.push(doc.sculptor.endStroke());
    std::printf("  micro dab at %.1fM micro triangles: %.2f ms, %.2f bytes/micro triangle\n", double(mm.microTriangles()) / 1e6,
                ms, bytesPerMicroTriangle);
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(ms < 10.0);
}

TEST(micro_tile_codec_round_trip) {
    const uint32_t S = 45;
    std::vector<int16_t> v(S * 5, 0);
    for (uint32_t k = 0; k < S; ++k) v[S + k] = int16_t(int(k) * 7 - 100); // a spread tile
    for (uint32_t k = 0; k < S; ++k) v[3 * S + k] = int16_t(12345);        // constant, not zero
    v[4 * S] = -32767, v[4 * S + 1] = 32767;                                // the full range
    std::vector<uint8_t> packed;
    microPackTiles(v.data(), 5, S, packed);
    CHECK(packed.size() < v.size() * 2);
    std::vector<int16_t> back(v.size(), 0);
    CHECK(microUnpackTiles(packed.data(), packed.size(), 5, S, back.data()));
    CHECK(back == v);
    CHECK(!microUnpackTiles(packed.data(), packed.size() - 1, 5, S, back.data())); // damaged
}

TEST(micro_out_of_core_store) {
    MicroStore s;
    std::string err;
    const size_t n = size_t(64) << 20; // 128 MB of address space, untouched
    const bool mapped = s.assignMapped(n, err);
    CHECK(s.size() == n);
    if (mapped) {
        CHECK(s.mapped());
        CHECK(s.residentBytes() < (size_t(8) << 20)); // holes take no memory
        s[12345] = 77;
        s[n - 1] = -5;
        MicroStore copy = s;
        CHECK(copy.mapped() && copy[12345] == 77 && copy[n - 1] == -5 && copy[0] == 0);
    }
    // A micro level kept out of core sculpts, undoes and saves like one in memory.
    Document doc = makeDoc(makeBenchSphere(20000));
    CHECK(microDivide(doc.mesh, 3, err, 1));
    CHECK(doc.mesh.micro->disp.mapped() == mapped);
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.3f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    CHECK(watertight(doc.mesh));
    const std::vector<int16_t> sculpted = doc.mesh.micro->disp;
    const std::string path = tempPath("sf_micro_ooc.sfproj");
    CHECK(saveProject(path, doc, err));
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    CHECK(loaded.mesh.micro && loaded.mesh.micro->disp == sculpted);
    std::filesystem::remove(path);
}

TEST(micro_adaptive_brush_matches_full) {
    // A big brush evaluated on a coarse stride lands close to the same brush on every point.
    Document a = microDoc(4), b = microDoc(4);
    Symmetry sym;
    a.sculptor.microStroke().adaptive = false;
    for (Document* d : {&a, &b}) {
        d->sculptor.beginStroke(d->mesh, "Standard");
        d->sculptor.dab(d->mesh, d->bvh, BrushType::Standard, dabAt(glm::vec3(0, 0, 1), 0.6f, 1.0f), sym);
        d->undo.push(d->sculptor.endStroke());
    }
    CHECK(b.sculptor.microStroke().lastStride > 1);
    CHECK(a.sculptor.microStroke().lastStride == 1);
    const float ha = frontHeight(a), hb = frontHeight(b);
    CHECK(ha > 1.01f);
    CHECK(std::abs(ha - hb) < (ha - 1.0f) * 0.02f);
    CHECK(watertight(b.mesh));
}

TEST(micro_big_plane_brush_invariant_coarse_plane_stride) {
    // #184: a big brush's plane (Clay, Flatten, Trim) is found on fewer grid points (while thousands stay under the
    // brush), a small one's on every second point as before. Overlapping dabs over a rough surface land where the
    // every-second-point plane puts them (no wobble that could stack up), a small brush is bit-identical, and the
    // brushes keep their promise on either plane: Clay only adds, Trim only cuts.
    for (BrushType type : {BrushType::Clay, BrushType::Flatten, BrushType::Trim})
        for (float r : {0.6f, 0.04f}) {
            Document a = microDoc(5), b = microDoc(5);
            Symmetry sym;
            a.sculptor.microStroke().planeSamples = 1e30f; // (every second point)
            std::vector<int16_t> start;
            float startStep = 0.0f;
            for (Document* d : {&a, &b}) {
                microNoise(d->mesh, 0.002f, 60.0f, 7);
                start = d->mesh.micro->disp;
                startStep = d->mesh.micro->step;
                d->sculptor.beginStroke(d->mesh, "Clay");
                for (int i = 0; i < 6; ++i)
                    d->sculptor.dab(d->mesh, d->bvh, type, dabAt(glm::normalize(glm::vec3(r * 0.4f * float(i - 3), 0.05f, 1.0f)), r, 0.8f), sym);
                d->undo.push(d->sculptor.endStroke());
            }
            CHECK(a.sculptor.microStroke().lastPlaneStride == 2);
            const MicroMesh &ma = *a.mesh.micro, &mb = *b.mesh.micro;
            CHECK(ma.step == mb.step);
            float change = 0.0f, diff = 0.0f;
            for (size_t k = 0; k < ma.disp.size(); ++k) {
                change = std::max(change, std::abs(float(ma.disp[k]) - float(start[k])));
                diff = std::max(diff, std::abs(float(ma.disp[k]) - float(mb.disp[k])));
            }
            std::printf("  %s r %.2f: plane stride %u, largest change %.0f, largest difference %.0f (steps)\n", brushName(type), r,
                        b.sculptor.microStroke().lastPlaneStride, change, diff);
            CHECK(change > 0.0f);
            CHECK(watertight(b.mesh));
            if (mb.step == startStep && (type == BrushType::Clay || type == BrushType::Trim)) { // (a widen rescales the units)
                size_t wrong = 0;
                for (size_t k = 0; k < mb.disp.size(); ++k)
                    wrong += type == BrushType::Clay ? mb.disp[k] < start[k] : mb.disp[k] > start[k];
                CHECK(wrong == 0);
            }
            if (r < 0.1f) {
                CHECK(b.sculptor.microStroke().lastPlaneStride == 2);
                CHECK(ma.disp == mb.disp);
            } else {
                CHECK(b.sculptor.microStroke().lastPlaneStride > 2);
                CHECK(diff <= change * 0.01f);
            }
        }
}

TEST(micro_undo_journal_is_small) {
    // A stroke's undo step is its change, packed per tile in Morton order: far below 2 bytes per touched point.
    Document doc = microDoc(4);
    microNoise(doc.mesh, 0.003f, 30.0f); // detail under the brush (old values far from zero)
    Symmetry sym;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    for (int i = 0; i < 10; ++i)
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::normalize(glm::vec3(0.03f * float(i), 0.1f, 1)), 0.15f), sym);
    UndoRecord r = doc.sculptor.endStroke();
    CHECK(r.micro != nullptr);
    if (r.micro) r.micro->settle(); // (the journal is packed off the release: #184)
    CHECK(r.micro && r.micro->journal);
    const size_t raw = r.micro->tris.size() * doc.mesh.micro->samples * 2;
    std::printf("  micro undo journal: %zu tiles, %.1f KB (whole tiles %.1f KB)\n", r.micro->tris.size(),
                double(r.micro->bytes()) / 1e3, double(raw) / 1e3);
    CHECK(r.micro->bytes() * 2 < raw);
    CHECK(std::is_sorted(r.micro->keys.begin(), r.micro->keys.end()));
    const std::vector<int16_t> after = doc.mesh.micro->disp;
    doc.undo.push(std::move(r));
    doc.undo.undo(doc.mesh, doc.bvh);
    doc.undo.redo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.micro->disp == after);
}

TEST(micro_tier_config_and_page_table) {
    // The tier (feat/hw-profile) decides when a level goes out of core: past its resident cache.
    const TierConfig saved = currentTierConfig();
    TierConfig small = saved;
    small.outOfCorePoints = size_t(1) << 20;
    setCurrentTierConfig(small);
    if (!std::getenv("SF_MICRO_OUT_OF_CORE")) {
        CHECK(microWantsOutOfCore(size_t(2) << 20));
        CHECK(!microWantsOutOfCore(size_t(1) << 19));
    }
    setCurrentTierConfig(saved);
    Document doc = microDoc(2, 3000);
    PatchPageTable pages(*doc.mesh.micro);
    CHECK(pages.residentPatches() == doc.mesh.micro->baseTriangles); // in memory: every patch
    MicroPatch p = microPatch(doc.mesh, 5);
    CHECK(p.points() == doc.mesh.micro->samples && p.heights == doc.mesh.micro->disp.data() + 5 * p.points());
    std::string err;
    Document ooc = makeDoc(makeBenchSphere(20000));
    CHECK(microDivide(ooc.mesh, 3, err, 1));
    if (ooc.mesh.micro->disp.mapped()) {
        PatchPageTable cold(*ooc.mesh.micro);
        CHECK(cold.residentPatches() < ooc.mesh.micro->baseTriangles); // untouched: holes, not resident
        microPatch(ooc.mesh, 7).heights[0] = 3;
        CHECK(cold.resident(7));
        cold.trim(1);
        CHECK(ooc.mesh.micro->disp[7 * ooc.mesh.micro->samples] == 3); // trimmed pages come back from the file
    }
}

TEST(micro_core200m_scene_small) {
    // The core-200m research scene (docs/design/core-200m.md), at a test's size: in memory, detailed, and it
    // survives a project round trip unchanged.
    Document doc;
    doc.mesh = makeCore200mMesh(2000, 3);
    CHECK(doc.mesh.micro);
    if (!doc.mesh.micro) return;
    const MicroMesh& mm = *doc.mesh.micro;
    CHECK(mm.level == 3 && !mm.disp.mapped());
    CHECK(microTriangleCount(doc.mesh) == size_t(mm.baseTriangles) * 64);
    CHECK(mm.rangeMax > 0); // the noise is there
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string path = (fs::temp_directory_path(ec) / "sf-test-core200m.sfproj").string();
    std::string err;
    const bool saved = saveProject(path, doc, err);
    CHECK(saved);
    Document back;
    const bool loaded = saved && loadProject(path, back, err);
    CHECK(loaded);
    fs::remove(path, ec);
    if (!loaded) return;
    CHECK(back.mesh.micro);
    if (!back.mesh.micro) return;
    CHECK(back.mesh.micro->baseTriangles == mm.baseTriangles && back.mesh.micro->level == 3);
    bool same = true;
    for (size_t i = 0; i < mm.disp.size() && same; ++i) same = back.mesh.micro->disp[i] == mm.disp[i];
    CHECK(same);
}

TEST(wavelet_round_trip_and_coarse_edit) {
    // N2 (docs/design/core-200m.md): the integer lifting transform is exactly invertible, whatever the values, and a
    // coarse edit leaves the fine detail alone: the result is the old surface plus the coarse change interpolated.
    const uint32_t n = 32, S = (n + 1) * (n + 2) / 2;
    std::vector<int16_t> g(S), orig;
    uint32_t seed = 12345;
    for (int16_t& v : g) seed = seed * 1664525u + 1013904223u, v = int16_t(seed >> 16);
    orig = g;
    waveletForward(g.data(), n);
    CHECK(g != orig);
    waveletInverse(g.data(), n);
    CHECK(g == orig);
    // A smooth surface with fine detail, then a constant raise on the spacing-8 lattice.
    for (uint32_t j = 0; j <= n; ++j)
        for (uint32_t i = 0; i + j <= n; ++i)
            g[MicroMesh::index(i, j, n)] = int16_t(100 * i + 37 * j + ((i * 7 + j * 13) % 5) * 20);
    orig = g;
    waveletForward(g.data(), n);
    waveletAddCoarse(g.data(), n, 8, [](uint32_t, uint32_t, int) { return 500; });
    waveletInverse(g.data(), n);
    int worst = 0;
    for (uint32_t k = 0; k < S; ++k) worst = std::max(worst, std::abs(int(g[k]) - int(orig[k]) - 500));
    CHECK(worst <= 3); // (floor averages round at most once per finer level: 3 levels below spacing 8)
}

TEST(micro_dab_marks_only_what_changed) {
    // A dab marks dirty (what the renderer re-sends, re-bounds and re-shades) every patch whose heights changed,
    // and not the candidates it left alone: a Clay dab's plane misses much of what it covers (on a scan's micro
    // level, millions of tiny patches).
    Document doc = microDoc(2, 200000);
    doc.sculptor.microBaseDabs = false; // (the grid's dabs; one this big moves the base otherwise)
    MicroMesh& mm = *doc.mesh.micro;
    Symmetry sym;
    sym.x = false;
    doc.sculptor.beginStroke(doc.mesh, "Clay");
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::vec3(0, 0, 1), 0.3f), sym); // (some height to level)
    const std::vector<int16_t> before = mm.disp;
    mm.clearDirty();
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::vec3(0.05f, 0, 1), 0.3f), sym);
    doc.undo.push(doc.sculptor.endStroke());
    const uint32_t S = mm.samples;
    size_t changed = 0, marked = 0, missed = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const bool moved = !std::equal(before.begin() + size_t(t) * S, before.begin() + size_t(t + 1) * S, mm.disp.begin() + size_t(t) * S);
        const bool dirty = mm.dirtyBits[t >> 6] >> (t & 63) & 1;
        changed += moved, marked += dirty, missed += moved && !dirty;
    }
    std::printf("  clay dab: %zu patches changed, %zu marked\n", changed, marked);
    CHECK(changed > 100);
    CHECK(missed == 0);
    CHECK(marked <= changed + changed / 10); // (the ones changed, give or take a stitched edge)
    CHECK(watertight(doc.mesh));
}

TEST(micro_alpha_dab_marks_only_what_changed) {
    // #184: an alpha's dab looks at every patch in reach of its square's corners, and a sparse one (Bolt: a washer and a
    // bolt head) changes well under half of them. Marking them all re-shaded the rest for nothing, and a long stroke's
    // shading on a 2.6M base fell 1-2 s behind the brush.
    Document doc = microDoc(2, 200000);
    doc.sculptor.microBaseDabs = false;
    MicroMesh& mm = *doc.mesh.micro;
    BrushAlpha alpha;
    CHECK(BrushAlpha::builtin("Bolt", alpha));
    Symmetry sym;
    sym.x = false;
    doc.sculptor.beginStroke(doc.mesh, "Standard");
    const std::vector<int16_t> before = mm.disp;
    mm.clearDirty();
    DabParams p = dabAt(glm::vec3(0, 0, 1), 0.3f);
    p.alpha = &alpha;
    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, p, sym);
    doc.undo.push(doc.sculptor.endStroke());
    const uint32_t S = mm.samples;
    size_t changed = 0, marked = 0, missed = 0;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
        const bool moved = !std::equal(before.begin() + size_t(t) * S, before.begin() + size_t(t + 1) * S, mm.disp.begin() + size_t(t) * S);
        const bool dirty = mm.dirtyBits[t >> 6] >> (t & 63) & 1;
        changed += moved, marked += dirty, missed += moved && !dirty;
    }
    std::printf("  bolt dab: %zu patches changed, %zu marked\n", changed, marked);
    CHECK(changed > 100);
    CHECK(missed == 0);
    CHECK(marked <= changed + changed / 10);
    CHECK(watertight(doc.mesh));
}

TEST(micro_alpha_stroke_invariant_cut_only_and_every_change_marked) {
    // #184's promise on a dense curved micro mesh: an overlapping inverted Bolt stroke (six dabs, each over the last)
    // only cuts (no grid point rises), and each dab marks every patch it changed (an unmarked one would keep its old
    // shading) and few it didn't; the surface stays watertight across base edges.
    Document doc = microDoc(2, 200000);
    doc.sculptor.microBaseDabs = false;
    MicroMesh& mm = *doc.mesh.micro;
    BrushAlpha alpha;
    CHECK(BrushAlpha::builtin("Bolt", alpha));
    Symmetry sym;
    sym.x = false;
    const std::vector<int16_t> start = mm.disp;
    const uint32_t S = mm.samples;
    size_t changedAll = 0, markedAll = 0, missed = 0;
    CHECK(doc.sculptor.beginStroke(doc.mesh, "Standard"));
    for (int i = 0; i < 6; ++i) {
        const std::vector<int16_t> before = mm.disp;
        mm.clearDirty();
        DabParams p = dabAt(glm::normalize(glm::vec3(0.02f * float(i), 0.01f * float(i), 1.0f)), 0.3f);
        p.alpha = &alpha;
        p.invert = true;
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, p, sym);
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const bool moved = !std::equal(before.begin() + size_t(t) * S, before.begin() + size_t(t + 1) * S, mm.disp.begin() + size_t(t) * S);
            const bool dirty = mm.dirtyBits[t >> 6] >> (t & 63) & 1;
            changedAll += moved, markedAll += dirty, missed += moved && !dirty;
        }
    }
    doc.undo.push(doc.sculptor.endStroke());
    size_t rose = 0, sank = 0;
    for (size_t k = 0; k < start.size(); ++k) rose += mm.disp[k] > start[k], sank += mm.disp[k] < start[k];
    std::printf("  bolt stroke: %zu patch changes, %zu marked, %zu missed; %zu points sank, %zu rose\n", changedAll, markedAll, missed, sank, rose);
    CHECK(changedAll > 600);
    CHECK(missed == 0);
    CHECK(markedAll <= changedAll + changedAll / 10);
    CHECK(sank > 1000);
    CHECK(rose == 0);
    CHECK(watertight(doc.mesh));
}

namespace {

// A stroke across the sphere's front on `d` (a big brush: Sculptor::kMicroBaseSpan base edges and more).
void baseTestStroke(Document& d, BrushType type, float r, Symmetry sym = {}) {
    d.sculptor.beginStroke(d.mesh, "Stroke");
    for (int i = 0; i < 10; ++i) {
        const float x = -0.2f + 0.04f * float(i);
        d.sculptor.dab(d.mesh, d.bvh, type, dabAt(glm::vec3(x, 0.05f, std::sqrt(1.0f - x * x - 0.0025f)), r, 0.4f), sym);
    }
    d.undo.push(d.sculptor.endStroke());
}

// Every grid point of the micro surface, in order.
std::vector<glm::vec3> microSurface(const Mesh& m) {
    const MicroMesh& mm = *m.micro;
    std::vector<glm::vec3> out;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        for (uint32_t j = 0; j <= mm.n; ++j)
            for (uint32_t i = 0; i + j <= mm.n; ++i) out.push_back(microPoint(m, t, i, j));
    return out;
}

} // namespace

TEST(micro_base_stroke_is_the_plain_brush) {
    // A big Standard, Inflate or Clay brush on a micro level over a dense base (a scan's) moves the base vertices
    // (Sculptor::microBaseDab): as the plain brush moves the same mesh without the level (no detail: the corners are
    // the vertices), the detail riding along untouched (its heights bit-identical), one undo step
    // bringing every vertex and height back bit-exactly, redo the stroke.
    for (BrushType type : {BrushType::Standard, BrushType::Inflate, BrushType::Clay}) {
        for (bool detail : {false, true}) {
            Document plain = makeDoc(makeBenchSphere(200000)), base = microDoc(2, 200000);
            plain.sculptor.adaptiveDabs = false; // (its plane from every vertex, as the base path's)
            if (detail) microNoise(base.mesh, 0.002f, 60.0f, 7);
            const std::vector<glm::vec3> positions = base.mesh.positions;
            const std::vector<int16_t> heights = base.mesh.micro->disp;
            const std::vector<glm::vec3> surface = microSurface(base.mesh);
            for (Document* d : {&plain, &base}) baseTestStroke(*d, type, 0.3f);
            CHECK(base.mesh.micro->disp == heights);
            CHECK(base.sculptor.microStroke().points == 0); // (not one grid point visited)
            double height = 0, worst = 0;
            for (size_t v = 0; v < positions.size(); ++v) {
                height = std::max(height, double(glm::distance(plain.mesh.positions[v], positions[v])));
                worst = std::max(worst, double(glm::distance(plain.mesh.positions[v], base.mesh.positions[v])));
            }
            std::printf("  base stroke %d%s: height %.4f, off the plain brush by %.4f%% at most\n", int(type), detail ? " (detail)" : "",
                        height, 100.0 * worst / height);
            CHECK(height > 0.005);
            // (within a few percent: the plain brush picks and re-normals its region its own way, dab after dab; the
            // detail's corner heights tilt the brush plane a little more)
            CHECK(worst <= height * (detail ? 0.08 : 0.03));
            const std::vector<glm::vec3> after = microSurface(base.mesh);
            CHECK(base.undo.undo(base.mesh, base.bvh));
            CHECK(base.mesh.positions == positions && base.mesh.micro->disp == heights && microSurface(base.mesh) == surface);
            CHECK(base.undo.redo(base.mesh, base.bvh));
            CHECK(microSurface(base.mesh) == after);
            CHECK(watertight(base.mesh));
        }
    }
}

TEST(micro_base_release_dabs_fuse) {
    // The dabs still waiting when the pen lifts (Sculptor::microBaseFuse, App::endStroke) go through the plain
    // path, fused: they move the base exactly as the plain brush moves the same mesh, and a stroke whose first dabs
    // took the base kernel and whose last ones were fused is still one undo step, bit-exact both ways.
    Symmetry sym;
    sym.x = false;
    auto dabs = [&](Document& d, int from, int to) {
        for (int i = from; i < to; ++i) {
            const float x = -0.2f + 0.04f * float(i);
            d.sculptor.dab(d.mesh, d.bvh, BrushType::Clay, dabAt(glm::vec3(x, 0.05f, std::sqrt(1.0f - x * x - 0.0025f)), 0.3f, 0.4f), sym);
        }
    };
    {
        Document plain = makeDoc(makeBenchSphere(200000)), base = microDoc(2, 200000);
        microNoise(base.mesh, 0.002f, 60.0f, 7);
        const std::vector<int16_t> heights = base.mesh.micro->disp;
        base.sculptor.microBaseFuse = true;
        for (Document* d : {&plain, &base}) {
            d->sculptor.beginStroke(d->mesh, "Clay");
            d->sculptor.beginBatch();
            dabs(*d, 0, 10);
            d->sculptor.endBatch(d->mesh, d->bvh);
            d->undo.push(d->sculptor.endStroke());
        }
        CHECK(base.mesh.positions == plain.mesh.positions);
        CHECK(base.mesh.micro->disp == heights);
        CHECK(base.sculptor.microStroke().points == 0);
        CHECK(watertight(base.mesh));
    }
    {
        Document doc = microDoc(2, 200000);
        const std::vector<glm::vec3> positions = doc.mesh.positions;
        const std::vector<int16_t> heights = doc.mesh.micro->disp;
        doc.sculptor.beginStroke(doc.mesh, "Clay");
        dabs(doc, 0, 5);
        doc.sculptor.microBaseFuse = true;
        doc.sculptor.beginBatch();
        dabs(doc, 5, 10);
        doc.sculptor.endBatch(doc.mesh, doc.bvh);
        doc.sculptor.microBaseFuse = false;
        doc.undo.push(doc.sculptor.endStroke());
        const std::vector<glm::vec3> after = doc.mesh.positions;
        CHECK(after != positions);
        CHECK(doc.undo.undo(doc.mesh, doc.bvh));
        CHECK(doc.mesh.positions == positions && doc.mesh.micro->disp == heights);
        CHECK(doc.undo.redo(doc.mesh, doc.bvh));
        CHECK(doc.mesh.positions == after);
        CHECK(watertight(doc.mesh));
    }
}

TEST(micro_base_stroke_symmetry_hidden_mask) {
    // Symmetry: both sides move alike. Hidden faces: their vertices stay. A micro mask: the grid brush (it masks
    // per grid point), not the base.
    {
        Document doc = microDoc(2, 200000);
        Symmetry sym; // (x on)
        const std::vector<glm::vec3> before = doc.mesh.positions;
        doc.sculptor.beginStroke(doc.mesh, "Stroke");
        doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Standard, dabAt(glm::vec3(0.4f, 0.0f, std::sqrt(1.0f - 0.16f)), 0.25f), sym);
        doc.undo.push(doc.sculptor.endStroke());
        double left = 0, right = 0;
        for (size_t v = 0; v < before.size(); ++v) {
            const double d = glm::distance(before[v], doc.mesh.positions[v]);
            (before[v].x > 0 ? right : left) += d;
        }
        CHECK(right > 0.0);
        CHECK(std::abs(left - right) <= right * 0.02);
    }
    {
        // Hidden faces: the grid (it keeps hidden patches whole, and the base path would move their shared corners).
        Document doc = microDoc(2, 200000);
        Mesh& m = doc.mesh;
        m.hidden.assign(m.triangles.size(), 0);
        for (size_t t = 0; t < m.triangles.size(); ++t) {
            const glm::uvec3 tr = m.triangles[t];
            if ((m.positions[tr[0]].x + m.positions[tr[1]].x + m.positions[tr[2]].x) / 3.0f > 0.0f) m.hidden[t] = 1;
        }
        const std::vector<glm::vec3> before = m.positions;
        baseTestStroke(doc, BrushType::Standard, 0.3f);
        CHECK(m.positions == before);
        CHECK(doc.sculptor.microStroke().points > 0);
        CHECK(watertight(m));
    }
    {
        Document doc = microDoc(2, 200000);
        Symmetry sym;
        sym.x = false;
        doc.sculptor.beginStroke(doc.mesh, "Mask");
        doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::vec3(0, 0, 1), 0.1f, 1.0f), sym);
        doc.undo.push(doc.sculptor.endStroke());
        const std::vector<glm::vec3> before = doc.mesh.positions;
        const uint64_t p0 = doc.sculptor.microStroke().points;
        baseTestStroke(doc, BrushType::Standard, 0.3f);
        CHECK(doc.mesh.positions == before); // (the grid took it)
        CHECK(doc.sculptor.microStroke().points > p0);
    }
}

TEST(micro_crease_strokes_stay_smooth) {
    // #125: a deep Sharp Crease stroke at full strength left grid-scale noise on the micro level (the old sharpen
    // pushed every point away from its neighbours' average, which grows any noise dab after dab).
    for (float radius : {0.08f, 0.3f}) // (0.3: scale-adaptive, stride 2)
    for (BrushType type : {BrushType::Standard, BrushType::SharpCrease, BrushType::Crease, BrushType::Pinch}) {
        Document doc = microDoc(4);
        microNoise(doc.mesh, 0.0004f, 40.0f); // a little fine detail, as a sculpt has
        Symmetry sym;
        doc.sculptor.beginStroke(doc.mesh, brushName(type));
        for (int k = 0; k <= 60; ++k) {
            const float x = -0.15f + 0.3f * float(k) / 60.0f;
            DabParams p = dabAt(glm::normalize(glm::vec3(x, 0.0f, 1.0f)), radius, 1.0f);
            p.strokeDelta = glm::vec3(0.005f, 0.0f, 0.0f);
            doc.sculptor.dab(doc.mesh, doc.bvh, type, p, sym);
        }
        doc.undo.push(doc.sculptor.endStroke());
        // Grid-scale roughness: each interior point's distance from its six neighbours' average, against the
        // stroke's depth.
        const MicroMesh& mm = *doc.mesh.micro;
        const uint32_t n = mm.n;
        double lap = 0.0, deepest = 0.0;
        size_t count = 0;
        for (uint32_t t = 0; t < mm.baseTriangles; ++t) {
            const glm::uvec3 tr = doc.mesh.triangles[t];
            const glm::vec3 c = (doc.mesh.positions[tr.x] + doc.mesh.positions[tr.y] + doc.mesh.positions[tr.z]) / 3.0f;
            if (c.z < 0.9f || std::abs(c.y) > 0.05f || std::abs(c.x) > 0.12f) continue;
            const int16_t* g = mm.disp.data() + size_t(t) * mm.samples;
            for (uint32_t j = 1; j < n; ++j)
                for (uint32_t i = 1; i + j < n; ++i) {
                    const double v = g[MicroMesh::index(i, j, n)];
                    const double avg = (g[MicroMesh::index(i - 1, j, n)] + g[MicroMesh::index(i + 1, j, n)] +
                                        g[MicroMesh::index(i, j - 1, n)] + g[MicroMesh::index(i, j + 1, n)] +
                                        g[MicroMesh::index(i + 1, j - 1, n)] + g[MicroMesh::index(i - 1, j + 1, n)]) / 6.0;
                    lap += std::abs(v - avg), ++count;
                    deepest = std::max(deepest, std::abs(v));
                }
        }
        const double rough = lap / double(count) / deepest;
        // (11-05, radius 0.08: Sharp Crease 0.276 and Crease 0.144 before, 0.014 and 0.012 after, Standard 0.0016;
        // radius 0.3 brushes at stride 2: 0.011 and 0.008, and Pinch 0.021 with the pull interpolated, 0.081 without)
        CHECK(rough < 0.04);
        if (type == BrushType::Crease || type == BrushType::SharpCrease) CHECK(deepest * mm.step > 0.01); // (a groove)
        CHECK(watertight(doc.mesh));
    }
}

TEST(micro_stroke_end_no_spikes_on_a_rough_surface) {
    // Dabs piled on one spot (a stroke's end) over a rough surface (strands leaning over the skin beside them, the
    // lion's mane meeting its brow, 11-06) used to grow spikes on the grid: Clay moved points that can't reach its
    // plane along their own normals further away every dab. No point of the surface may move more than twice the
    // brush's radius in these 15 full-strength dabs (Clay's own build-up reaches 1.7; the grid's runaway reached 2.8).
    // The base path (a big brush) is printed only: sending its folding dabs to the grid (f084f9fd) made big brushes
    // 8x slower on the floor tier and tore them into shards on the lion, and was reverted for 0.11.15 (11-07).
    for (bool base : {false, true}) {
        Mesh rough = makeBenchSphere(200000);
        rough.recomputeAllNormals();
        for (size_t v = 0; v < rough.vertexCount(); ++v) { // narrow ridges leaning over the surface beside them
            const glm::vec3 p = rough.positions[v], nn = rough.normals[v];
            const float ridge = std::pow(std::max(0.0f, std::cos(p.x * 60.0f)), 8.0f);
            const glm::vec3 lean = glm::normalize(glm::cross(nn, glm::vec3(0, 1, 0)) + glm::vec3(1e-6f));
            rough.positions[v] += nn * (0.05f * ridge) + lean * (0.08f * ridge);
        }
        rough.recomputeAllNormals();
        Document doc = makeDoc(std::move(rough));
        std::string err;
        CHECK(microDivide(doc.mesh, 2, err));
        microNoise(doc.mesh, 0.002f, 60.0f, 7);
        doc.sculptor.microBaseDabs = base;
        const std::vector<glm::vec3> positions = doc.mesh.positions;
        const std::vector<glm::vec3> surface = microSurface(doc.mesh);
        const float r = base ? 0.6f : 0.3f; // (the base path takes Sculptor::kMicroBaseSpan base edges and more)
        Symmetry sym;
        doc.sculptor.beginStroke(doc.mesh, "Clay");
        for (int i = 0; i < 15; ++i)
            doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Clay, dabAt(glm::normalize(glm::vec3(0.002f * float(i % 3), 0.05f, 1.0f)), r, 1.0f), sym);
        doc.undo.push(doc.sculptor.endStroke());
        size_t turned = 0;
        for (const glm::uvec3& t : doc.mesh.triangles) {
            const glm::vec3 a = glm::cross(positions[t.y] - positions[t.x], positions[t.z] - positions[t.x]);
            const glm::vec3 b = glm::cross(doc.mesh.positions[t.y] - doc.mesh.positions[t.x], doc.mesh.positions[t.z] - doc.mesh.positions[t.x]);
            turned += glm::dot(a, b) < 0.0f;
        }
        const std::vector<glm::vec3> after = microSurface(doc.mesh);
        float worst = 0.0f, moved = 0.0f;
        for (size_t k = 0; k < after.size(); ++k) {
            const float d = glm::distance(after[k], surface[k]);
            worst = std::max(worst, d);
            moved += d;
        }
        std::printf("  %s, 15 Clay dabs (r %.2f): %zu base triangles turned over, largest move %.4f\n", base ? "base" : "grid", r, turned,
                    worst);
        CHECK(moved > 0.0f);
        CHECK(watertight(doc.mesh));
        if (base) continue;
        CHECK(turned == 0);
        CHECK(worst <= 2.0f * r);
    }
}

TEST(micro_smooth_never_shoots_points_out) {
    // Smooth on a Micro level (Shift held) shot points out (a member's report, 11-18): a big dab (stride > 1) worked out
    // the change on the coarse points and interpolated it onto fine points of other heights, raising a noisy level's
    // peaks to 2.2x (701 -> 1542 units at r 0.4); fixed with #220 (every grid point to its own average). Smooth only averages:
    // on a noisy level no displacement may end above the largest one it started with, and no surface point may move
    // farther than that noise's peak-to-peak span, whatever the radius (stride 1 and the coarse strides), with and
    // without a mask, over many overlapping dabs. Finite everywhere, and the copies stay stitched.
    for (bool masked : {false, true})
        for (float r : {0.03f, 0.12f, 0.4f}) {
            Document doc = microDoc(4, 60000);
            microNoise(doc.mesh, 0.004f, 80.0f, 3);
            Symmetry sym;
            sym.x = false;
            if (masked) {
                CHECK(doc.sculptor.beginStroke(doc.mesh, "Mask"));
                for (int i = 0; i < 4; ++i) doc.sculptor.dabMask(doc.mesh, doc.bvh, dabAt(glm::normalize(glm::vec3(0.1f, 0, 1)), r * 0.6f, 0.6f), sym);
                doc.undo.push(doc.sculptor.endStroke());
            }
            const MicroMesh& mm = *doc.mesh.micro;
            int peak0 = 0;
            for (size_t k = 0; k < mm.disp.size(); ++k) peak0 = std::max(peak0, std::abs(int(mm.disp[k])));
            const std::vector<glm::vec3> surface = microSurface(doc.mesh);
            for (int s = 0; s < 3; ++s) {
                doc.sculptor.beginStroke(doc.mesh, "Smooth");
                for (int i = 0; i < 24; ++i) {
                    const float x = -0.3f + 0.6f * float(i) / 23.0f;
                    doc.sculptor.dab(doc.mesh, doc.bvh, BrushType::Smooth, dabAt(glm::normalize(glm::vec3(x, 0.02f * float(s), 1.0f)), r, 1.0f), sym);
                }
                doc.undo.push(doc.sculptor.endStroke());
            }
            int peak = 0;
            for (size_t k = 0; k < mm.disp.size(); ++k) peak = std::max(peak, std::abs(int(mm.disp[k])));
            const std::vector<glm::vec3> after = microSurface(doc.mesh);
            float worst = 0.0f;
            size_t bad = 0;
            for (size_t k = 0; k < after.size(); ++k) {
                if (!std::isfinite(after[k].x) || !std::isfinite(after[k].y) || !std::isfinite(after[k].z)) ++bad;
                worst = std::max(worst, glm::distance(after[k], surface[k]));
            }
            const float span = 2.0f * float(peak0) * mm.step;
            std::printf("  r %.2f%s: peak %d -> %d units, largest move %.5f (noise span %.5f), %zu not finite\n", r, masked ? " masked" : "",
                        peak0, peak, worst, span, bad);
            CHECK(bad == 0);
            CHECK(peak <= peak0 + 1);
            CHECK(worst <= span + mm.step);
            CHECK(watertight(doc.mesh));
        }
}
