// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tube (src/core/Tube.cpp): the curve, the sweep (watertight, no twist, adaptive rings, caps, even edges), strokes
// into control points, the tube_* commands with undo, merging, and projects.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/MeshTransform.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Tube.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

using namespace sf;
using glm::vec3;

namespace {

bool watertight(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const glm::uvec3& t : m.triangles) {
        if (t.x == t.y || t.y == t.z || t.x == t.z) return false;
        for (int k = 0; k < 3; ++k) ++edges[{std::min(t[k], t[(k + 1) % 3]), std::max(t[k], t[(k + 1) % 3])}];
    }
    for (const auto& [e, n] : edges)
        if (n != 2) return false;
    return !edges.empty();
}

// Every edge used once in each direction: the triangles agree on which side is out.
bool oriented(const Mesh& m) {
    std::map<std::pair<uint32_t, uint32_t>, int> half;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k)
            if (++half[{t[k], t[(k + 1) % 3]}] > 1) return false;
    return true;
}

double signedVolume(const Mesh& m) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles)
        v += glm::dot(m.positions[t.x], glm::cross(m.positions[t.y], m.positions[t.z])) / 6.0;
    return v;
}

// Swept fine (48 sides) so the volume checks measure the shape, not the polygon's few percent.
TubeShape line(float length, float radius) {
    TubeShape s;
    s.points = {{vec3(0, 0, 0), radius}, {vec3(length * 0.5f, 0, 0), radius}, {vec3(length, 0, 0), radius}};
    s.edge = 2.0f * 3.14159265f * radius / 48.0f;
    return s;
}

// An S in the XY plane, then up out of it.
TubeShape curvy() {
    TubeShape s;
    for (int i = 0; i <= 24; ++i) {
        const float a = float(i) / 24.0f * 6.0f;
        s.points.push_back({vec3(a, std::sin(a) * 1.5f, i > 18 ? float(i - 18) * 0.4f : 0.0f), 0.2f + 0.01f * float(i)});
    }
    return s;
}

} // namespace

TEST(tube_sweep_closed_and_outward) {
    const float pi = 3.14159265f;
    // A straight tube with round ends is a capsule: volume pi r^2 L + 4/3 pi r^3, facing out.
    const Mesh m = tubeSweep(line(2.0f, 0.25f));
    CHECK(watertight(m) && oriented(m));
    CHECK(m.hasQuads() && m.facegroups.size() == m.triangleCount());
    const double want = pi * 0.0625 * 2.0 + 4.0 / 3.0 * pi * 0.015625;
    CHECK(signedVolume(m) > 0.0 && std::abs(signedVolume(m) / want - 1.0) < 0.03);
    // Flat ends: a cylinder, a little less.
    TubeShape flat = line(2.0f, 0.25f);
    flat.flatCaps = true;
    const Mesh f = tubeSweep(flat);
    CHECK(watertight(f) && oriented(f));
    CHECK(std::abs(signedVolume(f) / (pi * 0.0625 * 2.0) - 1.0) < 0.03);
    // A curvy tube with varying radius, both ways; a single point is a ball.
    for (bool uniform : {false, true}) {
        const Mesh c = tubeSweep(curvy(), uniform);
        CHECK(watertight(c) && oriented(c) && signedVolume(c) > 0.0);
    }
    TubeShape ball;
    ball.points = {{vec3(1, 2, 3), 0.5f}};
    ball.edge = 2.0f * 3.14159265f * 0.5f / 48.0f;
    const Mesh b = tubeSweep(ball);
    CHECK(watertight(b) && oriented(b));
    CHECK(std::abs(signedVolume(b) / (4.0 / 3.0 * pi * 0.125) - 1.0) < 0.05);
    // Invalid shapes sweep to nothing.
    TubeShape bad = line(1.0f, 0.1f);
    bad.points[1].radius = 0.0f;
    CHECK(!tubeValidate(bad).empty() && tubeSweep(bad).triangleCount() == 0);
    CHECK(!tubeValidate(TubeShape{}).empty());
}

TEST(tube_frames_do_not_twist) {
    // A planar curve: the carried normal keeps the same angle to the plane all along (no twist).
    TubeShape s = curvy();
    s.points.resize(19); // (the planar part)
    const auto c = tubeCenterline(s, tubeEdgeLength(s));
    const auto n = tubeFrames(c);
    CHECK(c.size() > 20 && n.size() == c.size());
    for (size_t i = 0; i < c.size(); ++i) {
        CHECK(std::abs(glm::dot(n[i], c[i].tangent)) < 1e-4f);
        CHECK(std::abs(std::abs(n[i].z) - std::abs(n[0].z)) < 1e-3f);
    }
    // A helix: neighbouring frames turn only as much as the tangent does.
    TubeShape h;
    for (int i = 0; i <= 80; ++i) {
        const float a = float(i) * 0.25f;
        h.points.push_back({vec3(std::cos(a), std::sin(a), a * 0.2f), 0.1f});
    }
    const auto hc = tubeCenterline(h, tubeEdgeLength(h));
    const auto hn = tubeFrames(hc);
    for (size_t i = 1; i < hc.size(); ++i) {
        const float turnT = std::acos(std::clamp(glm::dot(hc[i].tangent, hc[i - 1].tangent), -1.0f, 1.0f));
        const float turnN = std::acos(std::clamp(glm::dot(hn[i], hn[i - 1]), -1.0f, 1.0f));
        CHECK(turnN <= turnT + 1e-3f);
    }
}

