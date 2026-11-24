// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shift with the Paint brush (TexturePainter::smoothDab) on a texture with several channels, across the UV seams of an
// Auto UV Part: a member's strokes (painted with Base Color, Roughness and Metallic ticked) kept hard, streaky edges
// because only the colour smoothed. Every channel the stroke paints smooths now, and a Normal is turned into the
// neighbouring island's tangent frame before it is blended, then renormalised.
#include "TestHarness.h"

#include "core/AutoUv.h"
#include "core/Primitives.h"
#include "core/SurfaceQuery.h"
#include "core/Tangents.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <random>

using namespace sf;

namespace {

// A sphere cut into many UV islands by Auto UV, with a size x size texture whose Roughness, Metallic, Height and Normal
// channels are on.
Mesh textureSphere(int subdivisions, int size, int* islands = nullptr) {
    Mesh m = makeIcosphere(subdivisions);
    AutoUvResult r;
    std::string err;
    CHECK(computeAutoUv(m, r, err));
    m.uvs = r.uvs;
    if (islands) *islands = r.charts;
    m.texture = makeTexture(size, size);
    m.paintToTexture = true;
    m.rebuildTopology();
    m.recomputeAllNormals();
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height})
        setChannelEnabled(*m.texture, c, true);
    return m;
}

constexpr uint32_t kChannels = (1u << int(Channel::BaseColor)) | (1u << int(Channel::Roughness)) | (1u << int(Channel::Metallic)) |
                               (1u << int(Channel::Normal)) | (1u << int(Channel::Height));

glm::vec2 uvOf(const Mesh& m, const SurfaceHit& h) {
    return m.uvs[size_t(h.prim) * 3] * h.bary.x + m.uvs[size_t(h.prim) * 3 + 1] * h.bary.y + m.uvs[size_t(h.prim) * 3 + 2] * h.bary.z;
}

// Smooth dabs along the circle y = `y` of the unit sphere, `passes` times, at brush radius `radius`.
size_t smoothAlong(Mesh& m, const BVH& bvh, float y, float radius, int passes, uint32_t channels = kChannels, UndoRecord* record = nullptr) {
    TexturePainter painter;
    CHECK(painter.begin(m, channels));
    size_t changed = 0;
    const float ring = std::sqrt(1.0f - y * y);
    for (int pass = 0; pass < passes; ++pass)
        for (int i = 0; i < 160; ++i) {
            const float a = 6.2831853f * float(i) / 160.0f * (pass % 2 ? -1.0f : 1.0f);
            DabParams p;
            p.center = glm::vec3(ring * std::cos(a), y, ring * std::sin(a));
            p.viewDir = -p.center;
            p.radius = radius;
            p.strength = 1.0f;
            changed += painter.smoothDab(m, bvh, p, Symmetry{false, false, false});
        }
    UndoRecord local;
    painter.end(record ? *record : local);
    return changed;
}

// A channel's first component at a surface point.
float channelAt(const Mesh& m, const TriangleBVH& tb, Channel c, const glm::vec3& p) {
    SurfaceHit h;
    if (!tb.closestPoint(p, std::numeric_limits<float>::max(), h)) return 0.0f;
    const glm::vec2 uv = uvOf(m, h);
    if (c == Channel::BaseColor) return sampleTexture(*m.texture, uv).y; // (green, linear: the red strokes' 0.02 vs white's 1)
    float v[4] = {0, 0, 0, 0};
    channelImage(*m.texture, c).sample(uv.x, uv.y, v);
    return v[0];
}

// The largest step of a channel between neighbouring points (0.004 apart) on meridians crossing the edge y = 0.2, as a
// fraction of the contrast: 1 for a hard edge, small once smoothed.
float sharpness(const Mesh& m, const TriangleBVH& tb, Channel c, float contrast) {
    float worst = 0.0f;
    for (int k = 0; k < 24; ++k) {
        const float a = 6.2831853f * (float(k) + 0.3f) / 24.0f;
        float prev = 0.0f;
        for (int i = 0; i <= 160; ++i) {
            const float y = -0.1f + 0.0035f * float(i) * 1.0f;
            const float ring = std::sqrt(1.0f - y * y);
            const float v = channelAt(m, tb, c, glm::vec3(ring * std::cos(a), y, ring * std::sin(a)));
            if (i) worst = std::max(worst, std::abs(v - prev));
            prev = v;
        }
    }
    return worst / contrast;
}

} // namespace

