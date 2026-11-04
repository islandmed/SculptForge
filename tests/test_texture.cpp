// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture painting on UV maps (core/Texture.h): the UV rasteriser, the texture
// brush (falloff, mask, gutter, symmetry), tile undo, texture layers, the
// texture_* / uv_auto commands, .sfproj round trips, xatlas Auto UV and the
// cost of a dab at 4K on a million triangles.
#include "TestHarness.h"

#include "core/AutoUv.h"
#include "core/BrushAlpha.h"
#include "core/Commands.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "core/ThreadPool.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>

using namespace sf;

namespace {

// Planar UVs over the mesh's two widest axes (0..1 across its bounds), per corner.
void planarUvs(Mesh& m) {
    const AABB b = m.bounds();
    const glm::vec3 e = b.extent();
    int a0 = 0, a1 = 1;
    if (e.z > e.y) a1 = 2;
    if (e.x < std::min(e[a0 == 0 ? 1 : 0], e[a1])) a0 = a1 == 1 ? 2 : 1;
    if (a0 > a1) std::swap(a0, a1);
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2((p[a0] - b.min[a0]) / e[a0], (p[a1] - b.min[a1]) / e[a1]);
        }
}

// Spherical-projection UVs (seams and all; good enough for timing and coverage).
void sphericalUvs(Mesh& m) {
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = glm::normalize(m.positions[m.triangles[t][c]]);
            m.uvs[t * 3 + size_t(c)] = glm::vec2(0.5f + std::atan2(p.z, p.x) / 6.2831853f, 0.5f + std::asin(p.y) / 3.1415927f);
        }
}

// A plane facing +Y (makePlane is in XZ) with planar UVs and a white texture painted into.
Mesh texturedPlane(int resolution, int size) {
    Mesh m = makePlane(resolution);
    planarUvs(m);
    m.texture = makeTexture(size, size);
    m.paintToTexture = true;
    return m;
}

const uint8_t* texel(const PaintTexture& t, int x, int y) { return &t.pixels[(size_t(y) * size_t(t.width) + size_t(x)) * 4]; }
bool white(const uint8_t* p) { return p[0] == 255 && p[1] == 255 && p[2] == 255; }

// 3D point of texel (x, y) on a texturedPlane (UV u -> X, v -> Z over -1..1).
glm::vec3 planePoint(const PaintTexture& t, int x, int y) {
    return glm::vec3((float(x) + 0.5f) / float(t.width) * 2.0f - 1.0f, 0.0f, (float(y) + 0.5f) / float(t.height) * 2.0f - 1.0f);
}

DabParams dab(const glm::vec3& center, float radius, float strength = 1.0f) {
    DabParams p;
    p.center = center;
    p.viewDir = glm::vec3(0, -1, 0); // looking down onto the +Y plane
    p.radius = radius;
    p.strength = strength;
    return p;
}

size_t paintedTexels(const PaintTexture& t) {
    size_t n = 0;
    for (size_t i = 0; i < t.pixels.size(); i += 4) n += !white(&t.pixels[i]);
    return n;
}

} // namespace