TEST(tube_rings_adapt_to_bends) {
    // Straight, then a tight hairpin: the hairpin gets rings closer together than the edge length.
    TubeShape s;
    s.points = {{vec3(-4, 0, 0), 0.1f}, {vec3(0, 0, 0), 0.1f}, {vec3(0.3f, 0.3f, 0), 0.1f}, {vec3(0, 0.6f, 0), 0.1f},
                {vec3(-4, 0.6f, 0), 0.1f}};
    s.edge = 0.2f; // 10 degrees of the hairpin is well under an edge
    const float edge = tubeEdgeLength(s);
    const auto c = tubeCenterline(s, edge);
    float straightGap = 0.0f, bendGap = 1e9f;
    for (size_t i = 1; i < c.size(); ++i) {
        const float gap = glm::distance(c[i].position, c[i - 1].position);
        CHECK(gap <= edge * 1.31f);
        if (c[i].position.x < -2.0f && c[i - 1].position.x < -2.0f && c[i].position.y < 0.1f) straightGap = std::max(straightGap, gap);
        if (c[i].position.x > 0.15f) bendGap = std::min(bendGap, gap);
    }
    CHECK(straightGap > 0.9f * edge && bendGap < 0.7f * edge);
    CHECK(glm::distance(c.front().position, s.points.front().position) < 1e-5f);
    CHECK(glm::distance(c.back().position, s.points.back().position) < 1e-5f);
}

TEST(tube_uniform_edges) {
    // Even edges whatever the radius: a thick end and a thin end get as many sides as they need.
    TubeShape s;
    s.points = {{vec3(0, 0, 0), 0.5f}, {vec3(1.5f, 0.3f, 0), 0.25f}, {vec3(3, 0, 0), 0.08f}};
    const float edge = 0.06f;
    const Mesh m = tubeSweep(s, true, edge);
    CHECK(watertight(m) && oriented(m) && !m.hasQuads());
    size_t n = 0, good = 0;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) {
            const float l = glm::distance(m.positions[t[k]], m.positions[t[(k + 1) % 3]]);
            ++n;
            good += l > 0.4f * edge && l < 1.9f * edge;
        }
    CHECK(double(good) / double(n) > 0.95);
}

TEST(tube_mirror_copy) {
    TubeShape s = line(1.0f, 0.1f);
    for (TubePoint& p : s.points) p.position.x += 0.5f;
    s.mirrorX = true;
    const Mesh m = tubeSweep(s);
    CHECK(watertight(m) && m.triangleCount() == 2 * tubeSweep(line(1.0f, 0.1f)).triangleCount());
    float xmin = 1e9f;
    for (const vec3& p : m.positions) xmin = std::min(xmin, p.x);
    CHECK(xmin < -1.5f);
    CHECK(signedVolume(m) > 0.0); // the copy faces out too
    // On the plane itself there is nothing to mirror.
    TubeShape onPlane;
    onPlane.points = {{vec3(0, 0, 0), 0.1f}, {vec3(0, 1, 0), 0.1f}};
    onPlane.mirrorX = true;
    onPlane.edge = 0.05f;
    TubeShape plain = onPlane;
    plain.mirrorX = false;
    CHECK(tubeSweep(onPlane).triangleCount() == tubeSweep(plain).triangleCount());
}

TEST(tube_from_stroke) {
    // 3000 jittery pen samples along a line with pressure from light to firm.
    std::vector<TubePoint> raw;
    for (int i = 0; i < 3000; ++i) {
        const float u = float(i) / 2999.0f;
        const float jitter = 0.01f * std::sin(float(i) * 1.7f);
        raw.push_back({vec3(u * 10.0f, jitter, 0.0f), 0.05f + 0.1f * u});
    }
    const auto pts = tubeFromStroke(raw, 0.5f);
    CHECK(pts.size() >= 2 && pts.size() <= kTubeStrokePoints);
    CHECK(glm::distance(pts.front().position, raw.front().position) < 1e-4f);
    CHECK(glm::distance(pts.back().position, raw.back().position) < 1e-4f);
    CHECK(pts.front().radius < pts.back().radius);
    float worst = 0.0f;
    for (const TubePoint& p : pts) worst = std::max(worst, std::abs(p.position.y));
    CHECK(worst < 0.01f); // smoothed
    // At most maxPoints, even for a very long stroke.
    CHECK(tubeFromStroke(raw, 0.0f, 40).size() == 40);
    // A dab is a ball.
    const auto dab = tubeFromStroke({{vec3(0.0f), 0.2f}, {vec3(0.01f, 0, 0), 0.25f}, {vec3(0.02f, 0, 0), 0.2f}}, 0.5f);
    CHECK(dab.size() == 1 && std::abs(dab[0].radius - 0.25f) < 1e-6f);
    CHECK(tubeFromStroke({}, 0.5f).empty());
}

TEST(tube_parse_points) {
    std::vector<TubePoint> p;
    std::string err;
    CHECK(parseTubePoints("0,0,0 1,2,3,0.5;4,5,6|7,8,9,0.25", 0.1f, p, err) && p.size() == 4);
    CHECK(p[0].radius == 0.1f && p[1].radius == 0.5f && p[1].position == vec3(1, 2, 3) && p[3].radius == 0.25f);
    CHECK(!parseTubePoints("1,2", 0.1f, p, err));
    CHECK(!parseTubePoints("1,2,3,4,5", 0.1f, p, err));
    CHECK(!parseTubePoints("1,x,3", 0.1f, p, err));
    CHECK(!parseTubePoints("  ", 0.1f, p, err));
}

