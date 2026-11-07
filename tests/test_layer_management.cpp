// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt layer management (core/SculptLayers.h, Tool > Layers; @foirgairwolf's request): reorder, rename, Merge
// Down, Invert, weights from -1 to 1, the eye, Strength for all, Bake (one or all) and Clear All. Each is one undo
// step, survives saving and loading, works exactly on every subdivision level and beside a micro level, and
// exports as glTF morph targets (negative weights, inverted and baked layers).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/SculptLayers.h"
#include "io/GltfIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>

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

void stroke(Document& d, const std::string& points, const std::string& brush, double radius, int passes = 3) {
    for (int i = 0; i < passes; ++i)
        run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", 1.0}});
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

glm::dvec3 sum(const std::vector<glm::vec3>& a) {
    glm::dvec3 s(0.0);
    for (const auto& v : a) s += glm::dvec3(v);
    return s;
}

// positions == base + sum(shown weight * delta)
float layerMismatch(const Mesh& m) {
    if (m.layers.empty()) return 0.0f;
    float worst = 0.0f;
    for (size_t v = 0; v < m.vertexCount(); ++v) {
        glm::vec3 p = m.base[v];
        for (int i = 0; i < int(m.layers.size()); ++i) p += m.layerWeight(i) * m.layers[size_t(i)].delta[v];
        worst = std::max(worst, glm::length(p - m.positions[v]));
    }
    return worst;
}

// A cube with three layers sculpted into it: "Smile" (Inflate at the front), "Blink" (Standard above it) and
// "Brow" (Crease), each with its own offsets; `levels` Divides first (0 = a plain mesh).
Document threeLayers(int levels = 0) {
    Document d = makeDoc(makeCube(levels ? 3 : 12));
    for (int i = 0; i < levels; ++i) run(d, "subdiv_divide");
    const char* names[] = {"Smile", "Blink", "Brow"};
    const char* brushes[] = {"Inflate", "Standard", "Crease"};
    const char* points[] = {"0,-0.2,2", "0.3,0.4,2", "-0.3,0.3,2 0.3,0.35,2"};
    for (int i = 0; i < 3; ++i) {
        run(d, "layer_add", {{"name", std::string(names[i])}});
        d.activeLayer = i;
        stroke(d, points[i], brushes[i], 0.3);
    }
    return d;
}

// Every level's positions and each layer's offsets (by name), from the top down, stepping back to `back` after.
struct Levels {
    std::vector<std::vector<glm::vec3>> positions;
    std::vector<std::map<std::string, std::vector<glm::vec3>>> layers;
};
Levels captureLevels(Document& d) {
    Levels out;
    const int n = std::max(1, subdivLevelCount(d.mesh)), back = subdivCurrentLevel(d.mesh);
    out.positions.resize(size_t(n));
    out.layers.resize(size_t(n));
    for (int level = n - 1; level >= 0; --level) {
        if (n > 1) run(d, "subdiv_level", {{"level", double(level + 1)}});
        out.positions[size_t(level)] = d.mesh.positions;
        for (const SculptLayer& l : d.mesh.layers) out.layers[size_t(level)][l.name] = l.delta;
    }
    if (n > 1) run(d, "subdiv_level", {{"level", double(back + 1)}});
    return out;
}

Document saveAndLoad(const Document& d) {
    const std::string path = tempPath("sf_layer_management.sfproj");
    std::string err;
    Document e;
    CHECK(saveProject(path, d, err));
    CHECK(loadProject(path, e, err));
    std::filesystem::remove(path);
    return e;
}

} // namespace