TEST(texture_rasterizer_covers_each_texel_once) {
    // A jittered grid of quads, each split into two triangles (both diagonals, both windings):
    // every texel centre inside the square belongs to exactly one triangle.
    constexpr int W = 97, H = 83, N = 7;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> jitter(-0.03f, 0.03f);
    std::vector<glm::vec2> grid((N + 1) * (N + 1));
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i) {
            const bool border = i == 0 || j == 0 || i == N || j == N;
            grid[size_t(j * (N + 1) + i)] = glm::vec2(float(i) / N, float(j) / N) + (border ? glm::vec2(0) : glm::vec2(jitter(rng), jitter(rng)));
        }
    std::vector<int> hits(W * H, 0);
    bool inside = true;
    auto tri = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        rasterizeUvTriangle(a, b, c, W, H, [&](int x, int y, float b0, float b1, float b2) {
            ++hits[size_t(y * W + x)];
            // The texel centre really is inside (barycentrics reproduce it, all >= 0).
            const glm::vec2 p((float(x) + 0.5f) / W, (float(y) + 0.5f) / H);
            const glm::vec2 q = a * b0 + b * b1 + c * b2;
            inside = inside && b0 >= -1e-6f && b1 >= -1e-6f && b2 >= -1e-6f && std::abs(b0 + b1 + b2 - 1.0f) < 1e-5f &&
                     glm::distance(p, q) < 1e-4f;
        });
    };
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const glm::vec2 p00 = grid[size_t(j * (N + 1) + i)], p10 = grid[size_t(j * (N + 1) + i + 1)];
            const glm::vec2 p01 = grid[size_t((j + 1) * (N + 1) + i)], p11 = grid[size_t((j + 1) * (N + 1) + i + 1)];
            if ((i + j) % 2) {
                tri(p00, p10, p11);
                tri(p00, p01, p11); // clockwise
            } else {
                tri(p00, p10, p01);
                tri(p10, p11, p01);
            }
        }
    CHECK(inside);
    CHECK(std::all_of(hits.begin(), hits.end(), [](int h) { return h == 1; }));

    // Nothing outside a single triangle, and a zero-area triangle covers nothing.
    int outside = 0, count = 0;
    const glm::vec2 a(0.1f, 0.1f), b(0.8f, 0.3f), c(0.3f, 0.9f);
    rasterizeUvTriangle(a, b, c, 64, 64, [&](int x, int y, float, float, float) {
        ++count;
        const glm::vec2 p((float(x) + 0.5f) / 64, (float(y) + 0.5f) / 64);
        auto side = [](glm::vec2 u, glm::vec2 v, glm::vec2 w) { return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x); };
        if (side(a, b, p) < -1e-6f || side(b, c, p) < -1e-6f || side(c, a, p) < -1e-6f) ++outside;
    });
    CHECK(outside == 0);
    CHECK(std::abs(count - int(0.5f * std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 64 * 64)) < 40);
    int degenerate = 0;
    rasterizeUvTriangle(a, b, a + (b - a) * 0.5f, 64, 64, [&](int, int, float, float, float) { ++degenerate; });
    CHECK(degenerate == 0);
}

TEST(texture_dab_paints_the_brush_area_and_respects_the_mask) {
    Mesh m = texturedPlane(32, 256);
    m.rebuildTopology();
    m.recomputeAllNormals();
    BVH bvh;
    bvh.build(m);
    const glm::vec3 red = srgbToLinear(glm::vec3(1, 0, 0));
    const float r = 0.4f;

    TexturePainter painter;
    CHECK(painter.begin(m));
    CHECK(painter.dab(m, bvh, dab(glm::vec3(0.0f), r), red, Symmetry{false, false, false}) > 0);
    UndoRecord rec;
    painter.end(rec);
    CHECK(rec.textureTiles != nullptr);
    const PaintTexture& t = *m.texture;
    bool insideRed = true, outsideWhite = true;
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x < t.width; ++x) {
            const float d = glm::length(planePoint(t, x, y));
            const uint8_t* p = texel(t, x, y);
            if (d < r * 0.2f) insideRed = insideRed && p[0] > 200 && p[1] < 130; // strong red near the centre
            if (d > r * 1.02f) outsideWhite = outsideWhite && white(p);
        }
    CHECK(insideRed);
    CHECK(outsideWhite);
    // The falloff: weaker towards the rim.
    CHECK(texel(t, 128 + int(r * 0.8f * 128), 128)[1] > texel(t, 128 + int(r * 0.3f * 128), 128)[1]);

    // The mask: vertices with x < 0 fully masked leave their side of the texture white.
    Mesh mk = texturedPlane(32, 256);
    mk.rebuildTopology();
    mk.recomputeAllNormals();
    mk.ensureMask();
    for (uint32_t v = 0; v < mk.vertexCount(); ++v) mk.mask[v] = mk.positions[v].x < -0.01f ? 1.0f : 0.0f;
    bvh.build(mk);
    CHECK(painter.begin(mk));
    painter.dab(mk, bvh, dab(glm::vec3(0.0f), r), red, Symmetry{false, false, false});
    painter.end(rec);
    bool maskedWhite = true, freePainted = false;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const glm::vec3 p = planePoint(*mk.texture, x, y);
            if (p.x < -2.0f / 32.0f) maskedWhite = maskedWhite && white(texel(*mk.texture, x, y)); // a whole triangle in
            if (p.x > 0.1f && glm::length(p) < r * 0.5f) freePainted = freePainted || !white(texel(*mk.texture, x, y));
        }
    CHECK(maskedWhite);
    CHECK(freePainted);

    // Seen from below, the plane faces away: nothing is painted through it.
    Mesh back = texturedPlane(16, 64);
    back.rebuildTopology();
    back.recomputeAllNormals();
    bvh.build(back);
    DabParams p = dab(glm::vec3(0.0f), r);
    p.viewDir = glm::vec3(0, 1, 0);
    CHECK(painter.begin(back));
    CHECK(painter.dab(back, bvh, p, red, Symmetry{false, false, false}) == 0);
    painter.end(rec);

    // X symmetry paints the mirrored spot too.
    Mesh sym = texturedPlane(16, 128);
    sym.rebuildTopology();
    sym.recomputeAllNormals();
    bvh.build(sym);
    CHECK(painter.begin(sym));
    painter.dab(sym, bvh, dab(glm::vec3(0.5f, 0, 0), 0.2f), red, Symmetry{true, false, false});
    painter.end(rec);
    CHECK(!white(texel(*sym.texture, 96, 64)) && !white(texel(*sym.texture, 31, 64)));
}

