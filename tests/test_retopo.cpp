// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh (quad retopology) and ProjectAll (detail projection): the result is a closed 2-manifold made of
// quads, near the target count and the sculpt's surface (Hausdorff distance); Follow Face Groups, symmetry, open
// surfaces, vertex paint density, mask / colour transfer, cancel; projecting detail back onto a divided retopo
// brings the error down and respects the mask; both commands are one undo step.
// SF_RETOPO_BENCH=1 also times both on a 1M+ triangle sculpt; SF_PROJECT_BENCH=<.sfproj> times Project Detail old vs new.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/Projection.h"
#include "core/QRemesh.h"
#include "core/SurfaceQuery.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <set>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#endif

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

namespace {

// A flat grid of triangles in the plane x = `x` covering y, z in [-0.5, 1.5], facing +x (or -x).
Mesh wallPlate(float x, bool facePlusX, int n = 20) {
    Mesh m;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) m.positions.push_back({x, -0.5f + 2.0f * float(i) / float(n), -0.5f + 2.0f * float(j) / float(n)});
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const uint32_t a = uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
            if (facePlusX) m.triangles.push_back({a, b, c}), m.triangles.push_back({b, d, c});
            else m.triangles.push_back({a, c, b}), m.triangles.push_back({b, c, d});
        }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

} // namespace

// A crevice: two walls 0.055 apart facing each other. The target is a sheet facing +x, 0.03 in front of the left wall (it
// sits where the sculpt's wall was before the detail came in) and only 0.025 from the right wall, which faces away from it.
TEST(project_all_does_not_jump_across_a_crevice) {
    const Mesh left = wallPlate(-0.05f, true), right = wallPlate(0.005f, false);
    Mesh target = wallPlate(-0.02f, true, 10);
    for (auto& p : target.positions) p.y = (p.y + 0.5f) * 0.5f, p.z = (p.z + 0.5f) * 0.5f; // inside the walls' extent
    target.recomputeAllNormals();
    std::vector<const Mesh*> sources{&left, &right};
    auto run = [&](bool mean, float facing) {
        ProjectOptions o;
        o.distance = 0.1f;
        o.mean = mean;
        o.facing = facing;
        o.vertexpaint = false;
        std::vector<glm::vec3> positions;
        ProjectStats st;
        std::string err;
        CHECK(computeProjection(target, sources, o, positions, nullptr, st, err));
        CHECK(st.projected == target.vertexCount());
        float lo = 1e9f, hi = -1e9f;
        for (const glm::vec3& p : positions) lo = std::min(lo, p.x), hi = std::max(hi, p.x);
        return std::make_pair(lo, hi);
    };
    // Without the facing test the nearest hit is the right wall (or, with Mean, halfway to it): the sheet crosses the gap.
    const auto old = run(false, -1.0f);
    CHECK(std::abs(old.first - 0.005f) < 1e-4f && std::abs(old.second - 0.005f) < 1e-4f);
    const auto oldMean = run(true, -1.0f);
    CHECK(std::abs(oldMean.first - (-0.0225f)) < 1e-4f);
    // With it every vertex lands on the wall it belongs to, Mean included (one accepted hit: nothing to average).
    for (bool mean : {false, true})
        for (float facing : {0.0f, 0.2f, 0.5f}) {
            const auto r = run(mean, facing);
            CHECK(std::abs(r.first - (-0.05f)) < 1e-4f && std::abs(r.second - (-0.05f)) < 1e-4f);
        }
}

// The facing test also holds for the nearest-point fallback (no hit along the normal) and keeps a hit on a tilted wall.
TEST(project_all_facing_filters_the_closest_point_fallback) {
    const Mesh left = wallPlate(-0.05f, true), right = wallPlate(0.005f, false);
    TriangleBVH bvh;
    std::vector<glm::vec3> P = left.positions;
    std::vector<glm::uvec3> T = left.triangles;
    const uint32_t off = uint32_t(P.size());
    P.insert(P.end(), right.positions.begin(), right.positions.end());
    for (const auto& t : right.triangles) T.push_back(t + glm::uvec3(off));
    bvh.build(std::move(P), std::move(T));
    SurfaceHit h;
    const FacingFilter f{{1.0f, 0.0f, 0.0f}, 0.2f};
    CHECK(bvh.closestPoint({-0.02f, 0.5f, 0.5f}, 0.1f, h));
    CHECK(std::abs(h.position.x - 0.005f) < 1e-4f);
    CHECK(bvh.closestPoint({-0.02f, 0.5f, 0.5f}, 0.1f, h, &f));
    CHECK(std::abs(h.position.x - (-0.05f)) < 1e-4f);
    // A ray from the same point along +x skips the back-facing right wall and finds nothing further.
    CHECK(bvh.raycast({-0.02f, 0.5f, 0.5f}, {1, 0, 0}, 0.1f, h));
    CHECK(!bvh.raycast({-0.02f, 0.5f, 0.5f}, {1, 0, 0}, 0.1f, h, &f));
    CHECK(bvh.raycast({-0.02f, 0.5f, 0.5f}, {-1, 0, 0}, 0.1f, h, &f));
}

