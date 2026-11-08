// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Multiresolution subdivision levels (core/Multires.h): Divide keeps quads, detail survives stepping up and
// down bit-for-bit, low-level edits carry the high-level detail along, high-level edits show up below, mask /
// face groups / UVs / paint / sculpt layers follow the levels, undo steps across levels, Remove Below / Delete
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
#include <map>
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
    CHECK(subdivLevelText(d.mesh) == "Level 3/3");
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
    CHECK(subdivLevelText(d.mesh) == "Level 1/3");
    CHECK(bitEqual(d.mesh.positions, level2));
    CHECK(run(d, "subdiv_level", {{"level", 3.0}}).ok);
    CHECK(d.mesh.triangleCount() == tris2 * 16);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(run(d, "subdiv_delete_higher").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 2/2");
    CHECK(!run(d, "subdiv_higher").ok);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 2/3");
    // Deleting down to one level leaves a plain mesh.
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "subdiv_delete_higher").ok);
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
}

// A review hit "Dynamic topology needs a mesh without subdivision levels" on the divided default sphere: Freeze
// Levels keeps the level shown as a plain mesh in one undo step, and dyntopo freeze_levels=true does it and turns
// Dynamic on.
TEST(multires_freeze_levels_for_dynamic) {
    Document d = dividedCube(3);
    CHECK(stroke(d, "0,0,2", "Standard", 0.3));
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    const std::vector<glm::vec3> level2 = d.mesh.positions;
    const size_t tris2 = d.mesh.triangleCount();
    CHECK(!run(d, "dyntopo", {{"enabled", true}}).ok); // still blocked without the option
    CHECK(!d.sculptor.dyntopo.enabled);
    CHECK(run(d, "subdiv_freeze").ok);
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(d.mesh.triangleCount() == tris2);
    CHECK(bitEqual(d.mesh.positions, level2));
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 2/4");
    CHECK(bitEqual(d.mesh.positions, level2));
    // In one go, from the top level.
    Document e = dividedCube(2);
    const size_t top = e.mesh.triangleCount();
    const CommandResult r = run(e, "dyntopo", {{"enabled", true}, {"freeze_levels", true}});
    CHECK(r.ok);
    CHECK(e.sculptor.dyntopo.enabled);
    CHECK(subdivLevelCount(e.mesh) == 0);
    CHECK(e.mesh.triangleCount() == top);
    CHECK(run(e, "undo").ok);
    CHECK(subdivLevelCount(e.mesh) == 3);
    // Nothing to freeze on a plain mesh.
    Document p = makeDoc(makeCube(3));
    CHECK(!run(p, "subdiv_freeze").ok);
}

TEST(multires_reconstruct_finds_lower_levels) {
    // A Catmull-Clark subdivided cube, imported as a plain mesh.
    Document src = dividedCube(2, 2);
    CHECK(stroke(src, "0,0,2", "Standard", 0.3));
    Mesh plain = subdivLevelMesh(src.mesh, 2);
    const std::vector<glm::vec3> fine = plain.positions;
    Document d = makeDoc(std::move(plain));
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 1/2");
    CHECK(d.mesh.triangleCount() == src.mesh.triangleCount() / 4);
    CHECK(d.mesh.quadCount() * 2 == d.mesh.triangleCount());
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 1/3");
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
    CHECK(subdivLevelText(d.mesh) == "Level 1/2");
}

