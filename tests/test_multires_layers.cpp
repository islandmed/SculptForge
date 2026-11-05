// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt layers on subdivision levels 3D Layers: a layer recorded at the top level survives stepping
// down and up, Divide, Reconstruct Subdiv, Delete Lower / Delete Higher, saving and loading, and stays togglable and
// blendable at every level; layers added or sculpted at a low level show at the top; undo across levels; blend
// shapes export from the current level.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "io/GltfIO.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) {
    CommandResult r = registry().execute(d, name, a);
    if (!r.ok) std::printf("  (%s: %s)\n", name.c_str(), r.message.c_str());
    return r;
}

// `passes` strokes towards -z through `points`.
bool stroke(Document& d, const std::string& points, const std::string& brush = "Standard", double radius = 0.25,
            double strength = 1.0, int passes = 4) {
    bool ok = true;
    for (int i = 0; i < passes; ++i)
        ok &= run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", strength}}).ok;
    return ok;
}

float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::length(a[i] - b[i]));
    return m;
}

float maxLength(const std::vector<glm::vec3>& a) {
    float m = 0.0f;
    for (const auto& v : a) m = std::max(m, glm::length(v));
    return m;
}

bool bitEqual(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0);
}

// positions == base + sum(weight * delta)
float layerMismatch(const Mesh& m) {
    if (m.layers.empty()) return 0.0f;
    float worst = 0.0f;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 p = m.base[v];
        for (const SculptLayer& l : m.layers) p += l.weight * l.delta[v];
        worst = std::max(worst, glm::length(p - m.positions[v]));
    }
    return worst;
}

// A cube divided `levels` times (Catmull-Clark), a base stroke, then a layer "Smile" recorded at the top.
Document layeredCube(int levels) {
    Document d = makeDoc(makeCube(3));
    for (int i = 0; i < levels; ++i) run(d, "subdiv_divide");
    stroke(d, "-0.4,0.3,2 0.4,0.3,2", "Standard", 0.2);
    run(d, "layer_add", {{"name", "Smile"}});
    d.activeLayer = 0;
    stroke(d, "0,-0.2,2", "Standard", 0.3);
    stroke(d, "-0.3,-0.3,2 0.3,-0.35,2", "Crease", 0.1);
    return d;
}

} // namespace

TEST(layers_levels_record_at_top_step_down_and_up) {
    Document d = layeredCube(3);
    CHECK(subdivLevelText(d.mesh) == "SDiv 4/4");
    CHECK(d.mesh.layers.size() == 1);
    const std::vector<glm::vec3> top = d.mesh.positions, base = d.mesh.base, delta = d.mesh.layers[0].delta;
    CHECK(maxLength(delta) > 0.02f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    for (int level = 3; level >= 1; --level) {
        CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
        CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].name == "Smile");
        CHECK(layerMismatch(d.mesh) < 1e-5f);
    }
    // The low level shows the layer too (its broad part), and can switch it off and on.
    const std::vector<glm::vec3> low = d.mesh.positions;
    CHECK(maxLength(d.mesh.layers[0].delta) > 0.0f);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, d.mesh.base) < 1e-6f);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 1.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, low) < 1e-5f);
    CHECK(run(d, "subdiv_level", {{"level", 4.0}}).ok);
    // Untouched below: back bit for bit.
    CHECK(bitEqual(d.mesh.positions, top));
    CHECK(bitEqual(d.mesh.base, base));
    CHECK(bitEqual(d.mesh.layers[0].delta, delta));
}

