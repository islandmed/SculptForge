// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// 0.10.6 inbox: the Smooth brush on a Quad Remesh result (#69) and Keep Masked remeshing (#67).
//
// #69: smoothing averaged every vertex joined by a triangle edge, quad diagonals included. A vertex at one end of a
// quad's diagonal (and not the other) was pulled along it, so smoothing sheared a quad mesh into zigzags. Smooth now
// averages the polygon ring (Mesh::polygonNeighbors).
// #67: voxel remeshing leaves spots where the surface meets itself (where parts of a model touch: a lion's mane),
// and the stitch refused any such surface ("Couldn't cut the remeshed surface cleanly"), at every resolution.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/KeepMasked.h"
#include "core/Primitives.h"
#include "core/QRemesh.h"
#include "core/Voxel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <tuple>

using namespace sf;

namespace {

std::string repoPath(const char* rel) {
    std::string root = __FILE__;
    return root.substr(0, root.rfind("tests")) + rel;
}

size_t coincidentVertices(const Mesh& m) {
    std::map<std::tuple<float, float, float>, int> seen;
    size_t dup = 0;
    for (const auto& p : m.positions) dup += seen[{p.x, p.y, p.z}]++ > 0;
    return dup;
}

// Smooth strokes across the front, mirrored in X, as a user evens out a remeshed form.
void smoothAcross(CommandRegistry& reg, Document& doc, float radius) {
    const AABB b = doc.mesh.bounds();
    const glm::vec3 c = (b.min + b.max) * 0.5f, e = b.max - b.min;
    for (int i = 0; i < 30; ++i) {
        const float y = c.y + e.y * (-0.4f + 0.8f * float(i % 10) / 9.0f);
        char pts[160];
        std::snprintf(pts, sizeof pts, "%.4f,%.4f,%.4f %.4f,%.4f,%.4f", c.x - 0.45f * e.x, y, b.max.z + e.z, c.x + 0.45f * e.x,
                      y + 0.02f * e.y * float(i / 10), b.max.z + e.z);
        CHECK(reg.execute(doc, "stroke", {{"points", std::string(pts)}, {"brush", std::string("Smooth")}, {"radius", double(radius)},
                                          {"strength", 1.0}, {"symmetry", std::string("x")}})
                  .ok);
    }
}

// Quad Remesh, then smoothing: the quads keep their shape (no shear), and the surface stays closed and welded.
// `slack`: how much the mean corner angle deviation may grow (curved, uneven remeshes change a little as they smooth).
void checkSmoothKeepsQuads(Document& doc, double target, float radius, const char* name, float slack, float maxSlackDeg) {
    CommandRegistry reg;
    CHECK(reg.execute(doc, "qremesh", {{"target", target}, {"symmetry", std::string("x")}, {"quality", false}}).ok);
    const QuadQuality before = quadQuality(doc.mesh);
    CHECK(topologyReport(doc.mesh).boundaryEdges == 0);
    CHECK(coincidentVertices(doc.mesh) == 0); // (the remesh is welded: not the cause)
    smoothAcross(reg, doc, radius);
    const QuadQuality after = quadQuality(doc.mesh);
    const TopologyReport r = topologyReport(doc.mesh);
    std::printf("  %s: corner angle deviation %.2f -> %.2f deg (99th percentile %.2f -> %.2f)\n", name, before.angleDeviation,
                after.angleDeviation, before.angleDeviationMax, after.angleDeviationMax);
    // Before the fix: sphere 3.9 -> 8.8 deg (99th percentile 15 -> 41), lion 8.7 -> 19.5 deg (37 -> 55).
    CHECK(after.angleDeviation <= before.angleDeviation * slack);
    CHECK(after.angleDeviationMax <= before.angleDeviationMax + maxSlackDeg);
    CHECK(r.boundaryEdges == 0 && r.manifold());
}

} // namespace

TEST(inbox0106_polygon_neighbors_skip_quad_diagonals) {
    // A 3 x 3 grid of quads, each split along a diagonal running the same way: the middle vertex's triangle ring
    // has 6 vertices (4 edges + 2 diagonals), its polygon ring the 4 edge neighbours.
    Mesh m;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) m.positions.emplace_back(float(x), float(y), 0.0f);
    auto id = [](int x, int y) { return uint32_t(y * 4 + x); };
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) {
            const uint32_t t = uint32_t(m.triangles.size());
            m.triangles.emplace_back(id(x, y), id(x + 1, y), id(x + 1, y + 1));
            m.triangles.emplace_back(id(x, y), id(x + 1, y + 1), id(x, y + 1));
            m.pairQuad(t, t + 1);
        }
    m.rebuildTopology();
    const uint32_t v = id(1, 1);
    CHECK(m.neighbors(v).size() == 6);
    const IndexRing ring = m.polygonNeighbors(v);
    CHECK(ring.size() == 4);
    for (uint32_t w : ring) CHECK(w == id(0, 1) || w == id(2, 1) || w == id(1, 0) || w == id(1, 2));
    // Without quads, the triangle ring.
    m.clearQuads();
    CHECK(m.polygonNeighbors(v).size() == 6);
}

TEST(inbox0106_smooth_keeps_quad_remesh_regular_sphere) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}}).ok);
    checkSmoothKeepsQuads(doc, 3000.0, 0.25f, "sphere", 1.05f, 1.0f);
}

TEST(inbox0106_smooth_keeps_quad_remesh_regular_lion) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "open", {{"path", repoPath("assets/demo/lion-head.glb")}, {"subdivisions", 0.0}}).ok);
    const float size = glm::length(doc.mesh.bounds().extent());
    checkSmoothKeepsQuads(doc, 3000.0, 0.08f * size, "lion", 1.25f, 6.0f);
}

