// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SubTools: the Document data model (select / add / remove / move, per-SubTool
// undo and unsaved-changes tracking), mesh merge and split, the cube
// primitive, and the subtool_* commands.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Polygroups.h"
#include "core/Primitives.h"
#include "core/SubTools.h"
#include "io/ObjIO.h"

#include <cmath>
#include <set>

using namespace sf;

namespace {

size_t groupCount(const Mesh& m) {
    std::set<uint32_t> ids;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) ids.insert(m.polygroupAt(t));
    return ids.size();
}

} // namespace

TEST(subtool_document_basics) {
    Document doc;
    CHECK(doc.subtoolCount() == 1 && doc.activeSubtoolInfo().name == "Sculpt");
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(doc.subtoolCount() == 1 && doc.activeSubtoolInfo().name == "Sphere");
    const size_t sphereTris = doc.mesh.triangleCount();
    CHECK(!doc.dirty());

    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 1);
    CHECK(doc.activeSubtoolInfo().name == "Cube");
    CHECK(doc.mesh.triangleCount() == 48u * 48u * 12u);
    CHECK(doc.subtoolMesh(0).triangleCount() == sphereTris); // checked in, intact
    CHECK(doc.dirty());
    // Placed beside the sphere, not inside it.
    CHECK(doc.mesh.bounds().min.x > doc.subtoolMesh(0).bounds().max.x);

    // Each SubTool keeps its own undo history.
    doc.markClean();
    CHECK(reg.execute(doc, "mask_all").ok);
    CHECK(doc.undo.canUndo());
    doc.selectSubtool(0);
    CHECK(doc.mesh.triangleCount() == sphereTris);
    CHECK(!doc.undo.canUndo());
    CHECK(doc.dirty()); // the cube's edit is still unsaved
    doc.selectSubtool(1);
    CHECK(doc.undo.canUndo() && doc.mesh.hasMask());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.maskAt(0) == 0.0f); // undone (the attribute may stay allocated)

    // Picking needs the BVH of the selected SubTool.
    RayHit hit;
    const glm::vec3 c = doc.mesh.bounds().center();
    CHECK(doc.bvh.raycast(doc.mesh, c + glm::vec3(0, 0, 10), glm::vec3(0, 0, -1), hit));

    // Names stay unique; rename, move, visibility, solo.
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.activeSubtoolInfo().name == "Cube 2");
    CHECK(reg.execute(doc, "subtool_rename", {{"to", std::string("Cube")}}).ok);
    CHECK(doc.activeSubtoolInfo().name == "Cube 3"); // "Cube" is taken
    CHECK(reg.execute(doc, "subtool_rename", {{"to", std::string("Helmet")}}).ok);
    CHECK(reg.execute(doc, "subtool_move", {{"direction", std::string("up")}}).ok);
    CHECK(doc.activeSubtool == 1 && doc.subtools[1].name == "Helmet" && doc.subtools[2].name == "Cube");
    CHECK(reg.execute(doc, "subtool_solo").ok);
    CHECK(!doc.subtools[0].visible && doc.subtools[1].visible && !doc.subtools[2].visible);
    CHECK(reg.execute(doc, "subtool_solo").ok);
    CHECK(doc.subtools[0].visible && doc.subtools[2].visible);
    CHECK(reg.execute(doc, "subtool_visibility", {{"name", std::string("Cube")}, {"visible", false}}).ok);
    CHECK(!doc.subtools[2].visible);
    CHECK(reg.execute(doc, "subtool_select", {{"name", std::string("Cube")}}).ok);
    CHECK(doc.activeSubtool == 2 && doc.subtools[2].visible); // selecting shows it
    CHECK(!reg.execute(doc, "subtool_select", {{"name", std::string("Nope")}}).ok);
    CHECK(reg.execute(doc, "subtool_list").message.find("2. Cube (") != std::string::npos);

    // Delete; the last one stays.
    CHECK(reg.execute(doc, "subtool_delete").ok);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 1 && doc.activeSubtoolInfo().name == "Helmet");
    CHECK(reg.execute(doc, "subtool_delete", {{"index", 0.0}}).ok);
    CHECK(doc.subtoolCount() == 1 && doc.activeSubtool == 0);
    CHECK(!reg.execute(doc, "subtool_delete").ok);

    // A new scene goes back to one SubTool.
    CHECK(reg.execute(doc, "subtool_append").ok);
    CHECK(reg.execute(doc, "new_plane", {{"resolution", 4.0}}).ok);
    CHECK(doc.subtoolCount() == 1 && doc.activeSubtoolInfo().name == "Plane");
}