// #145: `coarse` (all quads) gets UDIM UVs, every quad in its own tile (so every edge is a seam), is divided once
// and imported as a plain mesh; Reconstruct must find `coarse` again with each face's corner UVs (not UVs from
// across seams or tiles), and stepping back up must give back the imported UVs exactly.
static void checkReconstructKeepsUvs(Mesh coarse, bool flip = false) {
    std::vector<uint32_t> quadOf(coarse.triangleCount());
    uint32_t quads = 0;
    for (uint32_t t = 0; t < coarse.triangleCount(); ++t) {
        const uint32_t q = coarse.quadPartnerAt(t);
        quadOf[t] = q != Mesh::kNoQuad && q < t ? quadOf[q] : quads++;
    }
    coarse.uvs.resize(coarse.triangleCount() * 3);
    for (size_t t = 0; t < coarse.triangleCount(); ++t)
        for (int c = 0; c < 3; ++c) {
            const uint32_t q = quadOf[t], v = coarse.triangles[t][c];
            const uint32_t h = (q * 2654435761u) ^ (v * 40503u + 977u);
            coarse.uvs[t * 3 + size_t(c)] = glm::vec2(float(q % 10), float(q / 10)) +
                                            glm::vec2(0.05f + 0.9f * float(h % 1000) / 1000.0f, 0.05f + 0.9f * float(h / 1000 % 1000) / 1000.0f);
        }
    Document src = makeDoc(std::move(coarse));
    CHECK(run(src, "subdiv_divide").ok);
    Mesh plain = subdivLevelMesh(src.mesh, 1);
    // An imported mesh numbers its vertices its own way (here: backwards), not base corners first.
    const uint32_t nf = uint32_t(plain.vertexCount());
    std::reverse(plain.positions.begin(), plain.positions.end());
    for (glm::uvec3& t : plain.triangles) t = glm::uvec3(nf - 1) - t;
    // ... and may split its quads along the other diagonal (edge point to edge point, not corner to face point).
    for (uint32_t t = 0; flip && t < plain.triangleCount(); ++t) {
        const uint32_t u = plain.quadPartnerAt(t);
        if (u == Mesh::kNoQuad || u < t) continue;
        const glm::uvec3 a = plain.triangles[t], b = plain.triangles[u];
        auto uvOf = [&](uint32_t v) {
            for (uint32_t x : {t, u})
                for (int c = 0; c < 3; ++c)
                    if (plain.triangles[x][c] == v) return plain.uvs[size_t(x) * 3 + size_t(c)];
            return glm::vec2(0.0f);
        };
        int k = 0; // a's corner off the shared diagonal
        for (int c = 0; c < 3; ++c)
            if (b.x != a[c] && b.y != a[c] && b.z != a[c]) k = c;
        uint32_t w = b.x; // b's corner off it
        for (int c = 0; c < 3; ++c)
            if (b[c] != a.x && b[c] != a.y && b[c] != a.z) w = b[c];
        const uint32_t q[4] = {a[k], a[(k + 1) % 3], w, a[(k + 2) % 3]};
        const glm::vec2 uv[4] = {uvOf(q[0]), uvOf(q[1]), uvOf(q[2]), uvOf(q[3])};
        plain.triangles[t] = {q[0], q[1], q[2]};
        plain.triangles[u] = {q[0], q[2], q[3]};
        for (int c = 0; c < 3; ++c) {
            plain.uvs[size_t(t) * 3 + size_t(c)] = uv[c];
            plain.uvs[size_t(u) * 3 + size_t(c)] = uv[c == 0 ? 0 : c + 1];
        }
    }
    plain.recomputeAllNormals();
    const std::vector<glm::vec2> fineUvs = plain.uvs;
    const std::vector<glm::uvec3> fineTris = plain.triangles;
    const std::vector<glm::vec3> finePositions = plain.positions;
    Document d = makeDoc(std::move(plain));
    CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelText(d.mesh) == "Level 1/2");
    const Mesh base = subdivLevelMesh(src.mesh, 0);
    CHECK(d.mesh.triangleCount() == base.triangleCount() && d.mesh.vertexCount() == base.vertexCount());
    CHECK(d.mesh.uvs.size() == d.mesh.triangleCount() * 3);
    if (d.mesh.uvs.size() != d.mesh.triangleCount() * 3 || d.mesh.vertexCount() != base.vertexCount()) return;
    // The base sits on the fine level's corners, which a divided level numbers as the base vertices.
    std::vector<uint32_t> baseIndex(d.mesh.vertexCount(), UINT32_MAX);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        for (uint32_t b = 0; b < base.vertexCount(); ++b)
            if (d.mesh.positions[v] == finePositions[nf - 1 - b]) baseIndex[v] = b;
    // Every base triangle lies in one original quad and has that quad's corner UVs.
    size_t wrong = 0;
    for (size_t t = 0; t < d.mesh.triangleCount(); ++t) {
        bool found = false;
        for (uint32_t b = 0; b < base.triangleCount() && !found; ++b) {
            const uint32_t partner = base.quadPartnerAt(b);
            auto uvAt = [&](uint32_t v, glm::vec2& uv) {
                for (uint32_t bt : {b, partner}) {
                    if (bt == Mesh::kNoQuad) continue;
                    for (int k = 0; k < 3; ++k)
                        if (base.triangles[bt][k] == v) {
                            uv = base.uvs[size_t(bt) * 3 + size_t(k)];
                            return true;
                        }
                }
                return false;
            };
            bool all = true;
            for (int c = 0; c < 3 && all; ++c) {
                glm::vec2 uv;
                all = uvAt(baseIndex[d.mesh.triangles[t][c]], uv) && uv == d.mesh.uvs[t * 3 + size_t(c)];
            }
            found = all;
        }
        if (!found && wrong++ == 0)
            std::printf("  base triangle %zu: UVs (%g,%g) (%g,%g) (%g,%g) aren't one original face's\n", t, d.mesh.uvs[t * 3].x,
                        d.mesh.uvs[t * 3].y, d.mesh.uvs[t * 3 + 1].x, d.mesh.uvs[t * 3 + 1].y, d.mesh.uvs[t * 3 + 2].x,
                        d.mesh.uvs[t * 3 + 2].y);
    }
    CHECK(wrong == 0);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(d.mesh.triangles == fineTris);
    CHECK(d.mesh.uvs == fineUvs);
    CHECK(run(d, "subdiv_level", {{"level", 1.0}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", 2.0}}).ok);
    CHECK(d.mesh.uvs == fineUvs);
}

TEST(multires_reconstruct_keeps_udim_uvs) {
    checkReconstructKeepsUvs(makePlane(4));        // open: a border
    checkReconstructKeepsUvs(makeTorus(12));       // closed, every vertex regular: four readings of the corners fit,
    checkReconstructKeepsUvs(makeTorus(12), true); // and with these diagonals the first one tried is a shifted one
    checkReconstructKeepsUvs(makeCube(3), true);
}

// #145 reopened: the reconstructed LOWER levels broke. `coarse` gets UDIM islands (by face direction; the x < 0 half
// mirrored onto the x > 0 half's tile), is divided `levels` times, and the top goes out and back in as an OBJ; then
// Reconstruct runs `levels` times. Every reconstructed level must be the divided one: each triangle inside one
// original face, with that face's corner UVs (so seams kept, islands intact, values in their UDIM tile).
static void checkReconstructLevelsKeepUvs(const char* name, Mesh coarse, int levels) {
    std::printf("  %s\n", name);
    coarse.recomputeAllNormals();
    coarse.uvs.resize(coarse.triangleCount() * 3);
    for (uint32_t t = 0; t < coarse.triangleCount(); ++t) {
        uint32_t f = t; // a quad's two triangles share an island: pick by the pair's first triangle
        if (coarse.quadPartnerAt(t) != Mesh::kNoQuad) f = std::min(t, coarse.quadPartnerAt(t));
        const glm::uvec3 tf = coarse.triangles[f];
        const glm::vec3 p0 = coarse.positions[tf.x], p1 = coarse.positions[tf.y], p2 = coarse.positions[tf.z];
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0), c = (p0 + p1 + p2) / 3.0f;
        int axis = 0;
        for (int a = 1; a < 3; ++a)
            if (std::abs(n[a]) > std::abs(n[axis])) axis = a;
        const uint32_t island = uint32_t(axis * 2 + (n[axis] < 0 ? 1 : 0)) * 2 + (c.y > 0.0f ? 1 : 0);
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 p = coarse.positions[coarse.triangles[t][k]];
            glm::vec2 uv(p[(axis + 1) % 3], p[(axis + 2) % 3]);
            if (axis != 0) uv.x = std::abs(uv.x); // mirrored: both halves on one tile
            uv = glm::clamp(uv * 0.2f + 0.5f, glm::vec2(0.01f), glm::vec2(0.99f));
            coarse.uvs[size_t(t) * 3 + size_t(k)] = uv + glm::vec2(float(island % 10), float(island / 10));
        }
    }
    Document src = makeDoc(std::move(coarse));
    for (int i = 0; i < levels; ++i) CHECK(run(src, "subdiv_divide").ok);
    const Mesh top = subdivLevelMesh(src.mesh, levels);
    const std::string path = tempPath("sf_reconstruct_uvs.obj");
    std::string err;
    CHECK(saveObj(path, top, err));
    Mesh plain;
    CHECK(loadObj(path, plain, err));
    std::filesystem::remove(path);
    // Divided levels number their vertices coarse corners first, so source level k's vertex b is the top's b (the OBJ
    // keeps the order; its positions and UVs are rounded to 7 digits).
    CHECK(plain.vertexCount() == top.vertexCount());
    std::map<std::tuple<float, float, float>, uint32_t> topIndex;
    for (uint32_t v = 0; v < plain.vertexCount(); ++v) topIndex[{plain.positions[v].x, plain.positions[v].y, plain.positions[v].z}] = v;
    Document d = makeDoc(std::move(plain));
    for (int i = 0; i < levels; ++i) CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelCount(d.mesh) == levels + 1);
    if (subdivLevelCount(d.mesh) != levels + 1) return;
    for (int k = 0; k < levels; ++k) {
        const Mesh want = subdivLevelMesh(src.mesh, k), got = subdivLevelMesh(d.mesh, k);
        CHECK(got.triangleCount() == want.triangleCount() && got.vertexCount() == want.vertexCount());
        CHECK(got.uvs.size() == got.triangleCount() * 3);
        if (got.uvs.size() != got.triangleCount() * 3 || got.vertexCount() != want.vertexCount()) continue;
        // The reconstructed level's corners sit on the top's vertices (the top is the imported mesh).
        std::vector<uint32_t> srcIndex(got.vertexCount(), UINT32_MAX);
        for (uint32_t v = 0; v < got.vertexCount(); ++v) {
            auto it = topIndex.find({got.positions[v].x, got.positions[v].y, got.positions[v].z});
            if (it != topIndex.end()) srcIndex[v] = it->second;
        }
        // Source triangles by vertex, then: each triangle lies in one source face and has its corner UVs.
        std::vector<std::vector<uint32_t>> facesOf(want.vertexCount());
        for (uint32_t t = 0; t < want.triangleCount(); ++t)
            for (int c = 0; c < 3; ++c) facesOf[want.triangles[t][c]].push_back(t);
        size_t wrong = 0;
        for (uint32_t t = 0; t < got.triangleCount(); ++t) {
            bool found = false;
            const uint32_t v0 = srcIndex[got.triangles[t].x];
            for (uint32_t b : v0 < want.vertexCount() ? facesOf[v0] : std::vector<uint32_t>{}) {
                auto uvAt = [&](uint32_t v, glm::vec2& uv) {
                    for (uint32_t bt : {b, want.quadPartnerAt(b)}) {
                        if (bt == Mesh::kNoQuad) continue;
                        for (int c = 0; c < 3; ++c)
                            if (want.triangles[bt][c] == v) {
                                uv = want.uvs[size_t(bt) * 3 + size_t(c)];
                                return true;
                            }
                    }
                    return false;
                };
                bool all = true;
                for (int c = 0; c < 3 && all; ++c) {
                    glm::vec2 uv;
                    all = uvAt(srcIndex[got.triangles[t][c]], uv) &&
                          glm::all(glm::lessThan(glm::abs(uv - got.uvs[size_t(t) * 3 + size_t(c)]), glm::vec2(1e-5f)));
                }
                if ((found = all)) break;
            }
            if (!found && wrong++ == 0)
                std::printf("    Level %d triangle %u: UVs (%g,%g) (%g,%g) (%g,%g) aren't one original face's\n", k + 1, t,
                            got.uvs[t * 3].x, got.uvs[t * 3].y, got.uvs[t * 3 + 1].x, got.uvs[t * 3 + 1].y, got.uvs[t * 3 + 2].x,
                            got.uvs[t * 3 + 2].y);
        }
        if (wrong) std::printf("    Level %d: %zu of %zu triangles wrong\n", k + 1, wrong, got.triangleCount());
        CHECK(wrong == 0);
    }
    // Stepping through every level and back to the top gives back the imported UVs.
    CHECK(run(d, "subdiv_level", {{"level", double(levels + 1)}}).ok);
    const std::vector<glm::vec2> topUvs = d.mesh.uvs;
    for (int k = levels; k >= 1; --k) CHECK(run(d, "subdiv_level", {{"level", double(k)}}).ok);
    CHECK(run(d, "subdiv_level", {{"level", double(levels + 1)}}).ok);
    CHECK(d.mesh.uvs == topUvs);
}

