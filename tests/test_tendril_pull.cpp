// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tendril pulls a round tube out of the surface (tests/GeomInvariants.h, docs/testing-flow.md "Brush and tool promises"): the
// cross-section of the pulled part, measured slice by slice along the pull, stays round (min/max extent of the slice's points
// around their centre), and no triangle folds over a neighbour. On a coarse Part (a few hundred triangles, a lumpy ball) with
// Dynamic on at its default Detail, short and long pulls, small and large brushes (Tpqz_'s report, 0.13.11: flat folded sheets).
#include "GeomInvariants.h"

#include <glm/gtc/constants.hpp>

#include <cstdarg>
#include <cstdio>

using namespace sf;
using namespace sf::geom;

namespace {

struct Pull {
    float radius = 0.25f;  // brush radius, in units of the Part's radius (1)
    float strength = 1.0f;
    float length = 1.5f;   // how far the cursor drags, in brush radii
    bool dynamic = true;
    bool mirror = true;
    bool viewFalloff = false;
    float angle = 0.65f;   // where on the Part (radians in the view plane from +X) the pull starts, and its direction (outward)
};

struct Result {
    float worstRound = 1.0f, meanRound = 1.0f; // min/max extent of the slices of the pulled part (1 = round, 0 = a sheet)
    int slices = 0;
    size_t folded = 0, inward = 0, tris = 0, nonFinite = 0;
    float farMoved = 0.0f; // View falloff: the largest move of an original vertex deeper than 2 radii behind the pull
    float moved = 0.0f;
    float reach = 0.0f; // how far past the start point the tip got, in brush radii
};

// A lumpy ball of `res` quads per cube side: what a low-poly blob Part looks like.
Mesh blob(int res) {
    Mesh m = makeQuadSphere(res, 2.0f);
    for (glm::vec3& p : m.positions) {
        const glm::vec3 n = glm::normalize(p);
        const float lump = 0.12f * std::sin(4.1f * n.x + 1.0f) * std::cos(3.3f * n.y) + 0.08f * std::sin(5.3f * n.z + 2.0f * n.x);
        p = n * (1.0f + lump);
        p.y *= 0.85f;
    }
    m.recomputeAllNormals();
    return m;
}

// Triangles lying against a neighbour across an edge (normals more than 120 degrees apart), by the artist close-up's rule.
size_t foldedNow(const Mesh& m) {
    std::unordered_map<uint64_t, uint32_t> first;
    std::vector<glm::vec3> nrm(m.triangleCount());
    std::vector<uint8_t> bad(m.triangleCount(), 0);
    for (uint32_t i = 0; i < m.triangleCount(); ++i) {
        const glm::uvec3 f = m.triangles[i];
        const glm::vec3 n = glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]);
        const float l = glm::length(n);
        nrm[i] = l > 0.0f ? n / l : glm::vec3(0.0f);
        const uint32_t c[3] = {f.x, f.y, f.z};
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = std::min(c[k], c[(k + 1) % 3]), b = std::max(c[k], c[(k + 1) % 3]);
            auto it = first.find((uint64_t(a) << 32) | b);
            if (it == first.end()) { first.emplace((uint64_t(a) << 32) | b, i); continue; }
            const uint32_t j = it->second;
            if (nrm[i] != glm::vec3(0.0f) && nrm[j] != glm::vec3(0.0f) && glm::dot(nrm[i], nrm[j]) < -0.5f) bad[i] = bad[j] = 1;
        }
    }
    size_t n = 0;
    for (uint8_t b : bad) n += b;
    return n;
}

// Roundness of the part pulled out along `axis` from `start`: slices a half radius thick, from one radius past the start to one
// radius short of the tip, of the vertices within 1.6 radii of the axis; each slice's 2D points (across the axis, one of them
// the view direction) around their centre: sqrt of the smaller over the larger eigenvalue of their covariance.
void measure(const Mesh& m, glm::vec3 start, glm::vec3 axis, float radius, Result& out) {
    const glm::vec3 view(0, 0, 1), across = glm::normalize(glm::cross(view, axis));
    float tip = 0.0f;
    for (const glm::vec3& p : m.positions) {
        const glm::vec3 q = p - start;
        const float s = glm::dot(q, axis);
        if (glm::length(q - axis * s) < 0.6f * radius) tip = std::max(tip, s);
    }
    out.reach = tip / radius;
    const float lo = 0.75f * radius, hi = tip - 0.5f * radius, step = 0.5f * radius;
    double sum = 0.0;
    for (float s0 = lo; s0 + step <= hi; s0 += step) {
        std::vector<glm::vec2> pts;
        for (const glm::vec3& p : m.positions) {
            const glm::vec3 q = p - start;
            const float s = glm::dot(q, axis);
            if (s < s0 || s >= s0 + step) continue;
            const glm::vec3 r = q - axis * s;
            if (glm::length(r) > 1.6f * radius) continue;
            pts.push_back({glm::dot(r, across), glm::dot(r, view)});
        }
        if (pts.size() < 6) continue;
        glm::vec2 c(0.0f);
        for (const glm::vec2& p : pts) c += p;
        c /= float(pts.size());
        double sxx = 0, syy = 0, sxy = 0;
        for (const glm::vec2& p : pts) {
            const glm::vec2 d = p - c;
            sxx += d.x * d.x, syy += d.y * d.y, sxy += d.x * d.y;
        }
        const double tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
        const double l1 = tr / 2 + disc, l2 = std::max(0.0, tr / 2 - disc);
        const float round = l1 > 1e-12 ? float(std::sqrt(l2 / l1)) : 1.0f;
        out.worstRound = std::min(out.worstRound, round);
        sum += round;
        ++out.slices;
    }
    out.meanRound = out.slices ? float(sum / out.slices) : 1.0f;
}