TEST(layer_mgmt_move_keeps_the_shape_and_follows_the_active_layer) {
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions;
    const std::vector<glm::vec3> brow = d.mesh.layers[2].delta;
    d.activeLayer = 2;
    CHECK(run(d, "layer_move", {{"index", 2.0}, {"to", 0.0}}).ok); // the drag: Brow to the top
    CHECK(d.mesh.layers[0].name == "Brow" && d.mesh.layers[1].name == "Smile" && d.mesh.layers[2].name == "Blink");
    CHECK(d.activeLayer == 0);
    CHECK(d.mesh.layers[0].delta == brow);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(run(d, "layer_move", {{"index", 0.0}, {"direction", 1.0}}).ok); // Move Down
    CHECK(d.mesh.layers[1].name == "Brow" && d.activeLayer == 1);
    CHECK(!run(d, "layer_move", {{"index", 0.0}, {"direction", -1.0}}).ok); // already at the top
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[0].name == "Brow" && d.activeLayer == 0); // the active layer follows undo too
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[0].name == "Smile" && d.mesh.layers[2].name == "Brow" && d.activeLayer == 2);
    CHECK(run(d, "redo").ok);
    CHECK(d.mesh.layers[0].name == "Brow" && d.activeLayer == 0);
    const Document e = saveAndLoad(d);
    CHECK(e.mesh.layers.size() == 3 && e.mesh.layers[0].name == "Brow" && e.mesh.layers[0].delta == brow);
}

TEST(layer_mgmt_rename_and_undo_and_save) {
    Document d = threeLayers();
    CHECK(run(d, "layer_rename", {{"index", 1.0}, {"name", std::string("eyeBlinkLeft")}}).ok);
    CHECK(d.mesh.layers[1].name == "eyeBlinkLeft");
    CHECK(!run(d, "layer_rename", {{"index", 1.0}, {"name", std::string()}}).ok);
    CHECK(!run(d, "layer_rename", {{"index", 7.0}, {"name", std::string("x")}}).ok);
    const Document e = saveAndLoad(d);
    CHECK(e.mesh.layers[1].name == "eyeBlinkLeft");
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[1].name == "Blink");
}

TEST(layer_mgmt_merge_down_keeps_the_shape) {
    Document d = threeLayers();
    run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.5}});
    run(d, "layer_set_weight", {{"index", 1.0}, {"weight", -0.75}});
    const std::vector<glm::vec3> shape = d.mesh.positions;
    std::vector<glm::vec3> expect(shape.size());
    for (size_t v = 0; v < expect.size(); ++v) expect[v] = 0.5f * d.mesh.layers[0].delta[v] - 0.75f * d.mesh.layers[1].delta[v];
    d.activeLayer = 0;
    CHECK(run(d, "layer_merge_down", {{"index", 0.0}}).ok);
    CHECK(d.mesh.layers.size() == 2 && d.mesh.layers[0].name == "Blink" && d.mesh.layers[0].weight == 1.0f);
    CHECK(d.activeLayer == 0);
    CHECK(maxDistance(d.mesh.layers[0].delta, expect) < 1e-6f);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-5f);
    CHECK(!run(d, "layer_merge_down", {{"index", 1.0}}).ok); // the bottom layer
    // Shown into hidden: refused (the shape would change).
    run(d, "layer_set_visible", {{"index", 1.0}, {"visible", false}});
    CHECK(!run(d, "layer_merge_down", {{"index", 0.0}}).ok);
    CHECK(run(d, "undo").ok); // the eye
    CHECK(run(d, "undo").ok); // the merge
    CHECK(d.mesh.layers.size() == 3 && d.mesh.layers[0].weight == 0.5f);
    CHECK(d.activeLayer == 0); // Smile again, not Blink (the merged layer's name)
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(run(d, "redo").ok);
    CHECK(d.mesh.layers.size() == 2 && d.activeLayer == 0);
}

TEST(layer_mgmt_invert_negates_and_twice_is_the_original) {
    Document d = threeLayers();
    const std::vector<glm::vec3> smile = d.mesh.layers[0].delta, shape = d.mesh.positions;
    CHECK(run(d, "layer_invert", {{"index", 0.0}}).ok);
    for (size_t v = 0; v < smile.size(); v += 7) CHECK(d.mesh.layers[0].delta[v] == -smile[v]);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(maxDistance(d.mesh.positions, shape) > 0.01f);
    // Inverting = weight -1.
    Document w = threeLayers();
    run(w, "layer_set_weight", {{"index", 0.0}, {"weight", -1.0}});
    CHECK(maxDistance(w.mesh.positions, d.mesh.positions) < 1e-5f);
    CHECK(run(d, "layer_invert", {{"index", 0.0}}).ok);
    CHECK(d.mesh.layers[0].delta == smile);
    CHECK(run(d, "undo").ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[0].delta == smile && maxDistance(d.mesh.positions, shape) < 1e-6f);
}

