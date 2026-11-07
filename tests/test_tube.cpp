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
