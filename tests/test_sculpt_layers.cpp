// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt layer stack tools (core/SculptLayers.h): move, rename, duplicate, merge down, invert, show / hide, weight
// from -1 to 1, the strength for all layers, bake selected / all, delete selected / all; one undo step each, on
// plain meshes and on every subdivision level, saved in projects (older projects open with every layer shown).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/SculptLayers.h"

#include <algorithm>
#include <cmath>
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

bool stroke(Document& d, const std::string& points, const std::string& brush = "Standard", double radius = 0.25) {
    bool ok = true;
    for (int i = 0; i < 4; ++i)
        ok &= run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", 1.0}}).ok;
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

std::vector<glm::vec3> negated(std::vector<glm::vec3> a) {
    for (auto& v : a) v = -v;
    return a;
}

// positions == base + sum(layerFactor * delta)
float layerMismatch(const Mesh& m) {
    if (m.layers.empty()) return 0.0f;
    float worst = 0.0f;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 p = m.base[v];
        for (size_t i = 0; i < m.layers.size(); ++i) p += m.layerFactor(i) * m.layers[i].delta[v];
        worst = std::max(worst, glm::length(p - m.positions[v]));
    }
    return worst;
}

// A sphere with three layers, each with its own stroke: Smile (front), Brow (top), Blink (side).
Document threeLayers(Mesh mesh) {
    Document d = makeDoc(std::move(mesh));
    const char* names[] = {"Smile", "Brow", "Blink"};
    const char* points[] = {"0,-0.3,2", "0,0.5,2", "0.5,0,2"};
    for (int i = 0; i < 3; ++i) {
        run(d, "layer_add", {{"name", std::string(names[i])}});
        stroke(d, points[i], "Standard", 0.3);
    }
    return d;
}

std::vector<std::string> names(const Mesh& m) {
    std::vector<std::string> out;
    for (const SculptLayer& l : m.layers) out.push_back(l.name);
    return out;
}

} // namespace

