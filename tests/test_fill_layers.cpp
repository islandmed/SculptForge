// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fill layers (core/LayerStack.h): generators reading the Mesh Maps (edge wear on convex rims, dirt in the trench),
// effect chains (invert, levels, blur, noise that ignores UV seams), material channels, the painted mask, partial
// re-evaluation after the Mesh Maps change, the fill_layer command with undo, and projects keeping it all.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/LayerStack.h"
#include "core/MaterialRecipe.h"
#include "core/MeshMaps.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "core/Undo.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace sf;

namespace {

// A plane with a V trench along z (concave floor, convex rims), planar UVs and a 1K texture.
Mesh trenchPart() {
    Mesh m = makePlane(96);
    for (glm::vec3& p : m.positions) p.y = -0.6f * std::max(0.0f, 0.3f - std::abs(p.x));
    m.recomputeAllNormals();
    const AABB b = m.bounds();
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2((p.x - b.min.x) / (b.max.x - b.min.x), (p.z - b.min.z) / (b.max.z - b.min.z)) * 0.96f + 0.02f;
        }
    m.texture = makeTexture(1024, 1024);
    m.paintToTexture = true;
    return m;
}

// Texel column of world x on the trench's texture (rows: z).
int texelX(float x) { return int(((x + 1.0f) / 2.0f * 0.96f + 0.02f) * 1024.0f); }

float alphaAt(const PaintTexture& t, int layer, int x, int y) {
    return float(t.layers[size_t(layer)].rgba[(size_t(y) * size_t(t.width) + size_t(x)) * 4 + 3]) / 255.0f;
}

MeshMapSettings quickMaps() {
    MeshMapSettings st;
    st.aoRays = 16;
    st.thicknessRays = 4;
    st.aoDistance = 0.2f;
    return st;
}

} // namespace

TEST(fill_layer_generators_follow_the_mesh_maps) {
    Mesh m = trenchPart();
    PaintTexture& t = *m.texture;
    auto maps = std::make_shared<MeshMapSet>();
    std::string err;
    CHECK(bakeMeshMaps(m, 1024, 1024, kAllMeshMaps, quickMaps(), *maps, err));
    t.meshMaps = maps;

    FillLayer wear;
    wear.material.color = glm::vec3(0.9f, 0.9f, 0.95f);
    wear.material.channels = channelBit(Channel::BaseColor);
    MaskEffect e;
    e.generator = GeneratorKind::EdgeWear;
    e.level = 0.6f;
    e.breakup = 0.0f;
    wear.effects.push_back(e);
    const int w = addFillLayer(m, t, "Edges", wear);
    FillLayer dirt = wear;
    dirt.effects[0].generator = GeneratorKind::Dirt;
    dirt.effects[0].level = 0.5f;
    const int d = addFillLayer(m, t, "Dirt", dirt);
    CHECK(isFillLayer(t, w) && isFillLayer(t, d) && !isFillLayer(t, -1));
    const int y = 512;
    const float rimW = alphaAt(t, w, texelX(0.3f), y), floorW = alphaAt(t, w, texelX(0.0f), y), flatW = alphaAt(t, w, texelX(0.75f), y);
    const float rimD = alphaAt(t, d, texelX(0.3f), y), floorD = alphaAt(t, d, texelX(0.0f), y), flatD = alphaAt(t, d, texelX(0.75f), y);
    std::printf("  edge wear rim %.2f floor %.2f flat %.2f; dirt rim %.2f floor %.2f flat %.2f\n", rimW, floorW, flatW, rimD, floorD, flatD);
    CHECK(rimW > 0.5f && floorW < 0.1f && flatW < 0.1f);
    CHECK(floorD > 0.5f && flatD < 0.1f && rimD < floorD);
    // The composite shows the fill's colour where the mask is on.
    const uint8_t* rim = &t.pixels[(size_t(y) * 1024 + size_t(texelX(0.3f))) * 4];
    CHECK(rim[0] > 200);
    // Light direction: surfaces facing up are covered, the trench's sloped walls less.
    FillLayer top = wear;
    top.effects[0].generator = GeneratorKind::LightDirection;
    top.effects[0].direction = glm::vec3(0, 1, 0);
    top.effects[0].level = 0.1f;
    const int l = addFillLayer(m, t, "Dust", top);
    CHECK(alphaAt(t, l, texelX(0.75f), y) > alphaAt(t, l, texelX(0.15f), y));
    CHECK(fillMapsNeeded(t) & meshMapBit(MeshMapKind::Curvature));
}

