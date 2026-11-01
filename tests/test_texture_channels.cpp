// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material channels of a painted texture (core/TextureChannels.h, core/TileImage.h): sparse copy-on-write tiles,
// enabling channels, material dabs on Base and on layers, per-(layer, channel) undo, per-channel blend modes
// with Normal Combine, height-to-normal, the solo channel view, layer operations on channels, the commands, and
// what a 5-channel dab costs next to a base-colour dab.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Texture.h"
#include "core/TextureChannels.h"
#include "core/TileImage.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sf;

namespace {

void planarUvs(Mesh& m) {
    const AABB b = m.bounds();
    const glm::vec3 e = b.extent();
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = m.positions[m.triangles[t][c]];
            m.uvs[t * 3 + size_t(c)] = glm::vec2((p.x - b.min.x) / e.x, (p.z - b.min.z) / e.z);
        }
}

void sphericalUvs(Mesh& m) {
    m.uvs.resize(m.triangleCount() * 3);
    for (size_t t = 0; t < m.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = glm::normalize(m.positions[m.triangles[t][c]]);
            m.uvs[t * 3 + size_t(c)] = glm::vec2(0.5f + std::atan2(p.z, p.x) / 6.2831853f, 0.5f + std::asin(p.y) / 3.1415927f);
        }
}

// A plane facing +Y with planar UVs and a texture.
Mesh texturedPlane(int resolution, int size) {
    Mesh m = makePlane(resolution);
    planarUvs(m);
    m.texture = makeTexture(size, size);
    m.paintToTexture = true;
    return m;
}

DabParams dab(const glm::vec3& center, float radius, float strength = 1.0f) {
    DabParams p;
    p.center = center;
    p.viewDir = glm::vec3(0, -1, 0);
    p.radius = radius;
    p.strength = strength;
    return p;
}

BrushMaterial rustyMetal() {
    BrushMaterial m;
    m.color = glm::vec3(0.6f, 0.2f, 0.05f);
    m.roughness = 0.2f;
    m.metallic = 1.0f;
    m.height = 0.8f;
    m.ao = 0.4f;
    m.channels = channelBit(Channel::BaseColor) | channelBit(Channel::Roughness) | channelBit(Channel::Metallic) |
                 channelBit(Channel::Height) | channelBit(Channel::AO);
    return m;
}

// Paints one dab of `m` at the plane's centre (one stroke) and returns its undo record.
UndoRecord paintCentre(Mesh& mesh, const BVH& bvh, const BrushMaterial& m, float radius = 0.4f) {
    TexturePainter painter;
    CHECK(painter.begin(mesh, m.channels));
    CHECK(painter.dab(mesh, bvh, dab(glm::vec3(0, 0, 0), radius), m, Symmetry{}) > 0);
    UndoRecord r;
    r.label = "Paint";
    painter.end(r);
    return r;
}

float centreValue(const PaintTexture& t, Channel c, int comp = 0) {
    return channelImage(t, c).value(t.width / 2, t.height / 2, comp);
}

} // namespace