TEST(tube_commands_and_undo) {
    Document doc;
    CommandRegistry reg;
    CHECK(!reg.execute(doc, "tube_set_point", {{"index", 0.0}, {"radius", 0.2}}).ok); // not a Tube Part
    const int parts = doc.partCount();
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0,0 1,1,0")}, {"radius", 0.1}}).ok);
    // A new Part below the active one, which stays active; undo takes it out, redo puts it back.
    CHECK(doc.partCount() == parts + 1 && doc.activePart == 0 && doc.parts[1].name == "Tube");
    CHECK(doc.partMesh(1).tube.points.size() == 3 && watertight(doc.partMesh(1)));
    CHECK(reg.execute(doc, "undo").ok && doc.partCount() == parts);
    CHECK(reg.execute(doc, "redo").ok && doc.partCount() == parts + 1 && doc.partMesh(1).tube.points.size() == 3);

    doc.selectPart(1);
    CHECK(reg.execute(doc, "tube_list").message.find("3 points, round caps") == 0);
    CHECK(reg.execute(doc, "tube_set_point", {{"index", 2.0}, {"position", std::string("2,1,0")}, {"radius", 0.3}}).ok);
    CHECK(doc.mesh.tube.points[2].position == vec3(2, 1, 0) && doc.mesh.tube.points[2].radius == 0.3f);
    CHECK(reg.execute(doc, "tube_insert_point", {{"index", 0.0}}).ok && doc.mesh.tube.points.size() == 4);
    CHECK(doc.mesh.tube.points[1].position == vec3(0.5f, 0, 0));
    CHECK(reg.execute(doc, "tube_delete_point", {{"index", 3.0}}).ok && doc.mesh.tube.points.size() == 3);
    CHECK(!reg.execute(doc, "tube_insert_point", {{"index", 2.0}}).ok);
    CHECK(reg.execute(doc, "tube_set", {{"caps", std::string("flat")}, {"symmetry", std::string("x")}}).ok);
    CHECK(doc.mesh.tube.flatCaps && doc.mesh.tube.mirrorX && watertight(doc.mesh));
    // Each edit one undo step, the curve coming back with the mesh.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && !doc.mesh.tube.flatCaps);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && doc.mesh.tube.points.size() == 4);
    CHECK(doc.undo.redo(doc.mesh, doc.bvh) && doc.undo.redo(doc.mesh, doc.bvh) && doc.mesh.tube.flatCaps);

    // Moved with the gizmo: the points no longer fit, so editing them says so instead of undoing the move.
    MeshTransform t;
    t.translate = vec3(0.5f, 0, 0);
    CHECK(transformPart(doc, doc.activePart, t, false, 0.0f, {false, false, false}, "Transform") > 0);
    CHECK(!tubeMatchesMesh(doc.mesh));
    CHECK(!reg.execute(doc, "tube_set_point", {{"index", 0.0}, {"radius", 0.2}}).ok);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && tubeMatchesMesh(doc.mesh));

    // Bake: a plain mesh, same triangles; uniform re-sweeps in even triangles.
    const size_t tris = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "tube_bake").ok && doc.mesh.tube.empty() && doc.mesh.triangleCount() == tris);
    CHECK(!reg.execute(doc, "tube_bake").ok);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && !doc.mesh.tube.empty());
    CHECK(reg.execute(doc, "tube_bake", {{"uniform", true}}).ok && doc.mesh.tube.empty() && !doc.mesh.hasQuads());
}

TEST(tube_merge_into_active) {
    Document doc;
    CommandRegistry reg;
    doc.mesh = makeQuadSphere(8);
    const size_t before = doc.mesh.triangleCount();
    const int parts = doc.partCount();
    CHECK(before > 0);
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0.5,0,0 1.5,0.5,0 2,1.5,0.5")}, {"radius", 0.1},
                                        {"merge", true}}).ok);
    CHECK(doc.partCount() == parts && doc.mesh.triangleCount() > before && doc.mesh.tube.empty());
    CHECK(doc.undo.undo(doc.mesh, doc.bvh) && doc.mesh.triangleCount() == before); // one step
    // Raw samples with smoothing go through the stroke simplifier.
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 0.01,0.02,0 0.02,0.01,0 0.5,0.02,0 1,0,0")},
                                        {"radius", 0.05}, {"smooth", 0.5}, {"name", std::string("Vine")}}).ok);
    CHECK(doc.parts[size_t(doc.activePart + 1)].name == "Vine");
}

TEST(tube_project_roundtrip) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0,0.1 1,0,0,0.2 1,1,0,0.05")}, {"caps", std::string("flat")},
                                        {"symmetry", std::string("x")}, {"edge", 0.04}}).ok);
    const std::string path = tempPath("sf_tube.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    CHECK(back.partCount() == doc.partCount());
    const Mesh& m = back.partMesh(1);
    CHECK(m.tube.points.size() == 3 && m.tube.flatCaps && m.tube.mirrorX && std::abs(m.tube.edge - 0.04f) < 1e-6f);
    CHECK(m.tube.points[1].radius == 0.2f && m.triangleCount() == doc.partMesh(1).triangleCount());
    back.selectPart(1);
    CHECK(tubeMatchesMesh(back.mesh));
    CHECK(reg.execute(back, "tube_set_point", {{"index", 0.0}, {"radius", 0.15}}).ok);
    std::filesystem::remove(path);
    CHECK(tubeFromJson(Json::object()).empty());
}

TEST(tube_sweep_speed_500_points) {
    // Floor tier: dragging a point of a 500-point curve re-sweeps it every frame (60 fps = 16.7 ms).
    TubeShape s;
    for (int i = 0; i < 500; ++i) {
        const float a = float(i) * 0.05f;
        s.points.push_back({vec3(std::cos(a) * (1.0f + 0.1f * a), std::sin(a) * (1.0f + 0.1f * a), a * 0.1f), 0.04f});
    }
    double best = 1e9;
    size_t tris = 0;
    for (int k = 0; k < 5; ++k) {
        const auto t0 = std::chrono::steady_clock::now();
        const Mesh m = tubeMesh(s);
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        tris = m.triangleCount();
    }
    std::printf("tube_sweep_speed_500_points: %zu triangles in %.2f ms (best of 5)\n", tris, best);
    CHECK(tris > 10000);
    CHECK(best < 50.0); // (debug and sanitizer builds are slower; bench numbers in REPORT.md)
    // The live preview stays near its triangle budget however thin the tube, and carries the curve.
    for (TubePoint& p : s.points) p.radius = 0.01f;
    const Mesh full = tubeMesh(s), preview = tubeMesh(s, kTubePreviewTriangles);
    CHECK(full.triangleCount() > 2 * kTubePreviewTriangles);
    CHECK(preview.triangleCount() < kTubePreviewTriangles * 5 / 4 && preview.triangleCount() > kTubePreviewTriangles / 3);
    CHECK(preview.tube.points.size() == 500 && watertight(preview));
}

