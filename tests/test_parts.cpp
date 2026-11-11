// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parts: the Document data model (select / add / remove / move, per-Part
// undo and unsaved-changes tracking), mesh merge and split, the cube
// primitive, and the part_* commands.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/FaceGroups.h"
#include "core/Multires.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Parts.h"
#include "io/ObjIO.h"
#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <set>

using namespace sf;

namespace {

size_t groupCount(const Mesh& m) {
    std::set<uint32_t> ids;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) ids.insert(m.facegroupAt(t));
    return ids.size();
}

} // namespace

TEST(part_document_basics) {
    Document doc;
    CHECK(doc.partCount() == 1 && doc.activePartInfo().name == "Sculpt");
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(doc.partCount() == 1 && doc.activePartInfo().name == "Sphere");
    const size_t sphereTris = doc.mesh.triangleCount();
    CHECK(!doc.dirty());

    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 1);
    CHECK(doc.activePartInfo().name == "Cube");
    CHECK(doc.mesh.triangleCount() == 48u * 48u * 12u);
    CHECK(doc.partMesh(0).triangleCount() == sphereTris); // checked in, intact
    CHECK(doc.dirty());
    // Placed beside the sphere, not inside it.
    CHECK(doc.mesh.bounds().min.x > doc.partMesh(0).bounds().max.x);

    // Each Part keeps its own undo history.
    doc.markClean();
    CHECK(reg.execute(doc, "mask_all").ok);
    CHECK(doc.undo.canUndo());
    doc.selectPart(0);
    CHECK(doc.mesh.triangleCount() == sphereTris);
    CHECK(!doc.undo.canUndo());
    CHECK(doc.dirty()); // the cube's edit is still unsaved
    doc.selectPart(1);
    CHECK(doc.undo.canUndo() && doc.mesh.hasMask());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.mesh.maskAt(0) == 0.0f); // undone (the attribute may stay allocated)

    // Picking needs the BVH of the selected Part.
    RayHit hit;
    const glm::vec3 c = doc.mesh.bounds().center();
    CHECK(doc.bvh.raycast(doc.mesh, c + glm::vec3(0, 0, 10), glm::vec3(0, 0, -1), hit));

    // Names stay unique; rename, move, visibility, solo.
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.activePartInfo().name == "Cube 2");
    CHECK(reg.execute(doc, "part_rename", {{"to", std::string("Cube")}}).ok);
    CHECK(doc.activePartInfo().name == "Cube 3"); // "Cube" is taken
    CHECK(reg.execute(doc, "part_rename", {{"to", std::string("Helmet")}}).ok);
    CHECK(reg.execute(doc, "part_move", {{"direction", std::string("up")}}).ok);
    CHECK(doc.activePart == 1 && doc.parts[1].name == "Helmet" && doc.parts[2].name == "Cube");
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(!doc.parts[0].visible && doc.parts[1].visible && !doc.parts[2].visible);
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(doc.parts[0].visible && doc.parts[2].visible);
    CHECK(reg.execute(doc, "part_visibility", {{"name", std::string("Cube")}, {"visible", false}}).ok);
    CHECK(!doc.parts[2].visible);
    CHECK(reg.execute(doc, "part_select", {{"name", std::string("Cube")}}).ok);
    CHECK(doc.activePart == 2 && doc.parts[2].visible); // selecting shows it
    CHECK(!reg.execute(doc, "part_select", {{"name", std::string("Nope")}}).ok);
    CHECK(reg.execute(doc, "part_list").message.find("2. Cube (") != std::string::npos);

    // Delete; the last one stays.
    CHECK(reg.execute(doc, "part_delete").ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 1 && doc.activePartInfo().name == "Helmet");
    CHECK(reg.execute(doc, "part_delete", {{"index", 0.0}}).ok);
    CHECK(doc.partCount() == 1 && doc.activePart == 0);
    CHECK(!reg.execute(doc, "part_delete").ok);

    // A new scene goes back to one Part.
    CHECK(reg.execute(doc, "part_append").ok);
    CHECK(reg.execute(doc, "new_plane", {{"resolution", 4.0}}).ok);
    CHECK(doc.partCount() == 1 && doc.activePartInfo().name == "Plane");
}