// The pull the app makes (App::tendrilTo): the grabbed tip follows the cursor in the view plane in steps of 0.15 radius.
Result pullOut(Mesh mesh, const Pull& pull) {
    mesh.recomputeAllNormals();
    const Mesh original = mesh;
    Document d = makeDoc(std::move(mesh));
    d.sculptor.dyntopo.enabled = pull.dynamic;
    const glm::vec3 axis(std::cos(pull.angle), std::sin(pull.angle), 0.0f);
    // The surface point where the ray along -view through the start meets the Part: found by marching in.
    glm::vec3 start = axis * 0.85f;
    RayHit hit;
    if (surfaceRaycast(d.mesh, d.bvh, start + glm::vec3(0, 0, 3), glm::vec3(0, 0, -1), hit)) start = hit.position;
    d.sculptor.beginStroke(d.mesh, "Tendril");
    DabParams p;
    p.viewDir = glm::vec3(0, 0, -1);
    p.radius = pull.radius;
    p.strength = pull.strength;
    p.pressure = 1.0f;
    p.viewFalloff = pull.viewFalloff;
    glm::vec3 tip = start;
    const glm::vec3 target = start + axis * (pull.length * pull.radius);
    const int steps = std::max(1, int(std::ceil(glm::length(target - start) / (pull.radius * 0.15f))));
    p.strokeDelta = (target - start) / float(steps);
    Symmetry sym{pull.mirror, false, false};
    for (int i = 0; i < steps; ++i) {
        p.center = tip;
        d.sculptor.dab(d.mesh, d.bvh, BrushType::Tendril, p, sym);
        tip += p.strokeDelta;
    }
    UndoRecord record = d.sculptor.endStroke();
    Result r;
    measure(d.mesh, start, axis, pull.radius, r);
    r.folded = foldedNow(d.mesh);
    for (uint32_t v = 0; v < std::min(d.mesh.vertexCount(), original.vertexCount()); ++v) r.moved = std::max(r.moved, glm::distance(d.mesh.positions[v], original.positions[v]));
    if (d.mesh.vertexCount() == original.vertexCount())
        for (uint32_t v = 0; v < original.vertexCount(); ++v)
            if (original.positions[v].z < start.z - 2.0f * pull.radius)
                r.farMoved = std::max(r.farMoved, glm::distance(d.mesh.positions[v], original.positions[v]));
    r.inward = inwardFaces(d.mesh);
    r.tris = d.mesh.triangleCount();
    r.nonFinite = nonFinite(d.mesh);
    return r;
}

} // namespace

// Dynamic on at its default Detail, Intensity 100, X symmetry: the pulled horn comes out round and unfolded on a sphere and on
// coarse lumpy Parts, short and long pulls, small and large brushes (before: round min 0.08-0.5, 250-460 folded on the long pulls).
TEST(tendril_invariant_pull_is_round_and_unfolded_with_dynamic) {
    struct Shape { const char* name; Mesh mesh; };
    const Shape shapes[] = {{"icosphere2", makeIcosphere(2)}, {"blob5", blob(5)}, {"blob8", blob(8)}};
    bool ok = true;
    for (const Shape& sh : shapes)
        for (float radius : {0.2f, 0.35f})
            for (float length : {3.0f, 10.0f}) {
                Pull p;
                p.radius = radius, p.length = length;
                const Result r = pullOut(sh.mesh, p);
                std::printf("  %-10s r %.2f len %4.1f: %5zu tris, reach %.1f radii, %2d slices, round min %.2f mean %.2f, %zu folded\n", sh.name,
                            radius, length, r.tris, r.reach, r.slices, r.worstRound, r.meanRound, r.folded);
                const bool good = r.slices > 0 && r.meanRound >= 0.8f && r.worstRound >= 0.6f && r.folded <= 4 && r.nonFinite == 0;
                if (!good) std::printf("  FAILED: %s r %.2f len %.1f: the pulled horn is a flat or folded sheet (round min %.2f mean %.2f, %zu folded)\n",
                                       sh.name, radius, length, r.worstRound, r.meanRound, r.folded);
                ok = ok && good;
            }
    CHECK(ok);
}

// Under View falloff (the cylinder along the view) the far side of a thick part is not dragged along with the front, and the
// horn stays round enough (a pull that carried the model's whole depth made a sheet).
TEST(tendril_invariant_view_falloff_leaves_the_far_side) {
    Pull p;
    p.viewFalloff = true, p.radius = 0.25f, p.length = 6.0f;
    // Fixed topology (indices stay), so the far side can be compared vertex by vertex.
    p.dynamic = false;
    {
        const Mesh sphere = makeIcosphere(5);
        const Result r = pullOut(sphere, p);
        CHECK(r.moved > 0.0f);
        CHECK(r.farMoved == 0.0f);
        std::printf("  View falloff, fixed topology: far side moved %.4f (reach %.1f radii)\n", r.farMoved, r.reach);
    }
    p.dynamic = true;
    for (const Mesh& m : {blob(5), blob(8), makeIcosphere(2)}) {
        const Result r = pullOut(m, p);
        std::printf("  View falloff, Dynamic: round min %.2f mean %.2f, %zu folded\n", r.worstRound, r.meanRound, r.folded);
        CHECK(r.meanRound >= 0.65f && r.worstRound >= 0.5f && r.folded <= 30 && r.nonFinite == 0);
    }
}
