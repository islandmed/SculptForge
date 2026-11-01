// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// New primitives (#31) and insert_primitive (#34).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Primitives.h"
#include "io/GltfIO.h"
#include "io/ObjIO.h"
#include "io/PlyIO.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    for (uint32_t t = 0; t < m.triangleCount(); ++t) n += m.polygroupAt(t) == g;
    return n;
}
uint32_t topGroup(const Mesh& m) {
    uint32_t g = 0;
    for (uint32_t x : m.polygroups) g = std::max(g, x);
    return g;
}
} // namespace

TEST(insert_mesh_from_subtool) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 3.0}}).ok);
    // A tall, off-centre box SubTool to insert: it's centred and scaled so its largest side is `size`.
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}, {"name", std::string("Stud")}}).ok);
    for (auto& p : doc.mesh.positions) p = glm::vec3(p.x, p.y * 4.0f, p.z) + glm::vec3(5.0f, 3.0f, 0.0f);
    const size_t studTris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "subtool_select", {{"index", 0.0}}).ok);
    const size_t before = doc.mesh.triangleCount();
    CommandResult r = reg.execute(doc, "insert_mesh", {{"subtool", std::string("Stud")}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0},
                                                        {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.4}});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() == before + studTris);
    CHECK(doc.subtoolCount() == 2); // the source SubTool stays
    // Its long (Y) axis lies along the normal (+X): it reaches 0.2 out from the surface point, 0.05 to the sides.
    const uint32_t g = topGroup(doc.mesh);
    CHECK(groupTriangles(doc.mesh, g) == studTris);
    AABB box;
    for (uint32_t t = 0; t < doc.mesh.triangleCount(); ++t)
        if (doc.mesh.polygroupAt(t) == g)
            for (int k = 0; k < 3; ++k) box.expand(doc.mesh.positions[doc.mesh.triangles[t][k]]);
    CHECK(std::abs(box.max.x - 1.2f) < 0.01f);
    CHECK(std::abs(box.min.x - 0.8f) < 0.01f);
    CHECK(std::abs(box.max.y - 0.05f) < 0.01f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.triangleCount() == before);
    // Symmetry makes a mirrored copy, as with the primitives.
    CHECK(reg.execute(doc, "insert_mesh", {{"subtool", std::string("Stud")}, {"symmetry", std::string("x")}, {"x", 1.0},
                                           {"y", 0.0}, {"z", 0.0}, {"nx", 1.0}, {"ny", 0.0}, {"nz", 0.0}, {"size", 0.4}}).ok);
    CHECK(doc.mesh.triangleCount() == before + 2 * studTris);
    float minX = 0.0f;
    for (const auto& p : doc.mesh.positions) minX = std::min(minX, p.x);
    CHECK(minX < -1.1f);
    // Errors: nothing named, both named, an unknown SubTool.
    CHECK(!reg.execute(doc, "insert_mesh", {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}}).ok);
    CHECK(!reg.execute(doc, "insert_mesh", {{"subtool", std::string("Stud")}, {"file", std::string("a.obj")}, {"x", 0.0},
                                            {"y", 0.0}, {"z", 0.0}}).ok);
    CHECK(!reg.execute(doc, "insert_mesh", {{"subtool", std::string("Nope")}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}}).ok);
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
