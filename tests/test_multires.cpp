// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multiresolution subdivision levels (core/Multires.h): Divide keeps quads, detail survives stepping up and
// down bit-for-bit, low-level edits carry the high-level detail along, high-level edits show up below, mask /
// face groups / UVs / paint / sculpt layers follow the levels, undo steps across levels, Delete Lower / Delete
// Higher / Reconstruct, projects keep every level, and exports can write the base.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Parts.h"
#include "io/ObjIO.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

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

// One stroke towards -z through `points` ("x,y,z x,y,z").
bool stroke(Document& d, const std::string& points, const std::string& brush = "Standard", double radius = 0.25,
            double strength = 0.8, bool invert = false) {
    return run(d, "stroke", {{"points", points}, {"brush", brush}, {"radius", radius}, {"strength", strength},
                             {"invert", invert}}).ok;
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

Document dividedCube(int levels, int resolution = 3) {
    Document d = makeDoc(makeCube(resolution));
    for (int i = 0; i < levels; ++i) run(d, "subdiv_divide");
    return d;
}

size_t countObjFaces(const std::string& path, int corners) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    std::string line;
    size_t n = 0;
    while (std::getline(f, line)) {
        if (line.rfind("f ", 0) != 0) continue;
        std::istringstream in(line.substr(2));
        std::string tok;
        int c = 0;
        while (in >> tok) ++c;
        if (c == corners) ++n;
    }
    return n;
}

} // namespace

TEST(multires_divide_keeps_quads_and_counts) {
    Document d = makeDoc(makeCube(3));
    const size_t baseTris = d.mesh.triangleCount();
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(run(d, "subdiv_divide").ok);
    CHECK(subdivLevelCount(d.mesh) == 2);
    CHECK(subdivCurrentLevel(d.mesh) == 1);
    CHECK(d.mesh.triangleCount() == baseTris * 4);
    CHECK(d.mesh.quadCount() * 2 == d.mesh.triangleCount()); // Catmull-Clark: all quads
    CHECK(run(d, "subdiv_divide").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 3/3");
    CHECK(d.mesh.triangleCount() == baseTris * 16);
    CHECK(d.mesh.quadCount() * 2 == d.mesh.triangleCount());
    CHECK(subdivLevelTriangles(d.mesh, 0) == baseTris);
    // Smooth: the cube's corners round off (a corner vertex moves inwards).
    float maxR = 0.0f;
    for (const auto& p : d.mesh.positions) maxR = std::max(maxR, glm::length(p));
    CHECK(maxR < std::sqrt(3.0f) - 0.05f);
    // Closed and consistent: every vertex has triangles, normals are finite.
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) CHECK(!d.mesh.vertexTriangles(v).empty());
    // Triangle meshes use Loop subdivision and stay triangles.
    Document t = makeDoc(makeIcosphere(2));
    const size_t tris = t.mesh.triangleCount();
    CHECK(run(t, "subdiv_divide").ok);
    CHECK(t.mesh.triangleCount() == tris * 4);
    CHECK(t.mesh.quadCount() == 0);
    // Loop on a sphere stays close to the sphere.
    for (const auto& p : t.mesh.positions) CHECK(std::abs(glm::length(p) - 1.0f) < 0.05f);
}

TEST(multires_flat_divide_keeps_the_shape) {
    Document d = makeDoc(makeCube(2));
    CHECK(run(d, "subdiv_divide", {{"smooth", false}}).ok);
    // Flat: every new vertex lies on the cube's faces.
    for (const auto& p : d.mesh.positions) {
        const float m = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
        CHECK(std::abs(m - 1.0f) < 1e-5f);
    }
}

TEST(multires_detail_survives_round_trips_bit_for_bit) {
    Document d = dividedCube(3);
    CHECK(stroke(d, "-0.3,0.2,2 0.3,0.25,2", "Standard", 0.2));
    CHECK(stroke(d, "0.1,-0.4,2 0.2,0.4,2", "Crease", 0.1));
    const std::vector<glm::vec3> top = d.mesh.positions;
    const std::vector<glm::vec3> before = subdivLevelMesh(d.mesh, 0).positions;
    for (int level : {1, 3, 2, 4, 1, 4}) CHECK(run(d, "subdiv_level", {{"level", double(level)}}).ok);
    CHECK(subdivCurrentLevel(d.mesh) == 3);
    CHECK(bitEqual(d.mesh.positions, top));
    // The lower levels didn't drift either.
    CHECK(bitEqual(subdivLevelMesh(d.mesh, 0).positions, before));
    CHECK(run(d, "subdiv_lower").ok);
    CHECK(run(d, "subdiv_higher").ok);
    CHECK(bitEqual(d.mesh.positions, top));
    CHECK(!run(d, "subdiv_higher").ok); // already at the top
}

