// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh (quad retopology) and ProjectAll (detail projection): the result is a closed 2-manifold made of
// quads, near the target count and the sculpt's surface (Hausdorff distance); Follow Face Groups, symmetry, open
// surfaces, vertex paint density, mask / colour transfer, cancel; projecting detail back onto a divided retopo
// brings the error down and respects the mask; both commands are one undo step.
// SF_RETOPO_BENCH=1 also times both on a 1M+ triangle sculpt.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Projection.h"
#include "core/QRemesh.h"
#include "core/SurfaceQuery.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <set>

using namespace sf;

namespace {

float diagonal(const Mesh& m) { return glm::length(m.bounds().extent()); }

// A sphere with a few brush strokes on the front: ridges, a crease and a dent.
Document sculptedSphere(int subdivisions = 6) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", double(subdivisions)}});
    auto stroke = [&](const char* points, const char* brush, double radius, bool invert = false) {
        const CommandResult r = reg.execute(doc, "stroke", {{"points", std::string(points)}, {"brush", std::string(brush)},
                                                            {"radius", radius}, {"strength", 1.0}, {"invert", invert}});
        CHECK(r.ok);
    };
    stroke("-0.6,0.3,2 0,0.5,2 0.6,0.3,2", "Standard", 0.18);
    stroke("-0.5,-0.3,2 0.5,-0.4,2", "Crease", 0.12);
    stroke("0.1,0,2 0.2,0.1,2", "Clay", 0.25, true);
    doc.undo.clear();
    return doc;
}

void checkQuadMesh(const Mesh& m, bool closed, const char* what) {
    const TopologyReport t = topologyReport(m);
    std::printf("  %s: %zu faces, %.1f%% quads, %zu boundary / %zu non-manifold edges, %zu non-manifold vertices\n", what,
                t.faces, 100.0f * t.quadFraction(), t.boundaryEdges, t.nonManifoldEdges, t.nonManifoldVertices);
    CHECK(t.manifold());
    CHECK(t.quadFraction() >= 0.95f); // brief: mostly quads (the method gives all quads)
    if (closed) CHECK(t.boundaryEdges == 0);
    CHECK(m.quadCount() * 2 == m.triangleCount() || t.triangles > 0);
}

} // namespace

TEST(qremesh_sphere_is_closed_quads_near_target) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    const Mesh source = doc.mesh;
    QRemeshOptions o;
    o.targetQuads = 2000;
    Mesh out;
    std::string err;
    QRemeshStats st;
    CHECK(qremesh(source, o, out, err, {}, &st));
    checkQuadMesh(out, true, "sphere");
    CHECK(st.quads >= 1700 && st.quads <= 2300);
    const SurfaceDeviation d = measureDeviation(out, source);
    std::printf("  sphere: %zu quads (target 2000), Hausdorff %.3f%% of the size, %.2f s\n", st.quads,
                100.0f * d.max / diagonal(source), st.seconds);
    CHECK(d.max < 0.01f * diagonal(source));
    // Same seed, same result.
    Mesh again;
    CHECK(qremesh(source, o, again, err));
    CHECK(again.positions == out.positions && again.triangles == out.triangles);
}

TEST(qremesh_sculpt_target_counts) {
    Document doc = sculptedSphere(5);
    for (int target : {500, 3000, 8000}) {
        QRemeshOptions o;
        o.targetQuads = target;
        Mesh out;
        std::string err;
        QRemeshStats st;
        CHECK(qremesh(doc.mesh, o, out, err, {}, &st));
        checkQuadMesh(out, true, "sculpt");
        const double ratio = double(st.quads) / double(target);
        std::printf("  target %d -> %zu quads (%.0f%%)\n", target, st.quads, ratio * 100.0);
        CHECK(ratio > 0.8 && ratio < 1.2);
    }
}

