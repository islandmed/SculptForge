// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Subdivision levels, polish: tools that rebuild the mesh say they drop the levels and undo brings them back,
// sculpt-layer detail turns with the low level, and inactive Parts don't keep cached levels.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/MeshTransform.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

bool stroke(Document& d, const std::string& points, const std::string& brush = "Standard", double radius = 0.3) {
    return run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", 0.8}}).ok;
}

float maxDistance(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    if (a.size() != b.size()) return 1e30f;
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, glm::length(a[i] - b[i]));
    return m;
}

bool bitEqual(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0);
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A cube divided twice, detail at the top, then a pending (not yet folded in) stroke at Level 2 of 3.
Document levelledCube() {
    Document d = makeDoc(makeCube(4));
    run(d, "subdiv_divide");
    run(d, "subdiv_divide");
    stroke(d, "0,0,2");
    run(d, "subdiv_level", {{"level", 2.0}});
    stroke(d, "0.3,0.3,2", "Inflate", 0.5);
    return d;
}

double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

// Timing of selecting Parts and stepping levels on a 1.77M-triangle top level (SF_LEVELS_BENCH=1).
TEST(multires_bench_select_and_step) {
    if (!std::getenv("SF_LEVELS_BENCH")) return;
    Document d = makeDoc(makeCube(3));
    for (int i = 0; i < 7; ++i) run(d, "subdiv_divide");
    d.addPart(makeIcosphere(3), "Eye", false);
    std::printf("  top level: %zu triangles\n", d.mesh.triangleCount());
    // Fill the level cache: down and back up.
    run(d, "subdiv_lower");
    std::printf("  level cache at Level 7: %.0f MB (the top level), ", double(subdivCacheBytes(d.mesh)) / 1e6);
    run(d, "subdiv_higher");
    std::printf("at Level 8: %.0f MB; the levels themselves %.0f MB\n", double(subdivCacheBytes(d.mesh)) / 1e6,
                double(subdivBytes(d.mesh)) / 1e6);
    for (int round = 0; round < 3; ++round) {
        auto t0 = std::chrono::steady_clock::now();
        d.selectPart(1);
        const double away = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        d.selectPart(0);
        const double back = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        run(d, "subdiv_lower");
        const double down = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        run(d, "subdiv_higher");
        const double up = msSince(t0);
        t0 = std::chrono::steady_clock::now();
        run(d, "subdiv_lower");
        const double down2 = msSince(t0);
        run(d, "subdiv_higher");
        std::printf("  select away %.1f ms, select back %.1f ms, then step down %.1f ms, up %.1f ms, down again %.1f ms\n",
                    away, back, down, up, down2);
    }
    // Sculpt layers across levels: a layer stroke at the top folded in on the way down, a base stroke one level
    // down, re-evaluating the top (with its layer) on the way up.
    run(d, "layer_add", {{"name", "Scales"}});
    d.activeLayer = 0;
    stroke(d, "0,0,2", "Standard", 0.2);
    auto t0 = std::chrono::steady_clock::now();
    run(d, "subdiv_lower");
    const double fold = msSince(t0);
    d.activeLayer = -1;
    stroke(d, "0.3,0,2", "Inflate", 0.5);
    t0 = std::chrono::steady_clock::now();
    run(d, "subdiv_higher");
    const double eval = msSince(t0);
    std::printf("  with a sculpt layer: step down folding a layer stroke in %.1f ms, step up re-evaluating %.1f ms\n", fold, eval);
    stroke(d, "-0.3,0,2", "Inflate", 0.3); // on the base, at the top
    t0 = std::chrono::steady_clock::now();
    run(d, "subdiv_lower");
    std::printf("  step down folding a base stroke in (one layer) %.1f ms\n", msSince(t0));
}