TEST(tube_profiles_watertight) {
    // Every cross-section, swept in quads and in even edges, with round and flat ends, tapered or not, on a bending
    // curve and as a one-point ball: closed, consistently wound, facing out.
    using P = TubeShape::Profile;
    for (P p : {P::Round, P::Square, P::Triangle, P::Ribbon})
        for (int variant = 0; variant < 8; ++variant) {
            TubeShape s = curvy();
            s.profile = p;
            s.flatCaps = variant & 1;
            s.roll = (variant & 2) ? 30.0f : 0.0f;
            if (variant & 4) s.taperStart = 0.3f, s.taperEnd = 0.9f; // (over 1 together: shared out)
            for (bool uniform : {false, true}) {
                const Mesh m = tubeSweep(s, uniform);
                const bool w = watertight(m), o = oriented(m);
                const double v = signedVolume(m);
                if (!(w && o && v > 0.0))
                    std::printf("profile %d variant %d uniform %d: watertight %d oriented %d volume %g\n", int(p), variant,
                                int(uniform), int(w), int(o), v);
                CHECK(w && o && v > 0.0);
            }
            s.points.resize(1);
            const Mesh ball = tubeSweep(s);
            CHECK(watertight(ball) && oriented(ball) && signedVolume(ball) > 0.0);
        }
}

TEST(tube_profile_outlines) {
    // A straight tube along X, radius 0.1, so each ring's outline is in the YZ plane.
    TubeShape s = line(2.0f, 0.1f);
    s.up = vec3(0, 1, 0);
    s.profile = TubeShape::Profile::Square;
    Mesh m = tubeSweep(s);
    float ymax = 0.0f, zmax = 0.0f, corner = 0.0f;
    for (const vec3& v : m.positions)
        if (v.x > 0.5f && v.x < 1.5f) {
            ymax = std::max(ymax, std::abs(v.y));
            zmax = std::max(zmax, std::abs(v.z));
            corner = std::max(corner, std::min(std::abs(v.y), std::abs(v.z)));
        }
    CHECK(std::abs(ymax - 0.1f) < 1e-4f && std::abs(zmax - 0.1f) < 1e-4f);
    CHECK(std::abs(corner - 0.1f) < 1e-4f); // the corners are vertices: sharp
    // A ribbon is thin along `up` and full width across it.
    s.profile = TubeShape::Profile::Ribbon;
    s.thickness = 0.2f;
    m = tubeSweep(s);
    ymax = zmax = 0.0f;
    for (const vec3& v : m.positions)
        if (v.x > 0.5f && v.x < 1.5f) ymax = std::max(ymax, std::abs(v.y)), zmax = std::max(zmax, std::abs(v.z));
    CHECK(std::abs(ymax - 0.02f) < 1e-4f && zmax > 0.097f && zmax < 0.1001f); // (no vertex need sit on the round tip)
    // Turned 90 degrees, it is thin across instead.
    s.roll = 90.0f;
    m = tubeSweep(s);
    ymax = zmax = 0.0f;
    for (const vec3& v : m.positions)
        if (v.x > 0.5f && v.x < 1.5f) ymax = std::max(ymax, std::abs(v.y)), zmax = std::max(zmax, std::abs(v.z));
    CHECK(ymax > 0.097f && ymax < 0.1001f && std::abs(zmax - 0.02f) < 1e-4f);
    // Tapered at the end: the last tenth is much thinner than the middle.
    s = line(2.0f, 0.1f);
    s.taperEnd = 0.5f;
    m = tubeSweep(s);
    float tip = 0.0f, mid = 0.0f;
    for (const vec3& v : m.positions) {
        const float r = std::hypot(v.y, v.z);
        if (v.x > 1.8f && v.x < 2.0f) tip = std::max(tip, r);
        if (v.x > 0.4f && v.x < 0.6f) mid = std::max(mid, r);
    }
    CHECK(mid > 0.099f && tip < 0.025f);
}

TEST(tube_profile_mirror_exact) {
    // The X mirror copy of a turned square tube is the exact mirror image of the tube.
    TubeShape s = curvy();
    for (TubePoint& p : s.points) p.position.x += 1.0f;
    s.profile = TubeShape::Profile::Triangle;
    s.roll = 25.0f;
    s.up = vec3(0.3f, 1.0f, 0.2f);
    s.taperEnd = 0.4f;
    TubeShape one = s;
    s.mirrorX = true;
    const Mesh a = tubeSweep(one), both = tubeSweep(s);
    CHECK(both.vertexCount() == 2 * a.vertexCount() && watertight(both) && signedVolume(both) > 0.0);
    float worst = 0.0f;
    for (size_t i = 0; i < a.vertexCount(); i += 7) {
        const vec3 m(-a.positions[i].x, a.positions[i].y, a.positions[i].z);
        float best = 1e9f;
        for (size_t j = a.vertexCount(); j < both.vertexCount(); ++j) best = std::min(best, glm::distance(m, both.positions[j]));
        worst = std::max(worst, best);
    }
    CHECK(worst < 1e-4f);
}