TEST(fill_layer_effect_chain_and_channels) {
    Mesh m = trenchPart();
    PaintTexture& t = *m.texture;
    auto maps = std::make_shared<MeshMapSet>();
    std::string err;
    CHECK(bakeMeshMaps(m, 1024, 1024, meshMapBit(MeshMapKind::Position), quickMaps(), *maps, err));
    t.meshMaps = maps;
    setChannelEnabled(t, Channel::Roughness, true);
    setChannelEnabled(t, Channel::Metallic, true);
    FillLayer f;
    f.material.channels = channelBit(Channel::Roughness) | channelBit(Channel::Metallic);
    f.material.roughness = 0.2f;
    f.material.metallic = 1.0f;
    MaskEffect fill;
    fill.kind = MaskEffectKind::Fill;
    fill.value = 0.7f;
    f.effects.push_back(fill);
    const int i = addFillLayer(m, t, "Metal", f);
    // Base colour untouched; roughness and metallic laid down at the mask's strength.
    CHECK(alphaAt(t, i, 300, 300) == 0.0f);
    const float r = channelImage(t, Channel::Roughness).value(300, 300), me = channelImage(t, Channel::Metallic).value(300, 300);
    std::printf("  roughness %.3f metallic %.3f (0.7 of the way)\n", r, me);
    CHECK(std::abs(me - 0.7f) < 0.02f);
    CHECK(std::abs(r - (0.65f + (0.2f - 0.65f) * 0.7f)) < 0.02f);
    // Invert, then levels.
    MaskEffect inv;
    inv.kind = MaskEffectKind::Invert;
    auto g = std::make_shared<FillLayer>(*t.layers[size_t(i)].fill);
    g->effects.push_back(inv);
    t.layers[size_t(i)].fill = g;
    evaluateFillLayer(m, t, i);
    CHECK(std::abs(fillMaskAt(m, t, i, 300, 300) - 0.3f) < 0.01f);
    MaskEffect lv;
    lv.kind = MaskEffectKind::Levels;
    lv.inLow = 0.0f, lv.inHigh = 0.6f;
    g->effects.push_back(lv);
    CHECK(std::abs(fillMaskAt(m, t, i, 300, 300) - 0.5f) < 0.01f);
    // Noise in object space: varies over the surface, stays in 0..1, and the same 3D point gives the same value.
    MaskEffect noise;
    noise.kind = MaskEffectKind::Noise;
    noise.scale = 0.05f;
    g->effects.assign(1, noise);
    float lo = 1, hi = 0;
    for (int x = 100; x < 900; x += 37) {
        const float v = fillMaskAt(m, t, i, x, 400);
        lo = std::min(lo, v), hi = std::max(hi, v);
        CHECK(v >= 0.0f && v <= 1.0f);
    }
    CHECK(hi - lo > 0.3f);
    // Blur smooths it: neighbouring texels move closer.
    auto roughness = [&](int y) {
        float s = 0;
        for (int x = 200; x < 800; ++x) s += std::abs(fillMaskAt(m, t, i, x + 1, y) - fillMaskAt(m, t, i, x, y));
        return s;
    };
    noise.scale = 0.004f;
    g->effects.assign(1, noise);
    const float before = roughness(500);
    MaskEffect blur;
    blur.kind = MaskEffectKind::Blur;
    blur.radius = 6.0f;
    g->effects.push_back(blur);
    const float after = roughness(500);
    std::printf("  noise variation %.2f, blurred %.2f\n", before, after);
    CHECK(after < before * 0.5f);
    // A fill layer's JSON round-trips.
    FillLayer back;
    CHECK(fillLayerFromJson(fillLayerJson(*g), back, err));
    CHECK(back.effects.size() == 2 && back.effects[1].kind == MaskEffectKind::Blur && back.effects[1].radius == 6.0f);
    CHECK(back.material.channels == g->material.channels);
}

