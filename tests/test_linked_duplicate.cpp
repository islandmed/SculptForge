// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Linked Duplicates (#94): Parts sharing one mesh (PartData), each placed by its own transform. Sculpting any of
// them changes all, undo is shared, Make Unique breaks the link, deleting one keeps the others; the transform
// command moves a linked copy as a whole; projects keep the links; exports instance (glTF, USD) or flatten (OBJ).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Json.h"
#include "core/Parts.h"
#include "core/Project.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/UsdIO.h"
#include "io/Zip.h"
#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace sf;

namespace {

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) { return glm::length(a - b) < eps; }

// A sphere "Body" and its linked copy "Body link" (active), 3 units along +X.
Document linkedScene(CommandRegistry& reg) {
    Document doc;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(reg.execute(doc, "part_rename", {{"to", std::string("Body")}}).ok);
    const CommandResult r = reg.execute(doc, "part_duplicate_linked", {{"offset", std::string("3,0,0")}});
    CHECK(r.ok);
    return doc;
}

// A stroke on the active Part at the top of its sphere, in scene coordinates (looking down).
CommandResult strokeTop(CommandRegistry& reg, Document& doc) {
    const AABB b = doc.partBounds(doc.activePart);
    const glm::vec3 c = b.center();
    const float y = b.max.y + 0.5f;
    char points[160];
    std::snprintf(points, sizeof points, "%g,%g,%g %g,%g,%g", double(c.x - 0.2f), double(y), double(c.z),
                  double(c.x + 0.2f), double(y), double(c.z));
    return reg.execute(doc, "stroke", {{"points", std::string(points)}, {"view", std::string("0,-1,0")},
                                       {"brush", std::string("Clay")}, {"radius", 0.3}, {"strength", 1.0}});
}

std::string readText(const std::string& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

size_t countOf(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) ++n;
    return n;
}

} // namespace

TEST(linked_duplicate_shares_mesh_and_history) {
    CommandRegistry reg;
    Document doc = linkedScene(reg);
    CHECK(doc.partCount() == 2 && doc.activePart == 1);
    CHECK(doc.activePartInfo().name == "Body link");
    CHECK(doc.linkCount(0) == 2 && doc.linkCount(1) == 2);
    CHECK(doc.meshKey(0) == doc.meshKey(1));       // one GPU mesh
    CHECK(&doc.partMesh(0) == &doc.partMesh(1));   // one mesh: the checked-out one
    CHECK(near(glm::vec3(doc.parts[1].transform[3]), glm::vec3(3, 0, 0)));
    CHECK(!doc.parts[0].moved() && doc.parts[1].moved());
    CHECK(near(doc.partBounds(1).center(), doc.partBounds(0).center() + glm::vec3(3, 0, 0), 1e-3f));
    CHECK(reg.execute(doc, "part_list").message.find("linked x2") != std::string::npos);

    // A stroke on the copy (in scene space, where the copy is) changes the one mesh both show.
    const std::vector<glm::vec3> before = doc.mesh.positions;
    CHECK(strokeTop(reg, doc).ok);
    CHECK(doc.mesh.positions != before);
    const std::vector<glm::vec3> after = doc.mesh.positions;
    // It landed on the top of the sphere in its own space (the scene point, moved back by the copy's transform).
    size_t movedTop = 0, movedElsewhere = 0;
    for (size_t v = 0; v < before.size(); ++v)
        if (before[v] != after[v]) (before[v].y > 0.5f ? movedTop : movedElsewhere) += 1;
    CHECK(movedTop > 0 && movedElsewhere == 0);

    // Selecting the source keeps the same data checked out: no swap, the stroke is there, the history is shared.
    doc.selectPart(0);
    CHECK(doc.partMesh(0).positions == after && doc.partMesh(1).positions == after);
    CHECK(doc.undo.canUndo());
    CHECK(reg.execute(doc, "undo").ok); // undone from the source: both change back
    CHECK(doc.partMesh(1).positions == before);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.partMesh(1).positions == after);

    // Another Part in between: the group's data goes back in and out intact.
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.partCount() == 3 && doc.linkCount(2) == 1);
    CHECK(doc.partMesh(0).positions == after && doc.partMesh(1).positions == after);
    doc.selectPart(1);
    CHECK(doc.mesh.positions == after && doc.undo.canUndo());
}

