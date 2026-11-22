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

#include <chrono>
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

namespace {

// A creature as one would block it out: body, neck and head, a tail chain, four legs in X symmetry, and `extra`
// more joints along the tail (a dense chain).
std::vector<ForgeSphere> creature(int extra = 0) {
    const Symmetry x{true, false, false};
    std::vector<ForgeSphere> s{{vec3(0.0f), 0.6f, -1}};
    int neck = -1, at = -1;
    forgesphereAdd(s, 0, vec3(0, 0.5f, 0.7f), 0.3f, x, &neck);
    forgesphereAdd(s, neck, vec3(0, 0.8f, 1.1f), 0.35f, x);
    forgesphereAdd(s, 0, vec3(0, 0.1f, -0.8f), 0.25f, x, &at);
    for (int k = 0; k < 3 + extra; ++k) {
        int next = -1;
        forgesphereAdd(s, at, vec3(0, 0.1f + 0.02f * float(k), -1.0f - 0.15f * float(k)), 0.1f, x, &next);
        at = next;
    }
    int leg = -1;
    forgesphereAdd(s, 0, vec3(0.4f, -0.6f, 0.4f), 0.18f, x, &leg); // front legs (a pair)
    forgesphereAdd(s, leg, vec3(0.45f, -1.1f, 0.45f), 0.12f, x);
    forgesphereAdd(s, 0, vec3(0.4f, -0.6f, -0.4f), 0.18f, x, &leg); // hind legs
    forgesphereAdd(s, leg, vec3(0.45f, -1.1f, -0.45f), 0.12f, x);
    return s;
}

// Rays as a perspective camera at `eye` looking down `forward` casts them through points spread over the view.
std::vector<std::pair<vec3, vec3>> fanRays(const vec3& eye, const vec3& forward, float spread, int n) {
    const vec3 up = std::abs(forward.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    const vec3 right = glm::normalize(glm::cross(forward, up)), top = glm::cross(right, forward);
    std::vector<std::pair<vec3, vec3>> out;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const float u = spread * (2.0f * float(i) / float(n - 1) - 1.0f), v = spread * (2.0f * float(j) / float(n - 1) - 1.0f);
            out.push_back({eye, glm::normalize(forward + right * u + top * v)});
        }
    return out;
}

float maxVertexGap(const Mesh& a, const Mesh& b, bool normals) {
    float worst = 0.0f;
    const auto& pa = normals ? a.normals : a.positions;
    const auto& pb = normals ? b.normals : b.positions;
    if (pa.size() != pb.size()) return 1e9f;
    for (size_t v = 0; v < pa.size(); ++v) worst = std::max(worst, glm::length(pa[v] - pb[v]));
    return worst;
}

} // namespace

// A drag redraws only what moved, and the result is the mesh a full rebuild makes: the same vertices, normals and
// triangles, with its mirror twin and both spheres' links following.
TEST(forgespheres_drag_update_matches_rebuild) {
    const Symmetry x{true, false, false};
    std::vector<ForgeSphere> s = creature(20);
    Mesh m = forgesphereDisplayMesh(s);
    const auto tris = m.triangles;
    const int leg = int(s.size()) - 4; // a hind leg (its twin follows)
    std::vector<uint32_t> moved;
    for (int step = 1; step <= 5; ++step) {
        std::vector<ForgeSphere> t = s;
        CHECK(forgesphereSet(t, leg, s[size_t(leg)].center + vec3(0.05f, -0.02f, 0.03f) * float(step), 0.2f, x));
        CHECK(forgesphereDisplayUpdate(m, t, &moved));
        const Mesh full = forgesphereDisplayMesh(t);
        CHECK(maxVertexGap(m, full, false) < 1e-6f);
        CHECK(maxVertexGap(m, full, true) < 1e-4f);
        CHECK(m.triangles == tris && m.forgespheres.size() == t.size());
        CHECK(m.dirtyEnd > m.dirtyBegin && !moved.empty());
    }
    // Two spheres (the leg and its twin) and their links changed: a small share of the armature's vertices.
    CHECK(moved.size() < m.vertexCount() / 8);
    CHECK(moved.size() >= 2 * 642);
    // A change of tree or of drawn links needs the full rebuild.
    std::vector<ForgeSphere> grown = m.forgespheres;
    forgesphereAdd(grown, 1, vec3(0.0f, 1.0f, 1.0f), 0.1f, x);
    Mesh before = m;
    CHECK(!forgesphereDisplayUpdate(m, grown, &moved));
    CHECK(m.positions == before.positions && m.forgespheres.size() == before.forgespheres.size());
    std::vector<ForgeSphere> onParent = m.forgespheres;
    onParent[1].center = onParent[0].center; // the neck's link has nothing to draw
    CHECK(!forgesphereDisplayUpdate(m, onParent, &moved));
    Mesh preview = forgesphereMesh(s, true);
    CHECK(!forgesphereDisplayUpdate(preview, s, &moved));
}