TEST(tile_image_is_sparse_and_copy_on_write) {
    TileImage img;
    img.reset(256, 256, 1, 8, TileImage::toUnits(8, 0.5f));
    CHECK(img.tilesX() == 4 && img.tilesY() == 4);
    CHECK(img.empty());
    CHECK(img.denseTiles() == 0);
    CHECK(std::abs(img.value(10, 10) - 0.5f) < 0.01f);
    const float v[1] = {1.0f};
    img.setValues(70, 3, v);
    CHECK(img.denseTiles() == 1);
    CHECK(img.value(70, 3) == 1.0f);
    CHECK(std::abs(img.value(71, 3) - 0.5f) < 0.01f);
    // A copy shares the tile until one side writes.
    TileImage copy = img;
    CHECK(copy.tilePtr(1) == img.tilePtr(1));
    const float z[1] = {0.0f};
    copy.setValues(70, 3, z);
    CHECK(copy.tilePtr(1) != img.tilePtr(1));
    CHECK(img.value(70, 3) == 1.0f);
    CHECK(copy.value(70, 3) == 0.0f);
    // Compacting: a tile of one value becomes uniform, one equal to the fill becomes null.
    for (int y = 0; y < 64; ++y)
        for (int x = 64; x < 128; ++x) copy.setValues(x, y, z);
    copy.compact();
    CHECK(copy.denseTiles() == 0);
    CHECK(!copy.empty());
    CHECK(copy.value(100, 10) == 0.0f);
    const float h[1] = {0.5f};
    for (int y = 0; y < 64; ++y)
        for (int x = 64; x < 128; ++x) copy.setValues(x, y, h);
    copy.compact();
    CHECK(copy.empty());
    // 16 bits keep fine steps; dense() / assignDense() round trip.
    TileImage h16;
    h16.reset(100, 70, 2, 16, TileImage::toUnits(16, 0.5f, 0.0f));
    const float fine[2] = {0.500015f, 1.0f};
    h16.setValues(99, 69, fine);
    CHECK(std::abs(h16.value(99, 69) - 0.500015f) < 2e-5f);
    const std::vector<uint8_t> d = h16.dense();
    CHECK(d.size() == size_t(100) * 70 * 4);
    TileImage back;
    back.reset(100, 70, 2, 16, h16.fill());
    back.assignDense(d);
    CHECK(back.sameTexels(h16));
    CHECK(back.denseTiles() == 1);
    // Bilinear sampling between two values.
    TileImage s;
    s.reset(2, 1, 1, 8, TileImage::toUnits(8, 0.0f));
    s.setValues(1, 0, v);
    float out[4] = {};
    s.sample(0.5f, 0.5f, out);
    CHECK(std::abs(out[0] - 0.5f) < 0.01f);
}

TEST(texture_channels_enable_and_cost_nothing_until_painted) {
    Mesh m = texturedPlane(16, 1024);
    PaintTexture& t = *m.texture;
    CHECK(!hasMaterialChannels(t));
    CHECK(enabledChannelMask(t) == 1u);
    const size_t before = t.bytes();
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Normal, Channel::Height, Channel::AO, Channel::Emissive,
                      Channel::Opacity})
        CHECK(setChannelEnabled(t, c, true));
    CHECK(!setChannelEnabled(t, Channel::Roughness, true)); // already on
    CHECK(!setChannelEnabled(t, Channel::BaseColor, false));
    CHECK(hasMaterialChannels(t));
    CHECK(enabledChannelMask(t) == 0xFFu);
    CHECK(t.bytes() - before < 64 * 1024); // tile tables only
    CHECK(std::abs(centreValue(t, Channel::Roughness) - 0.65f) < 0.01f);
    CHECK(centreValue(t, Channel::Metallic) == 0.0f);
    CHECK(std::abs(centreValue(t, Channel::Height) - 0.5f) < 1e-4f);
    CHECK(centreValue(t, Channel::AO) == 1.0f);
    CHECK(channelImage(t, Channel::Height).bits() == 16);
    CHECK(channelImage(t, Channel::Normal).components() == 3);
    // Effective normal of an unpainted texture is flat.
    const glm::vec3 n = effectiveNormalAt(t, 100, 100);
    CHECK(std::abs(n.z - 1.0f) < 1e-3f);
    CHECK(setChannelEnabled(t, Channel::Opacity, false));
    CHECK(!channelEnabled(t, Channel::Opacity));
}