TEST(linked_duplicate_undo_make_unique_delete) {
    CommandRegistry reg;
    Document doc = linkedScene(reg);
    // Undo takes the copy away (from the copy itself: the source takes over), redo brings it back.
    CHECK(reg.execute(doc, "undo").ok);
    CHECK(doc.partCount() == 1 && doc.activePartInfo().name == "Body" && doc.linkCount(0) == 1);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(doc.partCount() == 2 && doc.linkCount(0) == 2 && doc.parts[1].moved());

    // Make Unique: its own mesh (same shape), same place, fresh history; edits no longer reach the source.
    doc.selectPart(1);
    const std::vector<glm::vec3> shape = doc.mesh.positions;
    const glm::mat4 place = doc.parts[1].transform;
    CHECK(reg.execute(doc, "part_make_unique").ok);
    CHECK(doc.linkCount(0) == 1 && doc.linkCount(1) == 1 && doc.meshKey(0) != doc.meshKey(1));
    CHECK(doc.parts[1].transform == place && doc.mesh.positions == shape && !doc.undo.canUndo());
    CHECK(!reg.execute(doc, "part_make_unique").ok); // not linked any more
    CHECK(strokeTop(reg, doc).ok);
    CHECK(doc.mesh.positions != shape && doc.partMesh(0).positions == shape);

    // Delete keeps the others: link again, delete the source, the copy keeps the mesh.
    doc.selectPart(0);
    CHECK(reg.execute(doc, "part_duplicate_linked").ok);
    CHECK(doc.partCount() == 3 && doc.linkCount(0) == 2);
    CHECK(strokeTop(reg, doc).ok);
    const std::vector<glm::vec3> sculpted = doc.mesh.positions;
    CHECK(reg.execute(doc, "part_delete", {{"index", 0.0}}).ok);
    CHECK(doc.partCount() == 2);
    const int copy = doc.findPart("Body link 2");
    CHECK(copy >= 0);
    if (copy >= 0) {
        CHECK(doc.linkCount(copy) == 1 && doc.partMesh(copy).positions == sculpted);
        doc.selectPart(copy);
        CHECK(doc.undo.canUndo()); // the group's history went with it
    }
}

TEST(linked_duplicate_transform_moves_the_copy) {
    CommandRegistry reg;
    Document doc = linkedScene(reg);
    const std::vector<glm::vec3> shape = doc.mesh.positions;
    const glm::vec3 centre = doc.partBounds(1).center();
    // Unmasked: the copy moves as a whole (its transform), the shared mesh stays.
    CHECK(reg.execute(doc, "transform", {{"translate", std::string("0,2,0")}, {"rotate", std::string("0,90,0")}}).ok);
    CHECK(doc.mesh.positions == shape);
    CHECK(near(doc.partBounds(1).center(), centre + glm::vec3(0, 2, 0), 1e-3f));
    CHECK(near(doc.partBounds(0).center(), centre - glm::vec3(3, 0, 0), 1e-3f)); // the source didn't move
    // Uneven scale keeps a linked copy a similarity (one scale, the mean).
    CHECK(reg.execute(doc, "transform", {{"scale", std::string("1,2,3")}}).ok);
    const glm::mat3 l(doc.parts[1].transform);
    CHECK(std::abs(glm::length(l[0]) - 2.0f) < 1e-4f && std::abs(glm::length(l[1]) - 2.0f) < 1e-4f &&
          std::abs(glm::length(l[2]) - 2.0f) < 1e-4f);
    // One undo step each, in the shared history.
    CHECK(reg.execute(doc, "undo").ok && reg.execute(doc, "undo").ok);
    CHECK(near(doc.partBounds(1).center(), centre, 1e-3f) && doc.mesh.positions == shape);
    CHECK(reg.execute(doc, "redo").ok);
    CHECK(near(doc.partBounds(1).center(), centre + glm::vec3(0, 2, 0), 1e-3f));

    // Masked: the unmasked part of the shared mesh moves, in the copy's own space (turned 90 degrees about Y,
    // its local +X is the scene's -Z... a scene +Y move is local +Y either way).
    CHECK(reg.execute(doc, "mask_all").ok);
    for (size_t v = 0; v < doc.mesh.vertexCount(); ++v)
        if (doc.mesh.positions[v].y > 0.5f) doc.mesh.mask[v] = 0.0f; // the top is free
    doc.mesh.markAllDirty();
    const glm::mat4 place = doc.parts[1].transform;
    CHECK(reg.execute(doc, "transform", {{"translate", std::string("0,0.5,0")}}).ok);
    CHECK(doc.parts[1].transform == place);
    bool topUp = true, restStill = true;
    for (size_t v = 0; v < shape.size(); ++v) {
        if (shape[v].y > 0.5f) topUp &= std::abs(doc.mesh.positions[v].y - (shape[v].y + 0.5f)) < 1e-3f;
        else restStill &= doc.mesh.positions[v] == shape[v];
    }
    CHECK(topUp && restStill);
}