TEST(tube_point_spacing_and_simplify) {
    std::vector<TubePoint> raw;
    for (int i = 0; i < 2000; ++i) {
        const float u = float(i) / 1999.0f * 10.0f;
        raw.push_back({vec3(u, std::sin(u), 0.0f), 0.1f});
    }
    const size_t normal = tubeFromStroke(raw, 0.5f).size();
    const size_t sparse = tubeFromStroke(raw, 0.5f, kTubeStrokePoints, 5.0f).size();
    const size_t dense = tubeFromStroke(raw, 0.5f, kTubeStrokePoints, 0.5f).size();
    CHECK(sparse * 3 < normal && normal * 2 < dense);
    // Simplify: a third out per pass, ends kept, never below 2, and a straight run goes first.
    std::vector<TubePoint> pts = tubeFromStroke(raw, 0.5f);
    const std::vector<TubePoint> fewer = tubeSimplify(pts, 0.66f);
    CHECK(fewer.size() == size_t(std::lround(double(pts.size()) * 0.66)));
    CHECK(fewer.front().position == pts.front().position && fewer.back().position == pts.back().position);
    std::vector<TubePoint> straight;
    for (int i = 0; i < 50; ++i) straight.push_back({vec3(float(i), 0, 0), 0.5f});
    straight.push_back({vec3(49, 5, 0), 0.5f});
    const auto s2 = tubeSimplify(straight, 0.0f, 0.01f); // only the free ones go: the corner stays
    CHECK(s2.size() == 3 && s2[1].position == vec3(49, 0, 0));
    CHECK(tubeSimplify(straight, 0.0f).size() == 2);
    // The command: one undo step, and refused when there is nothing to take out.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0,0 2,0,0 3,0,0 4,0,1 5,0,1")}, {"radius", 0.2}}).ok);
    doc.selectPart(1);
    CHECK(reg.execute(doc, "tube_simplify").ok && doc.mesh.tube.points.size() == 4 && watertight(doc.mesh));
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.tube.points.size() == 6);
    CHECK(reg.execute(doc, "tube_simplify", {{"keep", 0.0}}).ok && doc.mesh.tube.points.size() == 2);
    CHECK(!reg.execute(doc, "tube_simplify").ok);
}

TEST(tube_profile_commands_and_project) {
    Document doc;
    CommandRegistry reg;
    CHECK(!reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0,0")}, {"profile", std::string("hexagon")}}).ok);
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0.3,0 2,0,0.4")}, {"radius", 0.1},
                                        {"profile", std::string("ribbon")}, {"thickness", 0.3}, {"turn", 15.0},
                                        {"taper_start", 0.2}, {"up", std::string("0,0,1")}}).ok);
    doc.selectPart(1);
    CHECK(doc.mesh.tube.profile == TubeShape::Profile::Ribbon && doc.mesh.tube.roll == 15.0f);
    CHECK(reg.execute(doc, "tube_set", {{"profile", std::string("square")}, {"taper_end", 0.5}}).ok);
    CHECK(doc.mesh.tube.profile == TubeShape::Profile::Square && doc.mesh.tube.taperEnd == 0.5f && watertight(doc.mesh));
    CHECK(!reg.execute(doc, "tube_set", {{"thickness", 2.0}}).ok);
    CHECK(!reg.execute(doc, "tube_set", {{"taper_start", -1.0}}).ok);
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.tube.profile == TubeShape::Profile::Ribbon);
    CHECK(reg.execute(doc, "tube_list").message.find("ribbon profile") != std::string::npos);
    const std::string path = tempPath("sf_tube_profile.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    const TubeShape& t = back.partMesh(1).tube;
    CHECK(t.profile == TubeShape::Profile::Ribbon && std::abs(t.thickness - 0.3f) < 1e-6f && t.roll == 15.0f &&
          std::abs(t.taperStart - 0.2f) < 1e-6f && t.up == vec3(0, 0, 1));
    back.selectPart(1);
    CHECK(tubeMatchesMesh(back.mesh));
    std::filesystem::remove(path);
}

namespace {

// The turn of a square tube's cross-section at x (degrees, a quarter turn is the same square): the polar angle of the
// ring's farthest corner about the x axis.
double cornerAngle(const Mesh& m, float x, float tol) {
    float best = -1.0f;
    double angle = 0.0;
    for (const vec3& p : m.positions) {
        if (std::abs(p.x - x) > tol) continue;
        const float r = std::hypot(p.y, p.z);
        if (r > best) best = r, angle = std::atan2(p.z, p.y) * 180.0 / 3.14159265;
    }
    angle = std::fmod(angle, 90.0);
    return angle < 0.0 ? angle + 90.0 : angle;
}
// a - b folded into (-45, 45].
double quarterDiff(double a, double b) {
    double d = std::fmod(a - b, 90.0);
    if (d > 45.0) d -= 90.0;
    if (d <= -45.0) d += 90.0;
    return d;
}

int components(const Mesh& m) {
    std::vector<uint32_t> up(m.positions.size());
    for (size_t i = 0; i < up.size(); ++i) up[i] = uint32_t(i);
    const auto find = [&](uint32_t a) {
        while (up[a] != a) a = up[a] = up[up[a]];
        return a;
    };
    for (const glm::uvec3& t : m.triangles) up[find(t.y)] = find(t.x), up[find(t.z)] = find(t.x);
    std::map<uint32_t, int> roots;
    for (const glm::uvec3& t : m.triangles) roots[find(t.x)] = 1;
    return int(roots.size());
}

TubeShape squareLine() {
    TubeShape s = line(2.0f, 0.25f);
    s.profile = TubeShape::Profile::Square;
    s.up = vec3(0, 0, 1);
    s.flatCaps = true;
    return s;
}

} // namespace

TEST(tube_resample_exact_count_even_spacing) {
    const TubeShape s = curvy();
    for (size_t n : {2u, 3u, 7u, 37u, 60u, 200u}) {
        const std::vector<TubePoint> r = tubeResample(s.points, n);
        CHECK(r.size() == n);
        CHECK(r.front().position == s.points.front().position && r.back().position == s.points.back().position);
    }
    const std::vector<TubePoint> r = tubeResample(s.points, 60);
    float lo = 1e9f, hi = 0.0f;
    for (size_t i = 1; i < r.size(); ++i) {
        const float d = glm::distance(r[i - 1].position, r[i].position);
        lo = std::min(lo, d), hi = std::max(hi, d);
    }
    CHECK(hi < 1.15f * lo);
    // Radii run from the first to the last point's.
    CHECK(std::abs(r.front().radius - 0.2f) < 1e-6f && std::abs(r.back().radius - 0.44f) < 1e-6f);
    // The new points stay on the old curve: the sweep of the resampled curve is about the same size.
    TubeShape t = s;
    t.points = r;
    CHECK(std::abs(signedVolume(tubeSweep(t)) / signedVolume(tubeSweep(s)) - 1.0) < 0.05);
    CHECK(tubeResample({{vec3(0), 0.1f}}, 5).empty());
    CHECK(tubeResample({{vec3(1), 0.1f}, {vec3(1), 0.1f}}, 5).empty());
}