// Solo remembers what was shown when it began and puts exactly that back (a hidden Part stays hidden).
TEST(part_solo_restores_hidden_parts) {
    Document doc;
    CommandRegistry reg;
    doc.setMesh(makeCube(4, 2.0f), "A");
    for (const char* n : {"B", "C", "D"}) {
        CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
        doc.activePartInfo().name = n;
    }
    CHECK(reg.execute(doc, "part_visibility", {{"index", 0.0}, {"visible", false}}).ok); // hide A
    CHECK(reg.execute(doc, "part_select", {{"name", std::string("B")}}).ok);
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(!doc.parts[0].visible && doc.parts[1].visible && !doc.parts[2].visible && !doc.parts[3].visible);

    // The project keeps Solo's memory (save / load while in Solo).
    const std::string path = (std::filesystem::temp_directory_path() / "sf_solo_hidden.sfproj").string();
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(reg.execute(back, "part_solo").ok);
    CHECK(!back.parts[0].visible && back.parts[1].visible && back.parts[2].visible && back.parts[3].visible);
    std::filesystem::remove(path);

    // During Solo: a new Part, a rename, a delete and a selection change keep the memory.
    CHECK(reg.execute(doc, "part_select", {{"name", std::string("C")}}).ok);
    CHECK(reg.execute(doc, "part_rename", {{"to", std::string("Cee")}}).ok);
    CHECK(reg.execute(doc, "part_delete", {{"name", std::string("D")}}).ok);
    CHECK(reg.execute(doc, "part_select", {{"name", std::string("B")}}).ok);
    CHECK(reg.execute(doc, "part_solo").ok); // soloing again after the selection moved: still B alone
    CHECK(doc.partCount() == 3 && doc.parts[1].visible && !doc.parts[0].visible && !doc.parts[2].visible);
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(!doc.parts[0].visible && doc.parts[1].visible && doc.parts[2].visible); // A still hidden
    CHECK(reg.execute(doc, "part_solo").ok); // Solo again (A still hidden); Unhide all then ends its memory
    CHECK(reg.execute(doc, "part_show_all").ok);
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(reg.execute(doc, "part_solo").ok);
    CHECK(doc.parts[0].visible && doc.parts[1].visible && doc.parts[2].visible); // "Unhide all" ended the old memory
}

TEST(part_cube_primitive) {
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

TEST(part_merge_and_split) {
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
    auto groups = splitByFaceGroups(m);
    CHECK(groups.size() == 7 && groups[0].triangleCount() == a.triangleCount());
    CHECK(groups[0].layers.size() == 1 && groups[0].hasMask());
}

TEST(part_merge_split_commands) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    const size_t sphereTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    const size_t cubeTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("sphere")}}).ok);
    CHECK(reg.execute(doc, "part_visibility", {{"visible", false}}).ok); // hide the last one

    // Exporters see only what's visible.
    CHECK(mergedVisibleMesh(doc).triangleCount() == sphereTris + cubeTris);

    CHECK(reg.execute(doc, "part_merge_visible").ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 0 && doc.activePartInfo().name == "Merged");
    CHECK(doc.mesh.triangleCount() == sphereTris + cubeTris);
    CHECK(!doc.parts[1].visible); // the hidden one is untouched
    CHECK(!doc.undo.canUndo());

    CHECK(reg.execute(doc, "part_split_parts").ok);
    CHECK(doc.partCount() == 3 && doc.parts[0].name == "Merged 1" && doc.parts[1].name == "Merged 2");
    CHECK(doc.activePart == 0 && doc.mesh.triangleCount() == cubeTris); // largest piece first

    // Merge down, then split by groups gives the pieces back (cube has 6 groups).
    CHECK(reg.execute(doc, "part_merge_down").ok);
    CHECK(doc.partCount() == 2 && doc.mesh.triangleCount() == sphereTris + cubeTris);
    CHECK(reg.execute(doc, "part_split_groups").ok);
    CHECK(doc.partCount() == 1 + 7);
    CHECK(!reg.execute(doc, "part_split_parts").ok); // a cube side is one piece

    // Duplicate sits right below the original and is independent.
    CHECK(reg.execute(doc, "part_select", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "part_duplicate").ok);
    CHECK(doc.activePart == 1 && doc.parts[1].name.find("copy") != std::string::npos);
    CHECK(reg.execute(doc, "mask_all").ok);
    CHECK(!doc.partMesh(0).hasMask());
}