TEST(linked_duplicate_project_round_trip) {
    CommandRegistry reg;
    Document doc = linkedScene(reg);
    CHECK(strokeTop(reg, doc).ok);
    CHECK(reg.execute(doc, "part_duplicate_linked", {{"offset", std::string("0,0,3")}}).ok);
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}}).ok);
    doc.selectPart(1);
    doc.parts[2].folder = doc.addPartFolder("Copies"); // a linked copy in a folder (#96) keeps both
    const std::string path = tempPath("sf_linked.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == 4 && back.activePart == 1);
    CHECK(back.linkCount(0) == 3 && back.linkCount(3) == 1);
    CHECK(back.meshKey(0) == back.meshKey(1) && back.meshKey(1) == back.meshKey(2));
    for (int i = 0; i < 4; ++i) {
        CHECK(back.parts[size_t(i)].name == doc.parts[size_t(i)].name);
        CHECK(back.parts[size_t(i)].transform == doc.parts[size_t(i)].transform);
        CHECK(back.partMesh(i).positions == doc.partMesh(i).positions);
        CHECK((back.parts[size_t(i)].folder != 0) == (i == 2));
    }
    CHECK(back.partFolders.size() == 1 && back.partFolders[0].name == "Copies");
    // A project with links needs format 8; an older reader says to update.
    const std::string text = [&] {
        std::vector<ZipEntry> entries;
        std::string e;
        readStoredZip(path, entries, e);
        for (const auto& z : entries)
            if (z.name == "manifest.json") return std::string(z.data.begin(), z.data.end());
        return std::string();
    }();
    Json manifest;
    std::string e;
    CHECK(Json::parse(text, manifest, e));
    CHECK(manifest["version"].asNumber(0) == 8);
    CHECK(manifest["parts"][size_t(1)]["link"].asNumber(-1) == 0 && manifest["parts"][size_t(2)]["link"].asNumber(-1) == 0);
    CHECK(manifest["parts"][size_t(3)]["link"].isNull());
    std::filesystem::remove(path);
}

TEST(linked_duplicate_memory_500_copies) {
    CommandRegistry reg;
    Document doc;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}}).ok);
    const size_t one = doc.mesh.memoryBytes();
    for (int i = 0; i < 500; ++i) doc.linkedDuplicate(0, glm::vec3(float(i % 25) * 2.5f, float(i / 25) * 2.5f, 0.0f));
    CHECK(doc.partCount() == 501 && doc.linkCount(0) == 501);
    // 500 linked copies ~= 1 mesh: one PartData (one mesh, one BVH) for all of them, a transform each.
    std::vector<const PartData*> distinct;
    for (const Part& p : doc.parts)
        if (std::find(distinct.begin(), distinct.end(), p.data.get()) == distinct.end()) distinct.push_back(p.data.get());
    CHECK(distinct.size() == 1);
    const size_t perCopy = sizeof(Part) + doc.parts.back().name.capacity();
    CHECK(perCopy * 500 < one / 4);
    // Bounds cover every copy where it is.
    const AABB all = doc.visibleBounds();
    CHECK(all.max.x > 24.0f * 2.5f && all.max.y > 19.0f * 2.5f);
}

