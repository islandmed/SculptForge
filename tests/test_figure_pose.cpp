// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figure Pose (core/FigurePose.h, docs/design/pose-all.md): the Pose Mesh, replaying its moves on the full-resolution
// Parts, one undo step across Parts, Cancel, and the cases Apply refuses.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/FigurePose.h"
#include "core/Multires.h"
#include "core/MeshTransform.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace sf;

namespace {

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& a = {}) { return registry().execute(d, name, a); }

template <class V> bool sameBits(const std::vector<V>& a, const std::vector<V>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0);
}

// An arm along X (dense) and a small "eye" ball at its far end: two Parts.
Document makeFigure(int armSubdivisions = 6) {
    Mesh arm = makeIcosphere(armSubdivisions, 1.0f);
    for (auto& p : arm.positions) p *= glm::vec3(3.0f, 0.6f, 0.6f);
    arm.recomputeAllNormals();
    Document d = makeDoc(std::move(arm));
    d.parts[0].name = "Arm";
    Mesh eye = makeIcosphere(3, 0.2f);
    for (auto& p : eye.positions) p += glm::vec3(2.6f, 0.0f, 0.65f);
    eye.recomputeAllNormals();
    d.addPart(std::move(eye), "Eye", false);
    return d;
}

void maskBelowX(Mesh& m, float edge) {
    m.ensureMask();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.mask[v] = m.positions[v].x < edge ? 1.0f : 0.0f;
}

} // namespace

TEST(figure_pose_builds_one_light_mesh) {
    Document d = makeFigure();
    const size_t armTris = d.partMesh(0).triangleCount();
    const FigurePoseResult r = figurePoseBegin(d, 50000);
    CHECK(r.ok);
    CHECK(r.parts == 2);
    CHECK(figurePoseEditing(d));
    CHECK(d.partCount() == 3);
    CHECK(d.mesh.triangleCount() < armTris);
    CHECK(d.mesh.triangleCount() <= 50000 + 2 * kFigurePosePartFloor);
    // One face group per source Part; the sources are hidden, the Pose Mesh shows.
    CHECK(d.mesh.hasFaceGroups());
    const auto [lo, hi] = std::minmax_element(d.mesh.facegroups.begin(), d.mesh.facegroups.end());
    CHECK(*lo == 1 && *hi == 2);
    CHECK(!d.parts[0].visible && !d.parts[1].visible);
    CHECK(d.activePartInfo().visible);
    CHECK(run(d, "figure_pose_info").message.find("2 Parts") != std::string::npos);
    CHECK(!figurePoseBegin(d).ok); // one at a time
}

TEST(figure_pose_rigid_move_is_exact) {
    Document d = makeFigure(5);
    const auto arm = d.partMesh(0).positions, eye = d.partMesh(1).positions;
    CHECK(figurePoseBegin(d, 50000).ok);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,30")}, {"translate", std::string("0,1,0")},
                               {"pivot", std::string("0,0,0")}})
              .ok);
    const FigurePoseResult r = figurePoseApply(d);
    CHECK(r.ok);
    CHECK(r.parts == 2 && r.steps == 1);
    CHECK(!figurePoseActive(d) && d.partCount() == 2);
    CHECK(d.parts[0].visible && d.parts[1].visible);
    CHECK(d.activePart == 0);
    MeshTransform t;
    t.rotate = eulerRotation(glm::vec3(0, 0, 30));
    t.translate = glm::vec3(0, 1, 0);
    float err = 0.0f;
    for (size_t v = 0; v < arm.size(); ++v) err = std::max(err, glm::length(d.partMesh(0).positions[v] - t.apply(arm[v])));
    for (size_t v = 0; v < eye.size(); ++v) err = std::max(err, glm::length(d.partMesh(1).positions[v] - t.apply(eye[v])));
    CHECK(err < 1e-4f);
}