namespace {

// A square grid over x, y in [-half, half] lifted to z = k (x^2 + y^2): a bowl (a concave pocket) facing +z.
Mesh bowl(float k, float half, int n) {
    Mesh m;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float x = -half + 2.0f * half * float(i) / float(n), y = -half + 2.0f * half * float(j) / float(n);
            m.positions.push_back({x, y, k * (x * x + y * y)});
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const uint32_t a = uint32_t(j * (n + 1) + i), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
            m.triangles.push_back({a, b, c});
            m.triangles.push_back({b, d, c});
        }
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

float triArea(const std::vector<glm::vec3>& p, const glm::uvec3& t) {
    return 0.5f * glm::length(glm::cross(p[t.y] - p[t.x], p[t.z] - p[t.x]));
}

} // namespace

// A concave pocket (a palate) projected from a source that is off it (a dome inside the pocket): the rays from the walls
// miss or hit the far side, and the nearest point of many vertices is the same spot. Taking it pinches the pocket to a
// star (triangles and edges shrink to nothing); a vertex with no surface its way follows its neighbours instead.
TEST(project_all_does_not_pinch_a_concave_pocket) {
    const Mesh source = bowl(-0.5f, 1.0f, 24);
    const Mesh target = bowl(1.5f, 1.0f, 40);
    std::vector<const Mesh*> sources{&source};
    auto worst = [&](const ProjectOptions& o) {
        std::vector<glm::vec3> positions;
        ProjectStats st;
        std::string err;
        CHECK(computeProjection(target, sources, o, positions, nullptr, st, err));
        float area = 1e9f, edge = 1e9f;
        for (const glm::uvec3& t : target.triangles) {
            area = std::min(area, triArea(positions, t) / triArea(target.positions, t));
            for (int k = 0; k < 3; ++k) {
                const uint32_t a = t[k], b = t[(k + 1) % 3];
                edge = std::min(edge, glm::length(positions[a] - positions[b]) / glm::length(target.positions[a] - target.positions[b]));
            }
        }
        return std::make_pair(area, edge);
    };
    ProjectOptions old; // before: any hit, any nearest point
    old.distance = 0.3f;
    old.vertexpaint = false;
    old.facing = -1.0f;
    old.fallbackCos = -1.0f;
    old.outlier = 0.0f;
    const auto before = worst(old);
    ProjectOptions now = old;
    now.facing = ProjectOptions().facing;
    now.fallbackCos = ProjectOptions().fallbackCos;
    now.outlier = ProjectOptions().outlier;
    const auto after = worst(now);
    std::printf("  smallest triangle area / edge length vs the target: %.3f / %.3f before, %.3f / %.3f after\n", before.first,
                before.second, after.first, after.second);
    CHECK(before.first < 0.1f || before.second < 0.1f); // the case is real: the old rule pinches it
    CHECK(after.first >= 0.1f);
    CHECK(after.second >= 0.1f);
}