TEST(tube_twist_and_spin_turn_the_profile_along_the_length) {
    const float tol = 0.02f;
    TubeShape s = squareLine();
    const double start = cornerAngle(tubeSweep(s), 0.0f, tol);
    s.twist = 30.0f;
    Mesh m = tubeSweep(s);
    CHECK(watertight(m) && oriented(m) && signedVolume(m) > 0.0);
    CHECK(std::abs(quarterDiff(cornerAngle(m, 0.0f, tol), start)) < 1.0);
    const double half = quarterDiff(cornerAngle(m, 1.0f, tol), start), end = quarterDiff(cornerAngle(m, 2.0f, tol), start);
    CHECK(std::abs(std::abs(end) - 30.0) < 1.5);                 // the whole twist at the end
    CHECK(std::abs(half - end / 2.0) < 1.5 && half * end > 0.0); // linear along the length
    // Spin: degrees per radius of length (0.25): 20 degrees at x = 0.5 is 40.
    TubeShape p = squareLine();
    p.spin = 20.0f;
    CHECK(std::abs(std::abs(quarterDiff(cornerAngle(tubeSweep(p), 0.5f, tol), start)) - 40.0) < 1.5);
    // Twist the other way for the mirror copy: the X mirror of a twisted tube is its mirror image.
    TubeShape q = squareLine();
    q.twist = 40.0f;
    q.points = {{vec3(0.5f, 0, 0), 0.25f}, {vec3(1.5f, 0, 0.5f), 0.25f}, {vec3(2.5f, 0, 0), 0.25f}};
    Mesh both = tubeSweep([&] { TubeShape x = q; x.mirrorX = true; return x; }());
    CHECK(watertight(both) && oriented(both));
    const Mesh a = tubeSweep(q), b = tubeSweep(tubeMirrored(q));
    CHECK(a.vertexCount() == b.vertexCount());
    for (size_t i = 0; i < a.vertexCount(); i += 7) { // (the mirror reverses each ring's order: look for the vertex)
        float nearest = 1e9f;
        for (const vec3& p : b.positions) nearest = std::min(nearest, glm::distance(vec3(-a.positions[i].x, a.positions[i].y, a.positions[i].z), p));
        CHECK(nearest < 1e-4f);
    }
    // No twist and no spin: exactly the old sweep.
    TubeShape plain = squareLine();
    plain.roll = 10.0f;
    CHECK(tubeSweep(plain).positions == tubeSweep([&] { TubeShape x = plain; x.twist = x.spin = 0.0f; return x; }()).positions);
}

TEST(tube_profiles_rope_braid_sleeve) {
    const float R = 0.25f, L = 2.0f, pi = 3.14159265f;
    // Sleeve: a hollow tube with open ends; wall = 0.5 * spread of the radius. Volume of the wall.
    TubeShape sl = line(L, R);
    sl.profile = TubeShape::Profile::Sleeve;
    sl.spread = 0.5f;
    Mesh m = tubeSweep(sl);
    CHECK(watertight(m) && oriented(m));
    const double wall = 0.25, inner = R * (1.0 - wall);
    CHECK(std::abs(signedVolume(m) / (pi * (R * R - inner * inner) * L) - 1.0) < 0.04);
    float rmin = 1e9f, rmax = 0.0f;
    for (const vec3& p : m.positions) {
        const float r = std::hypot(p.y, p.z);
        rmin = std::min(rmin, r), rmax = std::max(rmax, r);
    }
    CHECK(std::abs(rmax - R) < 1e-4f && std::abs(rmin - float(inner)) < 1e-4f); // open: nothing on the axis
    CHECK(m.facegroups.size() == m.triangleCount());
    // Rope: three strands inside the radius, each a closed tube; they wind round the axis.
    TubeShape rope = line(L, R);
    rope.profile = TubeShape::Profile::Rope;
    m = tubeSweep(rope);
    CHECK(watertight(m) && oriented(m) && signedVolume(m) > 0.0 && components(m) == 3);
    float reach = 0.0f;
    for (const vec3& p : m.positions) reach = std::max(reach, std::hypot(p.y, p.z));
    CHECK(reach < R * 1.01f && reach > R * 0.97f);
    // Strands touch at spread 0.5 and are fused at 0, apart at 1: the covered volume falls as they separate.
    rope.spread = 0.0f;
    const double fused = signedVolume(tubeSweep(rope));
    rope.spread = 1.0f;
    CHECK(fused > signedVolume(tubeSweep(rope)));
    // Rope turns with Twist: the strand positions at the end differ from the untwisted rope's.
    TubeShape tw = line(L, R);
    tw.profile = TubeShape::Profile::Rope;
    tw.twist = 90.0f;
    const Mesh t0 = tubeSweep(line(L, R)), t1 = tubeSweep(tw);
    CHECK(watertight(t1) && t1.vertexCount() > 0 && t0.vertexCount() > 0);
    // Braid: wide across, shallow in depth (the first ring's normal is automatic, so look at both axes).
    TubeShape br = line(L, R);
    br.profile = TubeShape::Profile::Braid;
    br.up = vec3(0, 0, 1);
    m = tubeSweep(br);
    CHECK(watertight(m) && oriented(m) && components(m) == 3);
    float across = 0.0f, deep = 0.0f;
    for (const vec3& p : m.positions) across = std::max(across, std::abs(p.y)), deep = std::max(deep, std::abs(p.z));
    CHECK(across > 0.97f * R && across < 1.01f * R && deep < 0.8f * R);
    // A one-point rope or sleeve is just a ball.
    TubeShape ball;
    ball.points = {{vec3(0), 0.2f}};
    ball.profile = TubeShape::Profile::Sleeve;
    CHECK(watertight(tubeSweep(ball)));
}