// A Part moved as a whole (a linked copy made unique keeps its transform) poses in scene space like the rest; a
// linked copy is left out, shown and untouched, because posing its shared mesh would pose every copy.
TEST(figure_pose_moved_parts_pose_in_scene_space_and_linked_copies_stay_out) {
    Document d = makeFigure(4);
    const int c = d.linkedDuplicate(1, glm::vec3(0.0f, 0.0f, -1.3f)); // a second eye, linked
    CHECK(c >= 0);
    const uint32_t copyId = d.parts[size_t(c)].id;
    const int moved = d.linkedDuplicate(1, glm::vec3(0.0f, 1.0f, 0.0f)); // (inserted after the eye: indices shift)
    CHECK(d.makeUnique(moved));
    const int copy = d.partIndex(copyId);
    CHECK(d.parts[size_t(moved)].moved());
    auto world = [&](int i) {
        std::vector<glm::vec3> w;
        for (const glm::vec3& p : d.partMesh(i).positions) w.push_back(glm::vec3(d.parts[size_t(i)].transform * glm::vec4(p, 1.0f)));
        return w;
    };
    const auto movedWorld = world(moved), linkedBefore = d.partMesh(1).positions;
    const FigurePoseResult b = figurePoseBegin(d, 50000);
    CHECK(b.ok);
    CHECK(b.parts == 2); // the Arm and the moved (now unique) eye; both linked eyes are left out
    CHECK(b.message.find("Linked copies left out") != std::string::npos);
    CHECK(d.parts[1].visible && d.parts[size_t(copy)].visible);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,30")}, {"pivot", std::string("0,0,0")}}).ok);
    CHECK(figurePoseApply(d).ok);
    MeshTransform t;
    t.rotate = eulerRotation(glm::vec3(0, 0, 30));
    const auto after = world(moved);
    float err = 0.0f;
    for (size_t v = 0; v < after.size(); ++v) err = std::max(err, glm::length(after[v] - t.apply(movedWorld[v])));
    CHECK(err < 1e-4f);
    CHECK(sameBits(d.partMesh(1).positions, linkedBefore));
    // One undo puts the moved Part back too.
    CHECK(run(d, "undo").ok);
    const auto undone = world(moved);
    err = 0.0f;
    for (size_t v = 0; v < undone.size(); ++v) err = std::max(err, glm::length(undone[v] - movedWorld[v]));
    CHECK(err < 1e-5f);
}

TEST(figure_pose_soft_joint_matches_full_resolution) {
    // A masked rotation with a soft falloff, posed on the Pose Mesh, lands close to the same rotation done on the
    // full-resolution arm directly, and the still part stays exactly still.
    Document ref = makeFigure();
    maskBelowX(ref.mesh, 0.0f);
    CHECK(run(ref, "transform", {{"rotate", std::string("0,0,45")}, {"pivot", std::string("0,0,0")}, {"falloff", 0.1}}).ok);

    Document d = makeFigure();
    const auto before = d.partMesh(0).positions;
    CHECK(figurePoseBegin(d, 50000).ok);
    maskBelowX(d.mesh, 0.0f);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,45")}, {"pivot", std::string("0,0,0")}, {"falloff", 0.1}}).ok);
    CHECK(figurePoseApply(d).ok);
    const Mesh& arm = d.partMesh(0);
    float worst = 0.0f;
    bool still = true;
    for (size_t v = 0; v < before.size(); ++v) {
        worst = std::max(worst, glm::length(arm.positions[v] - ref.mesh.positions[v]));
        if (before[v].x < -0.8f) still = still && arm.positions[v] == before[v];
    }
    CHECK(still);
    CHECK(worst < 0.05f); // arm is 6 long: under 1% of its length anywhere
    // The eye, at the moving end, turned with the arm.
    CHECK(d.partMesh(1).positions[0] != makeFigure(3).partMesh(1).positions[0]);
}