TEST(material_dab_paints_every_enabled_channel_and_undoes_per_channel) {
    Document doc = makeDoc(texturedPlane(32, 1024));
    Mesh& m = doc.mesh;
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Height, Channel::AO}) setChannelEnabled(*m.texture, c, true);
    doc.syncBvh();
    BrushMaterial mat = rustyMetal();
    mat.channels |= channelBit(Channel::Emissive); // not enabled on the texture: skipped
    UndoRecord r = paintCentre(m, doc.bvh, mat);
    const PaintTexture& t = *m.texture;
    CHECK(std::abs(centreValue(t, Channel::Roughness) - 0.2f) < 0.02f);
    CHECK(centreValue(t, Channel::Metallic) > 0.98f);
    CHECK(std::abs(centreValue(t, Channel::Height) - 0.8f) < 0.01f);
    CHECK(std::abs(centreValue(t, Channel::AO) - 0.4f) < 0.02f);
    CHECK(!channelEnabled(t, Channel::Emissive));
    const uint8_t* px = &t.pixels[(size_t(512) * 1024 + 512) * 4];
    CHECK(px[0] > px[1] && px[1] > px[2]);
    // Far from the dab nothing changed, and the images stay sparse.
    CHECK(std::abs(channelImage(t, Channel::Roughness).value(5, 5) - 0.65f) < 0.01f);
    CHECK(channelImage(t, Channel::Roughness).denseTiles() < channelImage(t, Channel::Roughness).tileCount() / 2);
    // One record: base colour tiles plus one entry per (layer, channel).
    CHECK(r.textureTiles != nullptr);
    CHECK(!r.textureTiles->texels.tiles.empty());
    CHECK(r.textureTiles->channels.size() == 4);
    for (const auto& ct : r.textureTiles->channels) CHECK(ct.layer == -1);
    doc.undo.push(std::move(r));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.65f) < 0.01f);
    CHECK(centreValue(*m.texture, Channel::Metallic) == 0.0f);
    CHECK(std::abs(centreValue(*m.texture, Channel::Height) - 0.5f) < 1e-4f);
    CHECK(m.texture->pixels[(size_t(512) * 1024 + 512) * 4] == 255);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.2f) < 0.02f);
    CHECK(std::abs(centreValue(*m.texture, Channel::Height) - 0.8f) < 0.01f);

    // A stroke on roughness only leaves the colour (and its undo) alone.
    BrushMaterial glossy;
    glossy.roughness = 0.0f;
    glossy.channels = channelBit(Channel::Roughness);
    const std::vector<uint8_t> colour = m.texture->pixels;
    UndoRecord r2 = paintCentre(m, doc.bvh, glossy, 0.2f);
    CHECK(m.texture->pixels == colour);
    CHECK(r2.textureTiles && r2.textureTiles->texels.tiles.empty() && r2.textureTiles->channels.size() == 1);
    CHECK(centreValue(*m.texture, Channel::Roughness) < 0.02f);
    doc.undo.push(std::move(r2));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.2f) < 0.02f);

    // Erasing (invert) on Base paints back to the defaults.
    {
        TexturePainter p;
        CHECK(p.begin(m, mat.channels));
        DabParams d = dab(glm::vec3(0), 0.4f);
        d.invert = true;
        p.dab(m, doc.bvh, d, mat, Symmetry{});
        UndoRecord e;
        p.end(e);
        CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.65f) < 0.02f);
        CHECK(std::abs(centreValue(*m.texture, Channel::Height) - 0.5f) < 0.01f);
    }
}