TEST(texture_dab_pads_uv_island_borders) {
    // Two UV islands: the plane's left half on the left of the texture, its right half shifted right,
    // leaving a gap. Painting across the cut fills a few texels of gutter around each island.
    Mesh m = makePlane(16);
    planarUvs(m);
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const glm::vec3 c = (m.positions[m.triangles[t].x] + m.positions[m.triangles[t].y] + m.positions[m.triangles[t].z]) / 3.0f;
        for (int k = 0; k < 3; ++k) {
            glm::vec2& uv = m.uvs[t * 3 + size_t(k)];
            uv.x = c.x < 0.0f ? uv.x * 0.8f : 0.2f + uv.x * 0.8f; // right island moves 0.2 over: a 51-texel gap
        }
    }
    m.texture = makeTexture(256, 256);
    m.paintToTexture = true;
    m.rebuildTopology();
    m.recomputeAllNormals();
    BVH bvh;
    bvh.build(m);
    TexturePainter painter;
    CHECK(painter.begin(m));
    painter.dab(m, bvh, dab(glm::vec3(0.0f), 0.5f), srgbToLinear(glm::vec3(0, 0, 1)), Symmetry{false, false, false});
    UndoRecord rec;
    painter.end(rec);
    // Left island ends at u = 0.4 (texel 102.4); its gutter reaches about kTextureGutter texels further.
    const int y = 128;
    CHECK(!white(texel(*m.texture, 102, y)));
    CHECK(!white(texel(*m.texture, 102 + kTextureGutter - 1, y)));
    CHECK(white(texel(*m.texture, 102 + kTextureGutter + 3, y)));
    CHECK(white(texel(*m.texture, 128, y))); // the middle of the gap
    // Right island starts at u = 0.6 (texel 153.6): gutter to its left.
    CHECK(!white(texel(*m.texture, 153 - kTextureGutter + 1, y)));
}

TEST(texture_stroke_undo_restores_exactly) {
    Document doc = makeDoc(texturedPlane(24, 256));
    const std::vector<uint8_t> before = doc.mesh.texture->pixels;
    TexturePainter painter;
    CHECK(painter.begin(doc.mesh));
    for (int i = 0; i < 10; ++i)
        painter.dab(doc.mesh, doc.bvh, dab(glm::vec3(-0.6f + 0.12f * float(i), 0, 0.1f), 0.25f, 0.3f), srgbToLinear(glm::vec3(0.2f, 0.8f, 0.1f)),
                    Symmetry{true, false, false});
    UndoRecord rec;
    rec.label = "Paint";
    painter.end(rec);
    CHECK(rec.textureTiles && !rec.textureTiles->texels.tiles.empty());
    // The touched tiles were blank: each keeps one colour, not 64x64 texels.
    CHECK(rec.textureTiles->texels.data.size() == rec.textureTiles->texels.tiles.size() * 4);
    CHECK(rec.approxBytes() >= rec.textureTiles->texels.data.size());
    doc.undo.push(std::move(rec));
    const std::vector<uint8_t> after = doc.mesh.texture->pixels;
    CHECK(after != before);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->pixels == before);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->pixels == after);

    // Repeated low-strength dabs build up (8-bit storage doesn't stall) but never past the colour.
    Mesh m = texturedPlane(8, 64);
    m.rebuildTopology();
    m.recomputeAllNormals();
    BVH bvh;
    bvh.build(m);
    CHECK(painter.begin(m));
    for (int i = 0; i < 400; ++i) painter.dab(m, bvh, dab(glm::vec3(0.0f), 0.6f, 0.02f), glm::vec3(0.0f), Symmetry{false, false, false});
    painter.end(rec);
    const uint8_t* c = texel(*m.texture, 32, 32);
    CHECK(c[0] < 10 && c[1] < 10 && c[2] < 10);
}