TEST(figure_pose_is_one_undo_step_across_parts) {
    Document d = makeFigure(5);
    const auto arm = d.partMesh(0).positions, eye = d.partMesh(1).positions;
    const auto armN = d.partMesh(0).normals;
    CHECK(figurePoseBegin(d, 50000).ok);
    maskBelowX(d.mesh, 0.0f);
    CHECK(run(d, "transform", {{"rotate", std::string("0,0,30")}, {"pivot", std::string("0,0,0")}, {"falloff", 0.1}}).ok);
    CHECK(run(d, "transform", {{"translate", std::string("0,0,0.5")}, {"masked", false}}).ok);
    CHECK(figurePoseApply(d).ok);
    const auto armPosed = d.partMesh(0).positions, eyePosed = d.partMesh(1).positions;
    CHECK(!sameBits(armPosed, arm) && !sameBits(eyePosed, eye));
    CHECK(d.undo.undoCount() == 1);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.partMesh(0).positions, arm));
    CHECK(sameBits(d.partMesh(1).positions, eye));
    CHECK(sameBits(d.partMesh(0).normals, armN));
    CHECK(run(d, "redo").ok);
    CHECK(sameBits(d.partMesh(0).positions, armPosed));
    CHECK(sameBits(d.partMesh(1).positions, eyePosed));
    // From the other Part too.
    d.selectPart(1);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.partMesh(0).positions, arm));
    CHECK(sameBits(d.partMesh(1).positions, eye));
}

TEST(figure_pose_undo_on_the_pose_mesh_drops_the_move) {
    Document d = makeFigure(5);
    const auto arm = d.partMesh(0).positions;
    CHECK(figurePoseBegin(d, 50000).ok);
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    CHECK(run(d, "transform", {{"translate", std::string("0,0,2")}}).ok);
    CHECK(run(d, "undo").ok); // only the first move stays
    CHECK(figurePoseApply(d).ok);
    bool exact = true;
    for (size_t v = 0; v < arm.size(); ++v)
        exact = exact && glm::length(d.partMesh(0).positions[v] - (arm[v] + glm::vec3(0, 1, 0))) < 1e-5f;
    CHECK(exact);
}

TEST(figure_pose_cancel_changes_nothing) {
    Document d = makeFigure(4);
    d.parts[1].visible = false; // a hidden Part stays out and stays hidden
    const auto arm = d.partMesh(0).positions;
    CHECK(figurePoseBegin(d, 50000).ok);
    CHECK(d.activePartInfo().name == "Pose Mesh");
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    CHECK(run(d, "figure_pose_cancel").ok);
    CHECK(!figurePoseActive(d));
    CHECK(d.partCount() == 2 && d.activePart == 0);
    CHECK(d.parts[0].visible && !d.parts[1].visible);
    CHECK(sameBits(d.mesh.positions, arm));
    CHECK(!run(d, "figure_pose_apply").ok);
}

TEST(figure_pose_leaves_parts_edited_during_the_pose) {
    Document d = makeFigure(4);
    CHECK(figurePoseBegin(d, 50000).ok);
    const int pose = d.activePart;
    d.selectPart(1); // edit the eye meanwhile
    CHECK(run(d, "transform", {{"translate", std::string("1,0,0")}}).ok);
    const auto eye = d.mesh.positions;
    d.selectPart(pose);
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    const FigurePoseResult r = figurePoseApply(d);
    CHECK(r.ok && r.parts == 1);
    CHECK(r.message.find("Eye") != std::string::npos);
    CHECK(sameBits(d.partMesh(1).positions, eye));
}

TEST(figure_pose_refuses_moves_it_cannot_replay) {
    Document d = makeFigure(4);
    CHECK(figurePoseBegin(d, 50000).ok);
    UndoRecord r; // a step that moved positions without a transform (a brush stroke)
    r.label = "Clay";
    r.positions.add(0, d.mesh.positions[0]);
    d.undo.push(std::move(r));
    const FigurePoseResult res = figurePoseApply(d);
    CHECK(!res.ok);
    CHECK(res.message.find("Clay") != std::string::npos);
    CHECK(figurePoseActive(d));
    // Only Gizmo moves record their op: a transform on any other Part keeps none.
    d.selectPart(0);
    CHECK(run(d, "transform", {{"translate", std::string("0,1,0")}}).ok);
    CHECK(!d.undo.lastUndo()->transformOp);
}