TEST(layer_mgmt_negative_weight_undo_and_save) {
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions;
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", -1.0}}).ok);
    CHECK(d.mesh.layers[0].weight == -1.0f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    // Sculpting into a negative layer works (the offsets are divided by the weight, so the stroke shows as made).
    d.activeLayer = 0;
    const std::vector<glm::vec3> before = d.mesh.positions;
    stroke(d, "0.2,-0.3,2", "Standard", 0.2, 1);
    CHECK(maxDistance(d.mesh.positions, before) > 1e-3f);
    CHECK(layerMismatch(d.mesh) < 1e-5f);
    CHECK(run(d, "undo").ok);
    const Document e = saveAndLoad(d);
    CHECK(e.mesh.layers[0].weight == -1.0f);
    CHECK(maxDistance(e.mesh.positions, d.mesh.positions) < 1e-6f);
    CHECK(run(d, "undo").ok); // the weight: a settings-only step
    CHECK(d.mesh.layers[0].weight == 1.0f);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(d.undo.canRedo());
}

TEST(layer_mgmt_eye_hides_keeps_the_weight_and_blocks_strokes) {
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions;
    run(d, "layer_set_weight", {{"index", 1.0}, {"weight", 0.6}});
    const std::vector<glm::vec3> partly = d.mesh.positions;
    CHECK(run(d, "layer_set_visible", {{"index", 1.0}, {"visible", false}}).ok);
    CHECK(!d.mesh.layers[1].visible && d.mesh.layers[1].weight == 0.6f);
    CHECK(d.mesh.layerWeight(1) == 0.0f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(maxDistance(d.mesh.positions, partly) > 0.01f);
    // A hidden layer can't be sculpted into.
    d.activeLayer = 1;
    const std::vector<glm::vec3> hidden = d.mesh.positions;
    run(d, "stroke", {{"points", std::string("0.3,0.4,2")}, {"brush", std::string("Standard")}, {"radius", 0.3}});
    CHECK(maxDistance(d.mesh.positions, hidden) == 0.0f);
    // Saved hidden, loaded hidden.
    const Document e = saveAndLoad(d);
    CHECK(!e.mesh.layers[1].visible && e.mesh.layers[1].weight == 0.6f && e.mesh.layers[0].visible);
    CHECK(maxDistance(e.mesh.positions, hidden) < 1e-6f);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[1].visible);
    CHECK(maxDistance(d.mesh.positions, partly) < 1e-6f);
    CHECK(run(d, "undo").ok);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
}

TEST(layer_mgmt_strength_scales_every_layer) {
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions, base = d.mesh.base;
    CHECK(run(d, "layer_strength", {{"strength", 0.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, base) < 1e-6f);
    CHECK(run(d, "layer_strength", {{"strength", 0.5}}).ok);
    for (size_t v = 0; v < shape.size(); v += 5) CHECK(glm::length(d.mesh.positions[v] - (base[v] + 0.5f * (shape[v] - base[v]))) < 1e-5f);
    const Document e = saveAndLoad(d);
    CHECK(e.mesh.layerStrength == 0.5f);
    CHECK(maxDistance(e.mesh.positions, d.mesh.positions) < 1e-6f);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layerStrength == 0.0f);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layerStrength == 1.0f && maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(!run(d, "layer_strength", {{"strength", std::nan("")}}).ok);
}

TEST(layer_mgmt_bake_one_and_all_keep_the_shape) {
    Document d = threeLayers();
    run(d, "layer_set_weight", {{"index", 1.0}, {"weight", -0.5}});
    run(d, "layer_strength", {{"strength", 0.8}});
    const std::vector<glm::vec3> shape = d.mesh.positions;
    d.activeLayer = 2;
    CHECK(run(d, "layer_bake", {{"index", 1.0}}).ok);
    CHECK(d.mesh.layers.size() == 2 && d.mesh.layers[1].name == "Brow" && d.activeLayer == 1);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-5f);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    // Bake All bakes the shown layers; a hidden one stays a layer.
    run(d, "layer_set_visible", {{"index", 0.0}, {"visible", false}});
    const std::vector<glm::vec3> withoutSmile = d.mesh.positions;
    CHECK(!run(d, "layer_bake", {{"index", 0.0}}).ok); // hidden
    CHECK(run(d, "layer_bake", {{"all", true}}).ok);
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].name == "Smile" && !d.mesh.layers[0].visible);
    CHECK(maxDistance(d.mesh.positions, withoutSmile) < 1e-5f);
    CHECK(d.activeLayer == -1);
    // Everything shown: no layers left, the mesh is plain again.
    run(d, "layer_set_visible", {{"index", 0.0}, {"visible", true}});
    const std::vector<glm::vec3> all = d.mesh.positions;
    CHECK(run(d, "layer_bake", {{"all", true}}).ok);
    CHECK(d.mesh.layers.empty() && d.mesh.base.empty());
    CHECK(maxDistance(d.mesh.positions, all) < 1e-5f);
    const Document e = saveAndLoad(d);
    CHECK(e.mesh.layers.empty() && maxDistance(e.mesh.positions, all) < 1e-6f);
    for (int i = 0; i < 5; ++i) CHECK(run(d, "undo").ok); // back to just after the Strength
    CHECK(d.mesh.layers.size() == 3 && d.mesh.layerStrength == 0.8f);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
}