TEST(fill_layer_painted_mask_and_partial_updates) {
    Mesh m = trenchPart();
    PaintTexture& t = *m.texture;
    FillLayer f;
    f.material.color = glm::vec3(1, 0, 0);
    f.material.channels = channelBit(Channel::BaseColor);
    MaskEffect paint;
    paint.kind = MaskEffectKind::Paint;
    paint.blend = MaskBlend::Add;
    f.effects.push_back(paint);
    const int i = addFillLayer(m, t, "Painted", f);
    t.activeLayer = i;
    CHECK(alphaAt(t, i, 512, 512) == 0.0f); // nothing painted yet
    // The Paint brush on a Fill layer paints its mask (PaintTexture::target), then the frame's update re-evaluates.
    BVH bvh;
    bvh.build(m);
    TexturePainter painter;
    CHECK(painter.begin(m, channelBit(Channel::BaseColor) | channelBit(Channel::Roughness)));
    CHECK(painter.channels() == 1u); // (a mask takes coverage only)
    DabParams p;
    p.center = glm::vec3(0.75f, 0.0f, 0.0f);
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = 0.1f;
    p.strength = 1.0f;
    CHECK(painter.dab(m, bvh, p, glm::vec3(1.0f), Symmetry{false, false, false}) > 0);
    UndoRecord r;
    painter.end(r);
    PaintTexture& tp = *m.texture;
    const int cx = texelX(0.75f), cy = 512;
    CHECK(tp.layers[size_t(i)].mask[(size_t(cy) * 1024 + size_t(cx)) * 4 + 3] > 200);
    CHECK(updateFillLayers(m, tp));
    CHECK(alphaAt(tp, i, cx, cy) > 0.8f);
    CHECK(alphaAt(tp, i, texelX(-0.75f), cy) == 0.0f);
    CHECK(!updateFillLayers(m, tp)); // nothing new
    // Undo the stroke: the mask goes back and the next update follows.
    r.textureTiles->swapWith(m);
    CHECK(updateFillLayers(m, *m.texture));
    CHECK(alphaAt(*m.texture, i, cx, cy) == 0.0f);

    // Partial re-evaluation: only the listed tiles change.
    FillLayer full;
    full.material.channels = channelBit(Channel::BaseColor);
    MaskEffect one;
    one.kind = MaskEffectKind::Fill;
    full.effects.push_back(one);
    PaintTexture& t2 = *m.texture;
    const int j = addFillLayer(m, t2, "All", full);
    auto g = std::make_shared<FillLayer>(*t2.layers[size_t(j)].fill);
    g->effects[0].value = 0.0f;
    t2.layers[size_t(j)].fill = g;
    const std::vector<uint32_t> tiles = {0};
    evaluateFillLayers(m, t2, &tiles);
    CHECK(alphaAt(t2, j, 10, 10) == 0.0f);    // in tile 0: evaluated again
    CHECK(alphaAt(t2, j, 600, 600) == 1.0f);  // elsewhere: as before
}

