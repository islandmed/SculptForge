// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// New primitives (#31) and insert_primitive (#34).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Primitives.h"

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