TEST(layer_mgmt_clear_all_is_one_undo) {
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions, base = d.mesh.base;
    CHECK(run(d, "layer_clear").ok);
    CHECK(d.mesh.layers.empty() && d.activeLayer == -1);
    CHECK(maxDistance(d.mesh.positions, base) < 1e-6f);
    CHECK(!run(d, "layer_clear").ok);
    CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3 && maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(run(d, "layer_info").message.find("Brow") != std::string::npos);
}

TEST(layer_mgmt_every_operation_is_exact_on_every_subdivision_level) {
    // Recorded at the top of 3 levels: each operation, then every level checked against what it should be.
    auto check = [](const char* what, const std::function<void(Document&)>& op,
                    const std::function<void(const Levels& before, int level, Document& d)>& expect) {
        for (int at : {1, 3}) { // operated on at the lowest and the top level
            Document d = threeLayers(2);
            run(d, "layer_set_weight", {{"index", 1.0}, {"weight", 0.6}});
            const Levels before = captureLevels(d);
            run(d, "subdiv_level", {{"level", double(at)}});
            op(d);
            float worst = 0.0f;
            for (int level = 3; level >= 1; --level) {
                CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
                worst = std::max(worst, layerMismatch(d.mesh));
                expect(before, level - 1, d);
            }
            std::printf("  %s at SDiv %d: layers consistent within %.2g\n", what, at, worst);
            CHECK(worst < 1e-5f);
        }
    };
    check("move", [](Document& d) { CHECK(run(d, "layer_move", {{"index", 2.0}, {"to", 0.0}}).ok); },
          [](const Levels& b, int l, Document& d) {
              CHECK(d.mesh.layers.size() == 3 && d.mesh.layers[0].name == "Brow");
              for (const SculptLayer& x : d.mesh.layers) CHECK(maxDistance(x.delta, b.layers[size_t(l)].at(x.name)) < 1e-5f);
              CHECK(maxDistance(d.mesh.positions, b.positions[size_t(l)]) < 1e-5f);
          });
    check("rename", [](Document& d) { CHECK(run(d, "layer_rename", {{"index", 0.0}, {"name", std::string("jawOpen")}}).ok); },
          [](const Levels& b, int l, Document& d) {
              CHECK(d.mesh.layers[0].name == "jawOpen");
              CHECK(maxDistance(d.mesh.layers[0].delta, b.layers[size_t(l)].at("Smile")) < 1e-5f);
              CHECK(maxDistance(d.mesh.positions, b.positions[size_t(l)]) < 1e-5f);
          });
    check("merge down", [](Document& d) { CHECK(run(d, "layer_merge_down", {{"index", 0.0}}).ok); },
          [](const Levels& b, int l, Document& d) {
              CHECK(d.mesh.layers.size() == 2 && d.mesh.layers[0].name == "Blink");
              CHECK(maxDistance(d.mesh.positions, b.positions[size_t(l)]) < 1e-5f);
          });
    check("invert", [](Document& d) { CHECK(run(d, "layer_invert", {{"index", 2.0}}).ok); },
          [](const Levels& b, int l, Document& d) {
              std::vector<glm::vec3> neg = b.layers[size_t(l)].at("Brow");
              for (auto& v : neg) v = -v;
              CHECK(maxDistance(d.mesh.layers[2].delta, neg) < 1e-5f);
          });
    check("bake", [](Document& d) { CHECK(run(d, "layer_bake", {{"index", 1.0}}).ok); },
          [](const Levels& b, int l, Document& d) {
              CHECK(d.mesh.layers.size() == 2 && d.mesh.layers[1].name == "Brow");
              CHECK(maxDistance(d.mesh.positions, b.positions[size_t(l)]) < 1e-5f);
              CHECK(maxDistance(d.mesh.layers[1].delta, b.layers[size_t(l)].at("Brow")) < 1e-5f);
          });
    check("bake all", [](Document& d) { CHECK(run(d, "layer_bake", {{"all", true}}).ok); },
          [](const Levels& b, int l, Document& d) {
              CHECK(d.mesh.layers.empty());
              CHECK(maxDistance(d.mesh.positions, b.positions[size_t(l)]) < 1e-5f);
          });
    check("clear", [](Document& d) { CHECK(run(d, "layer_clear").ok); },
          [](const Levels&, int, Document& d) { CHECK(d.mesh.layers.empty()); });
    check("hide + strength", [](Document& d) {
              CHECK(run(d, "layer_set_visible", {{"index", 0.0}, {"visible", false}}).ok);
              CHECK(run(d, "layer_strength", {{"strength", 0.5}}).ok);
          },
          [](const Levels&, int, Document& d) {
              CHECK(!d.mesh.layers[0].visible && d.mesh.layerStrength == 0.5f);
          });
}