// The promise of a plain move: the grabbed point stays under the pointer (on its ray) and the sphere keeps its depth
// in the view, across a wide perspective view, so it follows the cursor 1:1 without drifting nearer or farther.
TEST(forgespheres_move_view_plane) {
    const Symmetry x{true, false, false};
    const std::vector<ForgeSphere> s = creature();
    const vec3 eye(1.5f, 1.0f, 4.0f);
    const vec3 forward = glm::normalize(vec3(0.0f) - eye);
    const int i = 2; // the head
    const vec3 c0 = s[size_t(i)].center;
    const vec3 grabbed = c0 + vec3(0.1f, 0.15f, 0.0f) - forward * glm::dot(vec3(0.1f, 0.15f, 0.0f), forward);
    const vec3 grab = c0 - grabbed;
    // A ray through the grabbed point puts the sphere back where it was.
    CHECK(glm::distance(forgesphereMoveTarget(s, i, grab, eye, glm::normalize(grabbed - eye), forward, ForgeSphereDepth::View, x), c0) < 1e-5f);
    int checked = 0;
    for (const auto& [o, d] : fanRays(eye, forward, 0.6f, 9)) {
        const vec3 c = forgesphereMoveTarget(s, i, grab, o, d, forward, ForgeSphereDepth::View, x);
        CHECK(std::abs(glm::dot(c - c0, forward)) < 1e-5f); // the same view depth
        const vec3 g = c - grab; // the grabbed point, on the pointer's ray
        CHECK(glm::length(glm::cross(g - o, d)) < 1e-5f * glm::length(g - o) + 1e-6f);
        ++checked;
    }
    CHECK(checked == 81);
}

// Shift: a sphere swings around its parent and its link keeps its length wherever the pointer goes, past the
// parent's silhouette too; under the pointer while the pointer is over that sphere of reach.
TEST(forgespheres_move_keep_length) {
    const Symmetry x{true, false, false};
    const std::vector<ForgeSphere> s = creature();
    const vec3 eye(0.5f, 0.2f, 5.0f), forward = glm::normalize(vec3(0.0f) - eye);
    // Without symmetry the sphere is under the pointer wherever it can be; with it, a sphere on the centre line stays
    // on it (the link still keeps its length).
    for (const Symmetry sym : {Symmetry{false, false, false}, x})
    for (int i = 1; i < int(s.size()); ++i) {
        const vec3 p = s[size_t(s[size_t(i)].parent)].center;
        const float len = glm::distance(s[size_t(i)].center, p);
        const bool free = !sym.x || forgesphereMirror(s, i, vec3(-1, 1, 1)) != i;
        for (const auto& [o, d] : fanRays(eye, forward, 0.8f, 7)) {
            const vec3 c = forgesphereMoveTarget(s, i, vec3(0.0f), o, d, forward, ForgeSphereDepth::KeepLength, sym);
            if (!free) CHECK(c.x == 0.0f || std::abs(c.x) < 1e-6f);
            CHECK(std::abs(glm::distance(c, p) - len) < 1e-4f * (1.0f + len));
            // A ray that reaches the sphere of reach lands on it, on the ray.
            const vec3 oc = o - p;
            const float b = glm::dot(oc, d), disc = b * b - (glm::dot(oc, oc) - len * len);
            if (!free) continue;
            if (disc > 1e-6f) CHECK(glm::length(glm::cross(c - o, d)) < 1e-4f);
            // ... on the half it started in (facing the pointer's ray or away from it), so it never flips front to
            // back; past the silhouette, on the edge between them.
            const float side0 = glm::dot(s[size_t(i)].center - p, forward), side = glm::dot(c - p, d);
            CHECK(side * side0 >= 0.0f || std::abs(side) < 1e-4f * (1.0f + len));
        }
    }
    // The root has no parent: it moves on the view plane.
    const vec3 c = forgesphereMoveTarget(s, 0, vec3(0.0f), eye, glm::normalize(vec3(0.3f, 0.0f, 0.0f) - eye), forward,
                                         ForgeSphereDepth::KeepLength, x);
    CHECK(std::abs(glm::dot(c - s[0].center, forward)) < 1e-5f);
}