TEST(multires_high_edit_shows_at_lower_levels) {
    Document d = dividedCube(2);
    const std::vector<glm::vec3> base0 = subdivLevelMesh(d.mesh, 0).positions;
    const std::vector<glm::vec3> top0 = d.mesh.positions;
    CHECK(stroke(d, "-0.3,0,2 0.3,0,2", "Inflate", 0.6, 1.0));
    const std::vector<glm::vec3> top1 = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    // Base vertices keep their index at every level: where the top moved a broad stroke, the base follows
    // (softened), and the back face stays put.
    float backMove = 0.0f;
    int followed = 0, moved = 0;
    for (size_t v = 0; v < base0.size(); ++v) {
        const float top = glm::length(top1[v] - top0[v]), low = glm::length(d.mesh.positions[v] - base0[v]);
        if (top > 0.02f) {
            ++moved;
            followed += low > 0.5f * top && low < 1.5f * top;
        }
        if (base0[v].z < -0.9f) backMove = std::max(backMove, low);
    }
    CHECK(moved >= 4);
    CHECK(followed == moved);
    CHECK(backMove == 0.0f);
}

TEST(multires_low_edit_carries_the_detail) {
    Document d = dividedCube(3);
    CHECK(stroke(d, "-0.2,0.1,2 0.2,0.1,2", "Crease", 0.12));
    const std::vector<glm::vec3> top = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    // A rigid move of the base cage (rotate 90 degrees about y, then shift) moves the detail with it.
    const glm::mat4 xf = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -0.25f, 1.0f)) *
                         glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    for (auto& p : d.mesh.positions) p = glm::vec3(xf * glm::vec4(p, 1.0f));
    d.mesh.recomputeAllNormals();
    CHECK(run(d, "subdiv_level", {{"level", 4.0}}).ok);
    std::vector<glm::vec3> expected = top;
    for (auto& p : expected) p = glm::vec3(xf * glm::vec4(p, 1.0f));
    CHECK(maxDistance(d.mesh.positions, expected) < 1e-4f);

    // A local edit low down moves the top level smoothly: far away nothing moves, nearby it does, and
    // neighbouring vertices move by similar amounts (no tearing).
    Document e = dividedCube(3);
    CHECK(stroke(e, "0.1,0.1,2", "Crease", 0.1)); // detail on the front
    const std::vector<glm::vec3> top2 = e.mesh.positions;
    CHECK(run(e, "subdiv_level", {{"level", 2.0}}).ok);
    const std::vector<glm::vec3> low = e.mesh.positions;
    CHECK(stroke(e, "-0.2,0,2 0.2,0,2", "Inflate", 0.5, 1.0));
    const float lowMove = maxDistance(e.mesh.positions, low);
    CHECK(run(e, "subdiv_level", {{"level", 4.0}}).ok);
    float far = 0.0f, near = 0.0f, jump = 0.0f;
    for (size_t v = 0; v < top2.size(); ++v) {
        const float m = glm::length(e.mesh.positions[v] - top2[v]);
        if (top2[v].z < -0.5f) far = std::max(far, m);
        if (top2[v].z > 0.9f && glm::length(glm::vec2(top2[v])) < 0.2f) near = std::max(near, m);
        for (uint32_t u : e.mesh.neighbors(uint32_t(v)))
            jump = std::max(jump, glm::length((e.mesh.positions[v] - top2[v]) - (e.mesh.positions[u] - top2[u])));
    }
    CHECK(lowMove > 0.01f);
    CHECK(far < 1e-6f);
    CHECK(near > 0.5f * lowMove);
    CHECK(jump < near * 0.5f);
}