TEST(layer_mgmt_levels_undo_and_save) {
    Document d = threeLayers(2);
    const Levels before = captureLevels(d);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    const size_t steps = d.undo.undoCount();
    CHECK(run(d, "layer_bake", {{"index", 0.0}}).ok);
    CHECK(run(d, "layer_invert", {{"index", 0.0}}).ok);
    CHECK(run(d, "layer_set_visible", {{"index", 1.0}, {"visible", false}}).ok);
    const Levels after = captureLevels(d);
    // Saved and loaded: every level as it was.
    Document e = saveAndLoad(d);
    const Levels loaded = captureLevels(e);
    for (size_t l = 0; l < 3; ++l) CHECK(maxDistance(loaded.positions[l], after.positions[l]) < 1e-5f);
    CHECK(e.mesh.layers.size() == 2 && !e.mesh.layers[1].visible);
    // Undo back to before the three steps (and the level steps of captureLevels): every level as before.
    while (d.undo.undoCount() > steps) CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers.size() == 3);
    const Levels undone = captureLevels(d);
    float worst = 0.0f;
    for (size_t l = 0; l < 3; ++l) {
        worst = std::max(worst, maxDistance(undone.positions[l], before.positions[l]));
        for (const auto& [name, delta] : before.layers[l]) worst = std::max(worst, maxDistance(undone.layers[l].at(name), delta));
    }
    std::printf("  undone on every level within %.2g\n", worst);
    CHECK(worst < 1e-5f);
}

