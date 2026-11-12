// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Booleans between Parts (core/MeshBoolean.h): Union, Subtract and Intersect on cubes and spheres (closed, watertight,
// volumes right), coplanar faces, touching edges, open and self-intersecting input (a clear failure or the voxel
// path, never a crash), face groups and seams.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Cut.h"
#include "core/MeshBoolean.h"
#include "core/PartBoolean.h"
#include "core/Parts.h"
#include "core/Primitives.h"
#include "core/Project.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sf;

namespace {

Mesh moved(Mesh m, const glm::vec3& d) {
    for (glm::vec3& p : m.positions) p += d;
    m.recomputeAllNormals();
    return m;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

const CommandRegistry& registry() {
    static CommandRegistry r;
    return r;
}

CommandResult run(Document& d, const std::string& name, const Args& args = {}) { return registry().execute(d, name, args); }

// Base (a cube), Bolt (a sphere through its corner) and Slot (a box through its middle).
Document makeScene() {
    Document doc;
    doc.setMesh(makeCube(4), "Base");
    doc.addPart(moved(makeIcosphere(3, 0.6f), {1.0f, 1.0f, 0.2f}), "Bolt", false);
    Mesh slot = makeCube(2, 1.0f);
    for (glm::vec3& p : slot.positions) p *= glm::vec3(0.4f, 3.0f, 0.4f);
    slot.recomputeAllNormals();
    doc.addPart(std::move(slot), "Slot", false);
    return doc;
}

} // namespace

TEST(boolean_cubes_offset) {
    const Mesh a = makeCube(4); // -1..1
    const Mesh b = moved(makeCube(4), {1.0f, 0.5f, 0.3f});
    const double overlap = 1.0 * 1.5 * 1.7;
    struct Case {
        BooleanOp op;
        double volume;
    } cases[] = {{BooleanOp::Union, 16.0 - overlap}, {BooleanOp::Subtract, 8.0 - overlap}, {BooleanOp::Intersect, overlap}};
    for (const Case& c : cases) {
        Mesh out;
        std::string error;
        BooleanInfo info;
        CHECK(booleanExact(a, b, c.op, out, error, {}, &info));
        CHECK(error.empty());
        CHECK(isWatertight(out));
        CHECK(near(meshVolume(out), c.volume, 1e-4));
        CHECK(info.cutTriangles > 0);
    }
}

TEST(boolean_coplanar_faces) {
    // B's top, bottom, front and back faces lie in A's planes: the nudge has to settle them, the output keeps them flat.
    const Mesh a = makeCube(3);
    const Mesh b = moved(makeCube(3), {1.0f, 0.0f, 0.0f});
    for (BooleanOp op : {BooleanOp::Union, BooleanOp::Subtract, BooleanOp::Intersect}) {
        Mesh out;
        std::string error;
        BooleanInfo info;
        CHECK(booleanExact(a, b, op, out, error, {}, &info));
        CHECK(isWatertight(out));
        const double want = op == BooleanOp::Union ? 12.0 : 4.0;
        CHECK(near(meshVolume(out), want, 1e-3));
        // Flat: every vertex stays inside the union's box (no step a nudge's size left standing out).
        const AABB box = out.bounds();
        CHECK(box.min.y >= -1.0f - 1e-5f && box.max.y <= 1.0f + 1e-5f && box.max.z <= 1.0f + 1e-5f);
    }
    // The same cube on itself: Union is the cube, Subtract leaves nothing.
    Mesh out;
    std::string error;
    CHECK(booleanExact(a, a, BooleanOp::Union, out, error));
    CHECK(isWatertight(out) && near(meshVolume(out), 8.0, 1e-3));
    CHECK(!booleanExact(a, a, BooleanOp::Subtract, out, error));
    CHECK(error.find("nothing") != std::string::npos);
}

TEST(boolean_flush_cutter_opens) {
    // A box cutter whose top lies exactly in the cube's top face: Subtract opens a pocket (no zero-thickness film left
    // across it), Union is the cube, Intersect the box.
    const Mesh a = makeCube(3);
    Mesh b = makeCube(2, 1.0f);
    for (glm::vec3& p : b.positions) p = glm::vec3(p.x * 0.8f, p.y + 0.5f, p.z * 0.8f); // x, z: +-0.4; y: 0..1
    b.recomputeAllNormals();
    Mesh out;
    std::string error;
    CHECK(booleanExact(a, b, BooleanOp::Subtract, out, error));
    CHECK(isWatertight(out));
    CHECK(near(meshVolume(out), 8.0 - 0.64, 1e-3));
    size_t film = 0; // triangles on the top plane over the pocket
    for (const glm::uvec3& t : out.triangles) {
        const glm::vec3 c = (out.positions[t.x] + out.positions[t.y] + out.positions[t.z]) / 3.0f;
        if (std::abs(c.y - 1.0f) < 1e-4f && std::abs(c.x) < 0.39f && std::abs(c.z) < 0.39f) ++film;
    }
    CHECK(film == 0);
    CHECK(booleanExact(a, b, BooleanOp::Union, out, error));
    CHECK(isWatertight(out) && near(meshVolume(out), 8.0, 1e-3));
    CHECK(booleanExact(a, b, BooleanOp::Intersect, out, error));
    CHECK(isWatertight(out) && near(meshVolume(out), 0.64, 1e-3));
}

TEST(boolean_touching_edges) {
    // B touches A along one edge only.
    const Mesh a = makeCube(2);
    const Mesh b = moved(makeCube(2), {2.0f, 2.0f, 0.0f});
    Mesh out;
    std::string error;
    CHECK(booleanExact(a, b, BooleanOp::Union, out, error));
    CHECK(near(meshVolume(out), 16.0, 1e-3));
    CHECK(booleanExact(a, b, BooleanOp::Subtract, out, error));
    CHECK(isWatertight(out) && near(meshVolume(out), 8.0, 1e-3));
    // Face against face: B sits on A's right side.
    const Mesh c = moved(makeCube(2), {2.0f, 0.0f, 0.0f});
    CHECK(booleanExact(a, c, BooleanOp::Union, out, error));
    CHECK(near(meshVolume(out), 16.0, 1e-3));
    CHECK(booleanExact(a, c, BooleanOp::Subtract, out, error));
    CHECK(isWatertight(out) && near(meshVolume(out), 8.0, 1e-3));
}

TEST(boolean_spheres_volumes) {
    const Mesh a = makeIcosphere(3, 1.0f);
    const Mesh b = moved(makeIcosphere(3, 0.8f), {0.9f, 0.2f, 0.1f});
    Mesh u, d, i;
    std::string error;
    CHECK(booleanExact(a, b, BooleanOp::Union, u, error));
    CHECK(booleanExact(a, b, BooleanOp::Subtract, d, error));
    CHECK(booleanExact(a, b, BooleanOp::Intersect, i, error));
    CHECK(isWatertight(u) && isWatertight(d) && isWatertight(i));
    const double va = meshVolume(a), vb = meshVolume(b);
    // Polyhedra: the identities hold exactly, up to rounding.
    CHECK(near(meshVolume(u) + meshVolume(i), va + vb, 1e-4));
    CHECK(near(meshVolume(d) + meshVolume(i), va, 1e-4));
    CHECK(meshVolume(i) > 0.1 && meshVolume(i) < vb);
}

TEST(boolean_inside_and_apart) {
    const Mesh a = makeIcosphere(2, 1.0f);
    const Mesh small = makeIcosphere(2, 0.5f);
    Mesh out;
    std::string error;
    // Entirely inside: Subtract makes a hollow (two shells), Intersect is the small one, Union the big one.
    CHECK(booleanExact(a, small, BooleanOp::Subtract, out, error));
    CHECK(isWatertight(out));
    CHECK(near(meshVolume(out), meshVolume(a) - meshVolume(small), 1e-5));
    CHECK(booleanExact(a, small, BooleanOp::Intersect, out, error));
    CHECK(near(meshVolume(out), meshVolume(small), 1e-5));
    CHECK(booleanExact(a, small, BooleanOp::Union, out, error));
    CHECK(near(meshVolume(out), meshVolume(a), 1e-5));
    // Apart: Union keeps both, Intersect has nothing to keep.
    const Mesh far = moved(makeIcosphere(2, 0.5f), {5.0f, 0.0f, 0.0f});
    CHECK(booleanExact(a, far, BooleanOp::Union, out, error));
    CHECK(near(meshVolume(out), meshVolume(a) + meshVolume(far), 1e-5));
    CHECK(!booleanExact(a, far, BooleanOp::Intersect, out, error));
    CHECK(error.find("don't overlap") != std::string::npos);
}

TEST(boolean_open_and_self_intersecting_input) {
    const Mesh a = makeCube(3);
    const Mesh open = makePlane(4, 1.5f); // an open sheet through the cube
    Mesh out;
    std::string error;
    CHECK(!booleanExact(a, open, BooleanOp::Subtract, out, error));
    CHECK(error.find("isn't closed") != std::string::npos);
    BooleanOptions noFallback;
    noFallback.voxelFallback = false;
    error.clear();
    CHECK(!booleanMeshes(a, open, BooleanOp::Subtract, out, error, noFallback));
    CHECK(!error.empty());
    // The voxel path: an open sheet has no inside, which it says.
    error.clear();
    CHECK(!booleanMeshes(a, open, BooleanOp::Subtract, out, error));
    CHECK(error.find("open") != std::string::npos);
    // Non-manifold: an edge shared by three faces (a fin glued onto the cube).
    Mesh fin = a;
    fin.positions.push_back({0.0f, 3.0f, 0.0f});
    fin.triangles.push_back({fin.triangles[0].x, fin.triangles[0].y, uint32_t(fin.positions.size() - 1)});
    fin.rebuildTopology();
    error.clear();
    CHECK(!booleanExact(fin, a, BooleanOp::Union, out, error));
    CHECK(error.find("isn't closed") != std::string::npos);
    // Self-intersecting: two overlapping cubes in one mesh. The exact path refuses it or stays closed; the voxel
    // path always gives a closed surface. Either way quickly.
    const Mesh c = moved(makeCube(3), {0.7f, 0.4f, 0.2f});
    const Mesh twice = mergeMeshes({&a, &c});
    const Mesh tool = moved(makeIcosphere(2, 0.8f), {1.2f, 0.6f, 0.0f});
    const auto t0 = std::chrono::steady_clock::now();
    error.clear();
    BooleanInfo info;
    const bool ok = booleanMeshes(twice, tool, BooleanOp::Subtract, out, error, {}, &info);
    CHECK(ok || !error.empty());
    if (ok) CHECK(out.triangleCount() > 0);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0);
    // The voxel path directly: a closed result about the right size.
    CHECK(booleanVoxel(a, moved(makeCube(3), {1.0f, 0.5f, 0.3f}), BooleanOp::Union, 128, out, error));
    CHECK(isWatertight(out));
    CHECK(near(meshVolume(out), 16.0 - 2.55, 0.3));
}

