// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Spheres (src/core/Forge Spheres.cpp): the armature edits and their symmetry, the display mesh, the skin, the
// Forge Sphere_* commands with undo, and projects.
#include "TestHarness.h"

#include "core/BVH.h"
#include "core/Commands.h"
#include "core/Project.h"
#include "core/ForgeSpheres.h"

#include <cmath>
#include <set>

using namespace sf;
using glm::vec3;

namespace {

// A small figure: root body, a head above, one arm out to +X mirrored to -X.
std::vector<ForgeSphere> figure(Symmetry sym = {true, false, false}) {
    std::vector<ForgeSphere> s{{vec3(0.0f), 0.5f, -1}};
    forgesphereAdd(s, 0, vec3(0, 0.8f, 0), 0.3f, sym);
    forgesphereAdd(s, 0, vec3(0.9f, 0.1f, 0), 0.2f, sym);
    return s;
}

bool watertight(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = t[k], b = t[(k + 1) % 3];
            ++edges[{std::min(a, b), std::max(a, b)}];
        }
    for (const auto& [e, n] : edges)
        if (n != 2) return false;
    return !edges.empty();
}

} // namespace

TEST(forgespheres_symmetric_edits) {
    const auto s = figure();
    // Head on the plane: one sphere. The arm: a sphere and its mirror.
    CHECK(s.size() == 4);
    CHECK(s[1].parent == 0 && std::abs(s[1].center.x) < 1e-6f);
    CHECK(s[2].center.x > 0.0f && s[3].center.x < 0.0f && s[3].parent == 0);
    CHECK(forgesphereMirror(s, 2, vec3(-1, 1, 1)) == 3 && forgesphereMirror(s, 1, vec3(-1, 1, 1)) == 1);
    CHECK(forgespheresValidate(s).empty());

    // A hand on the arm: its mirror grows from the mirrored arm.
    auto t = s;
    int hand = -1;
    CHECK(forgesphereAdd(t, 2, vec3(1.4f, 0.1f, 0), 0.15f, {true, false, false}, &hand));
    CHECK(hand == 4 && t.size() == 6 && t[5].parent == 3 && std::abs(t[5].center.x + 1.4f) < 1e-6f);

    // Moving the arm moves its mirror; the head stays on the plane when dragged across it.
    CHECK(forgesphereSet(t, 2, vec3(1.0f, 0.3f, 0.1f), 0.25f, {true, false, false}));
    CHECK(glm::length(t[3].center - vec3(-1.0f, 0.3f, 0.1f)) < 1e-6f && std::abs(t[3].radius - 0.25f) < 1e-6f);
    CHECK(forgesphereSet(t, 1, vec3(0.2f, 0.9f, 0), 0.3f, {true, false, false}));
    CHECK(t[1].center.x == 0.0f && std::abs(t[1].center.y - 0.9f) < 1e-6f);
    // Without symmetry only the one sphere moves.
    CHECK(forgesphereSet(t, 2, vec3(1.2f, 0.3f, 0.1f), 0.25f, {false, false, false}));
    CHECK(std::abs(t[3].center.x + 1.0f) < 1e-6f);

    // Deleting the arm takes the hand, and with symmetry the other arm and hand; indices close up.
    auto d = s;
    forgesphereAdd(d, 2, vec3(1.4f, 0.1f, 0), 0.15f, {true, false, false});
    CHECK(forgesphereDelete(d, 2, {true, false, false}));
    CHECK(d.size() == 2 && forgespheresValidate(d).empty());
    CHECK(!forgesphereDelete(d, 0, {true, false, false})); // never the root
    CHECK(!forgesphereAdd(d, 7, vec3(0.0f), 0.1f, {})); // bad parent: untouched
    CHECK(d.size() == 2);

    // Inserting on a link puts a joint halfway, between the child and its old parent (both sides).
    auto in = s;
    int joint = -1;
    CHECK(forgesphereInsert(in, 2, {true, false, false}, &joint));
    CHECK(joint == 4 && in.size() == 6 && in[2].parent == 4 && in[4].parent == 0 && in[3].parent == 5);
    CHECK(glm::length(in[4].center - vec3(0.45f, 0.05f, 0)) < 1e-6f);
    CHECK(forgespheresValidate(in).empty());

    // Near the centre line (closer than its radius) a new sphere snaps onto it as one sphere, not two that
    // overlap; a sphere growing from the arm stays mirrored even when it reaches the centre.
    auto mid = s;
    int one = -1;
    CHECK(forgesphereAdd(mid, 0, vec3(0.08f, -0.6f, 0.1f), 0.2f, {true, false, false}, &one));
    CHECK(one == 4 && mid.size() == 5 && mid[4].center == vec3(0.0f, -0.6f, 0.1f) && mid[4].parent == 0);
    CHECK(forgesphereAdd(mid, 0, vec3(0.25f, -0.6f, 0.1f), 0.2f, {true, false, false}));
    CHECK(mid.size() == 7 && std::abs(mid[6].center.x + 0.25f) < 1e-6f); // a radius off: a mirrored pair
    CHECK(forgesphereAdd(mid, 0, vec3(0.08f, -0.6f, 0.1f), 0.2f, {false, false, false}));
    CHECK(mid.size() == 8 && mid[7].center.x == 0.08f); // no symmetry: where it was put
    CHECK(forgesphereAdd(mid, 2, vec3(0.1f, 0.1f, 0), 0.15f, {true, false, false}));
    CHECK(mid.size() == 10 && mid[9].parent == 3 && std::abs(mid[9].center.x + 0.1f) < 1e-6f);
    CHECK(forgespheresValidate(mid).empty());
}