TEST(material_layers_composite_per_channel_with_blend_modes) {
    Document doc = makeDoc(texturedPlane(32, 512));
    Mesh& m = doc.mesh;
    PaintTexture& t0 = *m.texture;
    setChannelEnabled(t0, Channel::Roughness, true);
    setChannelEnabled(t0, Channel::Normal, true);
    doc.syncBvh();
    // Base roughness 0.8.
    BrushMaterial rough;
    rough.roughness = 0.8f;
    rough.channels = channelBit(Channel::Roughness);
    paintCentre(m, doc.bvh, rough, 2.0f);
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.8f) < 0.01f);
    // A layer that multiplies roughness by 0.5.
    const int li = addTextureLayer(*m.texture, "Wet");
    CHECK(li == 0);
    CHECK(m.texture->layers[0].channels[size_t(Channel::Roughness)].image.valid());
    CHECK(m.texture->layers[0].channels[size_t(Channel::Roughness)].image.empty()); // transparent, no tiles
    m.texture->layers[0].channels[size_t(Channel::Roughness)].blend = uint8_t(BlendMode::Multiply);
    BrushMaterial half;
    half.roughness = 0.5f;
    half.channels = channelBit(Channel::Roughness);
    UndoRecord r = paintCentre(m, doc.bvh, half, 0.3f);
    CHECK(r.textureTiles && r.textureTiles->channels.size() == 1 && r.textureTiles->channels[0].layer == 0);
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.4f) < 0.02f);
    // Base stays 0.8 under the layer; away from the stroke the composite shares Base's tiles.
    CHECK(std::abs(m.texture->channels[size_t(Channel::Roughness)].base.value(256, 256) - 0.8f) < 0.01f);
    const TileImage& comp = m.texture->channels[size_t(Channel::Roughness)].composite;
    CHECK(comp.valid());
    CHECK(comp.tilePtr(0) == m.texture->channels[size_t(Channel::Roughness)].base.tilePtr(0));
    // Channel opacity and turning the channel off on the layer.
    m.texture->layers[0].channels[size_t(Channel::Roughness)].opacity = 0.5f;
    compositeTextureAll(*m.texture);
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.6f) < 0.02f);
    m.texture->layers[0].channels[size_t(Channel::Roughness)].enabled = false;
    compositeTextureAll(*m.texture);
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.8f) < 0.01f);
    m.texture->layers[0].channels[size_t(Channel::Roughness)].enabled = true;
    m.texture->layers[0].channels[size_t(Channel::Roughness)].opacity = 1.0f;
    compositeTextureAll(*m.texture);
    // Undo of the layer stroke.
    doc.undo.push(std::move(r));
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.8f) < 0.01f);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - 0.4f) < 0.02f);

    // Normal Combine: a tilted detail on a tilted base tilts further; Normal replaces.
    const glm::vec3 base = glm::normalize(glm::vec3(0.3f, 0, 1)), detail = glm::normalize(glm::vec3(0.3f, 0, 1));
    const glm::vec3 combined = normalCombine(base, detail);
    CHECK(combined.x > base.x + 0.1f);
    CHECK(std::abs(glm::length(combined) - 1.0f) < 1e-4f);
    const glm::vec3 flat = normalCombine(glm::vec3(0, 0, 1), detail);
    CHECK(glm::length(flat - detail) < 1e-4f);
    BrushMaterial tilt;
    tilt.normal = base;
    tilt.channels = channelBit(Channel::Normal);
    m.texture->activeLayer = -1;
    paintCentre(m, doc.bvh, tilt, 2.0f);
    m.texture->activeLayer = 0;
    m.texture->layers[0].channels[size_t(Channel::Normal)].blend = kBlendNormalCombine;
    paintCentre(m, doc.bvh, tilt, 0.3f);
    float nv[4];
    channelImage(*m.texture, Channel::Normal).values(256, 256, nv);
    const glm::vec3 got = glm::normalize(decodeNormal(nv[0], nv[1], nv[2]));
    CHECK(std::abs(got.x - combined.x) < 0.03f);
    m.texture->layers[0].channels[size_t(Channel::Normal)].blend = uint8_t(BlendMode::Normal);
    compositeTextureAll(*m.texture);
    channelImage(*m.texture, Channel::Normal).values(256, 256, nv);
    CHECK(std::abs(glm::normalize(decodeNormal(nv[0], nv[1], nv[2])).x - base.x) < 0.03f);

    // Merge down keeps the look; flatten keeps it too.
    const float before = centreValue(*m.texture, Channel::Roughness);
    std::string err;
    CHECK(mergeTextureLayerDown(*m.texture, 0, err));
    CHECK(m.texture->layers.empty());
    CHECK(std::abs(centreValue(*m.texture, Channel::Roughness) - before) < 0.02f);
}

