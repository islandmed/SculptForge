// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Texture transfer (core/TextureTransfer.h): Auto UV on a painted SubTool moves the paint (and its layers)
// onto the new UVs as one undo step; voxel remesh, QRemesh and merging SubTools keep the texture; the cost at
// 4K on a million triangles.
#include "TestHarness.h"

#include "core/AutoUv.h"
#include "core/Commands.h"
#include "core/Polypaint.h"
#include "core/Primitives.h"
#include "core/SubTools.h"
#include "core/Texture.h"
#include "core/TextureTransfer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

using namespace sf;

namespace {

// The test pattern: a smooth colour field over space (display values 0..1).
glm::vec3 pattern(const glm::vec3& p) {
    return glm::vec3(0.5f + 0.4f * std::sin(3.0f * p.x + 0.5f), 0.5f + 0.4f * std::cos(2.5f * p.y),
                     0.5f + 0.4f * std::sin(2.0f * p.z + 1.0f));
}
float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
glm::vec3 patternLinear(const glm::vec3& p) {
    const glm::vec3 c = pattern(p);
    return {toLinear(c.x), toLinear(c.y), toLinear(c.z)};
}

uint8_t byte(float x) { return uint8_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); }

// Paints the pattern into `plane` (RGBA8 of the mesh's texture), alpha `alpha`, gutters included.
void paintPattern(Mesh& m, std::vector<uint8_t>& plane, uint8_t alpha, float shift = 0.0f) {
    PaintTexture& tex = *m.texture;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3 tv = m.triangles[t];
        const glm::vec3 p = m.positions[tv.x] * b0 + m.positions[tv.y] * b1 + m.positions[tv.z] * b2;
        const glm::vec3 c = pattern(p + glm::vec3(shift));
        uint8_t* px = &plane[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        px[0] = byte(c.x), px[1] = byte(c.y), px[2] = byte(c.z), px[3] = alpha;
    }, false);
}

// A textured sphere (Auto UV packed for `packFor`, a size x size texture) painted with the pattern.
Document paintedSphere(int subdivisions, int size, int packFor) {
    Document doc = makeDoc(makeIcosphere(subdivisions));
    CommandRegistry reg;
    CHECK(reg.execute(doc, "uv_auto", {{"resolution", double(packFor)}}).ok);
    CHECK(reg.execute(doc, "texture_create", {{"size", std::to_string(size)}}).ok);
    paintPattern(doc.mesh, mutableTexture(doc.mesh).pixels, 255);
    doc.undo.clear();
    return doc;
}

// Random surface points (triangle + barycentrics).
struct SurfacePoint {
    uint32_t tri;
    glm::vec3 bary;
};
std::vector<SurfacePoint> randomPoints(const Mesh& m, int n, uint32_t seed = 7) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::vector<SurfacePoint> out;
    for (int i = 0; i < n; ++i) {
        float a = u(rng), b = u(rng);
        if (a + b > 1.0f) a = 1.0f - a, b = 1.0f - b;
        out.push_back({uint32_t(rng() % m.triangleCount()), glm::vec3(1.0f - a - b, a, b)});
    }
    return out;
}
glm::vec3 pointOf(const Mesh& m, const SurfacePoint& s) {
    const glm::uvec3 tv = m.triangles[s.tri];
    return m.positions[tv.x] * s.bary.x + m.positions[tv.y] * s.bary.y + m.positions[tv.z] * s.bary.z;
}

struct ErrorStats {
    float mean = 0, p99 = 0, max = 0;
};
ErrorStats errorStats(std::vector<float> e) {
    ErrorStats s;
    if (e.empty()) return s;
    std::sort(e.begin(), e.end());
    for (float x : e) s.mean += x;
    s.mean /= float(e.size());
    s.p99 = e[size_t(double(e.size() - 1) * 0.99)];
    s.max = e.back();
    return s;
}
float maxDiff(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 d = glm::abs(a - b);
    return std::max({d.x, d.y, d.z});
}