TEST(qremesh_keep_groups_follows_borders) {
    CommandRegistry reg;
    Document doc;
    doc.setMesh(makeCube(24));
    CHECK(reg.execute(doc, "facegroup_auto_by_normals", {{"angle", 40.0}}).ok);
    QRemeshOptions o;
    o.targetQuads = 1500;
    o.keepGroups = true;
    o.detectEdges = false; // the borders alone must hold the edges
    Mesh out;
    std::string err;
    CHECK(qremesh(doc.mesh, o, out, err));
    checkQuadMesh(out, true, "cube, Follow Face Groups");
    // Six groups, each quad entirely in one (halves agree), and the cube's sharp edges kept.
    CHECK(out.hasFaceGroups());
    std::set<uint32_t> groups(out.facegroups.begin(), out.facegroups.end());
    CHECK(groups.size() == 6);
    for (size_t t = 0; t + 1 < out.triangleCount(); t += 2) CHECK(out.facegroups[t] == out.facegroups[t + 1]);
    const SurfaceDeviation d = measureDeviation(out, doc.mesh);
    std::printf("  cube with Follow Face Groups: Hausdorff %.3f%% of the size\n", 100.0f * d.max / diagonal(doc.mesh));
    CHECK(d.max < 0.005f * diagonal(doc.mesh));
    // Without groups or edge detection the corners round off.
    o.keepGroups = false;
    Mesh soft;
    CHECK(qremesh(doc.mesh, o, soft, err));
    CHECK(measureDeviation(soft, doc.mesh).max > d.max * 2.0f);
    // Keep Hard Edges alone also keeps them.
    o.detectEdges = true;
    Mesh hard;
    CHECK(qremesh(doc.mesh, o, hard, err));
    CHECK(measureDeviation(hard, doc.mesh).max < 0.005f * diagonal(doc.mesh));
}

TEST(qremesh_symmetry_mirrors_the_topology) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    QRemeshOptions o;
    o.targetQuads = 1600;
    o.symmetryAxis = 0;
    Mesh out;
    std::string err;
    CHECK(qremesh(doc.mesh, o, out, err));
    checkQuadMesh(out, true, "sphere, X symmetry");
    // Every vertex has a mirror twin (exactly: the halves are copies).
    TriangleBVH bvh; // nearest vertex via degenerate triangles
    std::vector<glm::uvec3> points(out.vertexCount());
    for (uint32_t v = 0; v < out.vertexCount(); ++v) points[v] = {v, v, v};
    bvh.build(out.positions, points);
    size_t unmatched = 0;
    for (const glm::vec3& p : out.positions) {
        SurfaceHit hit;
        if (!bvh.closestPoint({-p.x, p.y, p.z}, 1.0f, hit) || hit.distance > 1e-5f) ++unmatched;
    }
    CHECK(unmatched == 0);
}

TEST(qremesh_open_surface_keeps_its_outline) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_plane", {});
    CHECK(doc.mesh.triangleCount() > 0);
    QRemeshOptions o;
    o.targetQuads = 400;
    Mesh out;
    std::string err;
    CHECK(qremesh(doc.mesh, o, out, err));
    checkQuadMesh(out, false, "plane");
    const TopologyReport t = topologyReport(out);
    CHECK(t.boundaryEdges > 40);
    const AABB a = doc.mesh.bounds(), b = out.bounds();
    CHECK(glm::length(a.min - b.min) < 0.01f * diagonal(doc.mesh));
    CHECK(glm::length(a.max - b.max) < 0.01f * diagonal(doc.mesh));
}

TEST(qremesh_transfers_mask_and_vertexpaint) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    Mesh& m = doc.mesh;
    m.ensureMask();
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) {
        m.mask[v] = m.positions[v].y > 0.0f ? 1.0f : 0.0f;
        m.colors[v] = m.positions[v].x > 0.0f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    }
    QRemeshOptions o;
    o.targetQuads = 1500;
    Mesh out;
    std::string err;
    CHECK(qremesh(m, o, out, err));
    CHECK(out.hasMask() && out.hasColors());
    size_t wrong = 0;
    for (uint32_t v = 0; v < out.vertexCount(); ++v) {
        const glm::vec3 p = out.positions[v];
        if (std::abs(p.y) > 0.1f && (out.mask[v] > 0.5f) != (p.y > 0.0f)) ++wrong;
        if (std::abs(p.x) > 0.1f && (out.colors[v].x > 0.5f) != (p.x > 0.0f)) ++wrong;
    }
    CHECK(wrong == 0);
}