// A Part with subdivision levels poses from its lowest level that has its share (no decimation of the full
// resolution), and every level takes the pose.
TEST(figure_pose_starts_from_a_lower_subdivision_level) {
    Document d = makeDoc(makeIcosphere(4, 1.0f)); // 5120 triangles
    for (int i = 0; i < 3; ++i) CHECK(run(d, "subdiv_divide").ok); // 20480, 81920, 327680
    CHECK(subdivLevelCount(d.mesh) == 4);
    const auto top = d.mesh.positions;
    const Mesh level0 = subdivLevelMesh(d.mesh, 0);
    CHECK(figurePoseBegin(d, 50000).ok);
    CHECK(d.mesh.triangleCount() == 81920); // level 2: the lowest with 50000, within twice the share
    CHECK(run(d, "transform", {{"rotate", std::string("0,40,0")}, {"translate", std::string("0.5,0,0")},
                               {"pivot", std::string("0,0,0")}, {"masked", std::string("false")}})
              .ok);
    CHECK(figurePoseApply(d).ok);
    MeshTransform t;
    t.rotate = eulerRotation(glm::vec3(0, 40, 0));
    t.translate = glm::vec3(0.5f, 0, 0);
    CHECK(subdivLevelCount(d.mesh) == 4 && subdivCurrentLevel(d.mesh) == 3);
    float err = 0.0f;
    for (size_t v = 0; v < top.size(); ++v) err = std::max(err, glm::length(d.mesh.positions[v] - t.apply(top[v])));
    CHECK(err < 1e-4f);
    // The base cage follows by multires's restriction (a blend of the fine vertices' moves, core/Multires.h): close
    // to the rigid move, off by about |R - I| times the cage's distance from the surface.
    const Mesh posed0 = subdivLevelMesh(d.mesh, 0);
    err = 0.0f;
    for (size_t v = 0; v < level0.positions.size(); ++v)
        err = std::max(err, glm::length(posed0.positions[v] - t.apply(level0.positions[v])));
    std::printf("  base cage off the rigid move by %.5f (radius 1)\n", err);
    CHECK(err < 0.01f);
}

TEST(figure_pose_sculpt_layers_turn_with_the_surface) {
    Document d = makeFigure(4);
    d.mesh.addLayer("Detail");
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.layers[0].delta[v] = d.mesh.normals[v] * 0.05f;
    d.mesh.setLayerWeight(0, 1.0f);
    const auto delta = d.mesh.layers[0].delta;
    CHECK(figurePoseBegin(d, 50000).ok);
    CHECK(run(d, "transform", {{"rotate", std::string("0,90,0")}, {"pivot", std::string("0,0,0")}}).ok);
    CHECK(figurePoseApply(d).ok);
    const glm::mat3 R = glm::mat3_cast(eulerRotation(glm::vec3(0, 90, 0)));
    float err = 0.0f;
    for (size_t v = 0; v < delta.size(); ++v) err = std::max(err, glm::length(d.mesh.layers[0].delta[v] - R * delta[v]));
    CHECK(err < 1e-5f);
    CHECK(run(d, "undo").ok);
    CHECK(sameBits(d.mesh.layers[0].delta, delta));
}

