// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// New primitives (#31) and insert_primitive (#34).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Primitives.h"

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