// A small fin standing in front of a plate: the rays of the few vertices under it hit the fin (a front-facing hit, a
// long way nearer than the plate their neighbours reach) and the sheet tears into a spike. Those vertices follow their ring.
TEST(project_all_pulls_back_vertices_that_reached_another_fin) {
    const Mesh plate = wallPlate(-0.3f, true);
    Mesh fin = wallPlate(-0.1f, true, 2);
    for (auto& p : fin.positions) p.y = 0.47f + (p.y + 0.5f) * 0.055f, p.z = 0.47f + (p.z + 0.5f) * 0.055f; // 0.47 .. 0.58: four vertices
    Mesh target = wallPlate(0.0f, true, 20);
    for (auto& p : target.positions) p.y = (p.y + 0.5f) * 0.5f, p.z = (p.z + 0.5f) * 0.5f; // 0 .. 1, an edge is 0.05
    target.recomputeAllNormals();
    std::vector<const Mesh*> sources{&plate, &fin};
    auto reach = [&](float outlier) {
        ProjectOptions o;
        o.distance = 0.3f; // 0.42: reaches the plate
        o.vertexpaint = false;
        o.outlier = outlier;
        std::vector<glm::vec3> positions;
        ProjectStats st;
        std::string err;
        CHECK(computeProjection(target, sources, o, positions, nullptr, st, err));
        size_t onFin = 0, onPlate = 0;
        for (const glm::vec3& p : positions) onFin += std::abs(p.x - (-0.1f)) < 1e-4f, onPlate += std::abs(p.x - (-0.3f)) < 1e-4f;
        CHECK(onFin + onPlate == positions.size());
        return onFin;
    };
    CHECK(reach(0.0f) == 4); // the four vertices under the fin, as before
    CHECK(reach(2.0f) == 0); // every one of them back on the plate
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

// The geometric invariant of Project Detail on a dense curved mesh: where the target and the source agree (no crevice, no
// pocket) the new rules change nothing, and nowhere do they make the surface rougher.
TEST(project_all_leaves_a_clean_dense_case_unchanged) {
    CommandRegistry reg;
    Document doc = sculptedSphere(6);
    doc.parts[0].name = "Sculpt";
    const Mesh source = doc.mesh;
    doc.addPart(doc.mesh, "Retopo");
    CHECK(reg.execute(doc, "qremesh", {{"target", 2000.0}}).ok);
    CHECK(reg.execute(doc, "subdivide", {{"levels", 2.0}}).ok);
    const Mesh divided = doc.mesh;
    std::vector<const Mesh*> sources{&source};
    ProjectOptions now, old;
    now.vertexpaint = old.vertexpaint = false;
    now.distance = old.distance = 0.05f;
    old.facing = -1.0f;
    old.fallbackCos = -1.0f;
    old.outlier = 0.0f;
    std::vector<glm::vec3> a, b;
    ProjectStats sa, sb;
    std::string err;
    CHECK(computeProjection(divided, sources, now, a, nullptr, sa, err));
    CHECK(computeProjection(divided, sources, old, b, nullptr, sb, err));
    const float tol = 1e-6f * diagonal(divided);
    size_t differ = 0;
    for (size_t v = 0; v < a.size(); ++v) differ += glm::length(a[v] - b[v]) > tol;
    // Spikes: a vertex whose offset differs from its ring's mean offset by more than 4 mean edge lengths.
    auto spikes = [&](const std::vector<glm::vec3>& p) {
        size_t n = 0;
        for (uint32_t v = 0; v < divided.vertexCount(); ++v) {
            glm::vec3 mean(0.0f);
            float edge = 0.0f;
            const auto ring = divided.neighbors(v);
            for (uint32_t u : ring) mean += p[u] - divided.positions[u], edge += glm::length(divided.positions[u] - divided.positions[v]);
            if (ring.empty()) continue;
            mean /= float(ring.size());
            edge /= float(ring.size());
            n += glm::length((p[v] - divided.positions[v]) - mean) > 4.0f * edge;
        }
        return n;
    };
    const size_t spikesNow = spikes(a), spikesOld = spikes(b);
    std::printf("  %zu of %zu vertices differ from the old result, spiked vertices %zu now / %zu before, missed %zu / %zu\n", differ,
                a.size(), spikesNow, spikesOld, sa.missed, sb.missed);
    CHECK(differ * 200 <= a.size()); // under 0.5 %: the retopo is far from any crevice here
    CHECK(spikesNow <= spikesOld);
    CHECK(sa.missed <= sb.missed + a.size() / 200);
}

// SF_PROJECT_BENCH=<project with a dense sculpt> [SF_PROJECT_BENCH_MODE=old|new|both]: the 11-27 repro (voxel remesh 300 of
// a copy, divided twice) timed with the old rules (facing, outlier and fallback off) against the new ones, interleaved.
TEST(project_all_bench_old_vs_new) {
    const char* path = std::getenv("SF_PROJECT_BENCH");
    if (!path) return;
    const std::string mode = std::getenv("SF_PROJECT_BENCH_MODE") ? std::getenv("SF_PROJECT_BENCH_MODE") : "both";
    CommandRegistry reg;
    Document doc;
    std::string err;
    CHECK(loadProject(path, doc, err));
    CHECK(reg.execute(doc, "part_duplicate", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "remesh", {{"resolution", 300.0}, {"keep_texture", false}}).ok);
    CHECK(reg.execute(doc, "subdiv_divide", {}).ok);
    CHECK(reg.execute(doc, "subdiv_divide", {}).ok);
    std::vector<const Mesh*> sources{&doc.partMesh(0)};
    ProjectOptions now, old;
    now.vertexpaint = old.vertexpaint = false;
    old.facing = -1.0f;
    old.fallbackCos = -1.0f;
    old.outlier = 0.0f;
    std::printf("  bench: target %zu triangles, source %zu\n", doc.mesh.triangleCount(), sources[0]->triangleCount());
    std::vector<double> tOld, tNew;
    for (int run = 0; run < 5; ++run)
        for (int which = 0; which < 2; ++which) {
            const bool useOld = which == 0;
            if ((useOld && mode == "new") || (!useOld && mode == "old")) continue;
            std::vector<glm::vec3> pos;
            ProjectStats st;
            CHECK(computeProjection(doc.mesh, sources, useOld ? old : now, pos, nullptr, st, err));
            (useOld ? tOld : tNew).push_back(st.seconds);
        }
    auto report = [](const char* name, std::vector<double> t) {
        if (t.empty()) return;
        std::sort(t.begin(), t.end());
        std::printf("  bench: %s median %.3f s, IQR %.3f (min %.3f max %.3f, %zu runs)\n", name, t[t.size() / 2],
                    t[(t.size() * 3) / 4] - t[t.size() / 4], t.front(), t.back(), t.size());
    };
    report("old", tOld);
    report("new", tNew);
#ifdef __linux__
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    std::printf("  bench: peak_ram_mb %ld (whole process, mode %s)\n", ru.ru_maxrss / 1024, mode.c_str());
#endif
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
