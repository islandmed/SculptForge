// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extract (a shell from the mask, preview + Accept, undo), Mirror And Weld and Mirror (core/ExtractMirror.h).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/ExtractMirror.h"
#include "core/Polygroups.h"
#include "core/Primitives.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace sf;

namespace {

// Every edge shared by exactly two triangles, wound oppositely: closed and consistently oriented.
bool watertight(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> directed;
    for (const auto& t : m.triangles)
        for (int k = 0; k < 3; ++k) ++directed[{t[k], t[(k + 1) % 3]}];
    for (const auto& [e, n] : directed) {
        if (n != 1) return false;
        auto it = directed.find({e.second, e.first});
        if (it == directed.end() || it->second != 1) return false;
    }
    return !m.triangles.empty();
}

// Six times the enclosed volume: positive when the triangles face outward.
double signedVolume(const Mesh& m) {
    double v = 0.0;
    for (const auto& t : m.triangles)
        v += double(glm::dot(m.positions[t.x], glm::cross(m.positions[t.y], m.positions[t.z])));
    return v;
}

std::set<uint32_t> groups(const Mesh& m) {
    std::set<uint32_t> ids;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) ids.insert(m.polygroupAt(t));
    return ids;
}

// A sphere with its top (y > 0.5) masked and painted red.
void maskedSphere(Document& doc, CommandRegistry& reg) {
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}}).ok);
    doc.mesh.ensureMask();
    doc.mesh.ensureColors(glm::vec3(1.0f));
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (doc.mesh.positions[v].y > 0.5f) {
            doc.mesh.mask[v] = 1.0f;
            doc.mesh.colors[v] = glm::vec3(1, 0, 0);
        }
}

} // namespace

TEST(extract_shell_preview_accept_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(!reg.execute(doc, "extract").ok); // nothing masked
    CHECK(doc.subtoolCount() == 1);

    maskedSphere(doc, reg);
    const float radius = glm::length(doc.mesh.positions[0]);
    const CommandResult r = reg.execute(doc, "extract");
    CHECK(r.ok);
    CHECK(r.message.find("preview") != std::string::npos);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 0); // the preview sits below; the source stays active
    CHECK(doc.extractPreview.id != 0 && doc.extractPreviewCurrent());
    CHECK(!doc.undo.canUndo()); // a preview isn't a step yet
    const Mesh& shell = doc.subtoolMesh(1);
    CHECK(doc.subtools[1].name == "Extract");
    CHECK(watertight(shell));
    CHECK(signedVolume(shell) > 0.0);
    CHECK((groups(shell) == std::set<uint32_t>{1, 2, 3})); // outer, inner, rim
    CHECK(shell.hasQuads()); // the rim
    CHECK(!shell.hasMask());
    // Thick 0.015 of the sphere's size (its side is 2 * radius) outward; the inner surface is the sphere's.
    float rmax = 0.0f, rmin = 1e9f;
    for (const auto& p : shell.positions) {
        rmax = std::max(rmax, glm::length(p));
        rmin = std::min(rmin, glm::length(p));
    }
    CHECK(std::abs(rmax - radius * (1.0f + 0.03f)) < radius * 0.01f);
    CHECK(rmin > radius * 0.97f); // (E Smt pulls the edge in a little)
    // Polypaint comes along.
    CHECK(shell.hasColors() && shell.colors[0] == glm::vec3(1, 0, 0));

    // Extract again with other settings: the preview is replaced, not added to.
    const uint32_t previewId = doc.extractPreview.id;
    CHECK(reg.execute(doc, "extract", {{"thickness", 0.05}, {"double", true}, {"smooth", 0.0}}).ok);
    CHECK(doc.subtoolCount() == 2 && doc.extractPreview.id == previewId);
    rmin = 1e9f;
    for (const auto& p : doc.subtoolMesh(1).positions) rmin = std::min(rmin, glm::length(p));
    CHECK(rmin < radius * 0.95f); // Double grows inward too

    // Accept: one undo step on the source that takes the SubTool away and redo brings it back.
    CHECK(reg.execute(doc, "extract_accept").ok);
    CHECK(doc.extractPreview.id == 0 && doc.subtoolCount() == 2);
    CHECK(doc.undo.canUndo() && doc.undo.undoLabel() == "Extract");
    CHECK(!reg.execute(doc, "extract_accept").ok);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.subtoolCount() == 1 && doc.activeSubtool == 0);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.subtoolCount() == 2 && doc.subtools[1].id == previewId && doc.subtoolMesh(1).triangleCount() > 0);
    CHECK(doc.dirty());

    // A single-sided surface: Thick 0.
    CHECK(reg.execute(doc, "extract", {{"thickness", 0.0}, {"accept", true}}).ok);
    CHECK(doc.subtoolCount() == 3);
    const Mesh& surface = doc.subtoolMesh(1); // right below the source
    CHECK(!watertight(surface) && groups(surface) == std::set<uint32_t>{1});
}