// How far the texture of `m` strays from the pattern at random surface points (linear RGB).
ErrorStats patternError(const Mesh& m, int n = 4000) {
    std::vector<float> e;
    for (const SurfacePoint& s : randomPoints(m, n)) {
        const glm::vec3 p = pointOf(m, s);
        e.push_back(maxDiff(sampleTextureAt(m, s.tri, p), patternLinear(p)));
    }
    return errorStats(e);
}

} // namespace

TEST(texture_transfer_auto_uv_keeps_the_paint) {
    Document doc = paintedSphere(4, 1024, 512);
    const std::vector<glm::vec2> oldUvs = doc.mesh.uvs;
    const auto oldTexture = doc.mesh.texture;
    const ErrorStats before = patternError(doc.mesh);
    // Samples of the texture before, at fixed surface points.
    const auto pts = randomPoints(doc.mesh, 4000, 11);
    std::vector<glm::vec3> was;
    for (const auto& s : pts) was.push_back(sampleTextureAt(doc.mesh, s.tri, pointOf(doc.mesh, s)));

    CommandRegistry reg;
    const CommandResult r = reg.execute(doc, "uv_auto", {{"resolution", 2048.0}}); // packed differently
    CHECK(r.ok);
    CHECK(r.message.find("moved to the new UVs") != std::string::npos);
    CHECK(doc.mesh.uvs != oldUvs);
    CHECK(hasTexture(doc.mesh));
    CHECK(doc.mesh.texture != oldTexture);
    std::vector<float> e;
    for (size_t i = 0; i < pts.size(); ++i) e.push_back(maxDiff(sampleTextureAt(doc.mesh, pts[i].tri, pointOf(doc.mesh, pts[i])), was[i]));
    const ErrorStats moved = errorStats(e), after = patternError(doc.mesh);
    std::printf("  auto uv transfer: vs before mean %.4f p99 %.4f max %.4f; vs pattern %.4f -> %.4f (p99 %.4f -> %.4f)\n",
                moved.mean, moved.p99, moved.max, before.mean, after.mean, before.p99, after.p99);
    CHECK(moved.mean < 0.01f);
    CHECK(moved.p99 < 0.05f);
    CHECK(after.mean < before.mean + 0.01f);

    // Without the transfer the paint is scrambled (the old behaviour), which the check above would catch.
    {
        Document copy = paintedSphere(4, 1024, 512);
        CHECK(reg.execute(copy, "uv_auto", {{"resolution", 2048.0}, {"keep_texture", false}}).ok);
        CHECK(patternError(copy.mesh).mean > 0.05f);
    }

    // One undo step brings back the old UVs and the old texture; redo the new ones.
    CHECK(doc.undo.undoCount() == 1);
    const auto newTexture = doc.mesh.texture;
    doc.undo.undo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.uvs == oldUvs);
    CHECK(doc.mesh.texture == oldTexture);
    doc.undo.redo(doc.mesh, doc.bvh);
    CHECK(doc.mesh.texture == newTexture);
}

TEST(texture_transfer_auto_uv_keeps_layers) {
    Document doc = paintedSphere(3, 1024, 512);
    PaintTexture& t = mutableTexture(doc.mesh);
    const int layer = addTextureLayer(t, "Dirt");
    t.layers[size_t(layer)].opacity = 0.5f;
    t.layers[size_t(layer)].blend = BlendMode::Multiply;
    paintPattern(doc.mesh, t.layers[size_t(layer)].rgba, 200, 0.7f);
    compositeTextureAll(t);
    const auto pts = randomPoints(doc.mesh, 3000, 5);
    std::vector<glm::vec3> was;
    for (const auto& s : pts) was.push_back(sampleTextureAt(doc.mesh, s.tri, pointOf(doc.mesh, s)));
    const uint32_t id = t.layers[0].id;

    CommandRegistry reg;
    const CommandResult r = reg.execute(doc, "uv_auto", {{"resolution", 1024.0}});
    CHECK(r.ok);
    CHECK(r.message.find("1 layer") != std::string::npos);
    const PaintTexture& n = *doc.mesh.texture;
    CHECK(n.layers.size() == 1);
    CHECK(n.layers[0].name == "Dirt" && n.layers[0].id == id);
    CHECK(n.layers[0].opacity == 0.5f && n.layers[0].blend == BlendMode::Multiply);
    CHECK(n.base.size() == n.pixels.size());
    std::vector<float> e;
    for (size_t i = 0; i < pts.size(); ++i) e.push_back(maxDiff(sampleTextureAt(doc.mesh, pts[i].tri, pointOf(doc.mesh, pts[i])), was[i]));
    const ErrorStats s = errorStats(e);
    std::printf("  auto uv transfer with a layer: mean %.4f p99 %.4f\n", s.mean, s.p99);
    CHECK(s.mean < 0.015f);
    CHECK(s.p99 < 0.08f);
}