TEST(forgespheres_validate_and_json) {
    const auto s = figure();
    const auto back = forgespheresFromJson(forgespheresToJson(s));
    CHECK(back.size() == s.size());
    for (size_t i = 0; i < s.size() && i < back.size(); ++i)
        CHECK(back[i].center == s[i].center && back[i].radius == s[i].radius && back[i].parent == s[i].parent);
    std::vector<ForgeSphere> loop{{vec3(0.0f), 1.0f, -1}, {vec3(1.0f), 1.0f, 2}, {vec3(2.0f), 1.0f, 1}};
    CHECK(!forgespheresValidate(loop).empty());
    CHECK(forgespheresFromJson(forgespheresToJson(loop)).empty());
    CHECK(!forgespheresValidate({{vec3(0.0f), 0.0f, -1}}).empty());
    CHECK(!forgespheresValidate({{vec3(0.0f), 1.0f, 0}}).empty());
    CHECK(forgespheresFromJson(Json::array()).empty());
}

TEST(forgespheres_display_mesh_groups) {
    const auto s = figure();
    const Mesh m = forgesphereDisplayMesh(s);
    CHECK(m.forgespheres.size() == s.size() && m.colors.size() == m.vertexCount() && m.facegroups.size() == m.triangleCount());
    std::set<uint32_t> groups(m.facegroups.begin(), m.facegroups.end());
    CHECK(groups.size() == s.size() + (s.size() - 1)); // a group per sphere and per link
    bool link = true;
    CHECK(forgesphereOfGroup(kForgeSphereGroup + 3, &link) == 3 && !link);
    CHECK(forgesphereOfGroup(kForgeSphereLinkGroup + 2, &link) == 2 && link);
    CHECK(forgesphereOfGroup(0) == -1);
    // A ray at the arm sphere's centre hits the arm sphere's group.
    BVH bvh;
    bvh.build(m);
    RayHit hit;
    CHECK(bvh.raycast(m, vec3(0.9f, 0.1f, 5.0f), vec3(0, 0, -1), hit));
    CHECK(forgesphereOfGroup(m.facegroupAt(hit.triangle), &link) == 2 && !link);
}

TEST(forgespheres_skin) {
    const auto s = figure();
    const Mesh skin = forgesphereSkin(s, 64, 2);
    CHECK(skin.triangleCount() > 1000 && skin.forgespheres.empty());
    CHECK(watertight(skin));
    CHECK(skin.quadCount() * 2 * 10 >= skin.triangleCount() * 9); // (nearly) all quads
    // Covers every sphere: the bounds reach the head top and both hands, give or take a cell.
    const AABB b = skin.bounds();
    const float cell = 2.0f * 1.25f / 64.0f;
    CHECK(std::abs(b.max.y - 1.1f) < 2.0f * cell);
    CHECK(std::abs(b.max.x - 1.1f) < 2.0f * cell && std::abs(b.min.x + 1.1f) < 2.0f * cell);
    // Symmetric across X like the armature.
    CHECK(std::abs(b.max.x + b.min.x) < cell);
    // One face group per link.
    std::set<uint32_t> groups(skin.facegroups.begin(), skin.facegroups.end());
    CHECK(groups.size() == s.size() - 1);
    // A lone root is a sphere.
    const Mesh ball = forgesphereSkin({{vec3(1, 2, 3), 0.5f, -1}}, 48, 1);
    CHECK(ball.triangleCount() > 0 && watertight(ball));
    float worst = 0.0f;
    for (const vec3& p : ball.positions) worst = std::max(worst, std::abs(glm::length(p - vec3(1, 2, 3)) - 0.5f));
    CHECK(worst < 0.05f);
}