TEST(extract_preview_goes_when_the_scene_changes) {
    Document doc;
    CommandRegistry reg;
    maskedSphere(doc, reg);
    CHECK(reg.execute(doc, "extract").ok && doc.subtoolCount() == 2);
    CHECK(reg.execute(doc, "subtool_list").ok); // reading the scene leaves it
    CHECK(doc.subtoolCount() == 2);
    CHECK(reg.execute(doc, "extract_cancel").ok);
    CHECK(doc.subtoolCount() == 1 && doc.extractPreview.id == 0);

    CHECK(reg.execute(doc, "extract").ok);
    CHECK(reg.execute(doc, "mask_invert").ok); // an edit of the source: the preview no longer matches
    CHECK(reg.execute(doc, "subtool_list").ok);
    CHECK(doc.subtoolCount() == 1 && doc.extractPreview.id == 0);

    // Saving never writes a preview.
    CHECK(reg.execute(doc, "extract").ok && doc.subtoolCount() == 2);
    const std::string path = tempPath("sf_extract_preview.sfproj");
    CHECK(reg.execute(doc, "save_project", {{"path", path}}).ok);
    CHECK(doc.subtoolCount() == 1);
    Document back;
    CHECK(reg.execute(back, "open", {{"path", path}}).ok);
    CHECK(back.subtoolCount() == 1);
}

TEST(mirror_and_weld_sphere) {
    Document doc;
    CommandRegistry reg;
    maskedSphere(doc, reg);
    // Off-centre, so the plane cuts triangles; groups 7 on +x, 9 elsewhere.
    for (auto& p : doc.mesh.positions) p.x += 0.3f;
    doc.mesh.ensurePolygroups(9);
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t)
        if (doc.mesh.positions[doc.mesh.triangles[t].x].x > 0.8f) doc.mesh.polygroups[t] = 7;
    doc.mesh.recomputeAllNormals();
    const size_t before = doc.mesh.triangleCount();

    const CommandResult r = reg.execute(doc, "mirror_and_weld", {{"axis", std::string("x")}});
    CHECK(r.ok);
    const Mesh& m = doc.mesh;
    CHECK(watertight(m));
    CHECK(signedVolume(m) > 0.0);
    const AABB b = m.bounds();
    CHECK(std::abs(b.min.x + b.max.x) < 1e-5f);
    CHECK((groups(m) == std::set<uint32_t>{7, 9}));
    size_t red = 0;
    for (const auto& c : m.colors) red += c == glm::vec3(1, 0, 0);
    CHECK(red > 0 && m.hasMask() && m.mask.size() == m.vertexCount());
    // Every vertex has its mirror image.
    std::set<std::tuple<long, long, long>> keys;
    auto key = [](const glm::vec3& p) { return std::tuple<long, long, long>(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)); };
    for (const auto& p : m.positions) keys.insert(key(p));
    size_t missing = 0;
    for (const auto& p : m.positions) missing += !keys.count(key(glm::vec3(-p.x, p.y, p.z)));
    CHECK(missing == 0);

    CHECK(doc.undo.undoLabel() == "Mirror And Weld");
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.triangleCount() == before);

    // The other side, and bad arguments.
    CHECK(reg.execute(doc, "mirror_and_weld", {{"side", std::string("negative")}}).ok);
    CHECK(watertight(doc.mesh) && doc.mesh.bounds().max.x < 1.0f); // the smaller, -x side
    CHECK(!reg.execute(doc, "mirror_and_weld", {{"axis", std::string("w")}}).ok);
    for (auto& p : doc.mesh.positions) p.x += 5.0f; // all on +x
    CHECK(!reg.execute(doc, "mirror_and_weld", {{"side", std::string("negative")}}).ok);
}