TEST(fill_layer_command_undo_and_projects) {
    Document doc = makeDoc(trenchPart());
    CommandRegistry reg;
    // The command bakes the Mesh Maps it needs (no app here: at once), adds the layer and evaluates it.
    CommandResult r = reg.execute(doc, "fill_layer", {{"name", std::string("Worn edges")}, {"generator", std::string("edge_wear")},
                                                      {"color", std::string("#e0e0e8")}, {"metallic", 1.0}, {"level", 0.6},
                                                      {"breakup", 0.0}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    PaintTexture& t = *doc.mesh.texture;
    CHECK(t.layers.size() == 1 && isFillLayer(t, 0));
    CHECK(channelEnabled(t, Channel::Metallic)); // turned on for the layer
    CHECK(t.meshMaps && t.meshMaps->has(MeshMapKind::Curvature));
    CHECK(alphaAt(t, 0, texelX(0.3f), 512) > 0.5f);
    // An effect on top: invert.
    CHECK(reg.execute(doc, "fill_layer", {{"action", std::string("effect")}, {"kind", std::string("invert")}}).ok);
    CHECK(doc.mesh.texture->layers[0].fill->effects.size() == 2);
    CHECK(alphaAt(*doc.mesh.texture, 0, texelX(0.3f), 512) < 0.5f);
    CHECK(!reg.execute(doc, "fill_layer", {{"action", std::string("effect")}, {"kind", std::string("sparkle")}}).ok);
    CHECK(!reg.execute(doc, "fill_layer", {{"action", std::string("remove_effect")}, {"effect", 7.0}}).ok);
    CHECK(reg.execute(doc, "fill_layer", {{"action", std::string("info")}}).message.find("edge_wear") != std::string::npos);
    // Undo removes the invert again.
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.texture->layers[0].fill->effects.size() == 1);
    CHECK(alphaAt(*doc.mesh.texture, 0, texelX(0.3f), 512) > 0.5f);
    CHECK(doc.mesh.texture->meshMaps != nullptr); // (the maps stay through undo)

    // Projects keep the settings and the painted mask (format 7).
    doc.mesh.texture->layers[0].mask[(512 * 1024 + 100) * 4 + 3] = 77;
    const std::string path = tempPath("sf_fill_layer.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.mesh.texture && back.mesh.texture->layers.size() == 1 && isFillLayer(*back.mesh.texture, 0));
    const FillLayer& f = *back.mesh.texture->layers[0].fill;
    CHECK(f.effects.size() == 1 && f.effects[0].generator == GeneratorKind::EdgeWear && std::abs(f.effects[0].level - 0.6f) < 1e-4f);
    CHECK(back.mesh.texture->layers[0].mask[(512 * 1024 + 100) * 4 + 3] == 77);
    CHECK(std::abs(alphaAt(*back.mesh.texture, 0, texelX(0.3f), 512) - alphaAt(*doc.mesh.texture, 0, texelX(0.3f), 512)) < 0.01f);
}

TEST(material_recipes_are_data_and_wear_the_model) {
    const auto& list = builtinMaterialRecipes();
    CHECK(list.size() == 4);
    for (const MaterialRecipe& r : list) CHECK(!r.name.empty() && !r.description.empty() && r.layers.size() >= 2);
    CHECK(findMaterialRecipe("Worn Metal") && findMaterialRecipe("painted_edges") && !findMaterialRecipe("glitter"));
    Document doc = makeDoc(trenchPart());
    CommandRegistry reg;
    CommandResult r = reg.execute(doc, "material_recipe", {{"name", std::string("worn_metal")}});
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok);
    const PaintTexture& t = *doc.mesh.texture;
    CHECK(t.layers.size() == 4 && isFillLayer(t, 0) && isFillLayer(t, 3));
    CHECK(channelEnabled(t, Channel::Metallic) && channelEnabled(t, Channel::Roughness));
    // Polished edges on the rims are brighter than the plain steel on the flat.
    const uint8_t* rim = &t.pixels[(512 * 1024 + size_t(texelX(0.3f))) * 4];
    const uint8_t* flat = &t.pixels[(512 * 1024 + size_t(texelX(0.75f))) * 4];
    std::printf("  rim %d flat %d\n", rim[0], flat[0]);
    CHECK(rim[0] > flat[0] + 20);
    CHECK(channelImage(t, Channel::Metallic).value(texelX(0.75f), 512) > 0.9f);
    // Saved and read back.
    const std::string path = tempPath("sf_recipe.sfmaterial");
    CHECK(reg.execute(doc, "material_recipe", {{"action", std::string("save")}, {"name", std::string("Mine")}, {"path", path}}).ok);
    CHECK(reg.execute(doc, "material_recipe", {{"path", path}}).ok);
    CHECK(doc.mesh.texture->layers.size() == 8);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.texture->layers.size() == 4);
    CHECK(!reg.execute(doc, "material_recipe", {{"name", std::string("glitter")}}).ok);
    MaterialRecipe bad;
    std::string err;
    CHECK(!parseMaterialRecipe("{\"format\":\"other\"}", bad, err));
}