TEST(layers_levels_low_level_shows_the_layer_as_the_base_would) {
    // The same strokes at the top into a layer or into the base: every lower level shows the layer exactly as it
    // would show the base change (the layer is restricted to the low levels the way the shape is).
    Document a = makeDoc(makeCube(3)), b = makeDoc(makeCube(3));
    for (Document* d : {&a, &b}) {
        run(*d, "subdiv_divide");
        run(*d, "subdiv_divide");
    }
    std::vector<std::vector<glm::vec3>> plain(3);
    for (int level = 2; level >= 0; --level) plain[size_t(level)] = subdivLevelMesh(b.mesh, level).positions;
    CHECK(run(a, "layer_add", {{"name", "Smile"}}).ok);
    a.activeLayer = 0;
    for (Document* d : {&a, &b}) {
        stroke(*d, "0,-0.2,2", "Standard", 0.3);
        stroke(*d, "-0.3,-0.3,2 0.3,-0.35,2", "Crease", 0.1);
    }
    CHECK(maxDistance(a.mesh.positions, b.mesh.positions) < 1e-5f);
    for (int level = 2; level >= 1; --level) {
        CHECK(run(a, "subdiv_level", {{"level", double(level)}}).ok);
        CHECK(run(b, "subdiv_level", {{"level", double(level)}}).ok);
        std::vector<glm::vec3> change(b.mesh.positions.size());
        for (size_t v = 0; v < change.size(); ++v) change[v] = b.mesh.positions[v] - plain[size_t(level - 1)][v];
        std::printf("  SDiv %d: layer %.4f, base change %.4f, differ by %.2g; base untouched %.2g\n", level,
                    maxLength(a.mesh.layers[0].delta), maxLength(change), maxDistance(a.mesh.layers[0].delta, change),
                    maxDistance(a.mesh.base, plain[size_t(level - 1)]));
        CHECK(maxDistance(a.mesh.layers[0].delta, change) < 1e-4f);
        CHECK(maxDistance(a.mesh.base, plain[size_t(level - 1)]) < 1e-5f);
        CHECK(maxDistance(a.mesh.positions, b.mesh.positions) < 1e-4f);
    }
}

TEST(layers_levels_weights_blend_at_every_level) {
    Document d = layeredCube(2);
    const std::vector<glm::vec3> base = d.mesh.base, delta = d.mesh.layers[0].delta;
    // Half the layer, set at the bottom: the top shows half of it.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.5}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.layers[0].weight == 0.5f);
    CHECK(maxDistance(d.mesh.base, base) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    // Exaggerated (weight 2) and off at the top.
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 2.0}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
}

