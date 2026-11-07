// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// New primitives (#31) and insert_primitive (#34).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Parts.h"
#include "core/Primitives.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#include <algorithm>
#include <cmath>

using namespace sf;

TEST(primitives_closed_and_outward) {
    for (Mesh m : {makeCylinder(24), makeCone(24), makeTorus(32), makeCapsule(24), makeQuadSphere(8)}) {
        CHECK(m.vertexCount() > 0);
        double vol = 0.0;
        for (const auto& t : m.triangles)
            vol += glm::dot(glm::dvec3(m.positions[t[0]]), glm::cross(glm::dvec3(m.positions[t[1]]), glm::dvec3(m.positions[t[2]])));
        CHECK(vol > 0.0); // closed and facing outwards
        // Closed: every edge used exactly twice.
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        for (const auto& t : m.triangles)
            for (int e = 0; e < 3; ++e) {
                uint32_t a = t[e], b = t[(e + 1) % 3];
                if (a > b) std::swap(a, b);
                ++edges[{a, b}];
            }
        bool closed = true;
        for (const auto& [k, n] : edges) closed = closed && n == 2;
        CHECK(closed);
    }
}

TEST(insert_primitive_on_surface) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    CommandResult r = reg.execute(doc, "insert_primitive", {{"type", std::string("cylinder")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0},
                                                             {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.3}});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() > before);
    // The inserted part's axis follows the normal: it reaches out along +X.
    float maxX = 0.0f;
    for (const auto& p : doc.mesh.positions) maxX = std::max(maxX, p.x);
    CHECK(maxX > 1.1f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.triangleCount() == before);
    CHECK(!reg.execute(doc, "insert_primitive", {{"type", std::string("teapot")}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}).ok);
}

TEST(insert_primitive_mirrors_with_symmetry) {
    // #43: with symmetry on, Insert adds a mirrored copy across each plane, as one undo step.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    const Args at = {{"type", std::string("cone")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0},
                     {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.3}, {"twist", 30.0}};
    CHECK(reg.execute(doc, "insert_primitive", at).ok);
    const size_t one = doc.mesh.triangleCount() - before;
    CHECK(reg.execute(doc, "undo", {}).ok);

    Args mirrored = at;
    mirrored["symmetry"] = std::string("x");
    CHECK(reg.execute(doc, "insert_primitive", mirrored).ok);
    CHECK(doc.mesh.triangleCount() == before + 2 * one);
    // The copy reaches out along -X, and is not inside out: the outermost point's normal faces away.
    size_t minI = 0, maxI = 0;
    for (size_t i = 0; i < doc.mesh.positions.size(); ++i) {
        if (doc.mesh.positions[i].x < doc.mesh.positions[minI].x) minI = i;
        if (doc.mesh.positions[i].x > doc.mesh.positions[maxI].x) maxI = i;
    }
    CHECK(doc.mesh.positions[minI].x < -1.1f);
    CHECK(std::abs(doc.mesh.positions[minI].x + doc.mesh.positions[maxI].x) < 1e-3f);
    CHECK(doc.mesh.normals[minI].x < 0.0f);
    CHECK(doc.mesh.normals[maxI].x > 0.0f);
    CHECK(reg.execute(doc, "undo", {}).ok); // one step takes both away
    CHECK(doc.mesh.triangleCount() == before);

    // Z symmetry on a point on the Z plane would copy it onto itself: only the X copy is added.
    mirrored["symmetry"] = std::string("xz");
    CHECK(reg.execute(doc, "insert_primitive", mirrored).ok);
    CHECK(doc.mesh.triangleCount() == before + 2 * one);
    CHECK(reg.execute(doc, "undo", {}).ok);
    mirrored["symmetry"] = std::string("q");
    CHECK(!reg.execute(doc, "insert_primitive", mirrored).ok);
}

// insert_mesh (#34): your own mesh, placed like a primitive.
namespace {
size_t groupTriangles(const Mesh& m, uint32_t g) {
    size_t n = 0;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) n += m.facegroupAt(t) == g;
    return n;
}
uint32_t topGroup(const Mesh& m) {
    uint32_t g = 0;
    for (uint32_t x : m.facegroups) g = std::max(g, x);
    return g;
}
} // namespace

