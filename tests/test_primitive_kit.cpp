// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
// Parametric primitives (core/PrimitiveKit.h) and Solidify (core/Solidify.h).
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/ExtractMirror.h"
#include "core/PrimitiveKit.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Solidify.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace sf;

namespace {

double signedVolume(const Mesh& m) {
    double vol = 0.0;
    for (const auto& t : m.triangles)
        vol += glm::dot(glm::dvec3(m.positions[t[0]]), glm::cross(glm::dvec3(m.positions[t[1]]), glm::dvec3(m.positions[t[2]]))) / 6.0;
    return vol;
}

struct EdgeStats {
    size_t open = 0, nonManifold = 0;
};
EdgeStats edgeStats(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const auto& t : m.triangles)
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            ++edges[{a, b}];
        }
    EdgeStats s;
    for (const auto& [k, n] : edges) {
        if (n == 1) ++s.open;
        if (n > 2) ++s.nonManifold;
    }
    return s;
}

// Every quad pair points both ways and shares an edge; no degenerate triangle.
bool wellFormed(const Mesh& m) {
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        const glm::uvec3& c = m.triangles[t];
        if (c.x == c.y || c.y == c.z || c.x == c.z) return false;
        const uint32_t u = m.quadPartnerAt(t);
        if (u == Mesh::kNoQuad) continue;
        if (m.quadPartnerAt(u) != t) return false;
        int shared = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) shared += c[i] == m.triangles[u][j];
        if (shared != 2) return false;
    }
    return m.normals.size() == m.positions.size();
}

float maxRadiusXZ(const Mesh& m) {
    float r = 0.0f;
    for (const auto& p : m.positions) r = std::max(r, std::sqrt(p.x * p.x + p.z * p.z));
    return r;
}

PrimitiveSpec spec(const char* type, int segments, int rings, int caps, float size = 2.0f) {
    PrimitiveSpec s = primitiveDefaults(type, true);
    s.segments = segments;
    s.rings = rings;
    s.caps = caps;
    s.quadCaps = false; // (the low-poly preset has quad caps; these counts are for fans)
    s.size = size;
    return s;
}

} // namespace

TEST(primitive_kit_closed_quads_outward) {
    for (const char* type : {"cylinder", "cone", "torus", "capsule", "pipe", "cube", "quadsphere", "sphere", "icosphere"})
        for (bool low : {true, false}) {
            PrimitiveSpec s = primitiveDefaults(type, low);
            s.size = 2.0f;
            const Mesh m = makePrimitive(s);
            CHECK(m.vertexCount() > 0);
            CHECK(signedVolume(m) > 0.0);
            const EdgeStats e = edgeStats(m);
            CHECK(e.open == 0);
            CHECK(e.nonManifold == 0);
            CHECK(wellFormed(m));
            const glm::vec3 ext = m.bounds().extent();
            CHECK(std::max({ext.x, ext.y, ext.z}) <= 2.0f * 1.001f);
        }
    // A plane is the one open primitive.
    const Mesh plane = makePrimitive(primitiveDefaults("plane", true));
    CHECK(edgeStats(plane).open == 16);
    CHECK(wellFormed(plane));
}

TEST(primitive_kit_counts_cylinder_caps) {
    // Fan caps: segments * rings quads on the side, a fan of segments triangles per cap.
    Mesh m = makePrimitive(spec("cylinder", 12, 1, 1));
    CHECK(m.vertexCount() == 26);
    CHECK(m.quadCount() == 12);
    CHECK(m.triangleCount() == 48);
    CHECK(edgeStats(m).open == 0);
    // Three rings along the side.
    m = makePrimitive(spec("cylinder", 12, 3, 1));
    CHECK(m.quadCount() == 36);
    CHECK(m.vertexCount() == 12 * 4 + 2);
    // Open: no caps, 24 open edges (12 per end).
    m = makePrimitive(spec("cylinder", 12, 1, 0));
    CHECK(m.vertexCount() == 24);
    CHECK(m.triangleCount() == 24);
    CHECK(m.quadCount() == 12);
    CHECK(edgeStats(m).open == 24);
    // Cap rings: 4 means 3 rings of quads then a fan.
    m = makePrimitive(spec("cylinder", 12, 1, 4));
    CHECK(m.quadCount() == 12 + 2 * 3 * 12);
    CHECK(m.triangleCount() == 2 * m.quadCount() + 24);
    CHECK(edgeStats(m).open == 0);
    CHECK(signedVolume(m) > 0.0);
    // Quad caps: all quads, no pole; k = 3 cells a side, (k-1)^2 inner points per cap.
    PrimitiveSpec q = spec("cylinder", 12, 1, 1);
    q.quadCaps = true;
    m = makePrimitive(q);
    CHECK(m.triangleCount() == 2 * m.quadCount());
    CHECK(m.vertexCount() == 24 + 2 * 4);
    CHECK(m.quadCount() == 12 + 2 * 9);
    CHECK(edgeStats(m).open == 0);
    CHECK(edgeStats(m).nonManifold == 0);
    CHECK(signedVolume(m) > 0.0);
    CHECK(wellFormed(m));
    // Quad caps round the segments to a multiple of 4.
    q.segments = 13;
    m = makePrimitive(q);
    CHECK(m.triangleCount() == 2 * m.quadCount());
    CHECK(edgeStats(m).open == 0);
}