TEST(multires_reconstruct_lower_levels_keep_uvs) {
    checkReconstructLevelsKeepUvs("cube (valence-3 poles)", makeCube(2), 3);
    checkReconstructLevelsKeepUvs("plane (open border)", makePlane(3), 3);
    checkReconstructLevelsKeepUvs("torus (regular, closed)", makeTorus(8), 2);
    checkReconstructLevelsKeepUvs("quad sphere", makeQuadSphere(2), 2);
    checkReconstructLevelsKeepUvs("cylinder (n-gon caps)", makeCylinder(8), 2);
    checkReconstructLevelsKeepUvs("cone (triangles + quads)", makeCone(6), 2);
    checkReconstructLevelsKeepUvs("capsule", makeCapsule(8), 2);
}

// #145 (RodTDai's answers, screenshots and hint): a high-poly unwrapped on its own, so UV seams run THROUGH the
// faces of the levels Reconstruct finds, and islands overlap (automatic unwraps stack them). Before, such a face took
// each corner's UV from whichever island that corner was in and stretched across UDIM tiles. Fine UVs here: a planar
// projection per island, islands cut by planes off the coarse edges, two tiles of four overlapping islands each
// (as is, mirrored, rotated, shifted); out and back in as an OBJ. Every reconstructed triangle must lie in one
// island: all three corners near that island's projection of their positions.
static glm::vec2 islandUv(uint32_t island, glm::vec3 p) {
    glm::vec2 uv = glm::vec2(p.x, p.y + 0.5f * p.z) * 0.25f + 0.5f;
    switch (island >> 1) {
    case 1: uv.x = 1.0f - uv.x; break;
    case 2: uv = glm::vec2(1.0f) - uv; break;
    case 3: uv += glm::vec2(0.07f, 0.05f); break;
    }
    return uv + glm::vec2(float(island & 1), 0.0f);
}