TEST(texture_layers_composite_merge_and_round_trip) {
    Document doc = makeDoc(texturedPlane(16, 128));
    CommandRegistry reg;
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#808080")}, {"area", std::string("all")}}).ok);
    CHECK(reg.execute(doc, "paint_layer_add", {{"blend", std::string("multiply")}, {"name", std::string("Dirt")}}).ok);
    CHECK(doc.mesh.paintLayers.empty()); // the texture's stack, not polypaint's
    CHECK(doc.mesh.texture->layers.size() == 1 && doc.mesh.texture->activeLayer == 0);
    TexturePainter painter;
    CHECK(painter.begin(doc.mesh));
    painter.dab(doc.mesh, doc.bvh, dab(glm::vec3(0.0f), 0.5f), srgbToLinear(glm::vec3(0.5f, 0.5f, 1.0f)), Symmetry{false, false, false});
    UndoRecord rec;
    painter.end(rec);
    doc.undo.push(std::move(rec));
    // Base untouched by painting on the layer; multiply darkens red and green in the composite.
    const uint8_t* base = &doc.mesh.texture->base[(size_t(64) * 128 + 64) * 4];
    CHECK(base[0] == 128 && base[2] == 128);
    const uint8_t* mid = texel(*doc.mesh.texture, 64, 64);
    CHECK(mid[0] < 80 && mid[2] > 110);
    const std::vector<uint8_t> look = doc.mesh.texture->pixels;

    // Hide / show and opacity recomposite; undo puts the settings back.
    CHECK(reg.execute(doc, "paint_layer_set", {{"visible", false}}).ok);
    CHECK(texel(*doc.mesh.texture, 64, 64)[0] == 128);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->pixels == look);

    // Save and reopen: size, layer settings and texels come back exactly.
    const std::string path = tempPath("sf_test_texture.sfproj");
    CHECK(reg.execute(doc, "save_project", {{"path", path}}).ok);
    Document back;
    std::string err;
    CHECK(loadProject(path, back, err));
    CHECK(err.empty());
    CHECK(back.mesh.texture && back.mesh.texture->width == 128 && back.mesh.paintToTexture);
    CHECK(back.mesh.texture->layers.size() == 1 && back.mesh.texture->layers[0].name == "Dirt" &&
          back.mesh.texture->layers[0].blend == BlendMode::Multiply);
    CHECK(back.mesh.texture->base == doc.mesh.texture->base);
    CHECK(back.mesh.texture->layers[0].rgba == doc.mesh.texture->layers[0].rgba);
    CHECK(back.mesh.texture->pixels == look);
    std::filesystem::remove(path);

    // Merge down onto Base keeps the look; undo brings the layer back.
    CHECK(reg.execute(doc, "paint_layer_merge").ok);
    CHECK(doc.mesh.texture->layers.empty());
    size_t diff = 0;
    for (size_t i = 0; i < look.size(); ++i) diff = std::max<size_t>(diff, size_t(std::abs(int(look[i]) - int(doc.mesh.texture->pixels[i]))));
    CHECK(diff <= 1);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->layers.size() == 1 && doc.mesh.texture->pixels == look);
    CHECK(reg.execute(doc, "paint_layer_flatten").ok);
    CHECK(doc.mesh.texture->layers.empty());

    // Version 2 projects (no texture) still load; textures arrived in version 3.
    CHECK(kProjectVersion >= 3);
}