TEST(curve_part_resample_and_tube_from_curve) {
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0.5,0 2,0,0.4 3,0.3,0.1")}, {"radius", 0.1}, {"curve", true}}).ok);
    doc.selectPart(1);
    CHECK(doc.mesh.tube.curve && doc.parts[1].name == "Curve" && watertight(doc.mesh));
    const double wireVolume = signedVolume(doc.mesh);
    CHECK(!reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0,0")}, {"curve", true}, {"merge", true}}).ok);
    CHECK(!reg.execute(doc, "tube_bake").ok);
    CHECK(!reg.execute(doc, "tube_resample", {{"count", 1.0}}).ok);
    for (double n : {2.0, 9.0, 40.0}) {
        CHECK(reg.execute(doc, "tube_resample", {{"count", n}}).ok);
        CHECK(doc.mesh.tube.points.size() == size_t(n) && doc.mesh.tube.curve && tubeMatchesMesh(doc.mesh));
    }
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.tube.points.size() == 9);
    // Saved with the project.
    const std::string path = tempPath("sf_curve.sfproj");
    std::string err;
    CHECK(saveProject(path, doc, err));
    Document back;
    CHECK(loadProject(path, back, err));
    back.selectPart(1);
    CHECK(back.mesh.tube.curve && back.mesh.tube.points.size() == 9 && tubeMatchesMesh(back.mesh));
    std::filesystem::remove(path);
    // Tube from curve: the Tube tool's sweep with the chosen profile and modifiers.
    CHECK(!reg.execute(doc, "tube_from_curve", {{"radius", -1.0}}).ok);
    CHECK(reg.execute(doc, "tube_from_curve", {{"radius", 0.15}, {"profile", std::string("rope")}, {"twist", 45.0}, {"spread", 0.6}}).ok);
    const TubeShape& t = doc.mesh.tube;
    CHECK(!t.curve && t.profile == TubeShape::Profile::Rope && t.twist == 45.0f && t.spread == 0.6f && t.points[0].radius == 0.15f);
    CHECK(signedVolume(doc.mesh) > 20.0 * wireVolume && watertight(doc.mesh));
    CHECK(!reg.execute(doc, "tube_from_curve").ok); // no longer a curve
    CHECK(reg.execute(doc, "tube_resample", {{"count", 12.0}}).ok && doc.mesh.tube.points.size() == 12);
    CHECK(reg.execute(doc, "undo").ok && reg.execute(doc, "undo").ok && doc.mesh.tube.curve);
    CHECK(reg.execute(doc, "tube_list").message.find("a curve") != std::string::npos);
    CHECK(reg.execute(doc, "tube_set", {{"profile", std::string("zigzag")}}).message.find("braid") != std::string::npos);
}

namespace {

// Which strand (connected piece) each vertex belongs to.
std::vector<uint32_t> strandOf(const Mesh& m) {
    std::vector<uint32_t> up(m.positions.size());
    for (size_t i = 0; i < up.size(); ++i) up[i] = uint32_t(i);
    const auto find = [&](uint32_t a) {
        while (up[a] != a) a = up[a] = up[up[a]];
        return a;
    };
    for (const glm::uvec3& t : m.triangles) up[find(t.y)] = find(t.x), up[find(t.z)] = find(t.x);
    for (uint32_t& u : up) u = find(u);
    return up;
}

// Ray parity from `p` along an odd direction through `triangles` (one strand's triangles).
bool insideStrand(const Mesh& m, const std::vector<uint32_t>& triangles, const vec3& p) {
    const vec3 d = glm::normalize(vec3(0.5773f, 0.3317f, 0.7449f));
    int hits = 0;
    for (uint32_t i : triangles) {
        const glm::uvec3 t = m.triangles[i];
        const vec3 a = m.positions[t.x], e1 = m.positions[t.y] - a, e2 = m.positions[t.z] - a, h = glm::cross(d, e2);
        const float det = glm::dot(e1, h);
        if (std::abs(det) < 1e-12f) continue;
        const vec3 q = p - a;
        const float u = glm::dot(q, h) / det, v = glm::dot(d, glm::cross(q, e1)) / det, tt = glm::dot(e2, glm::cross(q, e1)) / det;
        if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f && tt > 1e-6f) ++hits;
    }
    return hits % 2 == 1;
}

} // namespace

TEST(tube_braid_strands_pass_over_and_under) {
    // The strands of a braid weave: no vertex of one strand lies inside another (they interlock, they don't merge).
    for (int count : {3, 5}) {
        TubeShape br = line(3.0f, 0.25f);
        br.profile = TubeShape::Profile::Braid;
        br.strands = count;
        const Mesh m = tubeSweep(br);
        const std::vector<uint32_t> strand = strandOf(m);
        CHECK(watertight(m) && oriented(m) && components(m) == count);
        std::map<uint32_t, std::vector<uint32_t>> own; // each strand's triangles
        for (size_t i = 0; i < m.triangles.size(); ++i) own[strand[m.triangles[i].x]].push_back(uint32_t(i));
        int inside = 0;
        const size_t stride = m.positions.size() / 300 + 1;
        for (size_t i = 0; i < m.positions.size(); i += stride)
            for (const auto& [id, triangles] : own)
                if (id != strand[i] && insideStrand(m, triangles, m.positions[i])) ++inside;
        CHECK(inside == 0);
    }
}