static void checkReconstructSeamsThroughFaces(const char* name, Mesh coarse, int levels) {
    std::printf("  %s\n", name);
    Document src = makeDoc(std::move(coarse));
    for (int i = 0; i < levels; ++i) CHECK(run(src, "subdiv_divide").ok);
    Mesh top = subdivLevelMesh(src.mesh, levels);
    top.uvs.resize(top.triangleCount() * 3);
    for (uint32_t t = 0; t < top.triangleCount(); ++t) {
        uint32_t f = t; // both halves of a fine quad in one island
        if (top.quadPartnerAt(t) != Mesh::kNoQuad) f = std::min(t, top.quadPartnerAt(t));
        const uint32_t g = top.quadPartnerAt(f);
        glm::vec3 c(0.0f);
        int k = 0;
        for (uint32_t x : {f, g})
            if (x != Mesh::kNoQuad)
                for (int j = 0; j < 3; ++j) c += top.positions[top.triangles[x][j]], ++k;
        c /= float(k);
        const uint32_t island = uint32_t(c.x > 0.13f) + 2u * uint32_t(c.y > -0.21f) + 4u * uint32_t(c.z > 0.37f);
        for (int j = 0; j < 3; ++j) top.uvs[size_t(t) * 3 + size_t(j)] = islandUv(island, top.positions[top.triangles[t][j]]);
    }
    const std::string path = tempPath("sf_reconstruct_seams.obj");
    std::string err;
    CHECK(saveObj(path, top, err));
    Mesh plain;
    CHECK(loadObj(path, plain, err));
    std::filesystem::remove(path);
    const std::vector<glm::vec2> fineUvs = plain.uvs;
    Document d = makeDoc(std::move(plain));
    for (int i = 0; i < levels; ++i) CHECK(run(d, "subdiv_reconstruct").ok);
    CHECK(subdivLevelCount(d.mesh) == levels + 1);
    for (int k = 0; k < levels && subdivLevelCount(d.mesh) == levels + 1; ++k) {
        const Mesh got = subdivLevelMesh(d.mesh, k);
        CHECK(got.uvs.size() == got.triangleCount() * 3);
        if (got.uvs.size() != got.triangleCount() * 3) continue;
        size_t wrong = 0;
        for (uint32_t t = 0; t < got.triangleCount(); ++t) {
            bool ok = false;
            for (uint32_t island = 0; island < 8 && !ok; ++island) {
                ok = true;
                for (int j = 0; j < 3 && ok; ++j)
                    ok = glm::length(got.uvs[size_t(t) * 3 + size_t(j)] - islandUv(island, got.positions[got.triangles[t][j]])) < 0.05f;
            }
            if (!ok && wrong++ == 0)
                std::printf("    Level %d triangle %u: UVs (%g,%g) (%g,%g) (%g,%g) aren't one island's\n", k + 1, t, got.uvs[t * 3].x,
                            got.uvs[t * 3].y, got.uvs[t * 3 + 1].x, got.uvs[t * 3 + 1].y, got.uvs[t * 3 + 2].x, got.uvs[t * 3 + 2].y);
        }
        if (wrong) std::printf("    Level %d: %zu of %zu triangles wrong\n", k + 1, wrong, got.triangleCount());
        CHECK(wrong == 0);
    }
    CHECK(run(d, "subdiv_level", {{"level", double(levels + 1)}}).ok);
    CHECK(d.mesh.uvs == fineUvs); // the imported level keeps its own UVs exactly
}