TEST(part_append_file) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    const std::string path = tempPath("sf_part_eye.obj");
    std::string err;
    CHECK(saveObj(path, makeIcosphere(1, 0.2f), err));
    CHECK(reg.execute(doc, "part_append", {{"path", path}}).ok);
    CHECK(doc.partCount() == 2 && doc.activePartInfo().name == "sf_part_eye");
    CHECK(doc.mesh.triangleCount() == makeIcosphere(1).triangleCount());
    CHECK(!reg.execute(doc, "part_append", {{"path", tempPath("sf_missing.obj")}}).ok);
    CHECK(!reg.execute(doc, "part_append", {{"type", std::string("teapot")}}).ok);
    std::filesystem::remove(path);
}

TEST(part_folders) {
    // #96: folders group Parts in the list; hide, show and move them together.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(reg.execute(doc, "part_rename", {{"to", std::string("Head")}}).ok);
    for (const char* name : {"Eye L", "Body", "Eye R"}) {
        CHECK(reg.execute(doc, "part_append", {{"type", std::string("sphere")}}).ok);
        CHECK(reg.execute(doc, "part_rename", {{"to", std::string(name)}}).ok);
    }
    doc.markClean();
    // A new folder takes the active Part (Eye R); a Part joining it goes right below the folder's last Part.
    CommandResult r = reg.execute(doc, "part_folder_new", {{"folder", std::string("Eyes")}});
    CHECK(r.ok && doc.partFolders.size() == 1 && doc.partFolders[0].name == "Eyes");
    CHECK(doc.activePartInfo().name == "Eye R" && doc.activePartInfo().folder == doc.partFolders[0].id);
    CHECK(doc.dirty());
    CHECK(reg.execute(doc, "part_set_folder", {{"name", std::string("Eye L")}, {"folder", std::string("Eyes")}}).ok);
    CHECK(doc.parts[0].name == "Head" && doc.parts[1].name == "Body" && doc.parts[2].name == "Eye R" &&
          doc.parts[3].name == "Eye L");
    CHECK(doc.activePartInfo().name == "Eye R"); // the active Part follows the move
    CHECK(!reg.execute(doc, "part_set_folder", {{"index", 0.0}, {"folder", std::string("Nope")}}).ok);
    // Names stay unique.
    CHECK(reg.execute(doc, "part_folder_new", {{"folder", std::string("Eyes")}, {"empty", true}}).ok);
    CHECK(doc.partFolders.size() == 2 && doc.partFolders[1].name == "Eyes 2");
    CHECK(reg.execute(doc, "part_folder_rename", {{"folder", std::string("Eyes 2")}, {"to", std::string("Props")}}).ok);
    CHECK(doc.partFolders[1].name == "Props");
    CHECK(reg.execute(doc, "part_list").message.find("Eye L (") != std::string::npos);
    CHECK(reg.execute(doc, "part_list").message.find("in Eyes") != std::string::npos);
    CHECK(reg.execute(doc, "part_list").message.find("Folder Eyes (2 Parts)") != std::string::npos);

    // Hide and show the whole folder.
    CHECK(reg.execute(doc, "part_folder_visibility", {{"folder", std::string("Eyes")}}).ok);
    CHECK(!doc.parts[2].visible && !doc.parts[3].visible && doc.parts[0].visible && doc.parts[1].visible);
    CHECK(reg.execute(doc, "part_folder_visibility", {{"folder", std::string("Eyes")}}).ok);
    CHECK(doc.parts[2].visible && doc.parts[3].visible);
    CHECK(!reg.execute(doc, "part_folder_visibility", {{"folder", std::string("Props")}}).ok); // empty
    CHECK(reg.execute(doc, "part_folder_collapse", {{"folder", std::string("Eyes")}}).ok && doc.partFolders[0].collapsed);

    // transform folder=true moves the folder's visible Parts, nothing else.
    const glm::vec3 head = doc.partMesh(0).bounds().center(), eyeL = doc.partMesh(3).bounds().center();
    const glm::vec3 eyeR = doc.mesh.bounds().center();
    r = reg.execute(doc, "transform", {{"translate", std::string("0,1,0")}, {"folder", true}});
    CHECK(r.ok && r.message.find("in 2 Parts") != std::string::npos);
    CHECK(std::abs(doc.mesh.bounds().center().y - eyeR.y - 1.0f) < 1e-4f);
    CHECK(std::abs(doc.partMesh(3).bounds().center().y - eyeL.y - 1.0f) < 1e-4f);
    CHECK(glm::length(doc.partMesh(0).bounds().center() - head) < 1e-6f);

    // Duplicates and split pieces stay in the folder; out of it, and removing the folder keeps the Parts.
    CHECK(reg.execute(doc, "part_duplicate").ok && doc.activePartInfo().folder == doc.partFolders[0].id);
    CHECK(reg.execute(doc, "part_set_folder", {{"folder", std::string()}}).ok && doc.activePartInfo().folder == 0);
    CHECK(reg.execute(doc, "part_folder_delete", {{"folder", std::string("Eyes")}}).ok);
    CHECK(doc.partFolders.size() == 1 && doc.partCount() == 5);
    for (const Part& s : doc.parts) CHECK(s.folder == 0);
    // A new scene has no folders.
    CHECK(reg.execute(doc, "new_sphere").ok && doc.partFolders.empty());
}