TEST(boolean_face_groups_uvs_and_seams) {
    Mesh a = makeCube(4); // one group per side (0..5)
    Mesh b = moved(makeIcosphere(3, 0.7f), {1.0f, 0.0f, 0.0f});
    b.ensureFaceGroups(100);
    Mesh out;
    std::string error;
    CHECK(booleanExact(a, b, BooleanOp::Subtract, out, error));
    bool hasA = false, hasB = false;
    for (uint32_t g : out.facegroups) {
        hasA = hasA || g < 6;
        hasB = hasB || g == 100;
    }
    CHECK(hasA && hasB);
    // Unwelded: the same surface, but each operand's piece has its own copy of the seam (open along it).
    BooleanOptions o;
    o.weld = false;
    Mesh loose;
    CHECK(booleanExact(a, b, BooleanOp::Subtract, loose, error, o));
    CHECK(loose.triangleCount() == out.triangleCount());
    CHECK(loose.vertexCount() > out.vertexCount());
    CHECK(!isWatertight(loose));
    CHECK(near(meshVolume(loose), meshVolume(out), 1e-6));
}

TEST(boolean_commands_registered) {
    for (const char* name : {"part_boolean", "boolean_parts", "make_boolean_mesh"}) {
        CHECK(registry().find(name) != nullptr);
        CHECK(registry().toolSchemaJson().find(std::string("\"") + name + "\"") != std::string::npos);
    }
    BooleanOp op;
    CHECK(parseBooleanOp("subtract", op) && op == BooleanOp::Subtract);
    PartBoolean mode;
    CHECK(parsePartBoolean("Intersect", mode) && mode == PartBoolean::Intersect);
    CHECK(!parsePartBoolean("xor", mode));
}

