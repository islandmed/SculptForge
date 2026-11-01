// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Community requests for 0.10.6: the Connected only and View falloff brush options (#55 in the discussions), the
// whole-mesh deformations (core/Deform.h), moving along the normal (lineRayClosest, brushAreaNormal) and Forge
// Spheres' Set as root (#8).
#include "TestHarness.h"

#include "core/BrushRecipe.h"
#include "core/Commands.h"
#include "core/Deform.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"
#include "core/ZSpheres.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>

using namespace sf;
using glm::vec3;

namespace {

// Two meshes as one (separate pieces), with topology and normals.
Mesh merged(const Mesh& a, const Mesh& b, const vec3& offsetB) {
    Mesh m = a;
    const uint32_t base = uint32_t(a.vertexCount());
    for (const vec3& p : b.positions) m.positions.push_back(p + offsetB);
    for (const glm::uvec3& t : b.triangles) m.triangles.push_back(t + glm::uvec3(base));
    m.quadPartners.clear();
    m.normals.clear();
    m.rebuildTopology();
    m.recomputeAllNormals();
    return m;
}

float stddevOver(const std::vector<float>& v, float& mean) {
    mean = 0.0f;
    for (float x : v) mean += x;
    mean /= float(v.size());
    float var = 0.0f;
    for (float x : v) var += (x - mean) * (x - mean);
    return std::sqrt(var / float(v.size()));
}

// Distances from the centroid: their spread over their mean (0 for a sphere).
float roundness(const std::vector<vec3>& P) {
    vec3 c(0.0f);
    for (const vec3& p : P) c += p;
    c /= float(P.size());
    std::vector<float> d;
    for (const vec3& p : P) d.push_back(glm::length(p - c));
    float mean = 0.0f;
    const float sd = stddevOver(d, mean);
    return sd / mean;
}

// The volume a closed mesh at positions P encloses.
float volume(const Mesh& m, const std::vector<vec3>& P) {
    double v = 0.0;
    for (const glm::uvec3& t : m.triangles) v += double(glm::dot(P[t.x], glm::cross(P[t.y], P[t.z])));
    return float(v / 6.0);
}

// An icosphere with every vertex pushed in or out a little (deterministic).
Mesh noisySphere(int subdivisions, float noise) {
    Mesh m = makeIcosphere(subdivisions, 1.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-noise, noise);
    for (vec3& p : m.positions) p *= 1.0f + u(rng);
    m.recomputeAllNormals();
    return m;
}

std::vector<float> edgeLengths(const Mesh& m, const std::vector<vec3>& P) {
    std::vector<float> out;
    for (const glm::uvec3& t : m.triangles)
        for (int k = 0; k < 3; ++k) out.push_back(glm::distance(P[t[k]], P[t[(k + 1) % 3]]));
    return out;
}

} // namespace

// --- Connected only and View falloff ---------------------------------------------------------------------

TEST(falloff_keep_connected_flood_fill) {
    // Two spheres 0.1 apart: a region reaching into both keeps only the piece the seed is on.
    const Mesh m = merged(makeIcosphere(3, 0.5f), makeIcosphere(3, 0.5f), vec3(1.1f, 0, 0));
    BVH bvh;
    bvh.build(m);
    const uint32_t firstB = uint32_t(makeIcosphere(3, 0.5f).vertexCount());
    std::vector<uint32_t> verts;
    ConnectedScratch scratch;
    brushRegion(m, bvh, vec3(0.5f, 0, 0), vec3(0, 0, -1), 0.4f, false, false, verts, scratch);
    const bool both = std::any_of(verts.begin(), verts.end(), [&](uint32_t v) { return v < firstB; }) &&
                      std::any_of(verts.begin(), verts.end(), [&](uint32_t v) { return v >= firstB; });
    CHECK(both);
    const size_t all = verts.size();
    brushRegion(m, bvh, vec3(0.5f, 0, 0), vec3(0, 0, -1), 0.4f, true, false, verts, scratch);
    CHECK(!verts.empty() && verts.size() < all);
    CHECK(std::all_of(verts.begin(), verts.end(), [&](uint32_t v) { return v < firstB; }));
    CHECK(std::is_sorted(verts.begin(), verts.end()));
    // From the other side: the other piece.
    brushRegion(m, bvh, vec3(0.6f, 0, 0), vec3(0, 0, -1), 0.4f, true, false, verts, scratch);
    CHECK(!verts.empty() && std::all_of(verts.begin(), verts.end(), [&](uint32_t v) { return v >= firstB; }));
    // One piece only: nothing dropped.
    const Mesh one = makeIcosphere(4, 1.0f);
    BVH ob;
    ob.build(one);
    std::vector<uint32_t> plain;
    brushRegion(one, ob, vec3(0, 0, 1), vec3(0, 0, -1), 0.5f, false, false, plain, scratch);
    brushRegion(one, ob, vec3(0, 0, 1), vec3(0, 0, -1), 0.5f, true, false, verts, scratch);
    CHECK(verts == plain);
}