TEST(texture_transfer_remesh_and_qremesh_keep_the_texture) {
    CommandRegistry reg;
    for (const char* cmd : {"remesh", "qremesh"}) {
        Document doc = paintedSphere(4, 1024, 1024);
        const ErrorStats before = patternError(doc.mesh);
        const auto oldTexture = doc.mesh.texture;
        const size_t oldTris = doc.mesh.triangleCount();
        Args a = std::string(cmd) == "remesh" ? Args{{"resolution", 96.0}} : Args{{"target", 2000.0}};
        const CommandResult r = reg.execute(doc, cmd, a);
        CHECK(r.ok);
        CHECK(r.message.find("texture kept") != std::string::npos);
        CHECK(doc.mesh.triangleCount() != oldTris);
        CHECK(hasTexture(doc.mesh));
        CHECK(doc.mesh.texture->width == 1024);
        CHECK(doc.mesh.paintToTexture);
        const ErrorStats after = patternError(doc.mesh);
        std::printf("  %s keeps the texture: pattern error mean %.4f -> %.4f, p99 %.4f -> %.4f (%s)\n", cmd, before.mean,
                    after.mean, before.p99, after.p99, r.message.c_str());
        CHECK(after.mean < 0.02f);
        CHECK(after.p99 < 0.08f);
        // One undo step: the old mesh with its UVs and texture.
        CHECK(doc.undo.undoCount() == 1);
        doc.undo.undo(doc.mesh, doc.bvh);
        CHECK(doc.mesh.triangleCount() == oldTris);
        CHECK(doc.mesh.texture == oldTexture);

        // keep_texture=false drops it, and says so.
        a["keep_texture"] = false;
        const CommandResult d = reg.execute(doc, cmd, a);
        CHECK(d.ok);
        CHECK(d.message.find("texture was dropped") != std::string::npos);
        CHECK(!hasTexture(doc.mesh));
    }
}

TEST(texture_transfer_decimate_keeps_the_texture) {
    // Decimate keeps the UVs; vertices on UV seams stay put so the corners across a seam keep their island's UVs.
    Document doc = paintedSphere(4, 1024, 1024);
    const size_t before = doc.mesh.triangleCount();
    CommandRegistry reg;
    CHECK(reg.execute(doc, "decimate", {{"ratio", 0.3}}).ok);
    CHECK(hasTexture(doc.mesh));
    CHECK(doc.mesh.triangleCount() <= before * 35 / 100);
    const ErrorStats after = patternError(doc.mesh);
    std::printf("  decimate keeps the texture: %zu -> %zu tris, pattern error mean %.4f p99 %.4f max %.4f\n", before,
                doc.mesh.triangleCount(), after.mean, after.p99, after.max);
    CHECK(after.mean < 0.01f);
    CHECK(after.p99 < 0.03f);
}