TEST(height_makes_a_normal_and_the_solo_view_shows_channels) {
    Document doc = makeDoc(texturedPlane(32, 512));
    Mesh& m = doc.mesh;
    setChannelEnabled(*m.texture, Channel::Height, true);
    setChannelEnabled(*m.texture, Channel::Roughness, true);
    doc.syncBvh();
    BrushMaterial bump;
    bump.height = 1.0f;
    bump.roughness = 0.1f;
    bump.channels = channelBit(Channel::Height) | channelBit(Channel::Roughness);
    paintCentre(m, doc.bvh, bump, 0.3f);
    const PaintTexture& t = *m.texture;
    // On the bump's left flank the normal tilts left (-x), on the right flank right.
    const int y = 256;
    int left = -1, right = -1;
    for (int x = 0; x < 512 && left < 0; ++x)
        if (channelImage(t, Channel::Height).value(x, y) > 0.6f) left = x;
    for (int x = 511; x >= 0 && right < 0; --x)
        if (channelImage(t, Channel::Height).value(x, y) > 0.6f) right = x;
    CHECK(left > 0 && right > left);
    CHECK(effectiveNormalAt(t, left, y).x < -0.05f);
    CHECK(effectiveNormalAt(t, right, y).x > 0.05f);
    CHECK(std::abs(effectiveNormalAt(t, 5, 5).z - 1.0f) < 1e-3f);

    // Solo view: roughness as grey.
    PaintTexture& tex = *m.texture;
    CHECK(&tex.shown() == &tex.pixels);
    syncChannelView(tex, Channel::Roughness);
    CHECK(tex.viewChannel == Channel::Roughness);
    CHECK(&tex.shown() == &tex.viewPixels);
    const uint8_t* c = &tex.shown()[(size_t(256) * 512 + 256) * 4];
    CHECK(c[0] == c[1] && c[1] == c[2] && c[0] < 40);
    const uint8_t* far = &tex.shown()[(size_t(5) * 512 + 5) * 4];
    CHECK(std::abs(int(far[0]) - 166) <= 1); // 0.65
    // Painting while the view shows roughness updates just the dirty tiles.
    tex.clearDirty();
    BrushMaterial glossy;
    glossy.roughness = 1.0f;
    glossy.channels = channelBit(Channel::Roughness);
    TexturePainter p;
    CHECK(p.begin(m, glossy.channels));
    p.dab(m, doc.bvh, dab(glm::vec3(0), 0.1f), glossy, Symmetry{});
    UndoRecord rec;
    p.end(rec);
    syncChannelView(*m.texture, Channel::Roughness);
    CHECK(m.texture->shown()[(size_t(256) * 512 + 256) * 4] > 240);
    // Normal view shows the height's normal; off goes back to the colour.
    syncChannelView(*m.texture, Channel::Normal); // not enabled: back to colour
    CHECK(&m.texture->shown() == &m.texture->pixels);
    syncChannelView(*m.texture, Channel::Height);
    CHECK(m.texture->viewChannel == Channel::Height);
    syncChannelView(*m.texture, Channel::BaseColor);
    CHECK(m.texture->viewPixels.empty());
}