TEST(layer_mgmt_with_a_micro_level) {
    // A micro level can't sit on sculpt layers (core/Micro): Bake All, then divide; no new layer until it's deleted.
    Document d = threeLayers();
    const std::vector<glm::vec3> shape = d.mesh.positions;
    CHECK(!run(d, "micro_divide", {{"level", 2.0}}).ok);
    CHECK(run(d, "layer_bake", {{"all", true}}).ok);
    CHECK(d.mesh.layers.empty() && maxDistance(d.mesh.positions, shape) < 1e-5f);
    CHECK(run(d, "micro_divide", {{"level", 2.0}}).ok);
    CHECK(d.mesh.micro != nullptr);
    CHECK(!run(d, "layer_add", {{"name", std::string("Late")}}).ok);
    CHECK(d.mesh.layers.empty() && d.mesh.micro != nullptr);
    CHECK(run(d, "undo").ok); // the micro level
    CHECK(d.mesh.micro == nullptr);
    CHECK(run(d, "undo").ok); // the bake
    CHECK(d.mesh.layers.size() == 3 && maxDistance(d.mesh.positions, shape) < 1e-6f);
    CHECK(run(d, "layer_add", {{"name", std::string("Late")}}).ok);
}

TEST(layer_mgmt_gltf_morph_targets_negative_inverted_and_baked) {
    Document d = threeLayers();
    run(d, "layer_set_weight", {{"index", 0.0}, {"weight", -0.5}});
    run(d, "layer_invert", {{"index", 1.0}});
    run(d, "layer_set_visible", {{"index", 2.0}, {"visible", false}});
    const glm::dvec3 smile = sum(d.mesh.layers[0].delta), blink = sum(d.mesh.layers[1].delta);
    auto exported = [&](Document& doc) {
        const std::string path = tempPath("sf_layer_management.glb");
        CHECK(run(doc, "export_glb", {{"path", path}}).ok);
        Mesh m;
        std::string err;
        CHECK(loadGltf(path, m, err));
        std::filesystem::remove(path);
        return m;
    };
    Mesh m = exported(d);
    CHECK(m.layers.size() == 3);
    if (m.layers.size() == 3) {
        CHECK(m.layers[0].name == "Smile" && std::abs(m.layers[0].weight + 0.5f) < 1e-6f); // negative as set
        CHECK(std::abs(m.layers[2].weight) < 1e-6f);                                        // hidden: 0
        CHECK(glm::length(sum(m.layers[0].delta) - smile) < 1e-3 * std::max(1.0, glm::length(smile)));
        CHECK(glm::length(sum(m.layers[1].delta) - blink) < 1e-3 * std::max(1.0, glm::length(blink))); // inverted
        CHECK(glm::length(blink) > 1e-3);
        CHECK(layerMismatch(m) < 1e-4f); // opens showing what was exported
        CHECK(std::abs(maxLength(m.positions) - maxLength(d.mesh.positions)) < 1e-4f);
    }
    // Baked: no morph target any more, the shape is in the base.
    run(d, "layer_bake", {{"index", 0.0}});
    m = exported(d);
    CHECK(m.layers.size() == 2 && m.layers[0].name == "Blink");
    double baseA = 0.0, baseB = 0.0;
    for (const auto& p : m.basePositions()) baseA += glm::length(p);
    for (const auto& p : d.mesh.base) baseB += glm::length(p);
    CHECK(std::abs(baseA - baseB) < 1e-3 * std::max(baseB, 1.0));
}

TEST(layer_mgmt_settings_undo_is_small) {
    Document d = threeLayers();
    const size_t before = d.undo.undoBytes();
    run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.3}});
    run(d, "layer_set_visible", {{"index", 1.0}, {"visible", false}});
    run(d, "layer_strength", {{"strength", 1.5}});
    CHECK(d.undo.undoBytes() - before < 1024); // settings only, no offsets
}