TEST(texture_transfer_merge_keeps_both_textures) {
    Document doc = paintedSphere(3, 1024, 512);
    Mesh second = makeIcosphere(3, 0.5f);
    for (auto& p : second.positions) p += glm::vec3(2.0f, 0.0f, 0.0f);
    second.recomputeAllNormals();
    {
        AutoUvResult uv;
        std::string err;
        CHECK(computeAutoUv(second.positions, second.triangles, 512, uv, err));
        second.uvs = uv.uvs;
        second.texture = makeTexture(2048, 2048); // the bigger texture sets the merged size
        second.paintToTexture = true;
        paintPattern(second, second.texture->pixels, 255);
    }
    Mesh plain = makeIcosphere(2, 0.3f); // a part without texture comes out white
    for (auto& p : plain.positions) p += glm::vec3(-2.0f, 0.0f, 0.0f);
    doc.addSubtool(second, "Second", false);
    doc.addSubtool(plain, "Plain", false);
    CommandRegistry reg;
    const CommandResult r = reg.execute(doc, "subtool_merge_visible", {});
    CHECK(r.ok);
    CHECK(r.message.find("textures kept") != std::string::npos);
    CHECK(doc.subtoolCount() == 1);
    CHECK(hasTexture(doc.mesh));
    CHECK(doc.mesh.texture->width == 2048);
    // Points on the two painted spheres follow the pattern; the plain one is white.
    std::vector<float> painted;
    float plainError = 0.0f;
    for (const auto& s : randomPoints(doc.mesh, 4000, 3)) {
        const glm::vec3 p = pointOf(doc.mesh, s), c = sampleTextureAt(doc.mesh, s.tri, p);
        if (p.x < -1.0f) plainError = std::max(plainError, maxDiff(c, glm::vec3(1.0f)));
        else painted.push_back(maxDiff(c, patternLinear(p)));
    }
    const ErrorStats e = errorStats(painted);
    std::printf("  merged textures: pattern error mean %.4f p99 %.4f, plain part %.4f\n", e.mean, e.p99, plainError);
    CHECK(e.mean < 0.02f);
    CHECK(e.p99 < 0.08f);
    CHECK(plainError < 0.01f);
}

TEST(texture_transfer_pads_the_new_islands) {
    // Texels just outside the new islands carry the island's colour (no white seams once filtered).
    Document doc = paintedSphere(3, 1024, 512);
    CommandRegistry reg;
    CHECK(reg.execute(doc, "uv_auto", {{"resolution", 1024.0}}).ok);
    const PaintTexture& t = *doc.mesh.texture;
    ensureCoverage(doc.mesh, const_cast<PaintTexture&>(t), true);
    size_t border = 0, white = 0;
    for (int y = 1; y + 1 < t.height; ++y)
        for (int x = 1; x + 1 < t.width; ++x) {
            if (t.covered(x, y)) continue;
            bool next = false;
            for (int d = 0; d < 4 && !next; ++d) next = t.covered(x + (d == 0) - (d == 1), y + (d == 2) - (d == 3));
            if (!next) continue;
            ++border;
            const uint8_t* p = &t.pixels[(size_t(y) * size_t(t.width) + size_t(x)) * 4];
            if (p[0] == 255 && p[1] == 255 && p[2] == 255) ++white;
        }
    CHECK(border > 0);
    CHECK(white * 100 < border); // the pattern never reaches pure white
}

TEST(texture_transfer_auto_uv_of_a_dense_sphere_does_not_fold) {
    // xatlas chained the near-coplanar faces of a dense smooth mesh into one "planar" chart, folded in two (every
    // texel covered twice), or crashed on it. Now: separate islands, no texel covered twice.
    Mesh m = makeIcosphere(6); // 82k triangles
    AutoUvResult r;
    std::string err;
    CHECK(computeAutoUv(m, r, err));
    CHECK(r.charts > 4);
    std::vector<uint8_t> hits(1024 * 1024, 0);
    size_t twice = 0, covered = 0;
    for (size_t t = 0; t < m.triangleCount(); ++t)
        rasterizeUvTriangle(r.uvs[t * 3], r.uvs[t * 3 + 1], r.uvs[t * 3 + 2], 1024, 1024, [&](int x, int y, float, float, float) {
            uint8_t& h = hits[size_t(y) * 1024 + size_t(x)];
            covered += h == 0;
            twice += h == 1;
            h = uint8_t(std::min(h + 1, 2));
        });
    std::printf("  dense sphere Auto UV: %d islands, %zu texels covered, %zu twice\n", r.charts, covered, twice);
    CHECK(covered > 1024 * 1024 / 3);
    CHECK(twice < covered / 1000);
}