TEST(falloff_connected_brush_leaves_the_other_piece) {
    // Two fingers side by side: inflating one with a brush that reaches the other leaves the other one alone.
    const Mesh start = merged(makeIcosphere(3, 0.5f), makeIcosphere(3, 0.5f), vec3(1.1f, 0, 0));
    const uint32_t firstB = uint32_t(makeIcosphere(3, 0.5f).vertexCount());
    auto run = [&](bool connected) {
        Document e = makeDoc(start);
        DabParams p;
        p.center = vec3(0.5f, 0, 0);
        p.viewDir = vec3(0, 0, -1);
        p.radius = 0.4f;
        p.strength = 1.0f;
        p.connectedOnly = connected;
        CHECK(e.sculptor.beginStroke(e.mesh, "Inflate"));
        e.sculptor.dab(e.mesh, e.bvh, BrushType::Inflate, p, Symmetry{false, false, false});
        e.undo.push(e.sculptor.endStroke());
        float movedB = 0.0f, movedA = 0.0f;
        for (uint32_t v = 0; v < e.mesh.vertexCount(); ++v) {
            const float l = glm::distance(e.mesh.positions[v], start.positions[v]);
            (v < firstB ? movedA : movedB) += l;
        }
        return std::make_pair(movedA, movedB);
    };
    const auto [a0, b0] = run(false);
    const auto [a1, b1] = run(true);
    CHECK(a0 > 0.0f && b0 > 0.0f); // the plain brush moves both
    CHECK(a1 > 0.0f && b1 == 0.0f);
    // The Move brush too.
    Document e = makeDoc(start);
    DabParams p;
    p.center = vec3(0.5f, 0, 0);
    p.viewDir = vec3(0, 0, -1);
    p.radius = 0.4f;
    p.strength = 1.0f;
    p.connectedOnly = true;
    CHECK(e.sculptor.beginStroke(e.mesh, "Move"));
    e.sculptor.beginGrab(e.mesh, e.bvh, p, Symmetry{false, false, false});
    e.sculptor.grab(e.mesh, e.bvh, vec3(0, 0.2f, 0));
    e.undo.push(e.sculptor.endStroke());
    bool aMoved = false, bMoved = false;
    for (uint32_t v = 0; v < e.mesh.vertexCount(); ++v)
        (v < firstB ? aMoved : bMoved) |= e.mesh.positions[v] != start.positions[v];
    CHECK(aMoved && !bMoved);
}

TEST(falloff_view_cylinder) {
    // Two planes one unit apart, seen from above: the view falloff reaches the lower one, the sphere doesn't.
    const Mesh m = merged(makePlane(32, 2.0f), makePlane(32, 2.0f), vec3(0, -1, 0));
    BVH bvh;
    bvh.build(m);
    const uint32_t firstB = uint32_t(makePlane(32, 2.0f).vertexCount());
    std::vector<uint32_t> sphere, cylinder;
    ConnectedScratch scratch;
    brushRegion(m, bvh, vec3(0, 0, 0), vec3(0, -1, 0), 0.3f, false, false, sphere, scratch);
    brushRegion(m, bvh, vec3(0, 0, 0), vec3(0, -1, 0), 0.3f, false, true, cylinder, scratch);
    CHECK(std::none_of(sphere.begin(), sphere.end(), [&](uint32_t v) { return v >= firstB; }));
    CHECK(std::any_of(cylinder.begin(), cylinder.end(), [&](uint32_t v) { return v >= firstB; }));
    // Every vertex within 0.3 of the vertical line, and only those (a brute-force check of the BVH query).
    std::vector<uint32_t> brute;
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (std::hypot(m.positions[v].x, m.positions[v].z) <= 0.3f) brute.push_back(v);
    CHECK(cylinder == brute);
    // With Connected only, the lower plane is a separate piece again.
    std::vector<uint32_t> both;
    brushRegion(m, bvh, vec3(0, 0, 0), vec3(0, -1, 0), 0.3f, true, true, both, scratch);
    CHECK(!both.empty() && std::none_of(both.begin(), both.end(), [&](uint32_t v) { return v >= firstB; }));
    // The distance: across the view only.
    CHECK(std::abs(brushDistance(vec3(0.1f, -5.0f, 0.0f), vec3(0.0f), vec3(0, -1, 0), true) - 0.1f) < 1e-6f);
    CHECK(std::abs(brushDistance(vec3(0.1f, -5.0f, 0.0f), vec3(0.0f), vec3(0, -1, 0), false) - std::hypot(0.1f, 5.0f)) < 1e-5f);
    // A tilted view: brute force again.
    const vec3 view = glm::normalize(vec3(0.3f, -1.0f, 0.2f));
    bvh.queryVerticesCylinder(m, vec3(0.1f, 0.0f, 0.1f), view, 0.25f, cylinder);
    brute.clear();
    for (uint32_t v = 0; v < m.vertexCount(); ++v)
        if (brushDistance(m.positions[v], vec3(0.1f, 0.0f, 0.1f), view, true) <= 0.25f) brute.push_back(v);
    CHECK(cylinder == brute);
}