TEST(sculpt_layers_default_weight_means_as_sculpted) {
    // The weight's meaning stays: a new layer has 1 (as sculpted); 0 is off; -1 is the opposite way.
    Document d = threeLayers(makeIcosphere(4));
    CHECK(d.mesh.layers[0].weight == 1.0f && d.mesh.layers[0].visible && d.mesh.layerStrength == 1.0f);
    const std::vector<glm::vec3> shown = d.mesh.positions;
    const std::vector<glm::vec3> smile = d.mesh.layers[0].delta;
    CHECK(maxLength(smile) > 0.02f);
    CHECK(run(d, "layer_set", {{"index", 0.0}, {"weight", -1.0}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    std::vector<glm::vec3> expect = shown;
    for (size_t v = 0; v < expect.size(); ++v) expect[v] -= 2.0f * smile[v];
    CHECK(maxDistance(d.mesh.positions, expect) < 1e-5f);
    CHECK(!run(d, "layer_set", {{"index", 0.0}, {"weight", std::nan("")}}).ok);
    CHECK(run(d, "layer_set", {{"index", 0.0}, {"weight", 5.0}}).ok); // clamped to the range
    CHECK(d.mesh.layers[0].weight == 1.0f);
}

TEST(sculpt_layers_move_rename_duplicate_one_undo_each) {
    Document d = threeLayers(makeIcosphere(4));
    const std::vector<glm::vec3> shown = d.mesh.positions;
    const size_t steps = d.undo.undoCount();
    d.activeLayer = 0;
    CHECK(run(d, "layer_move", {{"index", 0.0}, {"direction", std::string("down")}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Brow", "Smile", "Blink"}));
    CHECK(d.activeLayer == 1); // the active layer follows its layer
    CHECK(run(d, "layer_move", {{"index", 1.0}, {"direction", std::string("bottom")}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Brow", "Blink", "Smile"}));
    CHECK(run(d, "layer_move", {{"index", 2.0}, {"direction", std::string("top")}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Smile", "Brow", "Blink"}));
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f); // order doesn't change the shape
    CHECK(d.undo.undoCount() == steps + 3);

    // Names stay unique (levels match layers by name).
    CHECK(run(d, "layer_set", {{"index", 2.0}, {"name", std::string("Brow")}}).ok);
    CHECK(d.mesh.layers[2].name == "Brow 2");
    CHECK(run(d, "layer_add", {{"name", std::string("Smile")}}).ok);
    CHECK(d.mesh.layers[3].name == "Smile 2");
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3 && d.mesh.layers[2].name == "Blink");

    CHECK(run(d, "layer_duplicate", {{"index", 0.0}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Smile", "Smile copy", "Brow", "Blink"}));
    CHECK(d.activeLayer == 1);
    CHECK(maxDistance(d.mesh.layers[1].delta, d.mesh.layers[0].delta) == 0.0f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f);
}

TEST(sculpt_layers_merge_down_invert_hide_strength_keep_the_look) {
    Document d = threeLayers(makeIcosphere(4));
    CHECK(run(d, "layer_set", {{"index", 0.0}, {"weight", 0.5}}).ok);
    CHECK(run(d, "layer_set", {{"index", 1.0}, {"weight", -0.75}}).ok);
    const std::vector<glm::vec3> shown = d.mesh.positions, brow = d.mesh.layers[1].delta, smile = d.mesh.layers[0].delta;

    // Merge Smile into Brow: one layer "Brow" at weight 1, the same shape.
    CHECK(run(d, "layer_merge_down", {{"index", 0.0}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Brow", "Blink"}));
    CHECK(d.mesh.layers[0].weight == 1.0f);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-5f);
    CHECK(!run(d, "layer_merge_down", {{"index", 1.0}}).ok); // nothing below the bottom layer
    CHECK(run(d, "undo").ok);
    CHECK(maxDistance(d.mesh.layers[1].delta, brow) == 0.0f && d.mesh.layers.size() == 3);

    // Invert: the offsets turn the other way; twice is the original.
    CHECK(run(d, "layer_invert", {{"index", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.layers[0].delta, negated(smile)) == 0.0f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(run(d, "layer_invert", {{"index", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f);

    // Hide: adds nothing, keeps its weight; merging a hidden layer leaves it out.
    CHECK(run(d, "layer_set", {{"index", 2.0}, {"visible", false}}).ok);
    CHECK(d.mesh.layers[2].weight == 1.0f && !d.mesh.layers[2].visible);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(maxDistance(d.mesh.positions, shown) > 0.01f);
    CHECK(run(d, "layer_set", {{"index", 2.0}, {"visible", true}}).ok);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f);

    // The strength for all layers scales every layer: 0 = the base, back to 1 = as before.
    CHECK(run(d, "layer_strength", {{"strength", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, d.mesh.base) < 1e-7f);
    CHECK(run(d, "layer_strength", {{"strength", 0.5}}).ok);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layerStrength == 1.0f);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f);
}

TEST(sculpt_layers_bake_and_delete_selected_and_all) {
    Document d = threeLayers(makeIcosphere(4));
    CHECK(run(d, "layer_set", {{"index", 1.0}, {"weight", 0.4}}).ok);
    CHECK(run(d, "layer_strength", {{"strength", 0.8}}).ok);
    const std::vector<glm::vec3> shown = d.mesh.positions;
    const size_t steps = d.undo.undoCount();

    // Bake Smile and Blink: the shape stays, Brow is left (still at its weight and the strength).
    CHECK(run(d, "layer_bake", {{"indices", std::string("0,2")}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Brow"}));
    CHECK(d.undo.undoCount() == steps + 1);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-5f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(!run(d, "layer_bake", {{"indices", std::string("4")}}).ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3);

    // Bake all: no layers, the same shape.
    CHECK(run(d, "layer_bake", {{"indices", std::string("all")}}).ok);
    CHECK(d.mesh.layers.empty() && d.mesh.base.empty());
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-5f);
    CHECK(run(d, "undo").ok);

    // Delete selected, then all; undo brings them back.
    CHECK(run(d, "layer_remove_many", {{"indices", std::string("1")}}).ok);
    CHECK((names(d.mesh) == std::vector<std::string>{"Smile", "Blink"}));
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(run(d, "layer_remove_many", {{"indices", std::string("all")}}).ok);
    CHECK(d.mesh.layers.empty());
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3);
    CHECK(maxDistance(d.mesh.positions, shown) < 1e-6f);
}

TEST(sculpt_layers_tools_on_subdivision_levels) {
    // Two layers recorded at the top of a divided cube; every tool used at a low level, then back at the top the
    // layers are exactly what the same tool would make there.
    Document d = makeDoc(makeCube(3));
    for (int i = 0; i < 3; ++i) run(d, "subdiv_divide");
    run(d, "layer_add", {{"name", "Smile"}});
    stroke(d, "0,-0.2,2", "Standard", 0.3);
    stroke(d, "-0.3,-0.3,2 0.3,-0.35,2", "Crease", 0.1);
    run(d, "layer_add", {{"name", "Brow"}});
    stroke(d, "0,0.4,2", "Inflate", 0.2);
    CHECK(subdivLevelText(d.mesh) == "Level 4/4");
    const std::vector<glm::vec3> top = d.mesh.positions, smile = d.mesh.layers[0].delta, brow = d.mesh.layers[1].delta;
    CHECK(maxLength(smile) > 0.01f && maxLength(brow) > 0.01f);
    auto atTop = [&](const char* what) {
        CHECK(run(d, "subdiv_level", {{"level", 4.0}}).ok);
        CHECK(layerMismatch(d.mesh) < 1e-5f);
        std::printf("  %s\n", what);
    };

    // Rename and move at Level 2: the top keeps each layer's fine detail.
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(run(d, "layer_set", {{"index", 0.0}, {"name", std::string("Grin")}}).ok);
    CHECK(run(d, "layer_move", {{"index", 0.0}, {"direction", std::string("down")}}).ok);
    atTop("renamed and moved at Level 2");
    CHECK((names(d.mesh) == std::vector<std::string>{"Brow", "Grin"}));
    CHECK(maxDistance(d.mesh.layers[1].delta, smile) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, brow) < 1e-5f);
    CHECK(maxDistance(d.mesh.positions, top) < 1e-5f);

    // Invert at Level 1: the top's offsets turn the other way too.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_invert", {{"index", 1.0}}).ok);
    atTop("inverted at Level 1");
    CHECK(maxDistance(d.mesh.layers[1].delta, negated(smile)) < 1e-5f);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_invert", {{"index", 1.0}}).ok);

    // Duplicate and merge down at Level 2: the merged layer at the top is both, fine detail and all.
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(run(d, "layer_set", {{"index", 0.0}, {"weight", 0.5}}).ok);
    CHECK(run(d, "layer_merge_down", {{"index", 0.0}}).ok);
    atTop("merged at Level 2");
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].name == "Grin");
    std::vector<glm::vec3> merged = smile;
    for (size_t v = 0; v < merged.size(); ++v) merged[v] += 0.5f * brow[v];
    std::printf("  merged layer within %.2g\n", double(maxDistance(d.mesh.layers[0].delta, merged)));
    CHECK(maxDistance(d.mesh.layers[0].delta, merged) < 1e-5f);
    const std::vector<glm::vec3> mergedTop = d.mesh.positions;

    // Bake at Level 1: the top keeps its shape, with no layers left.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "layer_bake", {{"indices", std::string("all")}}).ok);
    atTop("baked at Level 1");
    CHECK(d.mesh.layers.empty());
    std::printf("  baked top within %.2g\n", double(maxDistance(d.mesh.positions, mergedTop)));
    CHECK(maxDistance(d.mesh.positions, mergedTop) < 1e-4f);
    // Undo: the layers are back, on every level.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "undo").ok); // the step down
    CHECK(run(d, "undo").ok); // the step up after the bake
    CHECK(run(d, "undo").ok); // the bake
    CHECK(d.mesh.layers.size() == 1);
    atTop("undone");
    CHECK(maxDistance(d.mesh.positions, mergedTop) < 1e-5f);
}

TEST(sculpt_layers_saved_in_projects) {
    Document d = threeLayers(makeIcosphere(3));
    CHECK(run(d, "layer_set", {{"index", 1.0}, {"visible", false}, {"weight", -0.5}}).ok);
    CHECK(run(d, "layer_strength", {{"strength", 0.6}}).ok);
    const std::vector<glm::vec3> shown = d.mesh.positions;
    const std::string path = tempPath("sf_sculpt_layers.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    std::filesystem::remove(path);
    CHECK(e.mesh.layers.size() == 3 && !e.mesh.layers[1].visible && e.mesh.layers[1].weight == -0.5f);
    CHECK(e.mesh.layers[0].visible && e.mesh.layers[2].visible);
    CHECK(e.mesh.layerStrength == 0.6f);
    CHECK(maxDistance(e.mesh.positions, shown) < 1e-6f);
}