TEST(primitive_kit_counts_others) {
    // Cone to a point: the side is a fan too.
    Mesh m = makePrimitive(spec("cone", 12, 1, 1));
    CHECK(m.vertexCount() == 14);
    CHECK(m.triangleCount() == 24);
    CHECK(m.quadCount() == 0);
    CHECK(signedVolume(m) > 0.0);
    // A cone with a top radius is a frustum: quads on the side, two caps.
    PrimitiveSpec frustum = spec("cone", 12, 2, 1);
    frustum.radius2 = 0.4f;
    m = makePrimitive(frustum);
    CHECK(m.quadCount() == 24);
    CHECK(edgeStats(m).open == 0);
    CHECK(signedVolume(m) > 0.0);
    // Pipe: outer, inner and two end rings, all quads.
    m = makePrimitive(spec("pipe", 12, 1, 1));
    CHECK(m.vertexCount() == 48);
    CHECK(m.quadCount() == 48);
    CHECK(m.triangleCount() == 96);
    CHECK(edgeStats(m).open == 0);
    const PrimitiveSpec p = primitiveDefaults("pipe", true);
    const double r = p.radius, r2 = p.radius2; // of size / 2 = 1; height = size = 2
    const double expected = 6.0 * std::sin(2.0 * 3.14159265358979 / 12.0) * (r * r - r2 * r2) * 2.0; // 12-gon ring area x height
    CHECK(std::abs(signedVolume(m) - expected) < 1e-3);
    // Torus: segments x rings quads.
    m = makePrimitive(spec("torus", 16, 8, 1));
    CHECK(m.vertexCount() == 128);
    CHECK(m.quadCount() == 128);
    CHECK(m.triangleCount() == 256);
    CHECK(edgeStats(m).open == 0);
    CHECK(signedVolume(m) > 0.0);
    // Capsule: two poles, arcs of segments/4, the side rings.
    m = makePrimitive(spec("capsule", 12, 2, 1));
    CHECK(edgeStats(m).open == 0);
    CHECK(signedVolume(m) > 0.0);
    CHECK(wellFormed(m));
    // A capsule whose height is twice its radius is a ball: no flat side rings, still closed.
    PrimitiveSpec ball = spec("capsule", 12, 3, 1);
    ball.height = 2.0f * ball.radius * 0.5f;
    m = makePrimitive(ball);
    CHECK(edgeStats(m).open == 0);
    CHECK(wellFormed(m));
}

TEST(primitive_kit_dimensions) {
    PrimitiveSpec s = spec("cylinder", 24, 1, 1, 2.0f);
    s.radius = 0.6f;
    s.height = 0.5f;
    Mesh m = makePrimitive(s);
    CHECK(std::abs(maxRadiusXZ(m) - 0.6f) < 1e-5f);
    const AABB b = m.bounds();
    CHECK(std::abs(b.min.y + 0.5f) < 1e-5f && std::abs(b.max.y - 0.5f) < 1e-5f);
    // The cone's top ring sits at the top radius.
    PrimitiveSpec c = spec("cone", 24, 1, 1, 2.0f);
    c.radius2 = 0.2f;
    m = makePrimitive(c);
    float topR = 0.0f;
    for (const auto& p : m.positions)
        if (p.y > 0.999f) topR = std::max(topR, std::sqrt(p.x * p.x + p.z * p.z));
    CHECK(std::abs(topR - 0.2f) < 1e-5f);
    // Placement moves the mesh.
    s.placement = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
    m = makePrimitive(s);
    CHECK(std::abs(m.bounds().center().x - 3.0f) < 1e-5f);
    // Axis: the height runs along X or Z instead of Y; still closed and outward.
    s.placement = glm::mat4(1.0f);
    s.axis = 0;
    m = makePrimitive(s);
    AABB bx = m.bounds();
    CHECK(std::abs(bx.max.x - 0.5f) < 1e-5f && std::abs(bx.max.y - 0.6f) < 1e-5f);
    CHECK(signedVolume(m) > 0.0 && edgeStats(m).open == 0);
    s.axis = 2;
    m = makePrimitive(s);
    bx = m.bounds();
    CHECK(std::abs(bx.max.z - 0.5f) < 1e-5f && std::abs(bx.max.x - 0.6f) < 1e-5f);
    CHECK(signedVolume(m) > 0.0 && edgeStats(m).open == 0);
    // The low-poly cylinder and cone come with quad caps (no pole): all quads.
    const Mesh low = makePrimitive(primitiveDefaults("cylinder", true));
    CHECK(low.triangleCount() == 2 * low.quadCount());
    CHECK(edgeStats(low).open == 0 && signedVolume(low) > 0.0);
}