TEST(falloff_options_in_recipes) {
    BrushRecipe r;
    CHECK(r.toJson()["connectedOnly"].isNull()); // other recipes' files stay as they were
    r.connectedOnly = true;
    r.viewFalloff = true;
    const BrushRecipe back = BrushRecipe::fromJson(r.toJson());
    CHECK(back.connectedOnly && back.viewFalloff);
}

// --- Along the normal ------------------------------------------------------------------------------------

TEST(along_normal_line_ray) {
    float s = 0.0f;
    // The line up the z axis; a ray across at height 5 meets it at s = 5.
    CHECK(lineRayClosest(vec3(0.0f), vec3(0, 0, 1), vec3(3, 0, 5), vec3(-1, 0, 0), s));
    CHECK(std::abs(s - 5.0f) < 1e-5f);
    // A skew ray: the nearest point of the line.
    CHECK(lineRayClosest(vec3(1, 2, 3), vec3(0, 1, 0), vec3(0, 0, 0), glm::normalize(vec3(1, 1, 3)), s));
    const vec3 q = vec3(1, 2, 3) + vec3(0, 1, 0) * s;
    const vec3 r = glm::normalize(vec3(1, 1, 3));
    const vec3 onRay = r * glm::dot(q, r);
    CHECK(std::abs(glm::dot(q - onRay, vec3(0, 1, 0))) < 1e-4f); // the gap is square to the line
    // The line pointing at the viewer: no answer.
    CHECK(!lineRayClosest(vec3(0.0f), vec3(0, 0, 1), vec3(0, 0, 5), vec3(0.01f, 0, -1), s));
    // The area normal of a plane, and of a sphere's pole.
    const Mesh plane = makePlane(16, 2.0f);
    BVH pb;
    pb.build(plane);
    CHECK(glm::distance(brushAreaNormal(plane, pb, vec3(0.0f), 0.3f), vec3(0, 1, 0)) < 1e-4f);
    const Mesh ball = makeIcosphere(4, 1.0f);
    BVH bb;
    bb.build(ball);
    const vec3 n = brushAreaNormal(ball, bb, glm::normalize(vec3(1, 1, 0)), 0.3f);
    CHECK(glm::distance(n, glm::normalize(vec3(1, 1, 0))) < 0.02f);
    CHECK(brushAreaNormal(ball, bb, vec3(5.0f), 0.3f) == vec3(0.0f));
}