TEST(texture_smooth_blends_every_channel_across_uv_seams) {
    int islands = 0;
    Mesh m = textureSphere(5, 1024, &islands);
    CHECK(islands >= 6);
    BVH bvh;
    bvh.build(m);
    TriangleBVH tb;
    tb.build(m);
    PaintTexture& tex = *m.texture;
    // A hard edge at y = 0.2 (across the seams of every island it crosses): red, rough, metallic and raised above it,
    // the blank values below. (The stroke the member painted: colour, Roughness 0.85, Metallic 0.15.)
    const float rough0 = channelFormat(int(Channel::Roughness)).defaultValue[0], rough1 = 0.85f, metal1 = 0.15f, height0 = 0.5f,
                height1 = 0.9f;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3 tv = m.triangles[t];
        const glm::vec3 p = m.positions[tv.x] * b0 + m.positions[tv.y] * b1 + m.positions[tv.z] * b2;
        const bool above = p.y > 0.2f;
        uint8_t* px = &tex.pixels[(size_t(y) * size_t(tex.width) + size_t(x)) * 4];
        px[0] = above ? 230 : 255, px[1] = above ? 20 : 255, px[2] = above ? 20 : 255, px[3] = 255;
        const float r = above ? rough1 : rough0, mt = above ? metal1 : 0.0f, ht = above ? height1 : height0;
        const float tilt[3] = {above ? 0.8f : 0.5f, 0.5f, above ? 0.9f : 1.0f}; // (unit directions: (0.6, 0, 0.8) and (0, 0, 1))
        tex.channels[size_t(Channel::Roughness)].base.setValues(x, y, &r);
        tex.channels[size_t(Channel::Metallic)].base.setValues(x, y, &mt);
        tex.channels[size_t(Channel::Height)].base.setValues(x, y, &ht);
        tex.channels[size_t(Channel::Normal)].base.setValues(x, y, tilt);
    }, false);
    const float contrast[] = {0.98f, rough1 - rough0, metal1, height1 - height0};
    const Channel chans[] = {Channel::BaseColor, Channel::Roughness, Channel::Metallic, Channel::Height};
    float before[4];
    for (int i = 0; i < 4; ++i) {
        before[i] = sharpness(m, tb, chans[i], contrast[i]);
        CHECK(before[i] > 0.3f); // the harness sees a hard edge
    }
    // (before the stroke: the channels at a few points, for undo)
    const std::vector<glm::vec3> probes = {glm::normalize(glm::vec3(1, 0.2f, 0.3f)), glm::normalize(glm::vec3(-0.4f, 0.21f, 0.9f)),
                                           glm::normalize(glm::vec3(0.1f, 0.19f, -1))};
    float held[4][3];
    for (int i = 0; i < 4; ++i)
        for (size_t q = 0; q < probes.size(); ++q) held[i][q] = channelAt(m, tb, chans[i], probes[q]);
    UndoRecord rec;
    const size_t changed = smoothAlong(m, bvh, 0.2f, 0.1f, 2, kChannels, &rec);
    CHECK(changed > 5000);
    CHECK(rec.textureTiles != nullptr);
    float smoothed[4][3];
    for (int i = 0; i < 4; ++i)
        for (size_t q = 0; q < probes.size(); ++q) smoothed[i][q] = channelAt(m, tb, chans[i], probes[q]);
    for (int i = 0; i < 4; ++i) {
        const float after = sharpness(m, tb, chans[i], contrast[i]);
        std::printf("  Shift on %s, %d islands: largest step across the edge %.2f -> %.2f of the contrast\n",
                    channelFormat(int(chans[i])).label, islands, double(before[i]), double(after));
        CHECK(after < 0.5f * before[i]); // (before: only the colour softened)
    }
    // The Normal stays a unit direction wherever it was blended.
    float worst = 0.0f;
    for (int k = 0; k < 24; ++k) {
        const float a = 6.2831853f * (float(k) + 0.3f) / 24.0f;
        for (int i = 0; i <= 80; ++i) {
            const float y = 0.1f + 0.0025f * float(i);
            const float ring = std::sqrt(1.0f - y * y);
            SurfaceHit h;
            CHECK(tb.closestPoint(glm::vec3(ring * std::cos(a), y, ring * std::sin(a)), 10.0f, h));
            const glm::vec2 uv = uvOf(m, h);
            // (the stored texel next to the point: bilinear blends of unit vectors are shorter, so look at one texel)
            float n[4] = {0, 0, 0, 0};
            channelImage(tex, Channel::Normal).values(std::clamp(int(uv.x * float(tex.width)), 0, tex.width - 1),
                                                       std::clamp(int(uv.y * float(tex.height)), 0, tex.height - 1), n);
            const glm::vec3 d(n[0] * 2.0f - 1.0f, n[1] * 2.0f - 1.0f, n[2] * 2.0f - 1.0f);
            worst = std::max(worst, std::abs(glm::length(d) - 1.0f));
        }
    }
    std::printf("  Normal length off by at most %.3f (a texel of the blended strip)\n", double(worst));
    CHECK(worst < 0.02f);
    // One undo step brings every channel back, and redo (the same swap) smooths again.
    CHECK(rec.textureTiles != nullptr);
    rec.textureTiles->swapWith(m);
    bool changedAtProbe = false;
    for (int i = 0; i < 4; ++i)
        for (size_t q = 0; q < probes.size(); ++q) {
            CHECK(std::abs(channelAt(m, tb, chans[i], probes[q]) - held[i][q]) < 1e-4f);
            changedAtProbe = changedAtProbe || std::abs(smoothed[i][q] - held[i][q]) > 0.02f;
        }
    CHECK(changedAtProbe); // (the probes sit on the blurred strip: the undo had something to bring back)
    rec.textureTiles->swapWith(m);
    for (int i = 0; i < 4; ++i)
        for (size_t q = 0; q < probes.size(); ++q) CHECK(std::abs(channelAt(m, tb, chans[i], probes[q]) - smoothed[i][q]) < 1e-4f);
}