TEST(boolean_make_mesh_and_undo) {
    Document doc = makeScene();
    CHECK(!run(doc, "make_boolean_mesh").ok); // (nobody takes part yet)
    CHECK(run(doc, "part_boolean", {{"name", std::string("Base")}, {"mode", std::string("add")}}).ok);
    CHECK(run(doc, "part_boolean", {{"name", std::string("Bolt")}, {"mode", std::string("subtract")}}).ok);
    CHECK(run(doc, "part_boolean", {{"name", std::string("Slot")}, {"mode", std::string("subtract")}}).ok);
    CHECK(booleanPartIndices(doc).size() == 3);
    const size_t before = doc.partCount();
    const CommandResult r = run(doc, "make_boolean_mesh");
    CHECK(r.ok);
    CHECK(r.message.find("exact") != std::string::npos);
    CHECK(doc.partCount() == int(before) + 1);
    CHECK(doc.activePartInfo().name == "Boolean");
    CHECK(doc.activePart == 3);
    CHECK(isWatertight(doc.mesh));
    // Sources untouched, hidden; the result takes no part itself.
    CHECK(!doc.parts[0].visible && !doc.parts[1].visible && !doc.parts[2].visible);
    CHECK(doc.partMesh(0).triangleCount() == makeCube(4).triangleCount());
    CHECK(doc.activePartInfo().boolean == PartBoolean::None);
    CHECK(booleanPartIndices(doc).empty());
    // The volume: the cube less the slot through it, less the bolt's share outside the slot (positive, below 8 - slot).
    const double v = meshVolume(doc.mesh);
    CHECK(v > 1.0 && v < 8.0 - 0.4 * 0.4 * 2.0);
    // Groups: the cube's six, plus one per cutter.
    uint32_t top = 0;
    for (uint32_t g : doc.mesh.facegroups) top = std::max(top, g);
    CHECK(top == 8); // (the cube's are 1-6, the bolt's 7, the slot's 8)
    // One undo step: the result goes, the sources show again.
    CHECK(run(doc, "undo").ok);
    CHECK(doc.partCount() == int(before));
    CHECK(doc.parts[0].visible && doc.parts[1].visible && doc.parts[2].visible);
    CHECK(doc.findPart("Boolean") < 0);
}