TEST(along_normal_move_extrudes) {
    // A Move stroke whose grab slides along the normal: the region moves straight out of the plane.
    Document d = makeDoc(makePlane(32, 2.0f));
    const std::vector<vec3> before = d.mesh.positions;
    const vec3 n = brushAreaNormal(d.mesh, d.bvh, vec3(0.0f), 0.3f);
    DabParams p;
    p.center = vec3(0.0f);
    p.viewDir = glm::normalize(vec3(0, -0.2f, -1)); // seen nearly edge on
    p.radius = 0.3f;
    p.strength = 1.0f;
    CHECK(d.sculptor.beginStroke(d.mesh, "Move"));
    d.sculptor.beginGrab(d.mesh, d.bvh, p, Symmetry{false, false, false});
    // The cursor ray passes 0.5 above the press point: the grab goes 0.5 out along the normal.
    float s = 0.0f;
    CHECK(lineRayClosest(vec3(0.0f), n, vec3(0, 0.5f, 4), vec3(0, 0, -1), s));
    CHECK(std::abs(s - 0.5f) < 1e-4f);
    d.sculptor.grab(d.mesh, d.bvh, n * s);
    d.undo.push(d.sculptor.endStroke());
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const vec3 m = d.mesh.positions[v] - before[v];
        CHECK(std::abs(m.x) < 1e-6f && std::abs(m.z) < 1e-6f); // only along the normal
    }
}

// --- Whole-mesh deformations -----------------------------------------------------------------------------

TEST(deform_smooth_and_keep_volume) {
    const Mesh m = noisySphere(4, 0.05f);
    const float r0 = roundness(m.positions);
    DeformOptions o;
    o.amount = 60.0f;
    const std::vector<vec3> plain = deformPositions(m, DeformOp::Smooth, o);
    o.keepVolume = true;
    const std::vector<vec3> taubin = deformPositions(m, DeformOp::Smooth, o);
    CHECK(roundness(plain) < 0.5f * r0 && roundness(taubin) < 0.5f * r0);
    const float v0 = volume(m, m.positions);
    const float lostPlain = v0 - volume(m, plain), lostTaubin = std::abs(v0 - volume(m, taubin));
    CHECK(lostPlain > 0.0f && lostTaubin < 0.25f * lostPlain);
    // 0 does nothing, more does more.
    o.keepVolume = false;
    o.amount = 0.0f;
    CHECK(deformPositions(m, DeformOp::Smooth, o) == m.positions);
    o.amount = 20.0f;
    CHECK(roundness(deformPositions(m, DeformOp::Smooth, o)) > roundness(plain));
}

TEST(deform_relax_evens_spacing_keeps_shape) {
    // A sphere whose vertices slid along it at random, by up to a third of the spacing (the shape is still the
    // sphere).
    Mesh m = makeIcosphere(4, 1.0f);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-0.025f, 0.025f);
    for (vec3& p : m.positions) p = glm::normalize(p + vec3(u(rng), u(rng), u(rng)));
    m.recomputeAllNormals();
    float mean = 0.0f;
    const float cv0 = stddevOver(edgeLengths(m, m.positions), mean) / mean;
    DeformOptions o;
    o.amount = 100.0f;
    const std::vector<vec3> P = deformPositions(m, DeformOp::Relax, o);
    const float cv1 = stddevOver(edgeLengths(m, P), mean) / mean;
    std::printf("relax: edge length spread %.4f -> %.4f\n", cv0, cv1);
    CHECK(cv1 < 0.7f * cv0);
    float off = 0.0f;
    for (const vec3& p : P) off = std::max(off, std::abs(glm::length(p) - 1.0f));
    CHECK(off < 0.02f); // still the sphere
}

TEST(deform_polish_keeps_planes_and_edges) {
    // A cube with a little noise on its faces: Polish takes the noise away, the corners and edges stay sharp.
    Mesh m = makeCube(12, 2.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-0.01f, 0.01f);
    auto onEdge = [](const vec3& p) {
        int at = 0;
        for (int a = 0; a < 3; ++a) at += std::abs(std::abs(p[a]) - 1.0f) < 1e-4f;
        return at >= 2;
    };
    std::vector<vec3> clean = m.positions;
    for (vec3& p : m.positions)
        if (!onEdge(p)) p += vec3(u(rng), u(rng), u(rng));
    m.recomputeAllNormals();
    // Distance of each vertex from the cube's surface.
    auto noise = [&](const std::vector<vec3>& P) {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = 0; i < P.size(); ++i) {
            if (onEdge(clean[i])) continue;
            int face = 0;
            for (int a = 1; a < 3; ++a)
                if (std::abs(clean[i][a]) > std::abs(clean[i][face])) face = a;
            const float h = std::abs(P[i][face]) - 1.0f;
            sum += double(h * h);
            ++n;
        }
        return float(std::sqrt(sum / double(n))); // RMS
    };
    DeformOptions o;
    o.amount = 100.0f;
    const std::vector<vec3> P = deformPositions(m, DeformOp::Polish, o);
    std::printf("polish noise %.4f -> %.4f\n", noise(m.positions), noise(P));
    CHECK(noise(P) < 0.5f * noise(m.positions));
    // The corners barely move (a plain smooth rounds them off).
    const std::vector<vec3> S = deformPositions(m, DeformOp::Smooth, o);
    float polishCorner = 0.0f, smoothCorner = 0.0f;
    for (size_t i = 0; i < P.size(); ++i)
        if (std::abs(std::abs(clean[i].x) - 1) < 1e-4f && std::abs(std::abs(clean[i].y) - 1) < 1e-4f &&
            std::abs(std::abs(clean[i].z) - 1) < 1e-4f) {
            polishCorner = std::max(polishCorner, glm::distance(P[i], clean[i]));
            smoothCorner = std::max(smoothCorner, glm::distance(S[i], clean[i]));
        }
    std::printf("corner polish %.4f smooth %.4f\n", polishCorner, smoothCorner);
    CHECK(polishCorner < 0.03f && smoothCorner > 4.0f * polishCorner);
}