TEST(primitive_kit_classic_defaults_unchanged) {
    // Append's dense meshes are what makeCylinder & co have always built (#31): same vertices, triangles and bounds.
    // (The revolved classics come out of weldImported without their quad pairs; the spec path keeps them, so it has
    // at least as many quads.)
    struct Case {
        const char* type;
        Mesh classic;
    };
    const Case cases[] = {{"cylinder", makeCylinder(48, 2.0f)}, {"cone", makeCone(48, 2.0f)}, {"torus", makeTorus(64, 2.0f)},
                          {"capsule", makeCapsule(48, 2.0f)},   {"cube", makeCube(48, 2.0f)},  {"quadsphere", makeQuadSphere(32, 2.0f)},
                          {"sphere", makeIcosphere(5, 1.0f)},   {"plane", makePlane(128, 2.0f)}};
    for (const Case& c : cases) {
        PrimitiveSpec s = primitiveDefaults(c.type, false);
        s.size = 2.0f;
        const Mesh m = makePrimitive(s);
        if (m.vertexCount() != c.classic.vertexCount() || m.triangleCount() != c.classic.triangleCount() ||
            m.quadCount() < c.classic.quadCount())
            std::printf("  %s: spec %zu v %zu t %zu q, classic %zu v %zu t %zu q\n", c.type, m.vertexCount(), m.triangleCount(),
                        m.quadCount(), c.classic.vertexCount(), c.classic.triangleCount(), c.classic.quadCount());
        CHECK(m.vertexCount() == c.classic.vertexCount());
        CHECK(m.triangleCount() == c.classic.triangleCount());
        CHECK(m.quadCount() >= c.classic.quadCount());
        const AABB a = m.bounds(), b = c.classic.bounds();
        CHECK(glm::distance(a.min, b.min) < 1e-4f && glm::distance(a.max, b.max) < 1e-4f);
    }
}

TEST(primitive_kit_json_and_clamp) {
    PrimitiveSpec s = spec("pipe", 20, 3, 2, 1.5f);
    s.quadCaps = true;
    s.axis = 2;
    s.radius = 0.75f;
    s.radius2 = 0.5f;
    s.height = 0.8f;
    s.placement = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    PrimitiveSpec back;
    CHECK(primitiveSpecFromJson(primitiveSpecJson(s), back));
    CHECK(back == s);
    CHECK(!primitiveSpecFromJson(Json::object(), back));
    // Clamps: ranges, quad caps to a multiple of 4, a pipe's wall, a torus inside its box.
    PrimitiveSpec c = spec("cylinder", 1000, 0, 99);
    clampPrimitiveSpec(c);
    CHECK(c.segments == 256 && c.rings == 1 && c.caps == 8);
    c.quadCaps = true;
    c.segments = 13;
    clampPrimitiveSpec(c);
    CHECK(c.segments % 4 == 0);
    PrimitiveSpec p = spec("pipe", 12, 1, 1);
    p.radius2 = 0.9f;
    clampPrimitiveSpec(p);
    CHECK(p.radius2 < p.radius);
    PrimitiveSpec t = spec("torus", 12, 6, 1);
    t.radius = 0.9f;
    t.radius2 = 0.5f;
    clampPrimitiveSpec(t);
    CHECK(t.radius + t.radius2 <= 1.0f + 1e-6f);
    const Mesh tm = makePrimitive(t);
    const glm::vec3 e = tm.bounds().extent();
    CHECK(std::max({e.x, e.y, e.z}) <= 2.0f * 1.001f);
}