TEST(part_split_by_selection) {
    // Split Hidden / Masked / Unmasked (foirgairwolf on Discord): the triangles leave for a new Part below, one undo
    // step.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t total = doc.mesh.triangleCount();
    CHECK(!reg.execute(doc, "part_split_masked").ok);   // nothing masked
    CHECK(!reg.execute(doc, "part_split_unmasked").ok); // everything unmasked: nothing would stay
    CHECK(!reg.execute(doc, "part_split_hidden").ok);
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 0.0f);
    for (size_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].x > 0.0f ? 1.0f : 0.0f;
    doc.mesh.colors.assign(doc.mesh.vertexCount(), glm::vec3(0.2f, 0.4f, 0.6f));
    size_t masked = 0;
    trianglesIn(doc.mesh, TriangleSet::Masked, &masked);
    CHECK(masked > 0 && masked < total);
    CHECK(reg.execute(doc, "part_split_masked").ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 0);
    CHECK(doc.mesh.triangleCount() == total - masked);
    CHECK(doc.partMesh(1).triangleCount() == masked && !doc.partMesh(1).hasMask()); // the new Part starts unmasked
    CHECK(doc.partMesh(1).hasColors() && doc.partMesh(1).colors[0] == glm::vec3(0.2f, 0.4f, 0.6f));
    CHECK(doc.parts[1].name == doc.parts[0].name + " masked");
    // One undo brings the source back whole and takes the new Part away; redo does both again.
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() == total);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.partCount() == 2 && doc.mesh.triangleCount() == total - masked && doc.partMesh(1).triangleCount() == masked);
    CHECK(reg.execute(doc, "undo").ok);

    // Unmasked: the masked side stays.
    CHECK(reg.execute(doc, "part_split_unmasked").ok);
    CHECK(doc.mesh.triangleCount() == masked && doc.partMesh(1).triangleCount() == total - masked);
    CHECK(reg.execute(doc, "undo").ok);

    // Hidden: the new Part shows them.
    doc.mesh.hidden.assign(total, uint8_t(0));
    for (size_t t = 0; t < total / 4; ++t) doc.mesh.hidden[t] = 1;
    CHECK(reg.execute(doc, "part_split_hidden").ok);
    CHECK(doc.partMesh(1).triangleCount() == total / 4 && !doc.partMesh(1).hasHidden());
    CHECK(doc.mesh.triangleCount() == total - total / 4 && !doc.mesh.hasHidden());
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() == total);

    // Delete Masked: the mirror of Remove Hidden.
    CHECK(reg.execute(doc, "delete_masked").ok);
    CHECK(doc.mesh.triangleCount() == total - masked);
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.triangleCount() == total);
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 1.0f);
    CHECK(!reg.execute(doc, "delete_masked").ok); // everything masked
    CHECK(!reg.execute(doc, "part_split_masked").ok);
}