TEST(deform_balloon_rounds_out) {
    const Mesh cube = makeCube(10, 2.0f);
    DeformOptions o;
    o.amount = 100.0f;
    const std::vector<vec3> P = deformPositions(cube, DeformOp::Balloon, o);
    CHECK(roundness(P) < 0.6f * roundness(cube.positions));
    CHECK(volume(cube, P) > volume(cube, cube.positions));
    o.amount = -100.0f;
    CHECK(volume(cube, deformPositions(cube, DeformOp::Balloon, o)) < volume(cube, cube.positions));
}

TEST(deform_mask_border_and_undo) {
    Document d = makeDoc(noisySphere(4, 0.05f));
    // Mask the top half: it stays exactly.
    std::vector<float>& mask = d.mesh.ensureMask();
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) mask[v] = d.mesh.positions[v].y > 0.0f ? 1.0f : 0.0f;
    const std::vector<vec3> before = d.mesh.positions;
    CommandRegistry reg;
    const CommandResult r = reg.execute(d, "deform_smooth", {{"amount", 80.0}});
    CHECK(r.ok);
    bool lowerMoved = false;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (mask[v] == 1.0f) CHECK(d.mesh.positions[v] == before[v]);
        else lowerMoved |= d.mesh.positions[v] != before[v];
    }
    CHECK(lowerMoved);
    // One undo step puts it back.
    const size_t steps = d.undo.undoCount();
    CHECK(steps == 1);
    CHECK(d.undo.undo(d.mesh, d.bvh));
    CHECK(d.mesh.positions == before);
    // masked=false moves the top too; every command runs.
    for (const char* name : {"deform_polish", "deform_relax", "deform_smooth", "deform_balloon"})
        CHECK(reg.execute(d, name, {{"amount", 50.0}, {"masked", false}}).ok);
    // Everything masked: an error, nothing recorded.
    std::fill(d.mesh.mask.begin(), d.mesh.mask.end(), 1.0f);
    const size_t n = d.undo.undoCount();
    CHECK(!reg.execute(d, "deform_relax").ok);
    CHECK(d.undo.undoCount() == n);
    // Open borders stay where they are.
    Document plane = makeDoc(makePlane(16, 2.0f));
    std::vector<vec3> flat = plane.mesh.positions;
    for (size_t i = 0; i < flat.size(); ++i) plane.mesh.positions[i].y = 0.05f * std::sin(7.0f * flat[i].x);
    plane.mesh.recomputeAllNormals();
    plane.bvh.build(plane.mesh);
    const std::vector<vec3> wavy = plane.mesh.positions;
    CHECK(reg.execute(plane, "deform_smooth", {{"amount", 100.0}}).ok);
    for (uint32_t v = 0; v < plane.mesh.vertexCount(); ++v)
        if (std::abs(std::abs(flat[v].x) - 1.0f) < 1e-4f || std::abs(std::abs(flat[v].z) - 1.0f) < 1e-4f)
            CHECK(plane.mesh.positions[v] == wavy[v]);
}