TEST(material_picking_samples_every_channel) {
    Document doc = makeDoc(texturedPlane(32, 512));
    Mesh& m = doc.mesh;
    for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Height, Channel::AO}) setChannelEnabled(*m.texture, c, true);
    doc.syncBvh();
    paintCentre(m, doc.bvh, rustyMetal(), 0.5f);
    BrushMaterial picked;
    CHECK(sampleMaterialAt(m, 0, m.positions[m.triangles[0].x], picked)); // a corner: defaults
    CHECK(std::abs(picked.roughness - 0.65f) < 0.02f);
    sampleMaterial(*m.texture, glm::vec2(0.5f, 0.5f), picked);
    CHECK(std::abs(picked.roughness - 0.2f) < 0.02f);
    CHECK(picked.metallic > 0.97f);
    CHECK(std::abs(picked.height - 0.8f) < 0.01f);
    CHECK(std::abs(picked.ao - 0.4f) < 0.02f);
    CHECK(std::abs(picked.color.x - 0.6f) < 0.03f);
    CHECK(picked.channels == (channelBit(Channel::BaseColor) | channelBit(Channel::Roughness) | channelBit(Channel::Metallic) |
                              channelBit(Channel::Height) | channelBit(Channel::AO)));
}

TEST(texture_channel_commands) {
    Document doc = makeDoc(texturedPlane(16, 1024));
    CommandRegistry reg;
    CHECK(reg.execute(doc, "texture_channels").ok);
    CHECK(reg.execute(doc, "texture_channels", {{"enable", std::string("roughness, metallic,normal")}}).ok);
    CHECK(channelEnabled(*doc.mesh.texture, Channel::Normal));
    CHECK(!reg.execute(doc, "texture_channels", {{"enable", std::string("sparkle")}}).ok);
    CHECK(!reg.execute(doc, "texture_channels", {{"disable", std::string("base_color")}}).ok);
    // Undo turns them off again.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(!hasMaterialChannels(*doc.mesh.texture));
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(channelEnabled(*doc.mesh.texture, Channel::Metallic));

    // paint_material: values add their channel; channels= sets exactly.
    CommandResult r = reg.execute(doc, "paint_material", {{"roughness", 0.25}, {"metallic", 1.0}});
    CHECK(r.ok);
    CHECK(doc.paintMaterial.channels == (channelBit(Channel::BaseColor) | channelBit(Channel::Roughness) | channelBit(Channel::Metallic)));
    CHECK(std::abs(doc.paintMaterial.roughness - 0.25f) < 1e-6f);
    r = reg.execute(doc, "paint_material", {{"height", 0.7}});
    CHECK(r.ok && r.message.find("not on this texture") != std::string::npos);
    CHECK(reg.execute(doc, "paint_material", {{"channels", std::string("roughness")}}).ok);
    CHECK(doc.paintMaterial.channels == channelBit(Channel::Roughness));
    CHECK(reg.execute(doc, "paint_material", {{"normal", std::string("0.5,0,1")}}).ok);
    CHECK(doc.paintMaterial.normal.x > 0.4f);
    CHECK(!reg.execute(doc, "paint_material", {{"normal", std::string("x")}}).ok);
    CHECK(!reg.execute(doc, "paint_material", {{"channels", std::string("glitter")}}).ok);
    CHECK(reg.execute(doc, "paint_material", {{"color", std::string("#ff0000")}, {"emissive", std::string("#00ff00")}}).ok);
    CHECK(doc.paintColor.x > 0.99f);

    // channel_view needs an enabled channel.
    CHECK(reg.execute(doc, "channel_view", {{"channel", std::string("roughness")}}).ok);
    CHECK(doc.channelView == Channel::Roughness);
    CHECK(doc.mesh.texture->viewChannel == Channel::Roughness);
    CHECK(!reg.execute(doc, "channel_view", {{"channel", std::string("height")}}).ok);
    CHECK(!reg.execute(doc, "channel_view", {{"channel", std::string("shiny")}}).ok);
    CHECK(reg.execute(doc, "channel_view", {{"channel", std::string("off")}}).ok);
    CHECK(doc.channelView == Channel::BaseColor);

    // texture_layer_channel on a layer, undoable.
    CHECK(reg.execute(doc, "paint_layer_add", {{"name", std::string("Grime")}}).ok);
    CHECK(!reg.execute(doc, "texture_layer_channel", {{"channel", std::string("roughness")}, {"index", -1.0}}).ok);
    CHECK(reg.execute(doc, "texture_layer_channel",
                      {{"channel", std::string("roughness")}, {"blend", std::string("multiply")}, {"opacity", 0.5}}).ok);
    CHECK(doc.mesh.texture->layers[0].channels[size_t(Channel::Roughness)].blend == uint8_t(BlendMode::Multiply));
    CHECK(!reg.execute(doc, "texture_layer_channel", {{"channel", std::string("roughness")}, {"blend", std::string("normal_combine")}}).ok);
    CHECK(reg.execute(doc, "texture_layer_channel", {{"channel", std::string("normal")}, {"blend", std::string("normal_combine")}}).ok);
    CHECK(reg.execute(doc, "texture_layer_channel", {{"channel", std::string("base_color")}, {"enabled", false}}).ok);
    CHECK(!doc.mesh.texture->layers[0].channels[0].enabled);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.texture->layers[0].channels[0].enabled);
    // Layer copies carry their channel settings; disabling a channel drops it from layers too.
    CHECK(reg.execute(doc, "paint_layer_duplicate").ok);
    CHECK(doc.mesh.texture->layers.size() == 2);
    CHECK(doc.mesh.texture->layers[1].channels[size_t(Channel::Normal)].blend == kBlendNormalCombine);
    CHECK(reg.execute(doc, "texture_channels", {{"disable", std::string("normal")}}).ok);
    CHECK(!doc.mesh.texture->layers[0].channels[size_t(Channel::Normal)].image.valid());
    // No texture: clear errors.
    Document plain = makeDoc(makePlane(4));
    CHECK(!reg.execute(plain, "texture_channels").ok);
    CHECK(!reg.execute(plain, "channel_view", {{"channel", std::string("roughness")}}).ok);
}