TEST(layers_levels_divide_with_layers) {
    Document d = layeredCube(2);
    const std::vector<glm::vec3> top = d.mesh.positions;
    const size_t tris = d.mesh.triangleCount();
    CHECK(run(d, "subdiv_divide").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 4/4");
    CHECK(d.mesh.triangleCount() == tris * 4);
    CHECK(d.mesh.layers.size() == 1);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    // The new level carries the layer: switching it off at the new top takes the stroke away.
    const float on = maxLength(d.mesh.layers[0].delta);
    std::printf("  layer after Divide: %.4f\n", on);
    CHECK(on > 0.02f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f); // the level divided from is as it was
    // Divide from a lower level steps to the top first.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "subdiv_divide").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 5/5");
    CHECK(d.mesh.layers.size() == 1 && layerMismatch(d.mesh) < 1e-5f);
    // A layer added at the top after dividing, and one more division.
    CHECK(run(d, "layer_add", {{"name", "Blink"}}).ok);
    d.activeLayer = 1;
    CHECK(stroke(d, "0.3,0.3,2", "Inflate", 0.15));
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(d.mesh.layers.size() == 2 && layerMismatch(d.mesh) < 1e-5f);
    CHECK(maxLength(d.mesh.layers[1].delta) > 0.0f);
}

TEST(layers_levels_added_or_sculpted_low_show_at_the_top) {
    Document d = layeredCube(2);
    const std::vector<glm::vec3> topDelta = d.mesh.layers[0].delta;
    // A new layer added and sculpted at SDiv 1.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_add", {{"name", "Jaw"}}).ok);
    d.activeLayer = 1;
    CHECK(stroke(d, "-0.2,-0.4,2 0.2,-0.4,2", "Standard", 0.4));
    const float lowJaw = maxLength(d.mesh.layers[1].delta);
    CHECK(lowJaw > 0.01f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.layers.size() == 2 && d.mesh.layers[1].name == "Jaw");
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    std::printf("  Jaw: %.3f at SDiv 1, %.3f at the top\n", lowJaw, maxLength(d.mesh.layers[1].delta));
    CHECK(maxLength(d.mesh.layers[1].delta) > 0.5f * lowJaw);
    CHECK(maxDistance(d.mesh.layers[0].delta, topDelta) < 1e-5f); // the other layer is untouched
    // The Smile layer sculpted further at SDiv 2 keeps its top-level detail and adds the broad change.
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    d.activeLayer = 0;
    CHECK(stroke(d, "0.35,0.35,2 0.45,0.45,2", "Standard", 0.3));
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    float changed = 0.0f, kept = 0.0f;
    for (size_t v = 0; v < topDelta.size(); ++v) {
        const float dv = glm::length(d.mesh.layers[0].delta[v] - topDelta[v]);
        if (d.mesh.positions[v].x > 0.2f && d.mesh.positions[v].y > 0.2f) changed = std::max(changed, dv);
        else if (d.mesh.positions[v].x < -0.2f) kept = std::max(kept, dv);
    }
    std::printf("  Smile after a stroke at SDiv 2: %.3f where it went, %.2g far away\n", changed, kept);
    CHECK(changed > 0.01f && kept < 1e-4f);
    // Removing a layer at a low level removes it everywhere.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_remove", {{"index", 1.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].name == "Smile");
    CHECK(layerMismatch(d.mesh) < 1e-5f);
}

TEST(layers_levels_base_edit_low_keeps_the_layer_on_top) {
    // Reshape the base at SDiv 1 (no layer active): the top keeps its layer, riding on the new shape.
    Document d = layeredCube(2);
    const std::vector<glm::vec3> delta = d.mesh.layers[0].delta;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    d.activeLayer = -1;
    const std::vector<glm::vec3> lowBase = d.mesh.base;
    CHECK(stroke(d, "-0.3,-0.4,2 0.3,-0.4,2", "Standard", 0.5));
    CHECK(maxDistance(d.mesh.base, lowBase) > 0.02f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    // The layer's offsets only turn with the surface: their lengths stay.
    float worst = 0.0f;
    for (size_t v = 0; v < delta.size(); ++v)
        worst = std::max(worst, std::abs(glm::length(d.mesh.layers[0].delta[v]) - glm::length(delta[v])));
    std::printf("  layer offset lengths changed by %.2g at most\n", worst);
    CHECK(worst < 0.25f * maxLength(delta));
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, d.mesh.base) < 1e-6f);
}

TEST(layers_levels_reconstruct_with_layers) {
    // A divided mesh imported as a plain mesh with a layer (as from a glTF with morph targets): Reconstruct
    // finds the lower levels and the layer comes along, exact at the top.
    Document src = layeredCube(2);
    Mesh plain = subdivLevelMesh(src.mesh, 2);
    CHECK(plain.layers.size() == 1);
    const std::vector<glm::vec3> top = plain.positions, delta = plain.layers[0].delta;
    Document d = makeDoc(std::move(plain));
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/2");
    CHECK(d.mesh.layers.size() == 1 && layerMismatch(d.mesh) < 1e-5f);
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/3");
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, d.mesh.base) < 1e-6f);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 1.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f);
}