TEST(texture_layer_undo_stores_only_what_changed) {
    Document doc = makeDoc(texturedPlane(32, 4096));
    // A busy Base: no tile is one colour, so nothing compresses by accident.
    std::vector<uint8_t>& px = doc.mesh.texture->pixels;
    for (size_t i = 0; i < px.size(); ++i) px[i] = (i & 3) == 3 ? 255 : uint8_t((uint32_t(i) * 2654435761u) >> 24);
    CommandRegistry reg;
    constexpr size_t kMB = size_t(1) << 20;
    struct State {
        std::vector<uint8_t> pixels, base;
        std::vector<std::vector<uint8_t>> layers;
        std::vector<std::string> names;
        int active;
    };
    auto state = [&] {
        if (!doc.mesh.texture) return State{{}, {}, {}, {}, -3};
        const PaintTexture& t = *doc.mesh.texture;
        State s{t.pixels, t.base, {}, {}, t.activeLayer};
        for (const TextureLayer& l : t.layers) s.layers.push_back(l.rgba), s.names.push_back(l.name);
        return s;
    };
    auto same = [&](const State& a) {
        const State b = state();
        return a.pixels == b.pixels && a.base == b.base && a.layers == b.layers && a.names == b.names && a.active == b.active;
    };
    // Runs a step, checks what its undo costs, then that undo and redo give back both states exactly.
    auto step = [&](const char* what, size_t maxBytes, const std::function<void()>& fn) {
        const State before = state();
        const size_t bytes0 = doc.undo.undoBytes();
        fn();
        const size_t cost = doc.undo.undoBytes() - bytes0;
        if (cost >= maxBytes) std::printf("  %s: undo costs %zu bytes\n", what, cost);
        CHECK(cost < maxBytes);
        const State after = state();
        CHECK(doc.undo.undo(doc.mesh, doc.bvh));
        CHECK(same(before));
        CHECK(doc.undo.redo(doc.mesh, doc.bvh));
        CHECK(same(after));
    };
    auto stroke = [&](const glm::vec3& color, bool erase) {
        TexturePainter painter;
        CHECK(painter.begin(doc.mesh));
        for (int i = 0; i < 8; ++i) {
            DabParams p = dab(glm::vec3(-0.2f + 0.05f * float(i), 0, 0.3f), 0.04f, 0.8f);
            p.invert = erase;
            CHECK(painter.dab(doc.mesh, doc.bvh, p, color, Symmetry{false, false, false}) > 0);
        }
        UndoRecord rec;
        rec.label = "Paint";
        painter.end(rec);
        doc.undo.push(std::move(rec));
    };

    // A new 4K layer is blank: its undo is a few bytes per tile, not 64 MB.
    step("add layer", kMB, [&] { CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Detail")}}).ok); });
    // The brief's case: a small stroke on a 4K layer costs well under 2 MB and undoes exactly.
    step("layer stroke", 2 * kMB / 8, [&] { stroke(srgbToLinear(glm::vec3(0.9f, 0.1f, 0.1f)), false); });
    step("layer erase", 2 * kMB, [&] { stroke(glm::vec3(0.0f), true); });
    step("settings", 64 * 1024, [&] { CHECK(reg.execute(doc, "paint_layer_set", {{"opacity", 0.5}, {"blend", std::string("multiply")}}).ok); });
    step("duplicate", kMB, [&] { CHECK(reg.execute(doc, "paint_layer_duplicate").ok); });
    step("move", 64 * 1024, [&] { CHECK(reg.execute(doc, "paint_layer_move", {{"direction", std::string("down")}}).ok); });
    // Whole-layer edits keep only the tiles they changed: clearing a layer with one stroke on it costs that stroke.
    step("clear layer", 2 * kMB, [&] { CHECK(reg.execute(doc, "paint_layer_clear").ok); });
    step("fill layer", kMB, [&] { CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#336699")}, {"area", std::string("all")}}).ok); });
    step("delete layer", kMB, [&] { CHECK(reg.execute(doc, "paint_layer_delete").ok); });
    CHECK(doc.mesh.texture->layers.size() == 1);
    // Merging a stroke into the busy Base changes only the tiles under it.
    step("merge", 2 * kMB, [&] { CHECK(reg.execute(doc, "paint_layer_merge").ok); });
    CHECK(doc.mesh.texture->layers.empty());
    step("base stroke", 2 * kMB, [&] { stroke(srgbToLinear(glm::vec3(0.1f, 0.2f, 0.9f)), false); });
    // Clearing the busy Base has to keep it all (64 MB of texels that can't be told apart from anything).
    step("clear base", 65 * kMB, [&] { CHECK(reg.execute(doc, "paint_clear").ok); });
    // A new texture replacing nothing, and deleting a white one: one colour per tile.
    step("delete white", kMB, [&] { CHECK(reg.execute(doc, "texture_delete").ok); });
    CHECK(!doc.mesh.texture);
}

TEST(texture_copies_are_independent) {
    Mesh a = texturedPlane(8, 64);
    a.rebuildTopology();
    a.recomputeAllNormals();
    Mesh b = a; // e.g. a duplicated SubTool or an undo snapshot: shares the texture until one changes it
    CHECK(a.texture == b.texture);
    BVH bvh;
    bvh.build(a);
    TexturePainter painter;
    CHECK(painter.begin(a));
    painter.dab(a, bvh, dab(glm::vec3(0.0f), 0.5f), glm::vec3(0.0f), Symmetry{false, false, false});
    UndoRecord rec;
    painter.end(rec);
    CHECK(a.texture != b.texture);
    CHECK(paintedTexels(*a.texture) > 0 && paintedTexels(*b.texture) == 0);
}

TEST(texture_commands) {
    Document doc = makeDoc(makeIcosphere(2));
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "texture_create", {{"size", std::string("1k")}});
    CHECK(!r.ok && r.message.find("UVs") != std::string::npos);
    CHECK(reg.execute(doc, "uv_auto").ok);
    CHECK(doc.mesh.hasUvs());
    CHECK(!reg.execute(doc, "texture_create", {{"size", std::string("3000")}}).ok);
    CHECK(reg.execute(doc, "texture_create", {{"size", std::string("1k")}}).ok);
    CHECK(hasTexture(doc.mesh) && doc.mesh.texture->width == 1024 && paintsTexture(doc.mesh));
    CHECK(reg.execute(doc, "texture_create", {{"size", 2048.0}}).ok);
    CHECK(doc.mesh.texture->width == 2048);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->width == 1024);

    // Fills paint the texture (the vertices stay unpainted), Texture > Polypaint copies it over.
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#ff0000")}}).ok);
    CHECK(!doc.mesh.hasColors());
    CHECK(paintedTexels(*doc.mesh.texture) > 100000);
    CHECK(reg.execute(doc, "polypaint_from_texture").ok);
    CHECK(doc.mesh.hasColors());
    CHECK(glm::all(glm::lessThan(glm::abs(doc.mesh.colors[0] - glm::vec3(1, 0, 0)), glm::vec3(0.02f))));

    // Polypaint > Texture bakes vertex colours (green) back.
    CHECK(reg.execute(doc, "paint_target", {{"target", std::string("vertex")}}).ok);
    CHECK(!paintsTexture(doc.mesh));
    CHECK(reg.execute(doc, "paint_fill", {{"color", std::string("#00ff00")}}).ok);
    CHECK(reg.execute(doc, "texture_from_polypaint").ok);
    CHECK(paintsTexture(doc.mesh));
    const glm::vec3 c = sampleTexture(*doc.mesh.texture, doc.mesh.uvs[0]);
    CHECK(c.y > 0.9f && c.x < 0.05f);

    // Export writes a PNG of the texture; delete is undoable.
    const std::string png = tempPath("sf_test_texture.png");
    CHECK(reg.execute(doc, "texture_export", {{"path", png}}).ok);
    Image img;
    std::string err;
    CHECK(Image::load(png, img, err) && img.width == 1024);
    CHECK(reg.execute(doc, "texture_import", {{"path", png}}).ok);
    CHECK(doc.mesh.texture->width == 1024);
    std::filesystem::remove(png);
    CHECK(reg.execute(doc, "texture_delete").ok);
    CHECK(!doc.mesh.texture && !paintsTexture(doc.mesh));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(hasTexture(doc.mesh));
}

TEST(texture_auto_uv_gives_every_corner_a_uv) {
    Mesh m = makeIcosphere(3);
    AutoUvResult r;
    std::string err;
    float lastProgress = -1.0f;
    CHECK(computeAutoUv(m, r, err, [&](float f) {
        lastProgress = f;
        return true;
    }));
    CHECK(err.empty());
    CHECK(r.uvs.size() == m.triangleCount() * 3);
    CHECK(r.charts > 1);
    CHECK(lastProgress > 0.5f);
    bool inRange = true;
    size_t flat = 0;
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        for (int c = 0; c < 3; ++c) {
            const glm::vec2 uv = r.uvs[t * 3 + size_t(c)];
            inRange = inRange && std::isfinite(uv.x) && std::isfinite(uv.y) && uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1;
        }
        const glm::vec2 a = r.uvs[t * 3], b = r.uvs[t * 3 + 1], c = r.uvs[t * 3 + 2];
        if (std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) < 1e-10f) ++flat;
    }
    CHECK(inRange);
    CHECK(flat == 0);
    // Cancelling stops it with a message.
    CHECK(!computeAutoUv(m, r, err, [](float) { return false; }));
    CHECK(!err.empty());
}