TEST(forgespheres_commands_and_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(!reg.execute(doc, "forgesphere_add", {{"parent", 0.0}, {"position", std::string("1,0,0")}}).ok); // not a Forge Sphere Part
    CHECK(reg.execute(doc, "forgesphere_new", {{"radius", 0.5}}).ok);
    CHECK(doc.partCount() == 2 && doc.parts[1].name == "Forge Sphere" && doc.mesh.forgespheres.size() == 1);
    CHECK(reg.execute(doc, "forgesphere_add", {{"parent", 0.0}, {"position", std::string("0.9,0,0")}, {"radius", 0.25},
                                           {"symmetry", std::string("x")}}).ok);
    CHECK(doc.mesh.forgespheres.size() == 3);
    CHECK(reg.execute(doc, "forgesphere_set", {{"index", 1.0}, {"radius", 0.3}, {"symmetry", std::string("x")}}).ok);
    CHECK(std::abs(doc.mesh.forgespheres[2].radius - 0.3f) < 1e-6f);
    CHECK(!reg.execute(doc, "forgesphere_delete", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "forgesphere_list").message.find("3 spheres") == 0);

    // Each edit is one undo step, and undo brings the armature back with the mesh.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(std::abs(doc.mesh.forgespheres[2].radius - 0.25f) < 1e-6f);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.forgespheres.size() == 1);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh) && doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.forgespheres.size() == 3);

    // Preview swaps the spheres for the skin and back, keeping the armature.
    const size_t sphereTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "forgesphere_preview", {{"on", true}}).ok);
    CHECK(doc.mesh.forgespherePreview && doc.mesh.forgespheres.size() == 3 && doc.mesh.triangleCount() != sphereTris);
    CHECK(reg.execute(doc, "forgesphere_add", {{"parent", 0.0}, {"position", std::string("0,0.7,0")}}).ok);
    CHECK(doc.mesh.forgespherePreview && doc.mesh.forgespheres.size() == 4); // edits while previewing keep previewing
    CHECK(reg.execute(doc, "forgesphere_preview").ok && !doc.mesh.forgespherePreview);

    // Make Mesh: a new sculptable Part below, the Forge Sphere Part hidden and kept.
    CHECK(reg.execute(doc, "forgesphere_make_mesh", {{"resolution", 48.0}}).ok);
    CHECK(doc.partCount() == 3 && doc.activePart == 2 && doc.parts[2].name == "Forge Sphere Mesh");
    CHECK(doc.mesh.forgespheres.empty() && watertight(doc.mesh));
    CHECK(!doc.parts[1].visible && doc.partMesh(1).forgespheres.size() == 4);
    CHECK(!reg.execute(doc, "forgesphere_make_mesh").ok); // the new mesh is not a Forge Sphere Part
    // keep=false replaces the Forge Sphere Part.
    doc.selectPart(1);
    CHECK(reg.execute(doc, "forgesphere_make_mesh", {{"resolution", 32.0}, {"keep", false}}).ok);
    CHECK(doc.partCount() == 3 && doc.parts[doc.activePart].name == "Forge Sphere Mesh 2");
    for (int i = 0; i < doc.partCount(); ++i) CHECK(doc.partMesh(i).forgespheres.empty());
}

TEST(forgespheres_project_roundtrip) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "forgesphere_new").ok);
    CHECK(reg.execute(doc, "forgesphere_add", {{"parent", 0.0}, {"position", std::string("0.8,0.2,0")}, {"radius", 0.2},
                                           {"symmetry", std::string("x")}}).ok);
    CHECK(reg.execute(doc, "forgesphere_preview", {{"on", true}}).ok);
    doc.selectPart(0); // the Forge Sphere Part saved while inactive too
    const std::string path = tempPath("sf_forgespheres.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 2 && back.partMesh(0).forgespheres.empty());
    const Mesh& z = back.partMesh(1);
    CHECK(z.forgespheres.size() == 3 && z.forgespherePreview && z.triangleCount() == doc.partMesh(1).triangleCount());
    back.selectPart(1);
    CHECK(reg.execute(back, "forgesphere_preview", {{"on", false}}).ok && back.mesh.forgespheres.size() == 3);
    std::filesystem::remove(path);
}