TEST(texture_smooth_turns_a_normal_into_the_next_islands_tangent_frame) {
    // A smooth normal field defined in space (the surface normal tilted towards a fixed direction), stored as a
    // tangent-space map through each corner's MikkTSpace frame. Smoothing blends neighbours, so a field this smooth stays
    // what it is, wherever the brush crosses a seam between islands turned any way against each other. Blending the
    // other island's texels as they are (its frame, not this one's) pulls the normal off by the angle between them.
    int islands = 0;
    Mesh m = textureSphere(5, 1024, &islands);
    CHECK(islands >= 6);
    const std::vector<glm::vec4> tangents = computeTangents(m);
    CHECK(tangents.size() == m.triangleCount() * 3);
    const glm::vec3 tiltDir = glm::normalize(glm::vec3(0.3f, 0.5f, 0.8f));
    auto tangentSpace = [&](uint32_t t, const glm::vec3& b) {
        const glm::uvec3 tv = m.triangles[t];
        const glm::vec3 N = glm::normalize(m.normals[tv.x] * b.x + m.normals[tv.y] * b.y + m.normals[tv.z] * b.z);
        glm::vec3 T = glm::vec3(tangents[size_t(t) * 3]) * b.x + glm::vec3(tangents[size_t(t) * 3 + 1]) * b.y + glm::vec3(tangents[size_t(t) * 3 + 2]) * b.z;
        T = glm::normalize(T - N * glm::dot(N, T));
        const float w = tangents[size_t(t) * 3].w;
        const glm::vec3 B = w * glm::cross(N, T);
        const glm::vec3 n = glm::normalize(N + 0.45f * (tiltDir - N * glm::dot(N, tiltDir))); // tilted towards tiltDir
        return glm::vec3(glm::dot(n, T), glm::dot(n, B), glm::dot(n, N));
    };
    PaintTexture& tex = *m.texture;
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::vec3 d = tangentSpace(t, glm::vec3(b0, b1, b2));
        const float v[3] = {d.x * 0.5f + 0.5f, d.y * 0.5f + 0.5f, d.z * 0.5f + 0.5f};
        tex.channels[size_t(Channel::Normal)].base.setValues(x, y, v);
    }, false);
    BVH bvh;
    bvh.build(m);
    size_t changed = 0;
    for (float y : {-0.5f, 0.0f, 0.5f})
        changed += smoothAlong(m, bvh, y, 0.3f, 2, channelBit(Channel::Normal));
    CHECK(changed > 20000);
    // The map still holds the field: at points of the triangles along the seams the brush reached (where a texel's
    // neighbours are on another island, turned against it), the stored direction against the field's.
    std::map<std::pair<uint32_t, uint32_t>, std::vector<std::pair<uint32_t, int>>> edges;
    for (uint32_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            uint32_t va = m.triangles[t][c], vb = m.triangles[t][(c + 1) % 3];
            if (va > vb) std::swap(va, vb);
            edges[{va, vb}].push_back({t, c});
        }
    std::vector<uint32_t> seamTris;
    for (const auto& [key, owners] : edges) {
        if (owners.size() != 2) continue;
        auto uvOfVertex = [&](const std::pair<uint32_t, int>& o, uint32_t v) {
            for (int c = 0; c < 3; ++c)
                if (m.triangles[o.first][c] == v) return m.uvs[size_t(o.first) * 3 + size_t(c)];
            return glm::vec2(0.0f);
        };
        if (uvOfVertex(owners[0], key.first) == uvOfVertex(owners[1], key.first) && uvOfVertex(owners[0], key.second) == uvOfVertex(owners[1], key.second))
            continue;
        for (const auto& o : owners) {
            const glm::uvec3 tv = m.triangles[o.first];
            const float cy = (m.positions[tv.x].y + m.positions[tv.y].y + m.positions[tv.z].y) / 3.0f;
            if (std::abs(cy + 0.5f) < 0.25f || std::abs(cy) < 0.25f || std::abs(cy - 0.5f) < 0.25f) seamTris.push_back(o.first);
        }
    }
    std::sort(seamTris.begin(), seamTris.end());
    seamTris.erase(std::unique(seamTris.begin(), seamTris.end()), seamTris.end());
    float worst = 0.0f;
    double sum = 0.0;
    int n = 0;
    for (uint32_t t : seamTris)
        for (const glm::vec3 bary : {glm::vec3(1.0f / 3.0f), glm::vec3(0.6f, 0.2f, 0.2f), glm::vec3(0.2f, 0.6f, 0.2f), glm::vec3(0.2f, 0.2f, 0.6f)}) {
            const glm::vec2 uv = m.uvs[size_t(t) * 3] * bary.x + m.uvs[size_t(t) * 3 + 1] * bary.y + m.uvs[size_t(t) * 3 + 2] * bary.z;
            float s[4] = {0, 0, 0, 0};
            channelImage(tex, Channel::Normal).sample(uv.x, uv.y, s);
            const glm::vec3 got = glm::normalize(glm::vec3(s[0] * 2.0f - 1.0f, s[1] * 2.0f - 1.0f, s[2] * 2.0f - 1.0f));
            const float err = glm::length(got - tangentSpace(t, bary));
            worst = std::max(worst, err);
            sum += double(err);
            ++n;
        }
    std::printf("  Normal field after Shift, %d points on seam triangles of %d islands: mean error %.4f, worst %.4f (unit vectors)\n", n,
                islands, sum / std::max(n, 1), double(worst));
    CHECK(n > 400);
    CHECK(sum / n < 0.02);
    CHECK(worst < 0.1f);
}