TEST(texture_transfer_speed_at_4k_on_a_million_triangles) {
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    // A 1M-triangle grid with planar UVs and a 4K texture with a layer; new UVs mirrored and rotated (a different
    // layout of the same triangles, as Auto UV gives).
    Mesh m = makePlane(724); // 1.05M triangles
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::clamp(glm::vec2(p.x, p.z) * 0.5f + 0.5f, 0.0f, 1.0f);
        }
    m.texture = makeTexture(4096, 4096);
    paintPattern(m, m.texture->pixels, 255);
    addTextureLayer(*m.texture, "Layer 1");
    std::vector<glm::vec2> uvs(m.uvs.size());
    for (size_t i = 0; i < uvs.size(); ++i) uvs[i] = glm::vec2(m.uvs[i].y, 1.0f - m.uvs[i].x);
    const auto t0 = Clock::now();
    TextureTransferStats st;
    const auto moved = retargetTexture(m, uvs, &st);
    const auto t1 = Clock::now();
    CHECK(moved != nullptr);
    CHECK(st.texels > 4096u * 4000u);

    // Surface transfer: the same texture onto a 250k-triangle grid hovering just above the source.
    Mesh target = makePlane(354);
    for (auto& p : target.positions) p.y += 0.001f;
    target.uvs.resize(target.triangleCount() * 3);
    for (size_t t = 0; t < target.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = target.positions[target.triangles[t][c]];
            target.uvs[t * 3 + size_t(c)] = glm::clamp(glm::vec2(p.z, p.x) * 0.5f + 0.5f, 0.0f, 1.0f);
        }
    std::string err;
    TextureTransferStats ss;
    const auto t2 = Clock::now();
    CHECK(transferTextureFromSurface(m, target, err, &ss));
    const auto t3 = Clock::now();
    std::printf("  texture transfer at 4K: new UVs on %zu tris %.0f ms (2 planes); from the surface of %zu tris onto %zu tris "
                "%.0f ms (%zu texels)\n",
                m.triangleCount(), ms(t0, t1), m.triangleCount(), target.triangleCount(), ms(t2, t3), ss.texels);
    CHECK(ms(t0, t1) < timeLimit(5000.0));
    CHECK(ms(t2, t3) < timeLimit(20000.0));
}

// --- Material channels (core/TextureChannels.h) travel with the texture ---

namespace {

float roughPattern(const glm::vec3& p) { return 0.5f + 0.35f * std::sin(3.0f * p.x + 0.3f); }
float heightPattern(const glm::vec3& p) { return 0.5f + 0.3f * std::cos(2.0f * p.y); }

// Paints the patterns into Base roughness / height and a layer's metallic coverage (in the top half).
void paintChannelPatterns(Mesh& m) {
    PaintTexture& tex = mutableTexture(m);
    for (Channel c : {Channel::Roughness, Channel::Height, Channel::Metallic}) setChannelEnabled(tex, c, true);
    const int layer = addTextureLayer(tex, "Metal");
    TileImage& rough = tex.channels[size_t(Channel::Roughness)].base;
    TileImage& height = tex.channels[size_t(Channel::Height)].base;
    TileImage& metal = tex.layers[size_t(layer)].channels[size_t(Channel::Metallic)].image;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3 tv = m.triangles[t];
        const glm::vec3 p = m.positions[tv.x] * b0 + m.positions[tv.y] * b1 + m.positions[tv.z] * b2;
        const float r[1] = {roughPattern(p)}, h[1] = {heightPattern(p)}, mt[2] = {1.0f, p.y > 0.0f ? 1.0f : 0.0f};
        rough.setValues(x, y, r);
        height.setValues(x, y, h);
        metal.setValues(x, y, mt);
    }, false);
    compositeTextureAll(tex);
}

// Error of the channels against the patterns at random points (metallic: 1 above y = 0.15, 0 below -0.15).
struct ChannelErrors {
    ErrorStats rough, height;
    float metalWrong = 0; // fraction of clear-cut points with the wrong metal value
};
ChannelErrors channelErrors(const Mesh& m) {
    std::vector<float> er, eh;
    int wrong = 0, counted = 0;
    for (const SurfacePoint& s : randomPoints(m, 3000, 11)) {
        const glm::vec3 p = pointOf(m, s);
        BrushMaterial got;
        if (!sampleMaterialAt(m, s.tri, p, got)) continue;
        er.push_back(std::abs(got.roughness - roughPattern(p)));
        eh.push_back(std::abs(got.height - heightPattern(p)));
        if (std::abs(p.y) > 0.15f) {
            ++counted;
            wrong += (got.metallic > 0.5f) != (p.y > 0.0f);
        }
    }
    ChannelErrors out;
    out.rough = errorStats(er);
    out.height = errorStats(eh);
    out.metalWrong = counted ? float(wrong) / float(counted) : 1.0f;
    return out;
}

} // namespace