// Every tool that rebuilds the mesh works on the current level (pending edits included), says the levels were
// removed, and undo brings back the whole stack: every level as it was, at the level the user was on.
TEST(multires_rebuilding_tools_drop_levels_and_undo_restores_them) {
    struct Case {
        const char* command;
        Args args;
    };
    const Case cases[] = {
        {"remesh", {{"resolution", 40.0}}},
        {"decimate", {{"ratio", 0.5}}},
        {"qremesh", {{"target", 150.0}}},
        {"subdivide", {}},
        {"delete_hidden", {}},
    };
    for (const Case& c : cases) {
        std::printf("  %s\n", c.command);
        Document d = levelledCube();
        if (std::string(c.command) == "delete_hidden")
            for (uint32_t t = 0; t < 40; ++t) d.mesh.ensureHidden()[t] = 1; // hidden at this level, pending
        CHECK(subdivLevelText(d.mesh) == "Level 2/3");
        CHECK(!subdivInSync(d.mesh)); // the stroke at Level 2 is still pending
        const std::vector<glm::vec3> working = d.mesh.positions;
        const size_t tris = d.mesh.triangleCount();
        std::vector<std::vector<glm::vec3>> levels;
        for (int i = 0; i < 3; ++i) levels.push_back(subdivLevelMesh(d.mesh, i).positions);
        const AABB box = d.mesh.bounds();

        const CommandResult r = run(d, c.command, c.args);
        CHECK(r.ok);
        CHECK(contains(r.message, "Subdivision levels removed: Level 2 of 3 kept as the new base"));
        CHECK(contains(r.message, "undo restores them"));
        CHECK(subdivLevelCount(d.mesh) == 0);
        CHECK(d.notice.empty());
        // Made from the current level, with the pending stroke.
        const std::string cmd = c.command;
        if (cmd == "subdivide") CHECK(d.mesh.triangleCount() == tris * 4);
        if (cmd == "delete_hidden") {
            CHECK(d.mesh.triangleCount() == tris - 40);
            for (const glm::vec3& p : d.mesh.positions) CHECK(std::find(working.begin(), working.end(), p) != working.end());
        }
        if (cmd == "decimate") CHECK(d.mesh.triangleCount() <= tris / 2 + 2);
        const AABB after = d.mesh.bounds();
        CHECK(glm::length(after.min - box.min) < 0.1f && glm::length(after.max - box.max) < 0.1f);

        // Undo: the whole stack, at Level 2 with the pending stroke, and every level as it was.
        CHECK(run(d, "undo").ok);
        CHECK(subdivLevelText(d.mesh) == "Level 2/3");
        CHECK(bitEqual(d.mesh.positions, working));
        for (int i : {2, 0, 1}) {
            CHECK(run(d, "subdiv_level", {{"level", double(i + 1)}}).ok);
            CHECK(maxDistance(d.mesh.positions, levels[size_t(i)]) < 1e-5f);
        }
        // Again from Level 2: redo drops the levels again, undo once more brings them back.
        CHECK(run(d, c.command, c.args).ok);
        CHECK(subdivLevelCount(d.mesh) == 0);
        CHECK(run(d, "undo").ok);
        CHECK(subdivLevelText(d.mesh) == "Level 2/3");
        CHECK(run(d, "redo").ok);
        CHECK(subdivLevelCount(d.mesh) == 0);
        CHECK(run(d, "undo").ok);
        CHECK(subdivLevelText(d.mesh) == "Level 2/3");
        CHECK(maxDistance(d.mesh.positions, levels[1]) < 1e-5f);
    }
    // Without levels there's no note.
    Document p = makeDoc(makeCube(4));
    CHECK(!contains(run(p, "decimate", {{"ratio", 0.5}}).message, "Subdivision"));
}

// Merging and splitting Parts (not undoable) use each Part's current level and say so.
TEST(multires_merge_and_split_say_levels_are_removed) {
    Document d = levelledCube();
    d.parts[0].name = "Body";
    const size_t current = d.mesh.triangleCount();
    d.addPart(makeIcosphere(1), "Eye", false);
    CommandResult r = run(d, "part_merge_down");
    CHECK(r.ok);
    CHECK(contains(r.message, "Subdivision levels removed: Body Level 2 of 3 kept as the new base"));
    CHECK(d.mesh.triangleCount() == current + makeIcosphere(1).triangleCount());
    CHECK(subdivLevelCount(d.mesh) == 0);
    // Split: pieces of the current level.
    Document t = levelledCube();
    for (uint32_t i = 0; i < t.mesh.triangleCount(); ++i) t.mesh.ensureFaceGroups()[i] = i < 100 ? 1 : 2;
    r = run(t, "part_split_groups");
    CHECK(r.ok);
    CHECK(contains(r.message, "Subdivision levels removed: Sculpt Level 2 of 3 kept as the new base"));
    CHECK(t.mesh.triangleCount() == 100 || t.partMesh(0).triangleCount() == 100);
    // Merge visible: both Parts with levels are named.
    Document m = levelledCube();
    m.addPart(makeCube(2), "Box", true);
    run(m, "subdiv_divide");
    r = run(m, "part_merge_visible");
    CHECK(r.ok);
    CHECK(contains(r.message, "Sculpt Level 2 of 3"));
    CHECK(contains(r.message, "Box Level 2 of 2"));
}