TEST(part_split_keep_original) {
    // Keep original (_tfx on Discord): keep=true copies the triangles into the new Part; the source stays untouched,
    // mask, levels and all; one undo takes the copy away.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(reg.execute(doc, "subdiv_divide").ok);
    const int levels = subdivLevelCount(doc.mesh);
    CHECK(levels == 2);
    const size_t total = doc.mesh.triangleCount();
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 0.0f);
    for (size_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].x > 0.0f ? 1.0f : 0.0f;
    doc.mesh.colors.assign(doc.mesh.vertexCount(), glm::vec3(0.7f, 0.1f, 0.3f));
    size_t masked = 0;
    trianglesIn(doc.mesh, TriangleSet::Masked, &masked);
    CHECK(reg.execute(doc, "part_split_masked", {{"keep", true}}).ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 0);
    CHECK(doc.mesh.triangleCount() == total && doc.mesh.hasMask() && subdivLevelCount(doc.mesh) == levels);
    CHECK(doc.partMesh(1).triangleCount() == masked && !doc.partMesh(1).hasMask());
    CHECK(doc.partMesh(1).hasColors() && doc.partMesh(1).colors[0] == glm::vec3(0.7f, 0.1f, 0.3f));
    CHECK(doc.parts[1].name == doc.parts[0].name + " masked");
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() == total && doc.mesh.hasMask());
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.partCount() == 2 && doc.partMesh(1).triangleCount() == masked && doc.mesh.triangleCount() == total);
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(reg.execute(doc, "part_split_unmasked", {{"keep", true}}).ok);
    CHECK(doc.partMesh(1).triangleCount() == total - masked && doc.mesh.triangleCount() == total);
    CHECK(reg.execute(doc, "undo").ok);
    // Everything masked: a plain split refuses (nothing would stay); keep copies it all.
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 1.0f);
    CHECK(!reg.execute(doc, "part_split_masked").ok);
    CHECK(reg.execute(doc, "part_split_masked", {{"keep", true}}).ok);
    CHECK(subdivLevelCount(doc.partMesh(1)) == 0 && subdivLevelCount(doc.mesh) == levels); // cut on the copy only
    CHECK(doc.partMesh(1).triangleCount() == total && doc.mesh.triangleCount() == total);
}

TEST(part_split_linked_copy) {
    // Splitting a moved linked copy (#94): the new Part stays where the copy is, not at the shared mesh's place.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(reg.execute(doc, "part_duplicate_linked", {{"offset", std::string("3,0,0")}}).ok);
    const int copy = doc.activePart;
    CHECK(doc.parts[size_t(copy)].moved());
    doc.mesh.mask.assign(doc.mesh.vertexCount(), 0.0f);
    for (size_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].x > 0.0f ? 1.0f : 0.0f;
    CHECK(reg.execute(doc, "part_split_masked").ok);
    CHECK(doc.parts[size_t(copy + 1)].transform == doc.parts[size_t(copy)].transform);
}