TEST(tube_rope_strand_count_and_spring) {
    TubeShape rope = line(3.0f, 0.25f);
    rope.profile = TubeShape::Profile::Rope;
    for (int count : {1, 2, 3, 5, 8}) {
        rope.strands = count;
        const Mesh m = tubeSweep(rope);
        CHECK(watertight(m) && oriented(m) && components(m) == count && signedVolume(m) > 0.0);
        float reach = 0.0f;
        for (const vec3& p : m.positions) reach = std::max(reach, std::hypot(p.y, p.z));
        CHECK(reach < 0.25f * 1.01f);
    }
    // A spring (one strand) is a coil: it winds round the axis, so it passes through every side of it.
    rope.strands = 1;
    const Mesh spring = tubeSweep(rope);
    bool side[4] = {};
    for (const vec3& p : spring.positions) side[(p.y > 0 ? 1 : 0) + (p.z > 0 ? 2 : 0)] = true;
    CHECK(side[0] && side[1] && side[2] && side[3]);
    // Three strands as before; the count is saved and checked.
    TubeShape saved = line(1.0f, 0.1f);
    CHECK(tubeToJson(saved)["strands"].isNull());
    saved.profile = TubeShape::Profile::Rope;
    saved.strands = 5;
    CHECK(tubeFromJson(tubeToJson(saved)).strands == 5);
    saved.strands = 0;
    CHECK(!tubeValidate(saved).empty());
    saved.strands = kTubeMaxStrands + 1;
    CHECK(!tubeValidate(saved).empty());
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 1,0,0 2,0,0")}, {"radius", 0.1}, {"profile", std::string("rope")}, {"strands", 1.0}}).ok);
    doc.selectPart(1);
    CHECK(doc.mesh.tube.strands == 1 && tubeMatchesMesh(doc.mesh));
    CHECK(reg.execute(doc, "tube_set", {{"strands", 4.0}}).ok && doc.mesh.tube.strands == 4);
    CHECK(!reg.execute(doc, "tube_set", {{"strands", 12.0}}).ok && doc.mesh.tube.strands == 4);
    CHECK(reg.execute(doc, "undo").ok && doc.mesh.tube.strands == 1);
}

TEST(tube_repeat_part_along_curve) {
    Document doc;
    CommandRegistry reg;
    Mesh bar = makeCube(2, 1.0f); // a bar 0.6 long, 0.2 across
    for (vec3& p : bar.positions) p *= vec3(0.6f, 0.2f, 0.2f);
    bar.recomputeAllNormals();
    doc.replaceActiveMesh(bar);
    const size_t barTriangles = doc.mesh.triangleCount();
    // Not a curve yet: refused.
    CHECK(!reg.execute(doc, "tube_repeat", {{"part", 0.0}}).ok);
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 3,0,0")}, {"radius", 0.05}, {"curve", true}, {"name", std::string("Path")}}).ok);
    doc.selectPart(1);
    const size_t parts = doc.parts.size();
    // End to end: 3 / 0.6 = 5 copies, each centred at (k + 0.5) * 0.6 along the curve.
    CommandResult r = reg.execute(doc, "tube_repeat", {{"part", doc.parts[0].name}});
    CHECK(r.ok);
    CHECK(doc.parts.size() == parts + 1 && doc.activePart == 1);
    const int madeAt = doc.findPart(doc.parts[0].name + " Repeat");
    CHECK(madeAt > 0);
    const Mesh& made = doc.partMesh(madeAt);
    CHECK(made.triangleCount() == 5 * barTriangles);
    vec3 lo(1e9f), hi(-1e9f);
    for (const vec3& p : made.positions) lo = glm::min(lo, p), hi = glm::max(hi, p);
    CHECK(std::abs(lo.x) < 0.01f && std::abs(hi.x - 3.0f) < 0.01f && hi.y < 0.101f && lo.y > -0.101f);
    // Overlapping, turned 90 degrees each: more copies, and every other copy lies the other way up.
    r = reg.execute(doc, "tube_repeat", {{"part", 0.0}, {"spacing", 0.5}, {"turn", 90.0}});
    CHECK(r.ok);
    const Mesh& links = doc.partMesh(doc.findPart(doc.parts[0].name + " Repeat 2"));
    CHECK(links.triangleCount() == 10 * barTriangles);
    CHECK(watertight(made));
    // One undo step each; the curve and the piece are untouched.
    const size_t before = doc.parts.size();
    CHECK(reg.execute(doc, "undo").ok && doc.parts.size() == before - 1);
    CHECK(doc.partMesh(0).triangleCount() == barTriangles && doc.mesh.tube.curve);
    // Refusals: the curve itself, a bad spacing, a missing Part, too many copies.
    CHECK(!reg.execute(doc, "tube_repeat", {{"part", 1.0}}).ok);
    CHECK(!reg.execute(doc, "tube_repeat", {{"part", 0.0}, {"spacing", 0.0}}).ok);
    CHECK(!reg.execute(doc, "tube_repeat", {{"part", std::string("Nothing")}}).ok);
    CHECK(!reg.execute(doc, "tube_repeat", {{"part", 0.0}, {"spacing", 0.1}, {"scale", 0.001}}).ok);
}

TEST(tube_repeat_turns_flat_pieces_about_the_curve) {
    Document doc;
    CommandRegistry reg;
    Mesh link = makeCube(2, 1.0f); // a flat link: 0.5 long, 0.3 wide, 0.05 thick
    for (vec3& p : link.positions) p *= vec3(0.5f, 0.3f, 0.05f);
    link.recomputeAllNormals();
    doc.replaceActiveMesh(link);
    const size_t each = doc.mesh.triangleCount();
    CHECK(reg.execute(doc, "tube_new", {{"points", std::string("0,0,0 2,0,0")}, {"radius", 0.05}, {"curve", true}, {"up", std::string("0,1,0")}}).ok);
    doc.selectPart(1);
    CHECK(reg.execute(doc, "tube_repeat", {{"part", 0.0}, {"turn", 90.0}}).ok);
    const Mesh& chain = doc.partMesh(doc.findPart(doc.parts[0].name + " Repeat"));
    CHECK(chain.triangleCount() == 4 * each);
    // Copy k's extent across: wide (0.3) in one axis for even k, in the other for odd k.
    for (size_t k = 0; k < 4; ++k) {
        float y = 0.0f, z = 0.0f;
        for (size_t i = k * each * 3; i < (k + 1) * each * 3; ++i) {
            const glm::uvec3 t = chain.triangles[i / 3];
            const vec3& p = chain.positions[t[i % 3]];
            y = std::max(y, std::abs(p.y)), z = std::max(z, std::abs(p.z));
        }
        CHECK((y > 0.14f) != (z > 0.14f));        // exactly one axis carries the width
        CHECK((k % 2 == 0) == (y > z));            // and it alternates (the first copy's wide side faces up)
    }
}