TEST(layers_levels_delete_lower_and_higher) {
    Document d = layeredCube(3);
    const std::vector<glm::vec3> top = d.mesh.positions, delta = d.mesh.layers[0].delta;
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    const std::vector<glm::vec3> level2 = d.mesh.positions, delta2 = d.mesh.layers[0].delta;
    CHECK(run(d, "subdiv_delete_lower").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/3");
    CHECK(maxDistance(d.mesh.positions, level2) < 1e-6f);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta2) < 1e-6f);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    // Delete Higher at SDiv 2: that level, layer included, becomes the top.
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    const std::vector<glm::vec3> mid = d.mesh.positions, midDelta = d.mesh.layers[0].delta;
    CHECK(run(d, "subdiv_delete_higher").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 2/2");
    CHECK(maxDistance(d.mesh.positions, mid) < 1e-6f && maxDistance(d.mesh.layers[0].delta, midDelta) < 1e-6f);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, mid) < 1e-5f && layerMismatch(d.mesh) < 1e-5f);
    // Undo brings both back.
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 2/3");
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f && maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
}

TEST(layers_levels_undo_across_levels) {
    Document d = layeredCube(2);
    const std::vector<glm::vec3> top = d.mesh.positions, delta = d.mesh.layers[0].delta;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    d.activeLayer = 0;
    const std::vector<glm::vec3> lowBefore = d.mesh.layers[0].delta;
    CHECK(stroke(d, "-0.2,-0.4,2 0.2,-0.4,2", "Standard", 0.4));
    std::printf("  low stroke into the layer: %.4f at SDiv 1\n", maxDistance(d.mesh.layers[0].delta, lowBefore));
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    std::printf("  and %.4f at the top\n", maxDistance(d.mesh.layers[0].delta, delta));
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) > 1e-2f);
    // Undo the level step and the low stroke (four passes): back to the top as it was.
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/3");
    for (int i = 0; i < 4; ++i) CHECK(run(d, "undo").ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, delta) < 1e-5f);
    // Undoing the layer's creation at the top (with levels) removes it everywhere.
    Document e = makeDoc(makeCube(3));
    run(e, "subdiv_divide");
    CHECK(run(e, "layer_add", {{"name", "Temp"}}).ok);
    CHECK(run(e, "undo").ok);
    CHECK(e.mesh.layers.empty());
    CHECK(run(e, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(e.mesh.layers.empty());
}

TEST(layers_levels_project_round_trip) {
    Document d = layeredCube(3);
    run(d, "layer_add", {{"name", "Blink"}});
    d.activeLayer = 1;
    stroke(d, "0.3,0.4,2", "Inflate", 0.15);
    run(d, "layer_set_weight", {{"index", 1.0}, {"weight", 0.7}});
    std::vector<std::vector<glm::vec3>> pos(5), del0(5), del1(5);
    for (int level = 4; level >= 1; --level) {
        CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
        pos[size_t(level)] = d.mesh.positions;
        del0[size_t(level)] = d.mesh.layers[0].delta;
        del1[size_t(level)] = d.mesh.layers[1].delta;
    }
    // Saved at the bottom (the levels above stored as they are) and at the top.
    for (int savedAt : {1, 4}) {
        CHECK(run(d, "subdiv_level", {{"level", double(savedAt)}}).ok);
        const std::string path = tempPath("sf_layers_levels.sfproj");
        std::string err;
        CHECK(saveProject(path, d, err));
        Document e;
        CHECK(loadProject(path, e, err));
        std::filesystem::remove(path);
        CHECK(subdivLevelCount(e.mesh) == 4 && subdivCurrentLevel(e.mesh) == savedAt - 1);
        CHECK(e.mesh.layers.size() == 2 && e.mesh.layers[1].name == "Blink" && e.mesh.layers[1].weight == 0.7f);
        float worst = 0.0f;
        for (int level = 1; level <= 4; ++level) {
            CHECK(run(e, "subdiv_level", {{"level", double(level)}}).ok);
            worst = std::max({worst, maxDistance(e.mesh.positions, pos[size_t(level)]),
                              maxDistance(e.mesh.layers[0].delta, del0[size_t(level)]),
                              maxDistance(e.mesh.layers[1].delta, del1[size_t(level)])});
            CHECK(layerMismatch(e.mesh) < 1e-5f);
        }
        std::printf("  saved at SDiv %d: levels and layers within %.2g after loading\n", savedAt, worst);
        CHECK(worst < 1e-5f);
    }
}

TEST(layers_levels_export_blend_shapes) {
    // Export writes the current level with its layers as morph targets: the top for the game mesh, a low level
    // for a light one.
    Document d = layeredCube(2);
    for (int level : {3, 1}) {
        CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
        const std::string path = tempPath("sf_layers_levels.glb");
        CHECK(run(d, "export_glb", {{"path", path}}).ok);
        Mesh m;
        std::string err;
        CHECK(loadGltf(path, m, err));
        std::filesystem::remove(path);
        CHECK(m.vertexCount() == d.mesh.vertexCount());
        CHECK(m.layers.size() == 1 && m.layers[0].name == "Smile");
        if (m.layers.size() != 1) continue;
        // Same offsets (by position: the importer may reorder vertices).
        double sumA = 0.0, sumB = 0.0;
        for (const auto& v : m.layers[0].delta) sumA += glm::length(v);
        for (const auto& v : d.mesh.layers[0].delta) sumB += glm::length(v);
        std::printf("  SDiv %d: %zu vertices, morph target total %.4f vs layer %.4f\n", level, m.vertexCount(), sumA, sumB);
        CHECK(std::abs(sumA - sumB) < 1e-3 * std::max(sumB, 1.0));
        CHECK(std::abs(maxLength(m.layers[0].delta) - maxLength(d.mesh.layers[0].delta)) < 1e-4f);
    }
}


TEST(layers_levels_subdiv_export_other_levels) {
    // subdiv_export writes another level than the current one with its blend shapes: the base cage from the
    // top, and the top from the base (where the levels above wait to be re-evaluated).
    Document d = layeredCube(2);
    std::vector<std::vector<glm::vec3>> delta(4);
    for (int level = 3; level >= 1; --level) {
        CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
        delta[size_t(level)] = d.mesh.layers[0].delta;
    }
    d.activeLayer = -1;
    CHECK(stroke(d, "-0.2,-0.4,2 0.2,-0.4,2", "Standard", 0.4)); // the levels above are stale now
    for (int from : {1, 3})
        for (int level : {1, 3}) {
            if (from == level) continue;
            CHECK(run(d, "subdiv_level", {{"level", double(from)}}).ok);
            Mesh expect = subdivLevelMesh(d.mesh, level - 1);
            CHECK(expect.layers.size() == 1);
            if (expect.layers.size() != 1) continue;
            const std::string path = tempPath("sf_layers_levels_export.glb");
            CHECK(run(d, "subdiv_export", {{"path", path}, {"level", double(level)}}).ok);
            Mesh m;
            std::string err;
            CHECK(loadGltf(path, m, err));
            std::filesystem::remove(path);
            CHECK(m.vertexCount() == expect.vertexCount());
            CHECK(m.layers.size() == 1);
            if (m.layers.size() != 1) continue;
            double sumA = 0.0, sumB = 0.0;
            for (const auto& v : m.layers[0].delta) sumA += glm::length(v);
            for (const auto& v : expect.layers[0].delta) sumB += glm::length(v);
            std::printf("  from SDiv %d, SDiv %d: morph target total %.4f, layer %.4f (as recorded %.4f)\n", from, level,
                        sumA, sumB, [&] {
                            double t = 0.0;
                            for (const auto& v : delta[size_t(level)]) t += glm::length(v);
                            return t;
                        }());
            CHECK(std::abs(sumA - sumB) < 1e-3 * std::max(sumB, 1.0));
            CHECK(maxDistance(expect.layers[0].delta, delta[size_t(level)]) < 0.25f * maxLength(delta[size_t(level)]));
        }
}