// Snap to Surface: the centre lands on the surface of the sphere under the pointer, never on the moved sphere, its
// mirror twin or what grows from them; with the pointer off every other sphere, the sphere moves on the view plane.
TEST(forgespheres_move_surface_snap) {
    const Symmetry x{true, false, false};
    const std::vector<ForgeSphere> s = creature();
    const int leg = int(s.size()) - 4, twin = forgesphereMirror(s, leg, vec3(-1, 1, 1));
    CHECK(twin > 0 && twin != leg);
    const vec3 eye(0.0f, 0.0f, 6.0f), forward(0, 0, -1);
    // At the body: its front surface, under the pointer.
    vec3 c = forgesphereMoveTarget(s, leg, vec3(0.0f), eye, glm::normalize(vec3(0.1f, 0.1f, 0.0f) - eye), forward,
                                   ForgeSphereDepth::Surface, x);
    CHECK(std::abs(glm::distance(c, s[0].center) - s[0].radius) < 1e-4f && c.z > 0.0f);
    // At its own twin (and the twin's foot): passes through, onto what lies behind or the view plane.
    int onOther = 0, onPlane = 0;
    for (const auto& [o, d] : fanRays(eye, forward, 0.3f, 11)) {
        c = forgesphereMoveTarget(s, leg, vec3(0.0f), o, d, forward, ForgeSphereDepth::Surface, x);
        bool onSome = false;
        for (size_t j = 0; j < s.size(); ++j) {
            const bool own = int(j) == leg || int(j) == twin || s[j].parent == leg || s[j].parent == twin;
            const bool on = std::abs(glm::distance(c, s[j].center) - s[j].radius) < 1e-4f;
            if (on && !own) onSome = true;
        }
        if (onSome) ++onOther;
        else {
            CHECK(std::abs(glm::dot(c - s[size_t(leg)].center, forward)) < 1e-5f);
            ++onPlane;
        }
    }
    CHECK(onOther > 10 && onPlane > 10);
}

// Symmetry holds through a drag: each step's target, mirrored by forgesphereSet, keeps the twin the leg's exact
// mirror image, and a sphere on the centre line stays on it.
TEST(forgespheres_move_symmetry_live) {
    const Symmetry x{true, false, false};
    const std::vector<ForgeSphere> s = creature();
    const int leg = int(s.size()) - 4, twin = forgesphereMirror(s, leg, vec3(-1, 1, 1));
    const vec3 eye(0.8f, 0.4f, 5.0f), forward = glm::normalize(-eye);
    for (const auto& [o, d] : fanRays(eye, forward, 0.5f, 6))
        for (ForgeSphereDepth depth : {ForgeSphereDepth::View, ForgeSphereDepth::KeepLength, ForgeSphereDepth::Surface}) {
            std::vector<ForgeSphere> t = s;
            const vec3 c = forgesphereMoveTarget(s, leg, vec3(0.0f), o, d, forward, depth, x);
            CHECK(forgesphereSet(t, leg, c, t[size_t(leg)].radius, x));
            CHECK(glm::distance(t[size_t(twin)].center, t[size_t(leg)].center * vec3(-1, 1, 1)) < 1e-6f);
            std::vector<ForgeSphere> h = s;
            const vec3 hc = forgesphereMoveTarget(s, 2, vec3(0.0f), o, d, forward, depth, x);
            CHECK(forgesphereSet(h, 2, hc, h[2].radius, x));
            CHECK(h[2].center.x == 0.0f);
        }
}

// The cost of a drag frame follows what moved: on a 400-joint chain the in-place update of one sphere is far cheaper
// than the full rebuild (display mesh, topology, normals, BVH) every pointer event paid before. Times print for the
// record; the check is on work done (vertices touched), which doesn't flake on a busy machine.
TEST(forgespheres_drag_cost_on_dense_chain) {
    const Symmetry x{true, false, false};
    std::vector<ForgeSphere> s = creature(400);
    Mesh m = forgesphereDisplayMesh(s);
    BVH bvh;
    bvh.build(m);
    const int tip = int(s.size()) - 9; // the tail's last joint (the legs come after it)
    std::vector<uint32_t> moved;
    const auto now = [] { return std::chrono::steady_clock::now(); };
    double fast = 0.0, full = 0.0;
    constexpr int kSteps = 20;
    for (int k = 0; k < kSteps; ++k) {
        std::vector<ForgeSphere> t = s;
        forgesphereSet(t, tip, s[size_t(tip)].center + vec3(0.01f * float(k), 0.0f, 0.0f), s[size_t(tip)].radius, x);
        auto a = now();
        CHECK(forgesphereDisplayUpdate(m, t, &moved));
        bvh.refit(m, moved);
        auto b = now();
        Mesh r = forgesphereMesh(t, false);
        BVH rb;
        rb.build(r);
        auto c = now();
        fast += std::chrono::duration<double, std::milli>(b - a).count();
        full += std::chrono::duration<double, std::milli>(c - b).count();
    }
    CHECK(moved.size() < m.vertexCount() / 100);
    std::printf("  forgespheres drag frame, %zu spheres, %zu vertices: in place %.3f ms, full rebuild %.3f ms\n", s.size(),
                m.vertexCount(), fast / kSteps, full / kSteps);
    // The refitted tree still finds the moved sphere where it is now.
    RayHit hit;
    const vec3 at = m.forgespheres[size_t(tip)].center;
    CHECK(bvh.raycast(m, at + vec3(5, 0, 0), vec3(-1, 0, 0), hit));
    bool link = false;
    CHECK(forgesphereOfGroup(m.facegroupAt(hit.triangle), &link) == tip);
}