TEST(subtool_cube_primitive) {
    Mesh cube = makeCube(4, 2.0f);
    CHECK(cube.triangleCount() == 4u * 4u * 12u);
    CHECK(cube.vertexCount() == 6u * 4u * 4u + 2u); // welded: V - E + F = 2
    CHECK(groupCount(cube) == 6);
    bool outward = true;
    for (size_t v = 0; v < cube.vertexCount(); ++v) outward &= glm::dot(cube.normals[v], cube.positions[v]) > 0.0f;
    CHECK(outward);
    CHECK(std::abs(cube.bounds().max.x - 1.0f) < 1e-5f);
    CHECK(splitIntoParts(cube).empty()); // one closed piece
}

TEST(subtool_merge_and_split) {
    Mesh a = makeIcosphere(2);
    Mesh b = makeCube(3);
    for (auto& p : b.positions) p.x += 4.0f;
    b.recomputeAllNormals();
    a.ensureMask(0.75f);
    b.ensureColors(glm::vec3(0.2f, 0.4f, 0.6f));
    a.addLayer("Smile");
    a.layers[0].delta.assign(a.vertexCount(), glm::vec3(0, 0.1f, 0));
    a.setLayerWeight(0, 0.5f);

    Mesh m = mergeMeshes({&a, &b});
    CHECK(m.vertexCount() == a.vertexCount() + b.vertexCount());
    CHECK(m.triangleCount() == a.triangleCount() + b.triangleCount());
    CHECK(m.hasMask() && m.mask.front() == 0.75f && m.mask.back() == 0.0f);
    CHECK(m.hasColors() && m.colors.front() == glm::vec3(1.0f) && m.colors.back() == glm::vec3(0.2f, 0.4f, 0.6f));
    CHECK(m.layers.size() == 1 && m.layers[0].name == "Smile" && m.layers[0].delta.back() == glm::vec3(0.0f));
    CHECK(std::abs(m.positions[0].y - a.positions[0].y) < 1e-5f); // layer at 0.5 survives the merge
    CHECK(groupCount(m) == 1 + 6); // sphere's one group + the cube's six, kept apart

    auto parts = splitIntoParts(m);
    CHECK(parts.size() == 2 && parts[0].triangleCount() == a.triangleCount() && parts[1].triangleCount() == b.triangleCount());
    auto groups = splitByPolygroups(m);
    CHECK(groups.size() == 7 && groups[0].triangleCount() == a.triangleCount());
    CHECK(groups[0].layers.size() == 1 && groups[0].hasMask());
}

TEST(subtool_merge_split_commands) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    const size_t sphereTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}}).ok);
    const size_t cubeTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("sphere")}}).ok);
    CHECK(reg.execute(doc, "subtool_visibility", {{"visible", false}}).ok); // hide the last one

    // Exporters see only what's visible.
    CHECK(mergedVisibleMesh(doc).triangleCount() == sphereTris + cubeTris);

    CHECK(reg.execute(doc, "subtool_merge_visible").ok);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 0 && doc.activeSubtoolInfo().name == "Merged");
    CHECK(doc.mesh.triangleCount() == sphereTris + cubeTris);
    CHECK(!doc.subtools[1].visible); // the hidden one is untouched
    CHECK(!doc.undo.canUndo());

    CHECK(reg.execute(doc, "subtool_split_parts").ok);
    CHECK(doc.subtoolCount() == 3 && doc.subtools[0].name == "Merged 1" && doc.subtools[1].name == "Merged 2");
    CHECK(doc.activeSubtool == 0 && doc.mesh.triangleCount() == cubeTris); // largest piece first

    // Merge down, then split by groups gives the pieces back (cube has 6 groups).
    CHECK(reg.execute(doc, "subtool_merge_down").ok);
    CHECK(doc.subtoolCount() == 2 && doc.mesh.triangleCount() == sphereTris + cubeTris);
    CHECK(reg.execute(doc, "subtool_split_groups").ok);
    CHECK(doc.subtoolCount() == 1 + 7);
    CHECK(!reg.execute(doc, "subtool_split_parts").ok); // a cube side is one piece

    // Duplicate sits right below the original and is independent.
    CHECK(reg.execute(doc, "subtool_select", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "subtool_duplicate").ok);
    CHECK(doc.activeSubtool == 1 && doc.subtools[1].name.find("copy") != std::string::npos);
    CHECK(reg.execute(doc, "mask_all").ok);
    CHECK(!doc.subtoolMesh(0).hasMask());
}

TEST(subtool_append_file) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    const std::string path = tempPath("sf_subtool_eye.obj");
    std::string err;
    CHECK(saveObj(path, makeIcosphere(1, 0.2f), err));
    CHECK(reg.execute(doc, "subtool_append", {{"path", path}}).ok);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtoolInfo().name == "sf_subtool_eye");
    CHECK(doc.mesh.triangleCount() == makeIcosphere(1).triangleCount());
    CHECK(!reg.execute(doc, "subtool_append", {{"path", tempPath("sf_missing.obj")}}).ok);
    CHECK(!reg.execute(doc, "subtool_append", {{"type", std::string("teapot")}}).ok);
    std::filesystem::remove(path);
}