TEST(qremesh_vertexpaint_density) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    Mesh& m = doc.mesh;
    m.ensureColors();
    for (uint32_t v = 0; v < m.vertexCount(); ++v) m.colors[v] = m.positions[v].y > 0.0f ? glm::vec3(1, 0, 0) : glm::vec3(1);
    QRemeshOptions o;
    o.targetQuads = 2000;
    o.adaptive = 0.0f;
    o.useVertexPaint = true;
    Mesh out;
    std::string err;
    CHECK(qremesh(m, o, out, err));
    checkQuadMesh(out, true, "sphere, vertexpaint density");
    size_t top = 0, bottom = 0;
    for (size_t t = 0; t < out.triangleCount(); t += 2) {
        const glm::uvec3 tri = out.triangles[t];
        const float y = out.positions[tri.x].y + out.positions[tri.y].y + out.positions[tri.z].y;
        ++(y > 0.0f ? top : bottom);
    }
    std::printf("  red top half: %zu quads, white bottom half: %zu\n", top, bottom);
    CHECK(top > bottom * 2);
}

TEST(qremesh_adaptive_size_varies_quads) {
    Document doc = sculptedSphere(5);
    auto spread = [&](float adaptive) {
        QRemeshOptions o;
        o.targetQuads = 3000;
        o.adaptive = adaptive;
        Mesh out;
        std::string err;
        CHECK(qremesh(doc.mesh, o, out, err));
        double s = 0.0, s2 = 0.0;
        size_t n = 0;
        for (const glm::uvec3& t : out.triangles) {
            const double l = glm::length(out.positions[t.x] - out.positions[t.y]);
            s += l, s2 += l * l, ++n;
        }
        const double mean = s / double(n);
        return std::sqrt(std::max(0.0, s2 / double(n) - mean * mean)) / mean; // coefficient of variation
    };
    const double even = spread(0.0f), adaptive = spread(1.0f);
    std::printf("  edge length variation: %.2f even, %.2f adaptive\n", even, adaptive);
    CHECK(adaptive > even * 1.2);
}

TEST(qremesh_cancel_and_errors) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 4.0}});
    Mesh out;
    std::string err;
    int calls = 0;
    CHECK(!qremesh(doc.mesh, {}, out, err, [&](float) { return ++calls < 3; }));
    CHECK(err.find("cancelled") != std::string::npos);
    CHECK(!qremesh(Mesh{}, {}, out, err));
}

TEST(qremesh_command_keeps_original_and_undoes) {
    CommandRegistry reg;
    Document doc = sculptedSphere(5);
    const size_t before = doc.mesh.triangleCount();
    const CommandResult r = reg.execute(doc, "qremesh", {{"target", 1200.0}, {"keep_original", true}, {"adaptive", 0.3}});
    CHECK(r.ok);
    CHECK(r.message.find("quads") != std::string::npos);
    CHECK(doc.partCount() == 2);
    CHECK(doc.activePart == 0);
    CHECK(!doc.parts[1].visible);
    CHECK(doc.parts[1].name == doc.parts[0].name + " Original");
    CHECK(doc.partMesh(1).triangleCount() == before);
    CHECK(doc.mesh.quadCount() * 2 == doc.mesh.triangleCount());
    CHECK(doc.undo.canUndo());
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.triangleCount() == before);
    CHECK(reg.execute(doc, "qremesh", {{"target", 800.0}, {"symmetry", std::string("x")}, {"keep_groups", true}}).ok);
    CHECK(!reg.execute(doc, "project_all", {{"source", std::string("Nope")}}).ok);
}

TEST(project_all_brings_detail_back) {
    CommandRegistry reg;
    Document doc = sculptedSphere(6);
    doc.parts[0].name = "Sculpt";
    // Paint the source so vertex paint comes across too.
    doc.mesh.ensureColors();
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v)
        doc.mesh.colors[v] = doc.mesh.positions[v].y > 0.0f ? glm::vec3(0.8f, 0.1f, 0.1f) : glm::vec3(0.1f, 0.2f, 0.8f);
    const Mesh source = doc.mesh;
    // Retopo a copy, divide it twice, then project from the original.
    doc.addPart(doc.mesh, "Retopo");
    CHECK(reg.execute(doc, "qremesh", {{"target", 2000.0}}).ok);
    CHECK(reg.execute(doc, "subdivide", {{"levels", 2.0}}).ok);
    const Mesh divided = doc.mesh;
    // Both ways: the divided retopo's vertices to the sculpt, and the sculpt's (its detail) to the retopo.
    const SurfaceDeviation before = measureDeviation(divided, source);
    const CommandResult r = reg.execute(doc, "project_all", {{"distance", 0.05}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    const SurfaceDeviation after = measureDeviation(doc.mesh, source);
    std::printf("  divided retopo vs sculpt: mean %.5f max %.5f before ProjectAll, mean %.5f max %.5f after\n",
                before.mean, before.max, after.mean, after.max);
    CHECK(after.mean < before.mean * 0.25f);
    // The worst single vertex: 0.37x on Linux and macOS, 0.50x with MSVC (whose QRemesh output differs a little).
    CHECK(after.max < before.max * 0.6f);
    // Colours came across.
    CHECK(doc.mesh.hasColors());
    size_t wrong = 0;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        const float y = doc.mesh.positions[v].y;
        if (std::abs(y) > 0.05f && (doc.mesh.colors[v].x > 0.5f) != (y > 0.0f)) ++wrong;
    }
    CHECK(wrong == 0);
    // One undo step back to the divided retopo.
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.positions == divided.positions);
    CHECK(doc.mesh.colors == divided.colors);
}