// Move Branch: what grows from a moved sphere moves with it and keeps its shape (every link in the branch the same
// vector), the twin's branch mirrors it, and the rest of the armature stays put.
TEST(forgespheres_move_branch_keeps_shape) {
    const Symmetry x{true, false, false};
    const std::vector<ForgeSphere> s = creature(5);
    const int leg = int(s.size()) - 4, twin = forgesphereMirror(s, leg, vec3(-1, 1, 1));
    std::vector<ForgeSphere> t = s;
    const vec3 by(0.2f, -0.1f, 0.3f);
    CHECK(forgesphereMoveBranch(t, leg, s[size_t(leg)].center + by, x));
    for (size_t j = 0; j < s.size(); ++j) {
        const bool mine = int(j) == leg || s[j].parent == leg, theirs = int(j) == twin || s[j].parent == twin;
        const vec3 want = mine ? s[j].center + by : theirs ? s[j].center + by * vec3(-1, 1, 1) : s[j].center;
        CHECK(glm::distance(t[j].center, want) < 1e-6f);
        CHECK(t[j].radius == s[j].radius && t[j].parent == s[j].parent);
    }
    // The root's branch is everything: the creature moves whole and stays on its centre line.
    std::vector<ForgeSphere> w = s;
    CHECK(forgesphereMoveBranch(w, 0, vec3(0.4f, 0.5f, 0.0f), x));
    for (size_t j = 0; j < s.size(); ++j) CHECK(glm::distance(w[j].center, s[j].center + vec3(0.0f, 0.5f, 0.0f)) < 1e-6f);
    CHECK(!forgesphereMoveBranch(w, 99, vec3(0.0f), x));
}

// Letting go of a drag lands the moved armature with the tree the drag kept fitted (no rebuild): picks find the moved
// sphere where it is now, and undo / redo swap the armature back and forth as with any edit.
TEST(forgespheres_release_keeps_the_drag_tree) {
    const Symmetry x{true, false, false};
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "forgesphere_new", {{"radius", 0.5}}).ok);
    std::vector<ForgeSphere> s = creature(30);
    doc.replaceMeshUndoable(forgesphereMesh(s, false), "creature");
    const Mesh before = doc.mesh;
    const int tip = int(s.size()) - 9;
    // The drag: in place, refitting as it goes, then refitAll and the hand-off as forgesphereRelease does.
    std::vector<uint32_t> moved;
    std::vector<ForgeSphere> t = s;
    CHECK(forgesphereSet(t, tip, s[size_t(tip)].center + vec3(0.0f, 1.5f, 0.0f), s[size_t(tip)].radius, x));
    CHECK(forgesphereDisplayUpdate(doc.mesh, t, &moved));
    doc.bvh.refit(doc.mesh, moved);
    Mesh after = std::move(doc.mesh);
    doc.bvh.refitAll(after);
    BVH tree = std::move(doc.bvh);
    doc.mesh = before;
    doc.replaceMeshUndoable(std::move(after), "Forge Sphere move", std::move(tree));
    CHECK(doc.bvh.nodeCount() > 0 && doc.mesh.forgespheres.size() == t.size());
    RayHit hit;
    const vec3 at = t[size_t(tip)].center;
    CHECK(doc.bvh.raycast(doc.mesh, at + vec3(5, 0, 0), vec3(-1, 0, 0), hit));
    CHECK(forgesphereOfGroup(doc.mesh.facegroupAt(hit.triangle)) == tip);
    CHECK(!doc.bvh.raycast(doc.mesh, s[size_t(tip)].center + vec3(5, 0, 0), vec3(-1, 0, 0), hit) ||
          forgesphereOfGroup(doc.mesh.facegroupAt(hit.triangle)) != tip);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.forgespheres[size_t(tip)].center == s[size_t(tip)].center);
    CHECK(doc.bvh.raycast(doc.mesh, s[size_t(tip)].center + vec3(5, 0, 0), vec3(-1, 0, 0), hit));
    CHECK(forgesphereOfGroup(doc.mesh.facegroupAt(hit.triangle)) == tip);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.forgespheres[size_t(tip)].center == t[size_t(tip)].center);
}