TEST(insert_mesh_from_part) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    // A tall, off-centre box Part to insert: it's centred and scaled so its largest side is `size`.
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}, {"name", std::string("Stud")}}).ok);
    for (auto& p : doc.mesh.positions) p = glm::vec3(p.x, p.y * 4.0f, p.z) + glm::vec3(5.0f, 3.0f, 0.0f);
    const size_t studTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "part_select", {{"index", 0.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    CommandResult r = reg.execute(doc, "insert_mesh", {{"part", std::string("Stud")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0},
                                                        {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.4}});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() == before + studTris);
    CHECK(doc.partCount() == 2); // the source Part stays
    // Its long (Y) axis lies along the normal (+X): it reaches 0.2 out from the surface point, 0.05 to the sides.
    const uint32_t g = topGroup(doc.mesh);
    CHECK(groupTriangles(doc.mesh, g) == studTris);
    AABB box;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t)
        if (doc.mesh.facegroupAt(t) == g)
            for (int k = 0; k < 3; ++k) box.expand(doc.mesh.positions[doc.mesh.triangles[t][k]]);
    CHECK(std::abs(box.max.x - 1.2f) < 0.01f);
    CHECK(std::abs(box.min.x - 0.8f) < 0.01f);
    CHECK(std::abs(box.max.y - 0.05f) < 0.01f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.triangleCount() == before);
    // Symmetry makes a mirrored copy, as with the primitives.
    CHECK(reg.execute(doc, "insert_mesh", {{"part", std::string("Stud")}, {"symmetry", std::string("x")}, {"x", 1.0},
                                           {"y", 0.0}, {"z", 0.0}, {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.4}}).ok);
    CHECK(doc.mesh.triangleCount() == before + 2 * studTris);
    float minX = 0.0f;
    for (const auto& p : doc.mesh.positions) minX = std::min(minX, p.x);
    CHECK(minX < -1.1f);
    // Errors: nothing named, both named, an unknown Part.
    CHECK(!reg.execute(doc, "insert_mesh", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}).ok);
    CHECK(!reg.execute(doc, "insert_mesh", {{"part", std::string("Stud")}, {"file", std::string("a.obj")}, {"x", 0.0},
                                            {"y", 0.0}, {"z", 0.0}}).ok);
    CHECK(!reg.execute(doc, "insert_mesh", {{"part", std::string("Nope")}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}).ok);
}

TEST(insert_mesh_from_file) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    // A small tetrahedron written as OBJ text (quads aren't needed).
    const std::string obj = tempPath("sf_insert_mesh.obj");
    if (FILE* f = std::fopen(obj.c_str(), "w")) {
        std::fputs("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n", f);
        std::fclose(f);
    }
    CHECK(reg.execute(doc, "insert_mesh", {{"file", obj}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}, {"size", 0.3}}).ok);
    CHECK(doc.mesh.triangleCount() == before + 4);
    float maxY = 0.0f;
    for (const auto& p : doc.mesh.positions) maxY = std::max(maxY, p.y);
    CHECK(std::abs(maxY - 1.15f) < 0.01f);
    // The same mesh as PLY and GLB, through their exporters.
    Mesh tet;
    std::string err;
    CHECK(loadObj(obj, tet, err));
    const std::string ply = tempPath("sf_insert_mesh.ply"), glb = tempPath("sf_insert_mesh.glb");
    CHECK(savePlyMesh(ply, tet, err));
    CHECK(saveGlb(glb, tet, "tet", err));
    CHECK(reg.execute(doc, "insert_mesh", {{"file", ply}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}}).ok);
    CHECK(reg.execute(doc, "insert_mesh", {{"file", glb}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}}).ok);
    CHECK(doc.mesh.triangleCount() == before + 12);
    // Only OBJ, PLY and GLB; a missing file is an error and changes nothing.
    CHECK(!reg.execute(doc, "insert_mesh", {{"file", tempPath("sf_insert_mesh.stl")}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}}).ok);
    CHECK(!reg.execute(doc, "insert_mesh", {{"file", tempPath("sf_insert_mesh_missing.obj")}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}}).ok);
    CHECK(doc.mesh.triangleCount() == before + 12);
    for (const std::string& p : {obj, ply, glb}) std::remove(p.c_str());
}