namespace {

// Each vertex's sculpt-layer offset at the top level: positions at weight 1 minus positions at weight 0.
std::vector<glm::vec3> layerOffsets(Document& d) {
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    const std::vector<glm::vec3> off = d.mesh.positions;
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 1.0}}).ok);
    std::vector<glm::vec3> o(off.size());
    for (size_t v = 0; v < o.size(); ++v) o[v] = d.mesh.positions[v] - off[v];
    return o;
}

float maxLength(const std::vector<glm::vec3>& a) {
    float m = 0.0f;
    for (const auto& v : a) m = std::max(m, glm::length(v));
    return m;
}

} // namespace

// A sculpt layer's fine detail turns with a low level rotated by the gizmo (the transform command).
TEST(multires_layer_detail_turns_with_a_rotated_low_level) {
    Document d = makeDoc(makeCube(3));
    run(d, "subdiv_divide");
    run(d, "subdiv_divide");
    CHECK(run(d, "layer_add", {{"name", "Scales"}}).ok);
    d.activeLayer = 0;
    for (int i = 0; i < 4; ++i) {
        CHECK(stroke(d, "0,0,2", "Standard", 0.25));
        CHECK(stroke(d, "0.4,-0.2,2 0.5,0.3,2", "Standard", 0.12));
    }
    const std::vector<glm::vec3> before = layerOffsets(d);
    const std::vector<glm::vec3> top = d.mesh.positions;
    CHECK(maxLength(before) > 0.02f);

    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    d.activeLayer = -1; // posing the whole Part: Base mesh active (with a layer active the turn goes into it, #133)
    CHECK(run(d, "transform", {{"rotate", "0,90,0"}, {"pivot", "origin"}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    const glm::mat3 R = glm::mat3_cast(eulerRotation(glm::vec3(0, 90, 0)));
    // The whole top level turned, layer included, and so did each vertex's layer offset.
    std::vector<glm::vec3> turned(top.size()), turnedOffsets(before.size());
    for (size_t v = 0; v < top.size(); ++v) turned[v] = R * top[v];
    for (size_t v = 0; v < before.size(); ++v) turnedOffsets[v] = R * before[v];
    const std::vector<glm::vec3> after = layerOffsets(d);
    std::printf("  top level %.2g from the turned shape, layer offsets %.2g from the turned ones (largest offset %.2g)\n",
                maxDistance(d.mesh.positions, turned), maxDistance(after, turnedOffsets), maxLength(before));
    CHECK(maxDistance(d.mesh.positions, turned) < 1e-4f);
    CHECK(maxDistance(after, turnedOffsets) < 1e-4f);
    // (Kept in world space, the fine part of the offsets would still point the old way.)
    CHECK(maxDistance(after, before) > 0.5f * maxLength(before));

    // Saved and loaded (format 5), the same.
    const std::string path = tempPath("sf_multires_layer_frames.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(run(e, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(e, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(maxDistance(e.mesh.positions, d.mesh.positions) < 1e-5f);
    std::filesystem::remove(path);
}

// Format 4 projects kept sculpt-layer details in world space: they load converted, so the levels evaluate as
// they did (tests/data/levels_layers_v4*.sfproj were saved by 0.4.2 + levels: a layer sculpted at Level 3, then a
// stroke on the base at Level 1, saved there with Level 2 and 3 waiting to be re-evaluated; and the same at Level 3).
TEST(multires_format4_layer_detail_is_converted) {
    Document ref, d;
    std::string err;
    CHECK(loadProject(SF_TEST_DATA_DIR "/levels_layers_v4_top.sfproj", ref, err));
    CHECK(loadProject(SF_TEST_DATA_DIR "/levels_layers_v4.sfproj", d, err));
    CHECK(subdivLevelText(ref.mesh) == "Level 3/3" && subdivLevelText(d.mesh) == "Level 1/3");
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.layers.size() == 1 && ref.mesh.layers.size() == 1);
    if (d.mesh.layers.size() != 1 || ref.mesh.layers.size() != 1) return;
    std::printf("  top level %.2g from 0.4.2's, layer %.2g\n", maxDistance(d.mesh.positions, ref.mesh.positions),
                maxDistance(d.mesh.layers[0].delta, ref.mesh.layers[0].delta));
    CHECK(maxDistance(d.mesh.positions, ref.mesh.positions) < 1e-5f);
    CHECK(maxDistance(d.mesh.base, ref.mesh.base) < 1e-5f);
    CHECK(maxDistance(d.mesh.layers[0].delta, ref.mesh.layers[0].delta) < 1e-5f);
    // And the converted details turn with the base.
    const std::vector<glm::vec3> before = layerOffsets(d), top = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "transform", {{"rotate", "0,0,90"}, {"pivot", "origin"}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    const glm::mat3 R = glm::mat3_cast(eulerRotation(glm::vec3(0, 0, 90)));
    std::vector<glm::vec3> turned(top.size()), turnedOffsets(before.size());
    for (size_t v = 0; v < top.size(); ++v) turned[v] = R * top[v];
    for (size_t v = 0; v < before.size(); ++v) turnedOffsets[v] = R * before[v];
    CHECK(maxDistance(d.mesh.positions, turned) < 1e-4f);
    CHECK(maxDistance(layerOffsets(d), turnedOffsets) < 1e-4f);
    // Saved again, it's format 5.
    const std::string path = tempPath("sf_levels_v5.sfproj");
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(maxDistance(e.mesh.positions, d.mesh.positions) < 1e-6f);
    std::filesystem::remove(path);
}

// Only the active Part keeps cached levels: leaving a Part frees its cache, and stepping levels after
// coming back still works (the level is rebuilt once).
TEST(multires_inactive_parts_free_their_level_cache) {
    Document d = makeDoc(makeCube(4));
    for (int i = 0; i < 3; ++i) run(d, "subdiv_divide");
    stroke(d, "0,0,2");
    CHECK(run(d, "subdiv_lower").ok);
    CHECK(run(d, "subdiv_higher").ok);
    const size_t cached = subdivCacheBytes(d.mesh);
    const size_t stackBytes = subdivBytes(d.mesh);
    std::printf("  cache %zu bytes, stack %zu bytes\n", cached, stackBytes);
    CHECK(cached > d.mesh.triangleCount() * 12); // at least the level left
    const std::vector<glm::vec3> top = d.mesh.positions;
    d.addPart(makeIcosphere(1), "Eye", true); // selects the new Part
    CHECK(subdivCacheBytes(d.partMesh(0)) == 0);
    CHECK(subdivBytes(d.partMesh(0)) == stackBytes); // the levels themselves are kept
    d.selectPart(0);
    CHECK(subdivLevelText(d.mesh) == "Level 4/4");
    CHECK(bitEqual(d.mesh.positions, top));
    CHECK(run(d, "subdiv_lower").ok);
    CHECK(run(d, "subdiv_higher").ok);
    CHECK(bitEqual(d.mesh.positions, top));
    CHECK(subdivCacheBytes(d.mesh) > 0);
    // A mesh kept by an undo step after a remesh holds no cache either.
    CHECK(run(d, "decimate", {{"ratio", 0.5}}).ok);
    CHECK(run(d, "undo").ok);
    CHECK(subdivCacheBytes(d.mesh) == 0);
    CHECK(run(d, "subdiv_lower").ok);
    CHECK(run(d, "subdiv_higher").ok);
    CHECK(bitEqual(d.mesh.positions, top));
}