TEST(primitive_kit_append_settings_and_freshness) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CommandResult r = reg.execute(doc, "part_append", {{"type", std::string("cylinder")}, {"low_poly", true}, {"quad_caps", false}});
    CHECK(r.ok);
    CHECK(doc.partCount() == 2 && doc.activePart == 1);
    CHECK(primitiveFresh(doc, 1));
    CHECK(doc.activePartInfo().data->primitive.spec().segments == 12);
    CHECK(doc.mesh.quadCount() == 12);
    // The Part sits beside the sphere: its placement holds the offset, the spec regenerates in place.
    const glm::vec3 at = doc.mesh.bounds().center();
    CHECK(at.x > 1.0f);
    r = reg.execute(doc, "part_primitive", {{"segments", 8.0}});
    CHECK(r.ok);
    CHECK(doc.mesh.quadCount() == 8);
    CHECK(primitiveFresh(doc, 1));
    CHECK(glm::distance(doc.mesh.bounds().center(), at) < 1e-4f);
    r = reg.execute(doc, "part_primitive", {{"info", true}});
    CHECK(r.ok && r.message.find("segments 8") != std::string::npos);
    // Undo walks back to the earlier settings (and they still apply); redo forward.
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.quadCount() == 12);
    CHECK(primitiveFresh(doc, 1));
    CHECK(doc.activePartInfo().data->primitive.spec().segments == 12);
    CHECK(reg.execute(doc, "redo", {}).ok);
    CHECK(doc.mesh.quadCount() == 8);
    CHECK(primitiveFresh(doc, 1));
    // Presets switch every setting; "no change" is fine.
    CHECK(reg.execute(doc, "part_primitive", {{"preset", std::string("dense")}}).ok);
    CHECK(doc.activePartInfo().data->primitive.spec().segments == 48);
    CHECK(doc.mesh.quadCount() > 1000);
    CHECK(reg.execute(doc, "part_primitive", {{"preset", std::string("lowpoly")}}).ok);
    CHECK(doc.mesh.quadCount() == 12 + 2 * 9); // (quad caps: 3 x 3 cells each)
    CHECK(reg.execute(doc, "part_primitive", {{"segments", 12.0}, {"quad_caps", false}}).ok);
    CHECK(doc.mesh.quadCount() == 12);
    // Axis: the same Part turned onto Z, as a setting.
    CHECK(reg.execute(doc, "part_primitive", {{"axis", std::string("z")}}).ok);
    CHECK(doc.mesh.bounds().extent().z > doc.mesh.bounds().extent().y);
    CHECK(primitiveFresh(doc, 1));
    CHECK(!reg.execute(doc, "part_primitive", {{"axis", std::string("w")}}).ok);
    CHECK(reg.execute(doc, "part_primitive", {{"axis", std::string("y")}}).ok);
    // Not a primitive: the sphere from File > New.
    CHECK(reg.execute(doc, "part_select", {{"index", 0.0}}).ok);
    CHECK(!primitiveFresh(doc, 0));
    CHECK(!reg.execute(doc, "part_primitive", {{"segments", 8.0}}).ok);
    CHECK(reg.execute(doc, "part_select", {{"index", 1.0}}).ok);
    CHECK(primitiveFresh(doc, 1));

    // A partial move (half the Part masked) edits the mesh: the settings no longer apply; undoing it brings them back.
    std::vector<float>& mask = doc.mesh.ensureMask(0.0f);
    for (size_t v = 0; v < mask.size() / 2; ++v) mask[v] = 1.0f;
    CHECK(reg.execute(doc, "transform", {{"translate", std::string("0,0.3,0")}, {"masked", true}}).ok);
    CHECK(!primitiveFresh(doc, 1));
    CHECK(!reg.execute(doc, "part_primitive", {{"segments", 6.0}}).ok);
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(primitiveFresh(doc, 1));
    doc.mesh.clearMask();
    // A whole move keeps them: the placement follows, and the next setting rebuilds the Part where it is now.
    const glm::vec3 before = doc.mesh.bounds().center();
    CHECK(reg.execute(doc, "transform", {{"translate", std::string("0,1,0")}, {"masked", false}}).ok);
    CHECK(primitiveFresh(doc, 1));
    CHECK(reg.execute(doc, "part_primitive", {{"segments", 6.0}}).ok);
    CHECK(doc.mesh.quadCount() == 6);
    CHECK(glm::distance(doc.mesh.bounds().center(), before + glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f);
    CHECK(reg.execute(doc, "transform", {{"rotate", std::string("0,0,90")}, {"scale", std::string("1.5")}, {"masked", false}}).ok);
    CHECK(primitiveFresh(doc, 1));
    CHECK(reg.execute(doc, "part_primitive", {{"rings", 2.0}}).ok);
    CHECK(doc.mesh.quadCount() == 12);
    // Undoing a whole move puts the positions back while the newest spec still holds the moved placement: the earlier
    // spec in the history matches, so the settings stay (and the next change rebuilds at the old place).
    const glm::vec3 moved = doc.mesh.bounds().center();
    CHECK(reg.execute(doc, "undo", {}).ok); // the rings change
    CHECK(reg.execute(doc, "undo", {}).ok); // the rotate + scale
    CHECK(primitiveFresh(doc, 1));
    CHECK(glm::distance(doc.mesh.bounds().center(), moved) < 1e-3f); // (a rotation about the centre, scaled: same centre)
    CHECK(reg.execute(doc, "undo", {}).ok); // the segments=6 rebuild (still translated)
    CHECK(primitiveFresh(doc, 1));
    CHECK(glm::distance(doc.mesh.bounds().center(), before + glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f);
    CHECK(reg.execute(doc, "undo", {}).ok); // the translate
    CHECK(primitiveFresh(doc, 1));
    CHECK(glm::distance(doc.mesh.bounds().center(), before) < 1e-3f);
    CHECK(reg.execute(doc, "part_primitive", {{"segments", 8.0}}).ok);
    CHECK(glm::distance(doc.mesh.bounds().center(), before) < 1e-3f);
    // Settings on Append itself, and an unknown type.
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("torus")}, {"segments", 8.0}, {"rings", 4.0}}).ok);
    CHECK(doc.mesh.quadCount() == 32);
    CHECK(primitiveFresh(doc, doc.activePart));
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("pipe")}, {"low_poly", true}}).ok);
    CHECK(doc.mesh.quadCount() == 48);
    CHECK(!reg.execute(doc, "part_append", {{"type", std::string("teapot")}}).ok);
}