TEST(boolean_parts_pair_and_options) {
    Document doc = makeScene();
    CHECK(!run(doc, "boolean_parts", {{"op", std::string("xor")}, {"a", std::string("Base")}, {"b", std::string("Bolt")}}).ok);
    CHECK(!run(doc, "boolean_parts", {{"op", std::string("union")}, {"a", std::string("Base")}, {"b", std::string("Nope")}}).ok);
    const CommandResult r = run(doc, "boolean_parts", {{"op", std::string("intersect")}, {"a", std::string("0")}, {"b", std::string("Bolt")}, {"cut_group", false}});
    CHECK(r.ok);
    CHECK(isWatertight(doc.mesh));
    CHECK(doc.partMesh(doc.findPart("Slot")).triangleCount() > 0 && doc.parts[size_t(doc.findPart("Slot"))].visible);
    CHECK(!doc.parts[0].visible && !doc.parts[1].visible);
    // Without the cut group the bolt keeps its own (single) group, after the cube's 1-6.
    bool bolt = false;
    for (uint32_t g : doc.mesh.facegroups) bolt = bolt || g == 7;
    CHECK(bolt);
    // Unwelded seams: an open result.
    CHECK(run(doc, "undo").ok);
    CHECK(run(doc, "boolean_parts", {{"op", std::string("union")}, {"a", std::string("Base")}, {"b", std::string("Bolt")}, {"weld", false}}).ok);
    CHECK(!isWatertight(doc.mesh));
    CHECK(!run(doc, "boolean_parts", {{"op", std::string("union")}, {"a", std::string("Base")}, {"b", std::string("Base")}}).ok);
    CHECK(!run(doc, "make_boolean_mesh", {{"resolution", 4.0}}).ok);
}