TEST(mirror_and_weld_keeps_quads_and_layers) {
    // A cube whose grid lines lie on the planes: nothing is cut, so every quad and group survives exactly.
    Mesh cube = makeCube(4, 2.0f);
    const size_t quads = cube.quadCount();
    cube.base = cube.positions;
    cube.layers.push_back({"Bump", {}, 0.5f}); // an inflate: symmetric, so the planes still fall on grid lines
    for (const auto& p : cube.positions) cube.layers[0].delta.push_back(0.1f * p);
    cube.evaluateAll();
    std::string error;
    for (int axis = 0; axis < 3; ++axis) {
        Mesh m = mirrorAndWeld(cube, axis, axis != 1, error);
        CHECK(watertight(m));
        CHECK(m.triangleCount() == cube.triangleCount() && m.vertexCount() == cube.vertexCount());
        CHECK(m.quadCount() == quads);
        CHECK(groups(m).size() == 5); // the far side takes the mirrored side's group
        CHECK(m.layers.size() == 1 && m.layers[0].name == "Bump");
        for (size_t v = 0; v < m.vertexCount(); ++v)
            CHECK(glm::length(m.positions[v] - (m.base[v] + 0.5f * m.layers[0].delta[v])) < 1e-5f);
    }
    // Both meshes in X: welding gives back as many vertices as the halves share.
    Mesh twice = mirrorAndWeld(mirrorAndWeld(cube, 0, true, error), 2, true, error);
    CHECK(watertight(twice) && twice.vertexCount() == cube.vertexCount());
}

TEST(mirror_and_weld_closes_a_half_model) {
    // The +x half of a cube, open along x = 0 and pushed a hair (0.05% of its size) off the plane, as a half model
    // from another app might be: the open edge is welded onto the plane, so the result is closed.
    Mesh cube = makeCube(4, 2.0f);
    std::vector<uint8_t> keep(cube.triangleCount());
    for (uint32_t t = 0; t < keep.size(); ++t) {
        const glm::uvec3 c = cube.triangles[t];
        keep[t] = cube.positions[c.x].x >= 0.0f && cube.positions[c.y].x >= 0.0f && cube.positions[c.z].x >= 0.0f;
    }
    Mesh half = extractTriangles(cube, keep);
    for (auto& p : half.positions) p.x += 0.001f;
    half.rebuildTopology();
    CHECK(!watertight(half));
    std::string error;
    const Mesh whole = mirrorAndWeld(half, 0, true, error);
    CHECK(watertight(whole));
    CHECK(whole.triangleCount() == 2 * half.triangleCount());
    CHECK(whole.vertexCount() == cube.vertexCount());
}

TEST(mirror_flip) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    for (auto& p : doc.mesh.positions) p += glm::vec3(0.5f, 0.0f, 0.0f);
    doc.mesh.recomputeAllNormals();
    const AABB before = doc.mesh.bounds();
    const double volume = signedVolume(doc.mesh);
    CHECK(reg.execute(doc, "mirror").ok);
    AABB after = doc.mesh.bounds();
    CHECK(std::abs(after.min.x + before.max.x) < 1e-5f && std::abs(after.max.x + before.min.x) < 1e-5f);
    CHECK(std::abs(signedVolume(doc.mesh) - volume) < std::abs(volume) * 1e-4); // still outward
    CHECK(doc.undo.undoLabel() == "Mirror");
    CHECK(reg.execute(doc, "mirror", {{"pivot", std::string("center")}, {"axis", std::string("yz")}}).ok);
    after = doc.mesh.bounds();
    CHECK(std::abs(after.center().y - before.center().y) < 1e-5f);
    CHECK(reg.execute(doc, "undo").ok && reg.execute(doc, "undo").ok);
    CHECK(std::abs(doc.mesh.bounds().min.x - before.min.x) < 1e-6f);
    CHECK(!reg.execute(doc, "mirror", {{"pivot", std::string("somewhere")}}).ok);
}

TEST(extract_tcorne_tborder) {
    // The top face of a cube: a square edge with four sharp corners. The rim is quads, without the corners'
    // with TCorne, and triangles all along with TBorder.
    Mesh cube = makeCube(4, 2.0f);
    cube.ensureMask();
    for (uint32_t v = 0; v < cube.vertexCount(); ++v) cube.mask[v] = cube.positions[v].y > 0.99f ? 1.0f : 0.0f;
    auto rimQuads = [](const Mesh& m) {
        size_t n = 0;
        for (uint32_t t = 0; t < m.triangleCount(); ++t)
            n += m.polygroupAt(t) == 3 && m.quadPartnerAt(t) != Mesh::kNoQuad;
        return n / 2;
    };
    std::string error;
    ExtractOptions o;
    o.smooth = 0;
    o.threshold = 1.0f; // only triangles with every corner masked: the top face exactly
    const Mesh plain = extractShell(cube, o, error);
    CHECK(watertight(plain));
    CHECK(rimQuads(plain) == 16); // 4 edges of 4 segments
    o.triangleCorners = true;
    const Mesh corners = extractShell(cube, o, error);
    CHECK(watertight(corners) && rimQuads(corners) == 8); // each corner takes the quad on both sides of it
    o.triangleBorder = true;
    const Mesh border = extractShell(cube, o, error);
    CHECK(watertight(border) && rimQuads(border) == 0);
    CHECK(border.triangleCount() == plain.triangleCount());
}