TEST(texture_dab_is_interactive_at_4k_on_a_million_triangles) {
    Mesh m = makeIcosphere(8); // 1.3M triangles
    sphericalUvs(m);
    m.texture = makeTexture(4096, 4096);
    m.paintToTexture = true;
    BVH bvh;
    bvh.build(m);
    TexturePainter painter;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(painter.begin(m)); // builds the island coverage once
    auto t1 = std::chrono::steady_clock::now();
    DabParams p = dab(glm::vec3(0, 0, 1), 0.08f, 0.5f);
    p.viewDir = glm::vec3(0, 0, -1);
    size_t texels = 0;
    for (int i = 0; i < 20; ++i) {
        p.center = glm::normalize(glm::vec3(-0.3f + 0.03f * float(i), 0.1f, 1.0f));
        texels += painter.dab(m, bvh, p, glm::vec3(1, 0, 0), Symmetry{true, false, false});
    }
    auto t2 = std::chrono::steady_clock::now();
    UndoRecord rec;
    painter.end(rec);
    const double beginMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double dabMs = std::chrono::duration<double, std::milli>(t2 - t1).count() / 20.0;
    std::printf("  texture dab at 4K / %zu tris: begin %.1f ms, %.2f ms per dab (%zu texels)\n", m.triangleCount(), beginMs,
                dabMs, texels / 20);
    CHECK(texels > 0);
    CHECK(dabMs < timeLimit(30.0)); // interactive: several dabs per frame
}