TEST(deform_large_mesh_timing) {
    // SF_DEFORM_BENCH=1: the four deformations and a Connected only dab on a 5M-vertex sphere (skipped otherwise).
    if (!std::getenv("SF_DEFORM_BENCH")) return;
    Document d = makeDoc(makeIcosphere(std::atoi(std::getenv("SF_DEFORM_BENCH")) > 1 ? 10 : 9, 1.0f)); // 2.6M or 10.5M vertices
    std::printf("bench mesh: %zu vertices, %zu triangles\n", d.mesh.vertexCount(), d.mesh.triangleCount());
    using clock = std::chrono::steady_clock;
    for (DeformOp op : {DeformOp::Polish, DeformOp::Relax, DeformOp::Smooth, DeformOp::Balloon}) {
        DeformOptions o;
        o.amount = 50.0f;
        const auto t0 = clock::now();
        std::string err;
        const size_t moved = deformActivePart(d, op, o, err);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        std::printf("deform %s 50: %zu vertices in %.0f ms\n", deformName(op), moved, ms);
    }
    for (bool connected : {false, true})
        for (float radius : {0.05f, 0.2f, 0.5f, 1.0f}) {
            DabParams p;
            p.center = *std::max_element(d.mesh.positions.begin(), d.mesh.positions.end(),
                                         [](const vec3& a, const vec3& b) { return a.z < b.z; }); // the top
            p.viewDir = vec3(0, 0, -1);
            p.radius = radius;
            p.strength = 0.5f;
            p.connectedOnly = connected;
            std::vector<uint32_t> verts;
            brushVertices(d.mesh, d.bvh, p.center, radius, verts);
            CHECK(d.sculptor.beginStroke(d.mesh, "Standard"));
            const auto t0 = clock::now();
            for (int i = 0; i < 10; ++i) { // in and out again, so the surface stays under the centre
                p.invert = i % 2 == 1;
                d.sculptor.dab(d.mesh, d.bvh, BrushType::Standard, p, Symmetry{false, false, false});
            }
            const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / 10.0;
            d.undo.push(d.sculptor.endStroke());
            std::printf("dab radius %.2f (%zu vertices)%s: %.2f ms\n", radius, verts.size(), connected ? " connected only" : "", ms);
        }
}

// --- Forge Spheres: Set as root --------------------------------------------------------------------------

TEST(zspheres_set_root) {
    // Root 0, an arm 1 from it, a hand 2 from the arm, a head 3 from the root.
    std::vector<ZSphere> s{{vec3(0.0f), 0.5f, -1}, {vec3(1, 0, 0), 0.3f, 0}, {vec3(2, 0, 0), 0.2f, 1}, {vec3(0, 1, 0), 0.3f, 0}};
    const std::vector<ZSphere> before = s;
    CHECK(!zsphereSetRoot(s, 0) && !zsphereSetRoot(s, 9));
    CHECK(zsphereSetRoot(s, 2));
    CHECK(zspheresValidate(s).empty());
    // The hand is the root now (index 0), the old root took its index (2).
    CHECK(s[0].center == before[2].center && s[0].parent == -1);
    CHECK(s[2].center == before[0].center && s[2].parent == 1); // the old root hangs from the arm
    CHECK(s[1].parent == 0);                                    // the arm from the hand
    CHECK(s[3].parent == 2);                                    // the head still from the old root
    // Same spheres, same links (as unordered pairs of centres).
    auto links = [](const std::vector<ZSphere>& z) {
        std::vector<std::pair<float, float>> out;
        for (const ZSphere& a : z)
            if (a.parent >= 0) {
                const float p = a.center.x + 10.0f * a.center.y, q = z[size_t(a.parent)].center.x + 10.0f * z[size_t(a.parent)].center.y;
                out.push_back({std::min(p, q), std::max(p, q)});
            }
        std::sort(out.begin(), out.end());
        return out;
    };
    CHECK(links(s) == links(before));
    // The command, with undo.
    Document doc;
    CommandRegistry reg;
    CHECK(reg.execute(doc, "zsphere_new", {{"radius", 0.5}}).ok);
    CHECK(reg.execute(doc, "zsphere_add", {{"parent", 0.0}, {"position", std::string("1,0,0")}, {"radius", 0.25}}).ok);
    CHECK(!reg.execute(doc, "zsphere_set_root", {{"index", 0.0}}).ok);
    CHECK(reg.execute(doc, "zsphere_set_root", {{"index", 1.0}}).ok);
    CHECK(doc.mesh.zspheres[0].center.x == 1.0f && doc.mesh.zspheres[1].parent == 0);
    CHECK(doc.undo.undo(doc.mesh, doc.bvh));
    CHECK(doc.mesh.zspheres[0].center.x == 0.0f);
}