TEST(project_all_respects_mask_and_options) {
    CommandRegistry reg;
    Document doc = sculptedSphere(5);
    doc.addPart(doc.mesh, "Retopo");
    CHECK(reg.execute(doc, "qremesh", {{"target", 1500.0}}).ok);
    CHECK(reg.execute(doc, "subdivide", {}).ok);
    const Mesh divided = doc.mesh;
    // Mask the top half: it must not move.
    doc.mesh.ensureMask();
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) doc.mesh.mask[v] = doc.mesh.positions[v].y > 0.0f ? 1.0f : 0.0f;
    CHECK(reg.execute(doc, "project_all", {{"vertexpaint", false}}).ok);
    size_t movedMasked = 0, movedFree = 0;
    for (uint32_t v = 0; v < doc.mesh.vertexCount(); ++v) {
        const bool moved = doc.mesh.positions[v] != divided.positions[v];
        if (doc.mesh.mask[v] > 0.5f) movedMasked += moved;
        else movedFree += moved;
    }
    CHECK(movedMasked == 0);
    CHECK(movedFree > doc.mesh.vertexCount() / 4);
    // A tiny Dist finds nothing for vertices far from the sculpt; geometry off moves nothing.
    doc.mesh.clearMask();
    std::vector<glm::vec3> positions;
    ProjectStats st;
    std::string err;
    ProjectOptions o;
    o.geometry = false;
    std::vector<const Mesh*> sources{&doc.partMesh(0)};
    CHECK(computeProjection(divided, sources, o, positions, nullptr, st, err));
    CHECK(positions == divided.positions);
    // Nothing visible to project from.
    doc.parts[0].visible = false;
    CHECK(!reg.execute(doc, "project_all", {}).ok);
}

TEST(retopo_bench_1m_triangles) {
    if (!std::getenv("SF_RETOPO_BENCH")) return;
    CommandRegistry reg;
    Document doc = sculptedSphere(6);
    CHECK(reg.execute(doc, "subdivide", {{"levels", 2.0}}).ok); // 1.3M triangles
    std::printf("  bench: sculpt with %zu triangles\n", doc.mesh.triangleCount());
    for (int target : {5000, 20000, 100000}) {
        QRemeshOptions o;
        o.targetQuads = target;
        Mesh out;
        std::string err;
        QRemeshStats st;
        CHECK(qremesh(doc.mesh, o, out, err, {}, &st));
        const TopologyReport t = topologyReport(out);
        std::printf("  bench: QRemesh target %d -> %zu quads (%.1f%% quads, manifold %d) in %.2f s\n", target, st.quads,
                    100.0f * t.quadFraction(), int(t.manifold()), st.seconds);
    }
    // ProjectAll onto a 1.3M-triangle target from the 1.3M-triangle sculpt.
    doc.addPart(doc.mesh, "Retopo");
    CHECK(reg.execute(doc, "qremesh", {{"target", 5000.0}}).ok);
    CHECK(reg.execute(doc, "subdivide", {{"levels", 4.0}}).ok);
    const auto t0 = std::chrono::steady_clock::now();
    const CommandResult r = reg.execute(doc, "project_all", {});
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  bench: ProjectAll onto %zu triangles: %.2f s (with undo and BVH) - %s\n", doc.mesh.triangleCount(), s,
                r.message.c_str());
}
