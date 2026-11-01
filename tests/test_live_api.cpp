// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// mesh_read / mesh_set_positions (#36): a round trip, undo, and the topology guard.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/LiveApi.h"
#include "core/Primitives.h"

using namespace sf;

TEST(live_api_round_trip) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CommandResult r = reg.execute(doc, "mesh_read", {});
    CHECK(r.ok);
    Json j;
    std::string err;
    CHECK(Json::parse(r.message, j, err));
    CHECK(size_t(j["vertices"].asNumber()) == doc.mesh.vertexCount());
    const std::string topo = j["topology"].asString();
    const std::vector<glm::vec3> before = doc.mesh.positions;
    // Re-send the same positions scaled up 10%: a coordinate-only update, one undo step.
    std::vector<glm::vec3> scaled = before;
    for (auto& p : scaled) p *= 1.1f;
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(scaled.data());
    const size_t size = scaled.size() * sizeof(glm::vec3);
    std::string b64;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t v = uint32_t(bytes[i]) << 16 | (i + 1 < size ? uint32_t(bytes[i + 1]) << 8 : 0u) |
                           (i + 2 < size ? uint32_t(bytes[i + 2]) : 0u);
        b64 += kB64[(v >> 18) & 63];
        b64 += kB64[(v >> 12) & 63];
        b64 += i + 1 < size ? kB64[(v >> 6) & 63] : '=';
        b64 += i + 2 < size ? kB64[v & 63] : '=';
    }
    const size_t tris = doc.mesh.triangleCount();
    r = reg.execute(doc, "mesh_set_positions", {{"positions", b64}, {"topology", topo}});
    CHECK(r.ok);
    CHECK(doc.mesh.triangleCount() == tris);
    CHECK(glm::length(doc.mesh.positions[7] - before[7] * 1.1f) < 1e-5f);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.positions[7] == before[7]);
    // A stale topology hash is refused.
    r = reg.execute(doc, "mesh_set_positions", {{"positions", b64}, {"topology", std::string("123")}});
    CHECK(!r.ok);
    // A wrong vertex count is refused.
    r = reg.execute(doc, "mesh_set_positions", {{"positions", std::string("AAAA")}, {"topology", topo}});
    CHECK(!r.ok);
}

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