namespace {

// The lion head masked by `masked` (in its box, -0.5..0.5), voxel remeshed with Keep masked.
CommandResult lionKeepMasked(const std::function<bool(const glm::vec3&)>& masked, double resolution, Document& doc) {
    CommandRegistry reg;
    CHECK(reg.execute(doc, "open", {{"path", repoPath("assets/demo/lion-head.glb")}, {"subdivisions", 0.0}}).ok);
    const AABB b = doc.mesh.bounds();
    const glm::vec3 c = (b.min + b.max) * 0.5f, e = b.max - b.min;
    auto& mask = doc.mesh.ensureMask();
    for (size_t v = 0; v < mask.size(); ++v) mask[v] = masked((doc.mesh.positions[v] - c) / e) ? 1.0f : 0.0f;
    return reg.execute(doc, "remesh", {{"resolution", resolution}, {"keep_masked", true}, {"keep_texture", false}});
}

} // namespace

TEST(inbox0106_keep_masked_voxel_on_a_real_model) {
    // Before the fix every one of these failed with "Couldn't cut the remeshed surface cleanly" (the plain voxel
    // remesh of the lion meets itself on 150-200 edges in the mane at any resolution).
    const std::function<bool(const glm::vec3&)> masks[] = {
        [](const glm::vec3& q) { return q.y > 0.1f; },                            // the top
        [](const glm::vec3& q) { return q.z > 0.2f; },                            // the face
        [](const glm::vec3& q) { return glm::length(glm::vec2(q.x, q.y)) < 0.2f; }, // a disc right through
        [](const glm::vec3& q) { return q.z > 0.0f && std::abs(q.x) < 0.15f; },   // a stripe down the front
    };
    for (const auto& masked : masks)
        for (double res : {128.0, 300.0}) {
            Document doc;
            const CommandResult r = lionKeepMasked(masked, res, doc);
            if (!r.ok) std::printf("  res %.0f: %s\n", res, r.message.c_str());
            CHECK(r.ok);
            if (!r.ok) continue;
            const TopologyReport t = topologyReport(doc.mesh);
            CHECK(t.boundaryEdges == 0);
            // Every kept triangle is still there with its mask.
            size_t kept = 0;
            for (uint8_t k : keepMaskedTriangles(doc.mesh)) kept += k;
            CHECK(kept > 1000);
        }
}

TEST(inbox0106_keep_masked_tidies_awkward_masks) {
    // A mask whose edge touches itself (stripes meeting at corners) and two masked spots so close that one cut
    // opened round both: tidied (the corner triangles remeshed, the gap kept) and joined, saying so.
    struct Case {
        const char* name;
        std::function<bool(const glm::vec3&)> masked;
    } cases[] = {
        {"stripes", [](const glm::vec3& p) { return std::sin(p.x * 25.0f) > 0.0f && p.z > 0.0f; }},
        {"close spots", [](const glm::vec3& p) {
             return glm::distance(p, glm::vec3(0.0f, 0.2f, 1.0f)) < 0.18f || glm::distance(p, glm::vec3(0.0f, -0.2f, 1.0f)) < 0.18f;
         }},
    };
    for (const Case& c : cases) {
        Document doc;
        CommandRegistry reg;
        CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}}).ok);
        auto& mask = doc.mesh.ensureMask();
        for (size_t v = 0; v < mask.size(); ++v) mask[v] = c.masked(doc.mesh.positions[v]) ? 1.0f : 0.0f;
        const std::vector<uint8_t> keep = keepMaskedTriangles(doc.mesh);
        const Mesh remeshed = voxelRemesh(doc.mesh, 128);
        Mesh out;
        std::string err, note;
        const bool ok = stitchKeptTriangles(doc.mesh, keep, remeshed, out, err, &note);
        std::printf("  %s: %s\n", c.name, ok ? note.c_str() : err.c_str());
        CHECK(ok);
        CHECK(!note.empty());
        const TopologyReport t = topologyReport(out);
        CHECK(t.boundaryEdges == 0 && t.manifold());
        // Through the command, the note is in the message.
        CommandResult r = reg.execute(doc, "remesh", {{"resolution", 128.0}, {"keep_masked", true}, {"keep_texture", false}});
        CHECK(r.ok && r.message.find("Keep masked:") != std::string::npos);
    }
}

TEST(inbox0106_keep_masked_says_what_to_change) {
    // A mask that runs into the inside of another part (two overlapping spheres as one Part, masked across both):
    // there's nothing to join it to, and the message says what to change.
    Mesh a = makeIcosphere(5), m = a;
    const uint32_t base = uint32_t(m.positions.size());
    for (const auto& p : a.positions) m.positions.push_back(p * 0.8f + glm::vec3(1.2f, 0.0f, 0.0f));
    for (const auto& t : a.triangles) m.triangles.push_back(t + glm::uvec3(base));
    m.quadPartners.clear();
    m.rebuildTopology();
    m.recomputeAllNormals();
    auto& mask = m.ensureMask();
    for (size_t v = 0; v < mask.size(); ++v) mask[v] = m.positions[v].y > 0.3f ? 1.0f : 0.0f;
    Mesh out;
    std::string err;
    CHECK(!stitchKeptTriangles(m, keepMaskedTriangles(m), voxelRemesh(m, 128), out, err));
    std::printf("  %s\n", err.c_str());
    CHECK(err.find("Tool > Masking > Grow") != std::string::npos);
    CHECK(out.triangleCount() == 0); // untouched
}