// Insert sets: one item at a time from a multi-object file or a folder of files.
TEST(insert_mesh_from_set) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    const Mesh tet = [] {
        Mesh m;
        m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        m.triangles = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
        m.sanitizeAttributes();
        return m;
    }();
    const Mesh cube = makeCube(2, 1.0f);
    std::string err;
    const std::string glb = tempPath("sf_insert_set.glb");
    CHECK(saveGlb(glb, {{"Tet", &tet}, {"Box", &cube}}, "kit", err));
    std::vector<InsertSetItem> items;
    CHECK(listInsertSet(glb, items, err));
    CHECK(items.size() == 2 && items[0].name == "Tet" && items[1].name == "Box");
    const Args at{{"x", 0.0}, {"y", 1.0}, {"z", 0.0}, {"size", 0.3}};
    Args a = at;
    a["set"] = glb;
    CHECK(reg.execute(doc, "insert_mesh", a).ok);
    CHECK(doc.mesh.triangleCount() == before + 4);
    a["index"] = 1.0;
    CommandResult r = reg.execute(doc, "insert_mesh", a);
    CHECK(r.ok && r.message.find("Box") != std::string::npos);
    CHECK(doc.mesh.triangleCount() == before + 4 + cube.triangleCount());
    a["index"] = 2.0;
    CHECK(!reg.execute(doc, "insert_mesh", a).ok);
    CHECK(reg.execute(doc, "insert_set_items", {{"set", glb}}).message.find("1. Box") != std::string::npos);

    // A folder: its model files by name, not its other files or subfolders.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sf_insert_set_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    std::filesystem::create_directories(dir / "empty");
    CHECK(saveObj((dir / "b_box.obj").string(), cube, err));
    CHECK(saveGlb((dir / "a_tet.glb").string(), tet, "tet", err));
    CHECK(saveObj((dir / "sub" / "c.obj").string(), cube, err));
    if (FILE* f = std::fopen((dir / "notes.txt").string().c_str(), "w")) std::fclose(f);
    CHECK(listInsertSet(dir.string(), items, err));
    CHECK(items.size() == 2 && items[0].name == "a_tet" && items[1].name == "b_box");
    const size_t now = doc.mesh.triangleCount();
    a["set"] = dir.string();
    a["index"] = 0.0;
    CHECK(reg.execute(doc, "insert_mesh", a).ok);
    CHECK(doc.mesh.triangleCount() == now + 4);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.triangleCount() == now);
    // Not a set: a file type Insert doesn't read, an empty folder, a missing file.
    CHECK(!listInsertSet((dir / "notes.txt").string(), items, err));
    CHECK(!listInsertSet((dir / "empty").string(), items, err));
    CHECK(!listInsertSet((dir / "missing.glb").string(), items, err));
    std::filesystem::remove_all(dir);
    std::remove(glb.c_str());
}