TEST(layer_mgmt_duplicate_top_bottom_and_several_together) {
    // @foirgairwolf's follow-up: duplicate, to the very top / bottom, and several layers moved together.
    Document d = threeLayers();
    const std::vector<glm::vec3> smile = d.mesh.layers[0].delta;
    CHECK(run(d, "layer_duplicate", {{"index", 0.0}}).ok);
    CHECK(d.mesh.layers.size() == 4 && d.mesh.layers[0].name == "Smile copy" && d.mesh.layers[1].name == "Smile");
    CHECK(d.mesh.layers[0].delta == smile && d.activeLayer == 0);
    CHECK(layerMismatch(d.mesh) < 1e-6f);
    CHECK(run(d, "layer_duplicate", {{"index", 1.0}}).ok);
    CHECK(d.mesh.layers[1].name == "Smile copy 2");
    CHECK(run(d, "undo").ok);
    // Order now: Smile copy, Smile, Blink, Brow.
    const std::vector<glm::vec3> shape = d.mesh.positions;
    d.activeLayer = 1;
    CHECK(run(d, "layer_move", {{"index", 1.0}, {"place", std::string("bottom")}}).ok);
    CHECK(d.mesh.layers[3].name == "Smile" && d.activeLayer == 3);
    CHECK(run(d, "layer_move", {{"index", 3.0}, {"place", std::string("top")}}).ok);
    CHECK(d.mesh.layers[0].name == "Smile" && d.activeLayer == 0);
    CHECK(!run(d, "layer_move", {{"index", 0.0}, {"place", std::string("top")}}).ok); // already there
    // Smile, Smile copy, Blink, Brow: Smile and Blink together to the bottom, then one up, then to place 0.
    CHECK(run(d, "layer_move", {{"indices", std::string("0,2")}, {"place", std::string("bottom")}}).ok);
    CHECK(d.mesh.layers[0].name == "Smile copy" && d.mesh.layers[1].name == "Brow" && d.mesh.layers[2].name == "Smile" &&
          d.mesh.layers[3].name == "Blink");
    CHECK(d.activeLayer == 2);
    CHECK(run(d, "layer_move", {{"indices", std::string("2,3")}, {"direction", -1.0}}).ok);
    CHECK(d.mesh.layers[1].name == "Smile" && d.mesh.layers[2].name == "Blink" && d.mesh.layers[3].name == "Brow");
    CHECK(run(d, "layer_move", {{"indices", std::string("1,2")}, {"to", 0.0}}).ok);
    CHECK(d.mesh.layers[0].name == "Smile" && d.mesh.layers[1].name == "Blink" && d.mesh.layers[2].name == "Smile copy");
    CHECK(!run(d, "layer_move", {{"indices", std::string("0,1")}, {"direction", -1.0}}).ok); // packed at the top
    CHECK(!run(d, "layer_move", {{"indices", std::string("0,x")}, {"place", std::string("top")}}).ok);
    CHECK(maxDistance(d.mesh.positions, shape) < 1e-6f);
    // One undo step per move.
    for (int i = 0; i < 5; ++i) CHECK(run(d, "undo").ok);
    CHECK(d.mesh.layers[0].name == "Smile copy" && d.mesh.layers[1].name == "Smile" && d.mesh.layers[3].name == "Brow");
    // On subdivision levels too: exact, every level.
    Document e = threeLayers(2);
    const Levels before = captureLevels(e);
    CHECK(run(e, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(e, "layer_duplicate", {{"index", 2.0}}).ok);
    CHECK(run(e, "layer_move", {{"indices", std::string("0,2")}, {"place", std::string("bottom")}}).ok);
    const Levels after = captureLevels(e);
    for (size_t l = 0; l < 3; ++l) {
        CHECK(maxDistance(after.layers[l].at("Brow copy"), before.layers[l].at("Brow")) < 1e-5f);
        CHECK(maxDistance(after.layers[l].at("Smile"), before.layers[l].at("Smile")) < 1e-5f);
    }
    CHECK(e.mesh.layers.back().name == "Brow copy");
    const Document f = saveAndLoad(e);
    CHECK(f.mesh.layers.size() == 4 && f.mesh.layers.back().name == "Brow copy");
}