TEST(multires_undo_steps_across_levels) {
    Document d = dividedCube(2);
    const std::vector<glm::vec3> top0 = d.mesh.positions;
    CHECK(stroke(d, "0,0,2", "Standard", 0.3));
    const std::vector<glm::vec3> top1 = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    const std::vector<glm::vec3> low1 = d.mesh.positions;
    CHECK(stroke(d, "0.3,0.3,2", "Inflate", 0.5, 1.0));
    const std::vector<glm::vec3> low2 = d.mesh.positions;
    CHECK(!bitEqual(low1, low2));
    // Undo: the low stroke, then the step (back at the top, exactly as sculpted), then the top stroke.
    CHECK(run(d, "undo").ok);
    CHECK(subdivCurrentLevel(d.mesh) == 0);
    CHECK(bitEqual(d.mesh.positions, low1));
    CHECK(run(d, "undo").ok);
    CHECK(subdivCurrentLevel(d.mesh) == 2);
    CHECK(bitEqual(d.mesh.positions, top1));
    CHECK(run(d, "undo").ok);
    CHECK(bitEqual(d.mesh.positions, top0));
    // Redo all three: the same shapes again.
    CHECK(run(d, "redo").ok);
    CHECK(bitEqual(d.mesh.positions, top1));
    CHECK(run(d, "redo").ok);
    CHECK(subdivCurrentLevel(d.mesh) == 0);
    CHECK(maxDistance(d.mesh.positions, low1) < 1e-5f);
    CHECK(run(d, "redo").ok);
    CHECK(maxDistance(d.mesh.positions, low2) < 1e-5f);
    // Undo everything, then the lower level follows the undone top stroke when stepped to.
    for (int i = 0; i < 3; ++i) CHECK(run(d, "undo").ok);
    CHECK(bitEqual(d.mesh.positions, top0));
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, subdivLevelMesh(dividedCube(2).mesh, 0).positions) < 1e-5f);
    // Undoing the first Divide gives back the plain mesh.
    Document p = makeDoc(makeCube(3));
    const std::vector<glm::vec3> plain = p.mesh.positions;
    CHECK(run(p, "subdiv_divide").ok);
    CHECK(run(p, "undo").ok);
    CHECK(subdivLevelCount(p.mesh) == 0);
    CHECK(bitEqual(p.mesh.positions, plain));
    CHECK(run(p, "redo").ok);
    CHECK(subdivLevelCount(p.mesh) == 2);
    CHECK(p.mesh.triangleCount() == makeCube(3).triangleCount() * 4);
}