// The built-in Creature bones set (#154): no file to pick, every piece inserts as quads, one undo step each.
TEST(insert_builtin_creature_bones) {
#if defined(_WIN32)
    const char* var = "APPDATA";
#else
    const char* var = "XDG_DATA_HOME";
#endif
    // the per-user data folder in a temp folder (the set is written there once per run)
    const std::filesystem::path data = std::filesystem::temp_directory_path() / "sf_builtin_sets";
    std::filesystem::remove_all(data);
    const char* had = std::getenv(var);
    const std::string old = had ? had : "";
    auto setVar = [&](const std::string& v) {
#if defined(_WIN32)
        _putenv_s(var, v.c_str());
#else
        if (v.empty() && !had) unsetenv(var);
        else setenv(var, v.c_str(), 1);
#endif
    };
    setVar(data.string());
    CHECK(builtinInsertSets().size() == 1 && std::string(builtinInsertSets()[0].id) == "creature-bones");
    const std::string set = std::string(kBuiltinInsertSetPrefix) + "creature-bones";
    std::vector<InsertSetItem> items;
    std::string err;
    CHECK(listInsertSet(set, items, err));
    const std::vector<std::string> names = {"Skull", "Jaw", "Fang", "Blade tooth", "Swept horn", "Spiral horn", "Claw",
                                            "Vertebra", "Tail vertebrae", "Rib", "Limb bone", "Wing bone",
                                            "Wing finger"};
    CHECK(items.size() == names.size());
    for (size_t i = 0; i < items.size() && i < names.size(); ++i) CHECK(items[i].name == names[i]);
    std::string file;
    CHECK(resolveInsertSetPath(set, file, err) && std::filesystem::exists(std::filesystem::path(std::u8string(file.begin(), file.end()))));
    CHECK(file.find("insert-sets") != std::string::npos);

    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    for (size_t i = 0; i < names.size(); ++i) {
        Mesh piece;
        std::string name;
        CHECK(loadInsertSetItem(set, int(i), piece, name, err) && name == names[i]);
        // clean starting pieces: closed, low-to-mid poly, nearly all quads
        size_t quads = 0;
        for (size_t t = 0; t < piece.triangles.size(); ++t)
            if (piece.hasQuads() && piece.quadPartners[t] != Mesh::kNoQuad) ++quads;
        CHECK(piece.triangleCount() >= 400 && piece.triangleCount() <= 12000);
        CHECK(quads >= piece.triangleCount() * 9 / 10);
        const CommandResult r = reg.execute(doc, "insert_mesh", {{"set", set}, {"index", double(i)}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}, {"size", 0.4}});
        CHECK(r.ok && r.message.find(names[i]) != std::string::npos);
        CHECK(doc.mesh.triangleCount() == before + piece.triangleCount());
        CHECK(reg.execute(doc, "undo", {}).ok);
        CHECK(doc.mesh.triangleCount() == before);
    }
    CHECK(reg.execute(doc, "insert_set_items", {{"set", set}}).message.find("12. Wing finger") != std::string::npos);
    CHECK(!listInsertSet(std::string(kBuiltinInsertSetPrefix) + "nothing", items, err));
    setVar(old);
}

// The Insert brush (@foirgairwolf): new_part=true makes each insert a Part of its own beside the active one, which
// stays active; placed and oriented as a merged insert, its mirrored copies in the same Part, one undo step.
TEST(insert_new_part_placement_orientation_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    const uint32_t sculptId = doc.activePartInfo().id;
    const AABB prim = makeCylinder(24, 0.4f).bounds(); // its axis is +Y
    // On the side facing -Z: the cylinder's axis turns onto the normal, its centre on the surface point.
    const Args at = {{"type", std::string("cylinder")}, {"x", 0.0}, {"y", 0.0}, {"z", -1.0},
                     {"nx", 0.0}, {"ny", 0.0}, {"nz", -1.0}, {"size", 0.4}, {"new_part", true}};
    const CommandResult r = reg.execute(doc, "insert_primitive", at);
    CHECK(r.ok && r.message.find("as Part Cylinder") != std::string::npos);
    CHECK(doc.partCount() == 2);
    CHECK(doc.activePartInfo().id == sculptId && doc.activePart == 0); // still sculpting the model
    CHECK(doc.mesh.triangleCount() == before);                         // the model is as it was
    CHECK(doc.parts[1].name == "Cylinder");
    const Mesh& piece = doc.partMesh(1);
    CHECK(piece.triangleCount() == makeCylinder(24, 0.4f).triangleCount());
    const AABB box = piece.bounds();
    const glm::vec3 c = box.center(), e = box.extent(), pe = prim.extent();
    CHECK(glm::length(c - glm::vec3(0.0f, 0.0f, -1.0f)) < 1e-3f);
    CHECK(std::abs(e.z - pe.y) < 1e-3f);                                       // its length along the normal
    CHECK(std::abs(e.x - pe.x) < 1e-3f && std::abs(e.y - pe.z) < 1e-3f);       // its round sides across it
    // One undo step takes the Part out; redo puts it back where it was.
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() == before);
    CHECK(reg.execute(doc, "redo", {}).ok);
    CHECK(doc.partCount() == 2 && doc.parts[1].name == "Cylinder" && doc.activePart == 0);
    CHECK(reg.execute(doc, "undo", {}).ok);

    // Mirrored with X symmetry: both copies in the one new Part, one step; the model untouched.
    Args mirrored = {{"type", std::string("cone")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"nx", 1.0}, {"ny", 0.0},
                     {"nz", 0.0}, {"size", 0.3}, {"twist", 30.0}, {"symmetry", std::string("x")}, {"new_part", true}};
    CHECK(reg.execute(doc, "insert_primitive", mirrored).ok);
    CHECK(doc.partCount() == 2 && doc.mesh.triangleCount() == before);
    const Mesh& pair = doc.partMesh(1);
    CHECK(pair.triangleCount() == 2 * makeCone(24, 0.3f).triangleCount());
    CHECK(pair.bounds().min.x < -1.1f && pair.bounds().max.x > 1.1f);
    CHECK(std::abs(pair.bounds().min.x + pair.bounds().max.x) < 1e-3f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.partCount() == 1);

    // Two in a row: two Parts, uniquely named, each its own undo step; the new Part stays in the model's folder.
    CHECK(reg.execute(doc, "part_folder_new", {{"folder", std::string("Body")}}).ok);
    const uint32_t folder = doc.activePartInfo().folder;
    Args sphere = {{"type", std::string("sphere")}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0}, {"size", 0.2}, {"new_part", true}};
    CHECK(reg.execute(doc, "insert_primitive", sphere).ok);
    CHECK(reg.execute(doc, "insert_primitive", sphere).ok);
    CHECK(doc.partCount() == 3 && doc.findPart("Sphere") >= 0 && doc.findPart("Sphere 2") >= 0);
    CHECK(doc.parts[1].folder == folder && doc.parts[2].folder == folder);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.partCount() == 2);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() == before);

    // Without new_part it joins the active Part as before (the brush's New Part off).
    sphere["new_part"] = false;
    CHECK(reg.execute(doc, "insert_primitive", sphere).ok);
    CHECK(doc.partCount() == 1 && doc.mesh.triangleCount() > before);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.triangleCount() == before);
}