TEST(texture_smooth_blends_a_layers_channel_by_its_coverage) {
    // The stroke on a layer: the layer's Roughness is 0.85 above y = 0.2 and not there (transparent) below, over the Base's
    // 0.65. Shift smooths the layer's value and its coverage, a transparent texel lends no value, and the composite follows.
    int islands = 0;
    Mesh m = textureSphere(5, 1024, &islands);
    BVH bvh;
    bvh.build(m);
    TriangleBVH tb;
    tb.build(m);
    PaintTexture& tex = *m.texture;
    const int layer = addTextureLayer(tex, "Wet");
    CHECK(layer == 0);
    tex.activeLayer = layer;
    syncLayerChannels(tex);
    TileImage& img = tex.layers[size_t(layer)].channels[size_t(Channel::Roughness)].image;
    CHECK(img.valid());
    bakeTexels(m, tex, [&](uint32_t t, float b0, float b1, float b2, int x, int y) {
        const glm::uvec3 tv = m.triangles[t];
        const glm::vec3 p = m.positions[tv.x] * b0 + m.positions[tv.y] * b1 + m.positions[tv.z] * b2;
        const float v[2] = {p.y > 0.2f ? 0.85f : 0.0f, p.y > 0.2f ? 1.0f : 0.0f}; // (value, coverage)
        img.setValues(x, y, v);
    }, false);
    compositeTextureAll(tex);
    const float rough0 = channelFormat(int(Channel::Roughness)).defaultValue[0];
    const float before = sharpness(m, tb, Channel::Roughness, 0.85f - rough0);
    CHECK(before > 0.3f);
    const size_t changed = smoothAlong(m, bvh, 0.2f, 0.1f, 2, channelBit(Channel::Roughness));
    CHECK(changed > 5000);
    const float after = sharpness(m, tb, Channel::Roughness, 0.85f - rough0);
    std::printf("  Shift on a layer's Roughness: largest step across the edge %.2f -> %.2f of the contrast\n", double(before), double(after));
    CHECK(after < 0.5f * before);
    // The strip stays between the two values (a transparent texel's 0 is not averaged in as a roughness of 0).
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i <= 120; ++i) {
        const float y = 0.0f + 0.003f * float(i);
        const float v = channelAt(m, tb, Channel::Roughness, glm::vec3(std::sqrt(1.0f - y * y), y, 0.0f));
        lo = std::min(lo, v), hi = std::max(hi, v);
    }
    CHECK(lo > rough0 - 0.02f);
    CHECK(hi < 0.85f + 0.02f);
}