TEST(multires_reconstruct_keeps_overlapping_uvs_with_seams_through_faces) {
    checkReconstructSeamsThroughFaces("cube", makeCube(3), 1);
    checkReconstructSeamsThroughFaces("cube, 2 levels", makeCube(3), 2);
    checkReconstructSeamsThroughFaces("quad sphere", makeQuadSphere(3), 2);
    checkReconstructSeamsThroughFaces("cone (triangles + quads)", makeCone(8), 2);
    checkReconstructSeamsThroughFaces("plane (open border)", makePlane(5), 2);
    checkReconstructSeamsThroughFaces("torus (regular, closed)", makeTorus(12), 1);
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
    CHECK(subdivLevelText(e.mesh) == "Level 2/4");
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
    CHECK(subdivLevelText(f.mesh) == "Level 2/2");
    f.selectPart(0);
    CHECK(subdivLevelText(f.mesh) == "Level 2/4");
    CHECK(run(f, "subdiv_level", {{"level", 4.0}}).ok);
    CHECK(maxDistance(f.mesh.positions, levels[3]) < 1e-6f);
    std::filesystem::remove(path);
}

TEST(multires_parts_exports_and_info) {
    Document d = dividedCube(2);
    d.addPart(makeIcosphere(1), "Eye", true);
    CHECK(subdivLevelCount(d.mesh) == 0);
    d.selectPart(0);
    CHECK(subdivLevelText(d.mesh) == "Level 3/3");
    CHECK(run(d, "part_list").message.find("Level 3/3") != std::string::npos);
    CHECK(run(d, "subdiv_info").message.find("Level 1: 108 triangles") != std::string::npos);
    // Exports write the current level; subdiv_export the base.
    const std::string cur = tempPath("sf_multires_cur.obj"), base = tempPath("sf_multires_base.obj");
    d.parts[1].visible = false;
    CHECK(run(d, "save_obj", {{"path", cur}}).ok);
    CHECK(run(d, "subdiv_export", {{"path", base}}).ok);
    CHECK(countObjFaces(cur, 4) == 54 * 16);
    CHECK(countObjFaces(base, 4) == 54);
    CHECK(subdivLevelText(d.mesh) == "Level 3/3"); // exporting the base doesn't change the scene
    std::filesystem::remove(cur);
    std::filesystem::remove(base);
    // A topology change (remesh, the old flat subdivide) drops the levels.
    CHECK(run(d, "subdivide").ok);
    CHECK(subdivLevelCount(d.mesh) == 0);
    CHECK(run(d, "undo").ok);
    CHECK(subdivLevelCount(d.mesh) == 3);
}