TEST(primitive_kit_project_roundtrip) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("cone")}, {"low_poly", true}, {"radius2", 0.3}, {"quad_caps", false}}).ok);
    CHECK(reg.execute(doc, "transform", {{"translate", std::string("0,0.5,0")}, {"masked", false}}).ok);
    const std::string path = tempPath("sf_test_primitive_kit.sfproj");
    std::string error;
    CHECK(saveProject(path, doc, error, false, false));
    CHECK(error.empty());
    Document loaded;
    CHECK(loadProject(path, loaded, error));
    CHECK(loaded.partCount() == 2);
    CHECK(primitiveFresh(loaded, 1));
    CHECK(loaded.parts[1].data->primitive.spec().type == "cone");
    CHECK(std::abs(loaded.parts[1].data->primitive.spec().radius2 - 0.3f) < 1e-6f);
    loaded.selectPart(1);
    CHECK(reg.execute(loaded, "part_primitive", {{"segments", 6.0}}).ok);
    CHECK(loaded.mesh.quadCount() == 6);
    std::remove(path.c_str());
}

TEST(solidify_plane) {
    Mesh m = makePlane(4, 2.0f);
    CHECK(edgeStats(m).open == 16);
    m.ensureColors(glm::vec3(0.5f));
    m.uvs.assign(m.triangleCount() * 3, glm::vec2(0.25f));
    const size_t nv = m.vertexCount(), nt = m.triangleCount();
    std::string error;
    SolidifyOptions o;
    o.thickness = 0.05f; // of the largest side (2): 0.1
    Mesh out = solidifyMesh(m, o, error);
    CHECK(error.empty());
    CHECK(out.vertexCount() == 2 * nv);
    CHECK(out.triangleCount() == 2 * nt + 2 * 16);
    CHECK(out.quadCount() == 2 * 16 + 16);
    const EdgeStats e = edgeStats(out);
    CHECK(e.open == 0 && e.nonManifold == 0);
    CHECK(wellFormed(out));
    const double vol = signedVolume(out);
    CHECK(std::abs(vol - 2.0 * 2.0 * 0.1) < 1e-4);
    // The front stays put, the back is behind it (along -normal, whichever way the plane faces).
    Mesh n = m;
    n.recomputeAllNormals();
    const float dir = n.normals[0].y > 0.0f ? 1.0f : -1.0f;
    for (size_t v = 0; v < nv; ++v) CHECK(out.positions[v] == m.positions[v]);
    CHECK(std::abs(out.positions[nv].y - (m.positions[0].y - dir * 0.1f)) < 1e-5f);
    // Attributes: paint and UVs on both sides, groups (none before) front 0, rim 1.
    CHECK(out.colors.size() == out.vertexCount());
    CHECK(out.uvs.size() == out.triangleCount() * 3);
    CHECK(out.facegroups.size() == out.triangleCount());
    CHECK(out.facegroups[0] == 0 && out.facegroups.back() == 1);
    // Both sides: half each way.
    o.bothSides = true;
    out = solidifyMesh(m, o, error);
    CHECK(error.empty());
    CHECK(std::abs(out.positions[0].y - (m.positions[0].y + dir * 0.05f)) < 1e-5f);
    CHECK(std::abs(out.positions[nv].y - (m.positions[0].y - dir * 0.05f)) < 1e-5f);
    CHECK(signedVolume(out) > 0.0);
    // Negative: in front.
    o.bothSides = false;
    o.thickness = -0.05f;
    out = solidifyMesh(m, o, error);
    CHECK(error.empty());
    CHECK(std::abs(out.positions[nv].y - (m.positions[0].y + dir * 0.1f)) < 1e-5f);
    CHECK(signedVolume(out) > 0.0);
    CHECK(edgeStats(out).open == 0);
    // Face groups of the front survive; the rim takes the next free one.
    Mesh grouped = makePlane(2, 2.0f);
    grouped.ensureFaceGroups(4);
    out = solidifyMesh(grouped, o, error);
    CHECK(error.empty());
    CHECK(out.facegroups[0] == 4 && out.facegroups[grouped.triangleCount()] == 4 && out.facegroups.back() == 5);
    // Refusals: a closed mesh, a zero thickness, nothing, an edge shared by three faces.
    CHECK(solidifyMesh(makeIcosphere(2), o, error).vertexCount() == 0);
    CHECK(!error.empty());
    Mesh fin = makePlane(2, 2.0f);
    {
        // A third face on an edge two faces share already (an inner edge of the plane).
        std::map<std::pair<uint32_t, uint32_t>, int> uses;
        for (const auto& t : fin.triangles)
            for (int e = 0; e < 3; ++e) ++uses[{std::min(t[e], t[(e + 1) % 3]), std::max(t[e], t[(e + 1) % 3])}];
        std::pair<uint32_t, uint32_t> inner{0, 0};
        for (const auto& [k, n] : uses)
            if (n == 2) inner = k;
        CHECK(inner.first != inner.second);
        fin.positions.push_back(fin.positions[inner.first] + glm::vec3(0.0f, 1.0f, 0.0f));
        fin.triangles.push_back({inner.first, inner.second, uint32_t(fin.positions.size() - 1)});
        if (fin.hasQuads()) fin.quadPartners.resize(fin.triangles.size(), Mesh::kNoQuad);
        fin.rebuildTopology();
        fin.recomputeAllNormals();
    }
    CHECK(solidifyMesh(fin, o, error).vertexCount() == 0);
    CHECK(error.find("more than two faces") != std::string::npos);
    o.thickness = 0.0f;
    CHECK(solidifyMesh(m, o, error).vertexCount() == 0);
    CHECK(!error.empty());
    CHECK(solidifyMesh(Mesh(), o, error).vertexCount() == 0);
}

TEST(solidify_command_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "new_sphere", {{"subdivisions", 2.0}}).ok);
    CHECK(!reg.execute(doc, "solidify", {{"thickness", 0.05}}).ok); // closed already
    CHECK(reg.execute(doc, "part_append", {{"type", std::string("plane")}, {"low_poly", true}}).ok);
    const size_t nv = doc.mesh.vertexCount();
    CHECK(primitiveFresh(doc, 1));
    CommandResult r = reg.execute(doc, "solidify", {{"thickness", 0.05}});
    CHECK(r.ok);
    CHECK(doc.mesh.vertexCount() == 2 * nv);
    CHECK(edgeStats(doc.mesh).open == 0);
    CHECK(!primitiveFresh(doc, 1)); // an ordinary mesh now
    CHECK(reg.execute(doc, "undo", {}).ok);
    CHECK(doc.mesh.vertexCount() == nv);
    CHECK(primitiveFresh(doc, 1));
}