TEST(insert_mesh_new_part) {
    // insert_mesh takes new_part too: a copy of another Part, named after it, normalised to `size`.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cube")}, {"name", std::string("Stud")}}).ok);
    CHECK(reg.execute(doc, "part_select", {{"index", 0.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "insert_mesh", {{"part", std::string("Stud")}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0},
                                           {"size", 0.25}, {"new_part", true}}).ok);
    CHECK(doc.partCount() == 3 && doc.activePart == 0 && doc.mesh.triangleCount() == before);
    const int i = doc.findPart("Stud 2");
    CHECK(i == 1); // right below the active Part
    const glm::vec3 e = doc.partMesh(i).bounds().extent();
    CHECK(std::abs(std::max({e.x, e.y, e.z}) - 0.25f) < 1e-3f);
    CHECK(glm::length(doc.partMesh(i).bounds().center() - glm::vec3(0, 1, 0)) < 1e-3f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.partCount() == 2 && doc.findPart("Stud 2") < 0);
}

TEST(insert_mask_rest) {
    // @foirgairwolf: inserting into the same Part with mask_rest=true masks everything that was there and leaves the
    // new piece free, ready to sculpt it in; one undo step brings back the mesh and its mask as they were.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    const size_t verts = doc.mesh.vertexCount();
    CHECK(!doc.mesh.hasMask());
    const CommandResult r = reg.execute(doc, "insert_mesh", {{"set", std::string("builtin:creature-bones")}, {"index", 6.0},
                                                             {"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"nx", 1.0}, {"ny", 0.0},
                                                             {"nz", 0.0}, {"size", 0.4}, {"mask_rest", true}});
    CHECK(r.ok && r.message.find("the rest masked") != std::string::npos);
    CHECK(doc.partCount() == 1 && doc.mesh.vertexCount() > verts && doc.mesh.hasMask());
    bool restMasked = true, pieceFree = true;
    for (size_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        if (v < verts) restMasked = restMasked && doc.mesh.mask[v] == 1.0f;
        else pieceFree = pieceFree && doc.mesh.mask[v] == 0.0f;
    }
    CHECK(restMasked && pieceFree);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.vertexCount() == verts && !doc.mesh.hasMask());
    // Ignored for a new Part (nothing of the model changes).
    CHECK(reg.execute(doc, "insert_primitive", {{"type", std::string("cube")}, {"x", 0.0}, {"y", 1.0}, {"z", 0.0},
                                                {"new_part", true}, {"mask_rest", true}}).ok);
    CHECK(doc.partCount() == 2 && !doc.mesh.hasMask());
}