TEST(texture_stroke_benchmark) {
    // Dab cost and the texels a frame uploads, for 2K / 4K, a small and a big brush, a light and a dense mesh
    // (docs/performance.md). Each dab is one frame here: the upload is the dirty rectangle the view sends.
    std::printf("  texture stroke (X symmetry, 12 dabs): size / tris / radius -> ms per dab, Mtexels uploaded per dab\n");
    double bigDab4k = 0.0;
    for (int level : {3, 7}) {
        Mesh base = makeIcosphere(level);
        sphericalUvs(base);
        BVH bvh;
        bvh.build(base);
        for (int size : {2048, 4096}) {
            for (float radius : {0.03f, 0.35f}) {
                Mesh m = base;
                m.texture = makeTexture(size, size);
                m.paintToTexture = true;
                TexturePainter painter;
                CHECK(painter.begin(m));
                m.texture->clearDirty();
                DabParams p = dab(glm::vec3(0, 0, 1), radius, 0.5f);
                p.viewDir = glm::vec3(0, 0, -1);
                double ms = 0.0, uploaded = 0.0;
                for (int i = 0; i < 12; ++i) {
                    p.center = glm::normalize(glm::vec3(-0.15f + 0.025f * float(i), 0.2f, 1.0f));
                    const auto t0 = std::chrono::steady_clock::now();
                    painter.dab(m, bvh, p, glm::vec3(1, 0, 0), Symmetry{true, false, false});
                    ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    uploaded += double(textureUploadTexels(*m.texture));
                    m.texture->clearDirty();
                }
                UndoRecord rec;
                painter.end(rec);
                std::printf("    %dK / %7zu / %.2f: %6.2f ms, %5.2f Mtexels\n", size / 1024, m.triangleCount(), radius,
                            ms / 12.0, uploaded / 12.0 / 1e6);
                if (size == 4096 && radius > 0.1f) bigDab4k = std::max(bigDab4k, ms / 12.0);
            }
        }
    }
    // Timing depends on the machine and on what else runs (CI runners are small and run tests in parallel), so
    // the limit is only checked on request: SF_BENCH_LIMITS=1. The M1 Pro target is ~10 ms.
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(bigDab4k < 60.0);
}