TEST(linked_duplicate_exports) {
    CommandRegistry reg;
    Document doc = linkedScene(reg);
    doc.path = tempPath("Linked.sfproj");
    // glTF: two nodes, one mesh; the copy's node carries its matrix.
    const std::string glb = tempPath("sf_linked.glb");
    CHECK(reg.execute(doc, "export_glb", {{"path", glb}}).ok);
    std::vector<MeshObject> objects;
    std::string err;
    CHECK(loadGltf(glb, objects, err) && objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].mesh.triangleCount() == objects[1].mesh.triangleCount());
        CHECK(near(objects[1].mesh.bounds().center(), objects[0].mesh.bounds().center() + glm::vec3(3, 0, 0), 1e-3f));
    }
    const std::string bin = readText(glb);
    CHECK(countOf(bin, "\"primitives\"") == 1 && bin.find("\"matrix\"") != std::string::npos);

    // USD: the copy is an instanceable prim referencing the first; SculptForge reads both back.
    const std::string usda = tempPath("sf_linked.usda");
    CHECK(reg.execute(doc, "export_usd", {{"path", usda}}).ok);
    const std::string text = readText(usda);
    CHECK(countOf(text, "def Mesh ") == 1);
    CHECK(text.find("instanceable = true") != std::string::npos);
    CHECK(text.find("prepend references = </Linked/Body>") != std::string::npos);
    CHECK(text.find("xformOp:transform") != std::string::npos);
    CHECK(countOf(text, "{") == countOf(text, "}"));
    SplatCloud splats;
    objects.clear();
    CHECK(loadUsd(usda, objects, splats, err) && objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].name == "Body" && objects[1].name == "Body_link");
        CHECK(near(objects[1].mesh.bounds().center(), objects[0].mesh.bounds().center() + glm::vec3(3, 0, 0), 1e-3f));
    }

    // OBJ (no instancing): the copy written where it is.
    const std::string obj = tempPath("sf_linked.obj");
    CHECK(reg.execute(doc, "save_obj", {{"path", obj}}).ok);
    Mesh merged;
    CHECK(loadObj(obj, merged, err));
    CHECK(merged.triangleCount() == 2 * doc.mesh.triangleCount());
    CHECK(merged.bounds().max.x > doc.partBounds(1).max.x - 1e-3f);

    // Merge Visible: the result is where the Parts were, not moved.
    CHECK(reg.execute(doc, "part_merge_visible").ok);
    CHECK(doc.partCount() == 1 && !doc.activePartInfo().moved());
    CHECK(doc.mesh.bounds().max.x > 3.9f);
    for (const std::string& p : {glb, usda, obj}) std::filesystem::remove(p);
}

TEST(camera_in_space_of_part) {
    Camera cam;
    cam.target = glm::vec3(1, 2, 3);
    cam.distance = 7.0f;
    cam.yaw = 0.4f;
    cam.pitch = -0.3f;
    cam.aspect = 1.5f;
    // A linked copy's place: turned, moved and scaled evenly.
    const glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(3, -1, 2)) *
                            glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(glm::vec3(1, 2, 0.5f))) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));
    const Camera local = cam.inSpaceOf(place);
    const glm::mat4 inv = glm::inverse(place);
    for (float x : {10.0f, 300.0f, 590.0f})
        for (float y : {20.0f, 200.0f, 390.0f}) {
            glm::vec3 o, d, lo, ld;
            cam.screenRay(x, y, 600.0f, 400.0f, o, d);
            local.screenRay(x, y, 600.0f, 400.0f, lo, ld);
            // The same ray, seen from the Part: the local ray's points map to the world ray's line.
            const glm::vec3 wd = glm::normalize(glm::mat3(inv) * d);
            CHECK(near(ld, wd, 1e-4f));
            const glm::vec3 wo = glm::vec3(inv * glm::vec4(o, 1.0f));
            const glm::vec3 off = lo - wo;
            CHECK(glm::length(off - wd * glm::dot(off, wd)) < 1e-3f);
        }
    // A pixel covers 1/scale as many of the Part's units; projecting a local point lands where the world one does.
    const glm::vec3 p(0.2f, 0.3f, -0.1f);
    const glm::vec3 wp = glm::vec3(place * glm::vec4(p, 1.0f));
    CHECK(std::abs(local.worldPerPixel(p, 400.0f) * 1.5f - cam.worldPerPixel(wp, 400.0f)) < 1e-4f);
    const glm::vec4 a = cam.projection() * cam.view() * glm::vec4(wp, 1.0f);
    const glm::vec4 b = local.projection() * local.view() * glm::vec4(p, 1.0f);
    CHECK(near(glm::vec3(a) / a.w, glm::vec3(b) / b.w, 1e-4f));
    CHECK(cam.inSpaceOf(glm::mat4(1.0f)).view() == cam.view());
}