TEST(part_split_mask_threshold) {
    // A triangle is masked when its corners' average mask is above 0.5 (kMaskedThreshold), not at it.
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    m.triangles = {{0, 1, 2}, {1, 3, 2}};
    m.mask = {0.5f, 0.5f, 0.5f, 1.0f};
    size_t n = 0;
    const auto in = trianglesIn(m, TriangleSet::Masked, &n);
    CHECK(n == 1 && in[0] == 0 && in[1] == 1);
    CHECK(trianglesIn(m, TriangleSet::Unmasked)[0] == 1);
}

// Alt+click on a Part (Discord, macOS: it often missed until the user zoomed in close): a thin Part a few points off
// the pointer still counts, near or far, and a click that wobbles a few points is still a click.
TEST(part_pick_near_thin_part) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}}).ok);
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    doc.selectPart(0); // the sphere is the Part being sculpted; the cube becomes a thin wire (a Part's mesh, not a
                       // scaled Part transform: those are similarities)
    const float W = 800.0f, H = 800.0f;
    Camera cam;
    // A wire 2 points wide on screen at the given camera distance (a Part about as thin as a visible one gets).
    auto placeWire = [&](float z) {
        const float pxPerUnit = H / (2.0f * std::max(cam.distance - 1.6f, 0.5f) * std::tan(cam.fovY * 0.5f));
        const float half = 1.0f / pxPerUnit;
        doc.partMesh(1) = placedMesh(makeCube(2, 2.0f), glm::scale(glm::mat4(1.0f), glm::vec3(half, 0.5f, half)));
        doc.parts[1].data->bvhDirty = true;
        doc.parts[1].transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 0.0f, z));
        doc.syncBvh();
    };

    for (float distance : {4.0f, 12.0f}) {
        cam.distance = distance;
        cam.aspect = W / H;
        placeWire(1.6f);
        CHECK(doc.parts[1].moved());
        const PartRayFn rayAt = [&](float x, float y, glm::vec3& o, glm::vec3& d) { cam.screenRay(x, y, W, H, o, d); };
        const glm::vec4 clip = cam.projection() * cam.view() * glm::vec4(0.3f, 0.0f, 1.6f, 1.0f);
        const float px = (clip.x / clip.w * 0.5f + 0.5f) * W, py = (0.5f - clip.y / clip.w * 0.5f) * H;
        const int sphere = pickPartAt(doc, rayAt, px + 40.0f, py, kPartPickTolerance, 0);
        CHECK(sphere == 0); // well off the wire: the sphere under the pointer
        CHECK(pickPartAt(doc, rayAt, px, py, 0.0f, 0) == 1); // dead on the wire: the exact pick finds it
        // 4 points off the wire: the exact pick sees the sphere (the active Part), the tolerant one the wire.
        RayHit hit;
        CHECK(pickPartAt(doc, rayAt, px + 4.0f, py, 0.0f, 0) == 0);
        CHECK(pickPartAt(doc, rayAt, px + 4.0f, py, kPartPickTolerance, 0, &hit) == 1);
        CHECK(hit.position.z > 1.5f);
        CHECK(pickPartAt(doc, rayAt, px - 5.0f, py + 30.0f, kPartPickTolerance, 0) == 1);
        // Behind the sphere's edge a Part is not picked through it: hide-and-seek stays honest.
        placeWire(-2.5f);
        CHECK(pickPartAt(doc, rayAt, px + 4.0f, py, kPartPickTolerance, 0) == 0);
        placeWire(1.6f);
    }

    // The press-to-release wobble that is still a click, and the drag that is not.
    CHECK(!partClickIsDrag(3.0f, 3.0f)); // 4.2 points: a trackpad click
    CHECK(!partClickIsDrag(5.0f, 0.0f));
    CHECK(partClickIsDrag(6.0f, 0.0f));
    CHECK(partClickIsDrag(4.5f, 4.5f));
}