TEST(texture_dab_is_deterministic_with_threads) {
    // The band pass gives the same texels (and undo tiles) on one thread as on many, with a layer and symmetry.
    Mesh base = makeIcosphere(5);
    sphericalUvs(base);
    BVH bvh;
    bvh.build(base);
    std::vector<uint8_t> result[2];
    size_t tiles[2] = {};
    for (int serial = 0; serial < 2; ++serial) {
        ThreadPool::shared().setSerial(serial == 1);
        Mesh m = base;
        m.texture = makeTexture(2048, 2048);
        m.paintToTexture = true;
        addTextureLayer(*m.texture, "Layer 1");
        TexturePainter painter;
        CHECK(painter.begin(m));
        DabParams p = dab(glm::vec3(0, 0, 1), 0.3f, 0.6f);
        p.viewDir = glm::vec3(0, 0, -1);
        for (int i = 0; i < 6; ++i) {
            p.center = glm::normalize(glm::vec3(-0.2f + 0.08f * float(i), 0.1f * float(i % 2), 1.0f));
            painter.dab(m, bvh, p, glm::vec3(0.2f, 0.4f, 0.9f), Symmetry{true, false, false});
        }
        UndoRecord rec;
        painter.end(rec);
        CHECK(rec.textureTiles != nullptr);
        tiles[serial] = rec.textureTiles ? rec.textureTiles->texels.tiles.size() : 0;
        result[serial] = m.texture->pixels;
    }
    ThreadPool::shared().setSerial(false);
    CHECK(tiles[0] == tiles[1]);
    CHECK(result[0] == result[1]);
}

TEST(texture_dab_sharpness_does_not_depend_on_mesh_density) {
    // The ZBrush tester's "resolution depends on the geometry density": a hard square alpha on a 2-triangle
    // plane must give the same texels as on a 64x64 plane, with an edge as sharp as the texture allows.
    BrushAlpha square; // fully opaque up to the square's edge: the hardest possible stamp
    square.width = square.height = 4;
    square.values.assign(16, 1.0f);
    std::vector<uint8_t> result[2];
    const int resolutions[2] = {1, 64};
    for (int i = 0; i < 2; ++i) {
        Mesh m = texturedPlane(resolutions[i], 1024);
        m.rebuildTopology();
        m.recomputeAllNormals();
        BVH bvh;
        bvh.build(m);
        TexturePainter painter;
        CHECK(painter.begin(m));
        DabParams p = dab(glm::vec3(0.013f, 0, -0.021f), 0.1f);
        p.alpha = &square;
        p.alphaUp = glm::vec3(0, 0, 1);
        p.focal = -1.0f; // (Focal Shift -100 keeps the alpha hard to its edge; 0 would soften it, #60)
        CHECK(painter.dab(m, bvh, p, glm::vec3(0.0f), Symmetry{false, false, false}) > 0);
        UndoRecord rec;
        painter.end(rec);
        result[i] = m.texture->pixels;
    }
    size_t differ = 0;
    for (size_t k = 0; k < result[0].size(); ++k) differ += std::abs(int(result[0][k]) - int(result[1][k])) > 2;
    CHECK(differ == 0);
    // Along the row through the centre: white, a ramp of at most a few texels, then black (the alpha's edge).
    PaintTexture t;
    t.width = t.height = 1024;
    t.pixels = result[0];
    const int y = int((-0.021f + 1.0f) * 0.5f * 1024.0f);
    int ramp = 0;
    for (int x = 0; x < 1024; ++x) {
        const uint8_t v = texel(t, x, y)[0];
        ramp += v > 8 && v < 247;
    }
    std::printf("  square alpha at 1K: %d edge texels across the row (2 edges)\n", ramp);
    CHECK(ramp <= 2 * 2);
}