// The app's live re-bake (App::updateMeshMaps): the update runs on a worker thread while the main thread keeps
// using the maps it updates (Fill layers evaluate from them, the texture is replaced by the next command, the Part
// is sculpted again). Many rounds in a row, each landing as the app lands it, end equal to a full bake of the
// final shape. (Under ThreadSanitizer or AddressSanitizer this catches shared writes and stale reads.)
TEST(mesh_maps_live_rebake_on_a_worker_while_fill_layers_evaluate) {
    Mesh m = trenchPart();
    const MeshMapSettings st = quickMaps();
    auto maps = std::make_shared<MeshMapSet>();
    std::string err;
    CHECK(bakeMeshMaps(m, 1024, 1024, kAllMeshMaps, st, *maps, err));
    m.texture->meshMaps = maps;
    FillLayer wear;
    wear.material.channels = channelBit(Channel::BaseColor) | channelBit(Channel::Roughness);
    MaskEffect e;
    e.generator = GeneratorKind::EdgeWear;
    e.level = 0.6f;
    wear.effects.push_back(e);
    MaskEffect blur;
    blur.kind = MaskEffectKind::Blur;
    blur.radius = 4.0f;
    wear.effects.push_back(blur);
    addFillLayer(m, *m.texture, "Edges", wear);
    int landed = 0;
    for (int round = 0; round < 6; ++round) {
        // Sculpt a dent somewhere new.
        const glm::vec2 c(-0.6f + 0.24f * float(round), 0.5f - 0.2f * float(round));
        for (glm::vec3& p : m.positions) {
            const float d = glm::distance(glm::vec2(p.x, p.z), c);
            if (d < 0.12f) p.y -= 0.05f * (0.5f + 0.5f * std::cos(d / 0.12f * 3.14159265f));
        }
        m.recomputeAllNormals();
        std::vector<uint32_t> moved;
        const AABB region = changedRegion(*m.texture->meshMaps, m, &moved);
        CHECK(region.valid());
        auto up = std::make_shared<MeshMapUpdate>();
        if (!prepareMeshMapUpdate(m, m.texture->meshMaps, region, std::move(moved), *up)) continue;
        std::atomic<bool> done{false};
        std::thread worker([up, &done] {
            runMeshMapUpdate(*up);
            done = true;
        });
        // Meanwhile: the Fill layers evaluate from the maps being updated, and a command replaces the texture
        // (its undo step keeps the old one, which then goes).
        int evaluations = 0;
        do {
            std::shared_ptr<PaintTexture> old = m.texture;
            PaintTexture& t = mutableTexture(m);
            evaluateFillLayers(m, t, nullptr);
            old.reset();
            ++evaluations;
        } while (!done && evaluations < 50);
        worker.join();
        CHECK(up->ok);
        if (m.texture->meshMaps != up->base) continue; // (as the app: only onto the set it was made from)
        std::shared_ptr<MeshMapSet> set = applyMeshMapUpdate(*up);
        CHECK(set != nullptr);
        mutableTexture(m).meshMaps = std::move(set);
        evaluateFillLayers(m, *m.texture, &up->tiles);
        ++landed;
    }
    CHECK(landed >= 5);
    CHECK(!changedRegion(*m.texture->meshMaps, m).valid());
    MeshMapSet full;
    CHECK(bakeMeshMaps(m, 1024, 1024, kAllMeshMaps, st, full, err));
    size_t differ = 0;
    const MeshMapSet& live = *m.texture->meshMaps;
    for (int k = 0; k < kMeshMapCount; ++k) {
        if (MeshMapKind(k) == MeshMapKind::Position) continue; // (keeps the first bake's box)
        for (int y = 0; y < 1024; y += 3)
            for (int x = 0; x < 1024; x += 3) {
                float a[4] = {}, b[4] = {};
                live.images[size_t(k)].values(x, y, a);
                full.images[size_t(k)].values(x, y, b);
                for (int i = 0; i < 3; ++i)
                    if (std::abs(a[i] - b[i]) > 1.0f / 255.0f) {
                        ++differ;
                        break;
                    }
            }
    }
    std::printf("  %d of 6 updates landed; %zu sampled texels differ from a full bake\n", landed, differ);
    CHECK(differ == 0);
}