TEST(figure_pose_saved_mid_pose_reopens_as_before) {
    Document d = makeFigure(3);
    CHECK(figurePoseBegin(d, 50000).ok);
    const std::string path = tempPath("sf_figure_pose_test.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(e.partCount() == 3);
    CHECK(e.parts[0].visible && e.parts[1].visible && !e.parts[2].visible); // the Pose Mesh comes back hidden
    std::filesystem::remove(path);
}

// Bench (docs/design/pose-all.md §4), off unless SF_FIGURE_POSE_MTRIS lists sizes in millions of triangles (e.g.
// "20,50"): per size, 20 Parts, three moves on the Pose Mesh (a masked rotation with a soft joint, a move, a turn of
// everything), then Apply; Begin / Apply times and peak memory go to stdout and to SF_FIGURE_POSE_OUT if set.
TEST(nightly_figure_pose_bench) {
    const char* env = std::getenv("SF_FIGURE_POSE_MTRIS");
    if (!env || !*env) return;
    const char* outPath = std::getenv("SF_FIGURE_POSE_OUT");
    if (outPath) std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
    std::FILE* out = outPath ? std::fopen(outPath, "w") : nullptr;
    auto statusMb = [](const char* key) {
        std::FILE* f = std::fopen("/proc/self/status", "r");
        long kb = 0;
        char line[256];
        while (f && std::fgets(line, sizeof line, f))
            if (std::strncmp(line, key, std::strlen(key)) == 0) kb = std::atol(line + std::strlen(key));
        if (f) std::fclose(f);
        return double(kb) / 1024.0;
    };
    for (const char* p = env; *p;) {
        const double mtris = std::max(0.1, std::atof(p));
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
        constexpr int kParts = 20;
        // SF_FIGURE_POSE_LEVELS=1: each Part is a cage divided 3 times (64x the triangles), so Begin starts from a
        // lower level instead of decimating.
        const bool levels = std::getenv("SF_FIGURE_POSE_LEVELS") != nullptr;
        const int res = std::max(levels ? 2 : 4,
                                 int(std::lround(std::sqrt(mtris * 1e6 / kParts / 12.0 / (levels ? 64.0 : 1.0)))));
        auto part = [&](int i) {
            Mesh m = makeCube(res, 1.0f);
            for (auto& q : m.positions) q += glm::vec3(float(i % 5) * 1.5f, float(i / 5) * 1.5f, 0.0f);
            std::string error;
            for (int l = 0; levels && l < 3; ++l) CHECK(subdivDivide(m, true, error));
            return m;
        };
        size_t tris = 0;
        char line[1024];
        {
            Document d = makeDoc(part(0));
            for (int i = 1; i < kParts; ++i) d.addPart(part(i), "Part " + std::to_string(i), false);
            for (int i = 0; i < d.partCount(); ++i) tris += d.partMesh(i).triangleCount();
            if (std::FILE* f = std::fopen("/proc/self/clear_refs", "w")) { // the peak (VmHWM) starts again from here
                std::fputs("5", f);
                std::fclose(f);
            }
            const double base = statusMb("VmRSS:");
            const FigurePoseResult b = figurePoseBegin(d);
            CHECK(b.ok);
            const double beginS = d.figurePose->beginSeconds, afterBegin = statusMb("VmRSS:");
            const size_t poseTris = d.mesh.triangleCount();
            maskBelowX(d.mesh, 3.0f);
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(run(d, "transform", {{"rotate", std::string("0,0,40")}, {"pivot", std::string("3,3,0")}, {"falloff", 0.1}}).ok);
            const double moveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            CHECK(run(d, "transform", {{"translate", std::string("0,0.5,0")}}).ok);
            CHECK(run(d, "transform", {{"rotate", std::string("0,20,0")}, {"masked", false}}).ok);
            const FigurePoseResult a = figurePoseApply(d);
            CHECK(a.ok && a.parts == kParts);
            std::snprintf(line, sizeof line,
                          "| %d%s | %.1fM | %zu | %d | %.2f s | +%.0f MB | %.0f ms | %.2f s | %.2f s | %.2f s | %zu | +%.0f MB | %.0f MB |\n",
                          kParts, levels ? " (3 levels)" : "", double(tris) / 1e6, poseTris, ThreadPool::shared().threads(), beginS,
                          afterBegin - base, moveMs, a.bindSeconds + a.replaySeconds, a.bindSeconds, a.replaySeconds,
                          a.vertices, statusMb("VmHWM:") - base, base);
        }
        std::fputs(line, stdout);
        if (out) {
            std::fputs(line, out);
            std::fflush(out);
        }
    }
    if (out) std::fclose(out);
}