TEST(texture_transfer_carries_material_channels) {
    CommandRegistry reg;
    // Auto UV again (same surface): Base channels and layer channels move exactly up to resampling.
    {
        Document doc = paintedSphere(3, 1024, 512);
        paintChannelPatterns(doc.mesh);
        const ChannelErrors before = channelErrors(doc.mesh);
        CHECK(reg.execute(doc, "uv_auto", {{"resolution", 1024.0}}).ok);
        const PaintTexture& t = *doc.mesh.texture;
        CHECK(channelEnabled(t, Channel::Roughness) && channelEnabled(t, Channel::Height) && channelEnabled(t, Channel::Metallic));
        CHECK(t.layers.size() == 1 && t.layers[0].channels[size_t(Channel::Metallic)].image.valid());
        CHECK(channelImage(t, Channel::Height).bits() == 16);
        const ChannelErrors after = channelErrors(doc.mesh);
        std::printf("  auto uv channels: roughness mean %.4f -> %.4f, height %.4f -> %.4f, metal wrong %.3f\n", before.rough.mean,
                    after.rough.mean, before.height.mean, after.height.mean, after.metalWrong);
        CHECK(after.rough.mean < 0.01f && after.rough.p99 < 0.05f);
        CHECK(after.height.mean < 0.01f && after.height.p99 < 0.05f);
        CHECK(after.metalWrong < 0.02f);
    }
    // Voxel remesh (new surface): the closest old point's channels.
    {
        Document doc = paintedSphere(4, 1024, 1024);
        paintChannelPatterns(doc.mesh);
        const CommandResult r = reg.execute(doc, "remesh", {{"resolution", 96.0}});
        CHECK(r.ok && r.message.find("texture kept") != std::string::npos);
        const ChannelErrors after = channelErrors(doc.mesh);
        std::printf("  remesh channels: roughness mean %.4f p99 %.4f, height mean %.4f, metal wrong %.3f\n", after.rough.mean,
                    after.rough.p99, after.height.mean, after.metalWrong);
        CHECK(channelEnabled(*doc.mesh.texture, Channel::Metallic));
        CHECK(after.rough.mean < 0.02f && after.rough.p99 < 0.08f);
        CHECK(after.height.mean < 0.02f);
        CHECK(after.metalWrong < 0.03f);
    }
    // Merging: a part without the channel reads its default (roughness 0.65).
    {
        Document doc = paintedSphere(3, 1024, 512);
        paintChannelPatterns(doc.mesh);
        Mesh second = makeIcosphere(3, 0.5f);
        for (auto& p : second.positions) p += glm::vec3(2.0f, 0.0f, 0.0f);
        second.recomputeAllNormals();
        AutoUvResult uv;
        std::string err;
        CHECK(computeAutoUv(second.positions, second.triangles, 512, uv, err));
        second.uvs = uv.uvs;
        second.texture = makeTexture(1024, 1024);
        second.paintToTexture = true;
        doc.addSubtool(second, "Second", false);
        CHECK(reg.execute(doc, "subtool_merge_visible", {}).ok);
        CHECK(channelEnabled(*doc.mesh.texture, Channel::Roughness));
        std::vector<float> first, other;
        for (const auto& s : randomPoints(doc.mesh, 3000, 5)) {
            const glm::vec3 p = pointOf(doc.mesh, s);
            BrushMaterial got;
            CHECK(sampleMaterialAt(doc.mesh, s.tri, p, got));
            if (p.x > 1.2f) other.push_back(std::abs(got.roughness - 0.65f));
            else first.push_back(std::abs(got.roughness - roughPattern(p)));
        }
        const ErrorStats a = errorStats(first), b = errorStats(other);
        std::printf("  merged channels: painted part roughness mean %.4f, plain part %.4f\n", a.mean, b.max);
        CHECK(a.mean < 0.02f);
        CHECK(b.max < 0.02f);
    }
}