TEST(boolean_open_part_command) {
    // An open Part: the exact path can't run; without the fallback that's a clear failure, with it the voxel path
    // still has no inside to work with (a flat sheet), which it says. Nothing changes either way.
    Document doc;
    doc.setMesh(makeCube(3), "Base");
    doc.addPart(makePlane(4, 1.5f), "Sheet", false);
    const int parts = doc.partCount();
    const CommandResult r = run(doc, "boolean_parts", {{"op", std::string("subtract")}, {"a", std::string("Base")}, {"b", std::string("Sheet")}, {"voxel_fallback", false}});
    CHECK(!r.ok);
    CHECK(r.message.find("isn't closed") != std::string::npos);
    CHECK(!run(doc, "boolean_parts", {{"op", std::string("subtract")}, {"a", std::string("Base")}, {"b", std::string("Sheet")}}).ok);
    CHECK(doc.partCount() == parts);
    CHECK(doc.parts[0].visible && doc.parts[1].visible);
}

TEST(boolean_modes_saved_in_project) {
    Document doc = makeScene();
    doc.parts[0].boolean = PartBoolean::Add;
    doc.parts[1].boolean = PartBoolean::Intersect;
    doc.parts[2].boolean = PartBoolean::Subtract;
    const std::string path = sf::test::tempPath("sf_boolean_modes.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 3);
    CHECK(back.parts[0].boolean == PartBoolean::Add);
    CHECK(back.parts[1].boolean == PartBoolean::Intersect);
    CHECK(back.parts[2].boolean == PartBoolean::Subtract);
    // And the result Part saves like any other.
    CHECK(run(back, "make_boolean_mesh").ok);
    const size_t tris = back.mesh.triangleCount();
    CHECK(saveProject(path, back, err));
    Document again;
    CHECK(loadProject(path, again, err));
    const int i = again.findPart("Boolean");
    CHECK(i >= 0);
    CHECK(again.partMesh(i).triangleCount() == tris);
    CHECK(!again.parts[0].visible);
}

// Opt-in bench (labels bench + nightly): SF_BOOLEAN_BENCH=1 times Union / Subtract / Intersect of two quad spheres of
// 100k and of 1M triangles each, overlapping by half a radius (best of 3), and the live preview's path (shapes only).
TEST(nightly_boolean_bench) {
    if (!std::getenv("SF_BOOLEAN_BENCH")) return;
    for (int res : {91, 289}) { // 6 * 2 * res^2 triangles: ~100k, ~1M
        const Mesh a = makeQuadSphere(res, 2.0f);
        Mesh b = makeQuadSphere(res, 2.0f);
        for (glm::vec3& p : b.positions) p = p * 0.8f + glm::vec3(0.55f, 0.21f, 0.13f);
        b.recomputeAllNormals();
        std::printf("operands: 2 x %zu triangles\n", a.triangleCount());
        for (BooleanOp op : {BooleanOp::Union, BooleanOp::Subtract, BooleanOp::Intersect}) {
            double best = 1e30;
            size_t tris = 0;
            BooleanInfo info;
            for (int k = 0; k < 3; ++k) {
                Mesh out;
                std::string error;
                const auto t0 = std::chrono::steady_clock::now();
                CHECK(booleanExact(a, b, op, out, error, {}, &info));
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                tris = out.triangleCount();
                CHECK(isWatertight(out));
            }
            std::printf("  %-9s %8.1f ms  -> %zu triangles, %zu cut, %d run(s)\n", booleanOpName(op), best, tris,
                        info.cutTriangles, info.attempts);
        }
        // The preview's path: copies of the shapes, combined (Subtract).
        double best = 1e30;
        for (int k = 0; k < 3; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<BooleanOperand> ops(2);
            ops[0].mesh.positions = a.positions;
            ops[0].mesh.triangles = a.triangles;
            ops[1].mesh.positions = b.positions;
            ops[1].mesh.triangles = b.triangles;
            ops[1].mode = PartBoolean::Subtract;
            Mesh out;
            std::string error;
            CHECK(combineOperands(std::move(ops), {}, out, error));
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::printf("  preview   %8.1f ms  (copy + Subtract + topology)\n", best);
    }
}