TEST(multires_attributes_follow_levels) {
    Document d = dividedCube(2);
    // Mask painted at the top shows at the corners of the base; face groups set low are inherited up.
    run(d, "mask_all");
    d.mesh.ensureMask();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) d.mesh.mask[v] = d.mesh.positions[v].x > 0 ? 1.0f : 0.0f;
    const std::vector<float> topMask = d.mesh.mask;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(d.mesh.hasMask());
    const Mesh base = subdivLevelMesh(d.mesh, 0);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) CHECK(d.mesh.mask[v] == (base.positions[v].x > 0 ? 1.0f : 0.0f));
    d.mesh.ensureFaceGroups();
    for (auto& g : d.mesh.facegroups) g = 7;
    d.mesh.facegroups[0] = 9;
    d.mesh.facegroups[1] = 9; // one quad
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.mask == topMask); // untouched mask detail kept
    size_t nine = 0;
    for (uint32_t g : d.mesh.facegroups) nine += g == 9;
    CHECK(nine == 2 * 16); // the quad's 16 grandchildren
    for (uint32_t g : d.mesh.facegroups) CHECK(g == 7 || g == 9);
    // Hidden faces at the top hide their parents below.
    d.mesh.ensureHidden();
    for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) d.mesh.hidden[t] = d.mesh.facegroups[t] == 9;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(d.mesh.hasHidden() && d.mesh.hidden[0] && d.mesh.hidden[1] && !d.mesh.hidden[2]);
    // Clearing the mask clears it everywhere.
    d.mesh.clearMask();
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(!d.mesh.hasMask());
    // UVs: divided with the mesh, kept through round trips.
    Document u = makeDoc(makePlane(4));
    u.mesh.uvs.resize(u.mesh.triangleCount() * 3);
    for (size_t t = 0; t < u.mesh.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = u.mesh.positions[u.mesh.triangles[t][c]];
            u.mesh.uvs[t * 3 + size_t(c)] = {p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f};
        }
    CHECK(run(u, "subdiv_divide").ok);
    CHECK(u.mesh.uvs.size() == u.mesh.triangleCount() * 3);
    // On a plane the UVs follow the (flat) positions linearly.
    bool linear = true;
    for (size_t t = 0; t < u.mesh.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const glm::vec3 p = u.mesh.positions[u.mesh.triangles[t][c]];
            const glm::vec2 uv = u.mesh.uvs[t * 3 + size_t(c)];
            linear = linear && std::abs(uv.x - (p.x * 0.5f + 0.5f)) + std::abs(uv.y - (p.z * 0.5f + 0.5f)) < 0.2f;
        }
    CHECK(linear);
    const std::vector<glm::vec2> uvs = u.mesh.uvs;
    CHECK(run(u, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(u, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(u.mesh.uvs == uvs);
    // Vertex paint painted low is interpolated up; paint at the top survives a low-level paint elsewhere.
    Document c = dividedCube(2);
    c.mesh.ensureColors();
    c.mesh.colors[0] = glm::vec3(1, 0, 0);
    CHECK(run(c, "subdiv_level", {{"level", 1.0}}).ok);
    c.mesh.ensureColors();
    c.mesh.colors[5] = glm::vec3(0, 0, 1);
    CHECK(run(c, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(c.mesh.colors[0] == glm::vec3(1, 0, 0));
    CHECK(c.mesh.colors[5] == glm::vec3(0, 0, 1));
}

TEST(multires_sculpt_layers_work_at_every_level) {
    Document d = dividedCube(2);
    CHECK(run(d, "layer_add", {{"name", "Smile"}}).ok);
    d.activeLayer = 0;
    CHECK(stroke(d, "0,0,2", "Standard", 0.4));
    const std::vector<glm::vec3> withLayer = d.mesh.positions;
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(d.mesh.layers.size() == 1 && d.mesh.layers[0].name == "Smile");
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 0.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.layers[0].weight == 0.0f);
    // Weight 0 at the top: the layer's bump is gone, the base remains.
    CHECK(maxDistance(d.mesh.positions, d.mesh.base) < 1e-6f);
    CHECK(run(d, "layer_set_weight", {{"index", 0.0}, {"weight", 1.0}}).ok);
    CHECK(maxDistance(d.mesh.positions, withLayer) < 1e-5f);
    CHECK(run(d, "layer_remove", {{"index", 0.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(d.mesh.layers.empty());
}

TEST(multires_delete_lower_and_higher) {
    Document d = dividedCube(3);
    CHECK(stroke(d, "0,0,2", "Standard", 0.3));
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    const std::vector<glm::vec3> level2 = d.mesh.positions;
    const size_t tris2 = d.mesh.triangleCount();
    CHECK(run(d, "subdiv_delete_lower").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/3");
    CHECK(bitEqual(d.mesh.positions, level2));
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.triangleCount() == tris2 * 16);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(run(d, "subdiv_delete_higher").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 2/2");
    CHECK(!run(d, "subdiv_higher").ok);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 2/3");
    // Deleting down to one level leaves a plain mesh.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "subdiv_delete_higher").ok);
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
}

TEST(multires_reconstruct_finds_lower_levels) {
    // A Catmull-Clark subdivided cube, imported as a plain mesh.
    Document src = dividedCube(2, 2);
    CHECK(stroke(src, "0,0,2", "Standard", 0.3));
    Mesh plain = subdivLevelMesh(src.mesh, 2);
    const std::vector<glm::vec3> fine = plain.positions;
    Document d = makeDoc(std::move(plain));
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/2");
    CHECK(d.mesh.triangleCount() == src.mesh.triangleCount() / 4);
    CHECK(d.mesh.quadCount() * 2 == d.mesh.triangleCount());
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/3");
    CHECK(d.mesh.triangleCount() == makeCube(2).triangleCount());
    CHECK(d.mesh.vertexCount() == makeCube(2).vertexCount());
    // The base sits on the fine level's corner vertices.
    for (const auto& p : d.mesh.positions) CHECK(std::find(fine.begin(), fine.end(), p) != fine.end());
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(bitEqual(d.mesh.positions, fine)); // the top is exactly the mesh it started from
    CHECK(!run(d, "subdiv_reconstruct").ok);   // only at the lowest level
    // Loop: an icosphere divided twice reconstructs down to the icosphere's topology.
    Document t = makeDoc(makeIcosphere(1));
    run(t, "subdiv_divide");
    run(t, "subdiv_divide");
    Mesh tplain = subdivLevelMesh(t.mesh, 2);
    Document r = makeDoc(std::move(tplain));
    CHECK(run(r, "subdiv_reconstruct").ok);
    CHECK(run(r, "subdiv_reconstruct").ok);
    CHECK(r.mesh.triangleCount() == makeIcosphere(1).triangleCount());
    CHECK(maxDistance(r.mesh.positions, subdivLevelMesh(t.mesh, 0).positions) < 0.1f);
    // A mesh that isn't a subdivision is refused, and undo restores the plain mesh.
    Document n = makeDoc(makeIcosphere(2));
    n.mesh.triangles[0] = {n.mesh.triangles[0].x, n.mesh.triangles[0].z, n.mesh.triangles[0].y};
    Document ok = makeDoc(makeCube(3));
    CHECK(!run(ok, "subdiv_reconstruct").ok); // a 3x3 cube isn't a 2x subdivision
    CHECK(subdivLevelCount(ok.mesh) == 0);
    CHECK(run(d, "undo").ok); // the step to level 3
    CHECK(run(d, "undo").ok); // the second reconstruct
    CHECK(subdivLevelText(d.mesh) == "SDiv 1/2");
}

TEST(multires_project_round_trip_keeps_every_level) {
    Document d = dividedCube(3);
    CHECK(stroke(d, "0,0,2", "Standard", 0.3));
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(stroke(d, "0.4,0.4,2", "Inflate", 0.4, 1.0)); // pending, not folded in yet
    d.mesh.ensureFaceGroups()[3] = 5;
    std::vector<std::vector<glm::vec3>> levels;
    for (int i = 0; i < 4; ++i) levels.push_back(subdivLevelMesh(d.mesh, i).positions);
    const std::string path = tempPath("sf_multires.sfproj");
    std::string err;
    CHECK(saveProject(path, d, err));
    Document e;
    CHECK(loadProject(path, e, err));
    CHECK(subdivLevelText(e.mesh) == "SDiv 2/4");
    CHECK(bitEqual(e.mesh.positions, levels[1]));
    CHECK(e.mesh.facegroupAt(3) == 5);
    for (int i : {0, 3, 2, 1}) {
        CHECK(run(e, "subdiv_level", {{"level", double(i + 1)}}).ok);
        CHECK(maxDistance(e.mesh.positions, levels[size_t(i)]) < 1e-6f);
    }
    // Saved through the command too (which folds pending edits in first), and a second Part keeps its own levels.
    e.addPart(makeIcosphere(1), "Eye", true);
    run(e, "subdiv_divide");
    CHECK(run(e, "save_project", {{"path", path}}).ok);
    Document f;
    CHECK(loadProject(path, f, err));
    CHECK(f.partCount() == 2);
    CHECK(subdivLevelText(f.mesh) == "SDiv 2/2");
    f.selectPart(0);
    CHECK(subdivLevelText(f.mesh) == "SDiv 2/4");
    CHECK(run(f, "subdiv_level", {{"level", 4.0}}).ok);
    CHECK(maxDistance(f.mesh.positions, levels[3]) < 1e-6f);
    std::filesystem::remove(path);
}

TEST(multires_parts_exports_and_info) {
    Document d = dividedCube(2);
    d.addPart(makeIcosphere(1), "Eye", true);
    CHECK(subdivLevelCount(d.mesh) == 0);
    d.selectPart(0);
    CHECK(subdivLevelText(d.mesh) == "SDiv 3/3");
    CHECK(run(d, "part_list").message.find("SDiv 3/3") != std::string::npos);
    CHECK(run(d, "subdiv_info").message.find("SDiv 1: 108 triangles") != std::string::npos);
    // Exports write the current level; subdiv_export the base.
    const std::string cur = tempPath("sf_multires_cur.obj"), base = tempPath("sf_multires_base.obj");
    d.parts[1].visible = false;
    CHECK(run(d, "save_obj", {{"path", cur}}).ok);
    CHECK(run(d, "subdiv_export", {{"path", base}}).ok);
    CHECK(countObjFaces(cur, 4) == 54 * 16);
    CHECK(countObjFaces(base, 4) == 54);
    CHECK(subdivLevelText(d.mesh) == "SDiv 3/3"); // exporting the base doesn't change the scene
    std::filesystem::remove(cur);
    std::filesystem::remove(base);
    // A topology change (remesh, the old flat subdivide) drops the levels.
    CHECK(run(d, "subdivide").ok);
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
}