TEST(material_dab_cost_against_base_colour_dab) {
    // 4K texture on a dense sphere, a big brush with X symmetry (as texture_stroke_benchmark): the 5-channel dab
    // (colour, roughness, metallic, height, AO) against the colour-only dab. Target: at most 2.5x.
    Mesh base = makeIcosphere(7);
    sphericalUvs(base);
    BVH bvh;
    bvh.build(base);
    double ms[2] = {0, 0};
    for (int pass = 0; pass < 2; ++pass) {
        Mesh m = base;
        m.texture = makeTexture(4096, 4096);
        m.paintToTexture = true;
        BrushMaterial mat = rustyMetal();
        if (pass == 0) {
            mat.channels = channelBit(Channel::BaseColor);
        } else {
            for (Channel c : {Channel::Roughness, Channel::Metallic, Channel::Height, Channel::AO}) setChannelEnabled(*m.texture, c, true);
        }
        TexturePainter painter;
        CHECK(painter.begin(m, mat.channels));
        DabParams p = dab(glm::vec3(0, 0, 1), 0.35f, 0.5f);
        p.viewDir = glm::vec3(0, 0, -1);
        for (int i = 0; i < 12; ++i) {
            p.center = glm::normalize(glm::vec3(-0.15f + 0.025f * float(i), 0.2f, 1.0f));
            const auto t0 = std::chrono::steady_clock::now();
            painter.dab(m, bvh, p, mat, Symmetry{true, false, false});
            ms[pass] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        UndoRecord rec;
        painter.end(rec);
        ms[pass] /= 12.0;
        if (pass == 1) std::printf("  4K big dab: base colour %.2f ms, 5 channels %.2f ms (%.2fx); channel memory %.1f MB\n",
                                   ms[0], ms[1], ms[1] / ms[0], double(channelBytes(*m.texture)) / 1048576.0);
    }
    CHECK(ms[0] > 0 && ms[1] > 0);
    if (std::getenv("SF_BENCH_LIMITS")) CHECK(ms[1] <= 2.5 * ms[0]);
}
