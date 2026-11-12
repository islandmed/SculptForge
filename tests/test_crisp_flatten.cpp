// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crisp Flatten (Hard Surface pack; thanks _tfx on Discord): a flatten that cuts a clean, sharp boundary.
// BrushRecipe::crispEdge (a hard tip with a thin anti-aliased band) and BrushRecipe::planeLock (the plane the
// stroke starts on, kept for the whole stroke).
#include "TestHarness.h"

#include "core/BrushRecipe.h"
#include "core/Primitives.h"
#include "core/Sculpt.h"

#include <algorithm>
#include <cmath>
#include <functional>

using namespace sf;

namespace {

constexpr Symmetry kNoSymmetry{false, false, false};

BrushRecipe builtin(const char* name) {
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == name) return r;
    CHECK(false);
    return {};
}

DabParams dabAt(glm::vec3 center, glm::vec3 viewDir, float radius = 0.4f, float strength = 0.5f) {
    DabParams p;
    p.center = center;
    p.viewDir = viewDir;
    p.radius = radius;
    p.strength = strength;
    return p;
}

void recipeStroke(Document& d, const BrushRecipe& r, const std::vector<DabParams>& dabs, Symmetry sym = kNoSymmetry) {
    d.sculptor.beginStroke(d.mesh, r.name);
    for (const DabParams& p : dabs) d.sculptor.dabRecipe(d.mesh, d.bvh, r, p, sym);
    d.undo.push(d.sculptor.endStroke());
}

// How a single dab left the surface it sat on, per vertex: how far toward the plane it moved (0 = untouched, 1 = on
// the plane), by distance from the dab centre in radii. The brush only cuts, so the surface under the dab is a
// plateau 0.02 above a floor (a shallow cut: a deep one widens its wall): the stroke's first dab, on the floor, takes the plane (level with the floor), and the
// second sits in the middle of the plateau, every vertex under it above the plane. `band` is the width (in radii) of
// the ring where the move is between 10% and 90% of the way; `inside` the worst fraction within 0.75 radii;
// `outside` the largest move past 1.05 radii.
struct Edge { float band = 0.0f, inside = 1.0f, outside = 0.0f; int partial = 0; };
Edge edgeOf(const BrushRecipe& r, int res = 160) {
    Mesh m = makePlane(res, 4.0f);
    for (glm::vec3& x : m.positions) x.y = 0.02f * std::clamp((x.x - 0.3f) / 0.06f, 0.0f, 1.0f);
    m.recomputeAllNormals();
    Document d = makeDoc(std::move(m));
    const std::vector<glm::vec3> before = d.mesh.positions;
    const glm::vec3 c(1.3f, 0.02f, 0.0f);
    const DabParams p = dabAt(c, glm::vec3(0, -1, 0));
    recipeStroke(d, r, {dabAt(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0, -1, 0)), p});
    const glm::vec3 n = d.sculptor.lastFrame.normal, at = d.sculptor.lastFrame.areaCenter;
    Edge e;
    float lo = 1e9f, hi = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const float dist = glm::distance(before[v], c) / p.radius;
        const float h0 = glm::dot(before[v] - at, n);
        const float moved = glm::length(d.mesh.positions[v] - before[v]);
        if (dist > 1.05f && before[v].x > 0.6f) e.outside = std::max(e.outside, moved);
        if (std::abs(h0) < 1e-3f || dist > 1.05f) continue;
        const float f = std::clamp(1.0f - glm::dot(d.mesh.positions[v] - at, n) / h0, 0.0f, 1.0f);
        if (dist < 0.75f) e.inside = std::min(e.inside, f);
        if (f > 0.1f && f < 0.9f) {
            lo = std::min(lo, dist);
            hi = std::max(hi, dist);
            ++e.partial;
        }
    }
    e.band = e.partial ? hi - lo : 0.0f;
    return e;
}

} // namespace

TEST(crisp_flatten_recipe_keys_round_trip) {
    BrushRecipe r;
    r.name = "Test";
    CHECK(r.toJson()["planeLock"].isNull() && r.toJson()["crispEdge"].isNull()); // (off: not written)
    r.planeLock = true;
    r.crispEdge = true;
    const BrushRecipe back = BrushRecipe::fromJson(r.toJson());
    CHECK(back.planeLock && back.crispEdge);
    const BrushRecipe crisp = builtin("Crisp Flatten");
    CHECK(crisp.origin.pack == "Hard Surface");
    CHECK(crisp.plane == BrushRecipe::Plane::Flatten && crisp.planeLock && crisp.crispEdge);
}

// The edge: everything well inside goes onto the plane in one dab, nothing past the radius moves, and the
// boundary is a thin band (still a band: vertices on it move part of the way, so it doesn't stair-step) where a
// soft flatten fades over most of the radius. (Where the cut is deep the band widens to about its depth.)
TEST(crisp_flatten_cuts_a_sharp_anti_aliased_edge) {
    const Edge crisp = edgeOf(builtin("Crisp Flatten"));
    BrushRecipe soft = builtin("Crisp Flatten");
    soft.crispEdge = false;
    const Edge plain = edgeOf(soft);
    const Edge flat = edgeOf(builtin("Flat Plane")); // (the hard-tipped, square one)
    std::printf("  band: crisp %.3f (%d partial), soft %.3f, flat plane %.3f; crisp inside %.3f\n", crisp.band,
                crisp.partial, plain.band, flat.band, crisp.inside);
    CHECK(crisp.inside > 0.97f);
    CHECK(crisp.outside < 1e-6f);
    CHECK(crisp.partial > 0);
    CHECK(crisp.band < 0.25f);
    CHECK(crisp.band < 0.5f * plain.band);
    // On a coarser mesh the band is at least about 2.5 vertex spacings, so it stays smooth.
    const Edge coarse = edgeOf(builtin("Crisp Flatten"), 70);
    std::printf("  coarse band %.3f (%d partial)\n", coarse.band, coarse.partial);
    CHECK(coarse.partial > 0 && coarse.band < 0.4f);
}

// Lock plane: a stroke along a wavy surface, started in a trough, cuts every crest down to the trough's plane and
// no lower; with it off the plane follows the surface, and keeps lowering it below that first plane. The next stroke
// takes a new plane.
TEST(crisp_flatten_lock_plane_holds_the_first_plane) {
    constexpr float kTrough = -0.5890486f; // (sin(8 x) = -1)
    auto run = [&](bool lock, float& above) {
        BrushRecipe r = builtin("Crisp Flatten");
        r.planeLock = lock;
        Mesh m = makePlane(128, 2.0f);
        for (glm::vec3& x : m.positions) x.y = 0.05f * std::sin(8.0f * x.x);
        m.recomputeAllNormals();
        Document d = makeDoc(std::move(m));
        std::vector<DabParams> dabs;
        for (int i = 0; i <= 40; ++i) {
            DabParams p = dabAt(glm::vec3(kTrough + 0.03f * float(i), 0.5f, 0.0f), glm::vec3(0, -1, 0), 0.15f);
            p.center.y = 0.05f * std::sin(8.0f * p.center.x);
            dabs.push_back(p);
        }
        glm::vec3 n0(0.0f), at0(0.0f);
        d.sculptor.beginStroke(d.mesh, r.name);
        for (size_t i = 0; i < dabs.size(); ++i) {
            d.sculptor.dabRecipe(d.mesh, d.bvh, r, dabs[i], kNoSymmetry);
            if (i == 0) n0 = d.sculptor.lastFrame.normal, at0 = d.sculptor.lastFrame.areaCenter;
        }
        d.undo.push(d.sculptor.endStroke());
        above = -1.0f; // the highest point along the stroke's core above the first dab's plane
        for (const glm::vec3& x : d.mesh.positions)
            if (std::abs(x.z) < 0.07f && x.x > kTrough + 0.1f && x.x < kTrough + 1.1f)
                above = std::max(above, glm::dot(x - at0, n0));
        return n0;
    };
    float locked = 0.0f, follow = 0.0f;
    const glm::vec3 n = run(true, locked);
    run(false, follow);
    std::printf("  highest above the first plane: locked %.4f, following %.4f\n", locked, follow);
    CHECK(n.y > 0.99f);
    CHECK(locked < 0.002f);
    // (cut-only, a plane that follows the surface keeps lowering it: each dab's plane is the mean of what the last left)
    CHECK(follow < locked - 0.003f);
    // The next stroke takes a new plane.
    BrushRecipe r = builtin("Crisp Flatten");
    Document d = makeDoc(makeIcosphere(5));
    recipeStroke(d, r, {dabAt(glm::vec3(0, 1, 0), glm::vec3(0, -1, 0))});
    const glm::vec3 side = glm::normalize(glm::vec3(1, 1, 0));
    recipeStroke(d, r, {dabAt(side, -side)});
    CHECK(d.sculptor.lastFrame.normal.x > 0.6f);
}

// Symmetry (each copy locks its own plane: the result mirrors exactly) and masks (masked vertices stay).
TEST(crisp_flatten_symmetry_and_mask) {
    const BrushRecipe r = builtin("Crisp Flatten");
    Document d = makeDoc(makeIcosphere(5));
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<DabParams> dabs;
    for (int i = 0; i <= 6; ++i) {
        const float a = 0.5f + 0.04f * float(i);
        const glm::vec3 c(std::sin(a), std::cos(a), 0.1f * float(i) * 0.1f);
        dabs.push_back(dabAt(glm::normalize(c), -glm::normalize(c), 0.25f));
    }
    recipeStroke(d, r, dabs, Symmetry{true, false, false});
    int movedLeft = 0;
    float worst = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        if (glm::distance(d.mesh.positions[v], before[v]) < 1e-6f) continue; // (only what moved)
        const glm::vec3 m(-before[v].x, before[v].y, before[v].z);
        uint32_t w = v;
        for (uint32_t u = 0; u < d.mesh.vertexCount(); ++u)
            if (glm::distance(before[u], m) < 1e-5f) w = u;
        if (w == v && std::abs(before[v].x) > 1e-5f) continue; // (no exact mirror vertex)
        const glm::vec3 a = d.mesh.positions[v], b = d.mesh.positions[w];
        worst = std::max(worst, glm::distance(a, glm::vec3(-b.x, b.y, b.z)));
        movedLeft += before[v].x < 0.0f && glm::distance(a, before[v]) > 0.002f;
    }
    std::printf("  mirror mismatch %.2g, %d moved on the mirrored side\n", worst, movedLeft);
    CHECK(movedLeft > 20);
    CHECK(worst < 1e-4f);

    // Masked: a fully masked half doesn't move; the rest flattens.
    Document m = makeDoc(makeIcosphere(5));
    const std::vector<glm::vec3> start = m.mesh.positions;
    m.mesh.mask.assign(m.mesh.vertexCount(), 0.0f);
    for (uint32_t v = 0; v < m.mesh.vertexCount(); ++v)
        if (start[v].x < 0.0f) m.mesh.mask[v] = 1.0f;
    recipeStroke(m, r, {dabAt(glm::vec3(0, 1, 0), glm::vec3(0, -1, 0))});
    int movedMasked = 0, movedFree = 0;
    for (uint32_t v = 0; v < m.mesh.vertexCount(); ++v) {
        const bool moved = glm::distance(m.mesh.positions[v], start[v]) > 1e-6f;
        (start[v].x < 0.0f ? movedMasked : movedFree) += moved;
    }
    CHECK(movedMasked == 0 && movedFree > 20);
}

// Dynamic topology: the surface is remeshed under the brush as it goes, and the stroke still ends flat.
TEST(crisp_flatten_with_dynamic_topology) {
    const BrushRecipe r = builtin("Crisp Flatten");
    Document d = makeDoc(makeIcosphere(4));
    d.sculptor.dyntopo.enabled = true;
    const size_t verts = d.mesh.vertexCount();
    std::vector<DabParams> dabs;
    for (int i = 0; i <= 8; ++i) {
        const float a = 0.04f * float(i);
        const glm::vec3 c(std::sin(a), std::cos(a), 0.0f);
        dabs.push_back(dabAt(c, glm::vec3(0, -1, 0), 0.3f));
    }
    recipeStroke(d, r, dabs);
    CHECK(d.mesh.vertexCount() != verts); // (remeshed)
    for (const glm::uvec3& t : d.mesh.triangles) CHECK(std::max({t.x, t.y, t.z}) < d.mesh.vertexCount());
    // The top is flat: every vertex near the axis sits at one height.
    float lo = 1e9f, hi = -1e9f;
    for (const glm::vec3& x : d.mesh.positions)
        if (std::abs(x.x - 0.1f) < 0.12f && std::abs(x.z) < 0.12f && x.y > 0.5f) lo = std::min(lo, x.y), hi = std::max(hi, x.y);
    std::printf("  dyntopo top spread %.4f\n", hi - lo);
    CHECK(hi > lo && hi - lo < 0.01f);
}

// A plane locked on a box's top and dragged over its edge: the side face (90 degrees off the plane) isn't folded
// up onto it as a flap.
TEST(crisp_flatten_leaves_the_side_of_a_box) {
    const BrushRecipe r = builtin("Crisp Flatten");
    Document d = makeDoc(makeCube(32));
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<DabParams> dabs;
    for (int i = 0; i <= 10; ++i) dabs.push_back(dabAt(glm::vec3(0.09f * float(i), 1.0f, 0.0f), glm::vec3(0, -1, 0), 0.4f));
    recipeStroke(d, r, dabs);
    float side = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
        if (before[v].x > 0.999f && before[v].y < 0.9f)
            side = std::max(side, glm::distance(d.mesh.positions[v], before[v]));
    std::printf("  box edge: side face moved %.4f, plane normal %.3f %.3f\n", side, d.sculptor.lastFrame.normal.x,
                d.sculptor.lastFrame.normal.y);
    CHECK(side < 0.01f);
}


// A stroke down a rounded box edge (a 45 degree chamfer, as in the Crisp Flatten UI test) leaves one flat facet: the
// height across the stroke's core deviates from the stroke's plane by less than a vertex spacing, and nothing is
// built up past the original surface (no raised lip along the facet's sides).
TEST(crisp_flatten_chamfer_is_one_flat_facet) {
    constexpr float kHalf = 1.0f, kRound = 0.15f;
    Mesh m = makeCube(96, 2.0f * kHalf);
    const float spacing = 2.0f * kHalf / 96.0f;
    for (glm::vec3& x : m.positions) { // the cube's surface onto a rounded box
        const glm::vec3 c = glm::clamp(x, glm::vec3(-(kHalf - kRound)), glm::vec3(kHalf - kRound));
        const glm::vec3 o = x - c;
        const float len = glm::length(o);
        if (len > 1e-6f) x = c + o * (kRound / len);
    }
    m.recomputeAllNormals();
    Document d = makeDoc(std::move(m));
    const std::vector<glm::vec3> before = d.mesh.positions;
    const std::vector<glm::vec3> nBefore = d.mesh.normals;
    const BrushRecipe r = builtin("Crisp Flatten");
    const glm::vec3 axis = glm::normalize(glm::vec3(1, 0, 1)); // (out of the vertical edge)
    const float apex = kHalf - kRound + kRound * 0.70710678f; // (the edge's surface, in x and z)
    std::vector<DabParams> dabs;
    for (int i = 0; i <= 14; ++i) dabs.push_back(dabAt(glm::vec3(apex, 0.45f - 0.06f * float(i), apex), -axis, 0.4f));
    d.sculptor.beginStroke(d.mesh, r.name);
    glm::vec3 n0(0.0f), at0(0.0f);
    for (size_t i = 0; i < dabs.size(); ++i) {
        d.sculptor.dabRecipe(d.mesh, d.bvh, r, dabs[i], kNoSymmetry);
        if (i == 0) n0 = d.sculptor.lastFrame.normal, at0 = d.sculptor.lastFrame.areaCenter;
    }
    d.undo.push(d.sculptor.endStroke());
    float core = 0.0f, lip = 0.0f, across = 0.0f;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const glm::vec3 o = before[v] - glm::vec3(apex, before[v].y, apex);
        const float side = glm::dot(o, glm::normalize(glm::vec3(1, 0, -1))); // (across the stroke)
        const float grown = glm::dot(d.mesh.positions[v] - before[v], nBefore[v]);
        lip = std::max(lip, grown);
        if (std::abs(before[v].y) < 0.25f && std::abs(side) < 0.3f && glm::dot(before[v] - glm::vec3(apex, before[v].y, apex), axis) > -0.2f &&
            glm::distance(d.mesh.positions[v], before[v]) > 1e-5f) {
            core = std::max(core, std::abs(glm::dot(d.mesh.positions[v] - at0, n0)));
            across = std::max(across, std::abs(side));
        }
    }
    std::printf("  chamfer: plane normal %.3f %.3f %.3f, core deviation %.4f (spacing %.4f), built up %.4f, facet half-width %.3f\n",
                n0.x, n0.y, n0.z, core, spacing, lip, across);
    CHECK(core < spacing);
    CHECK(lip < spacing);
}

// A region past the adaptive threshold (Sculptor::kAdaptiveMinVertices) takes the built-in plane brushes' normals
// path: the same cut as the exact path, and once the stroke ends, the exact normals.
TEST(crisp_flatten_big_region_matches_the_exact_path) {
    const Mesh sphere = makeIcosphere(8);
    std::vector<DabParams> dabs;
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 c = glm::normalize(glm::vec3(-0.3f + 0.05f * i, 1.0f, 0.1f));
        dabs.push_back(dabAt(c, -c, 1.0f));
    }
    const BrushRecipe crisp = builtin("Crisp Flatten");
    Document lazy = makeDoc(Mesh(sphere)), exact = makeDoc(Mesh(sphere));
    exact.sculptor.analyticNormals = false;
    recipeStroke(lazy, crisp, dabs);
    recipeStroke(exact, crisp, dabs);
    size_t region = 0;
    for (const glm::vec3& x : sphere.positions) region += glm::distance(x, dabs[0].center) < dabs[0].radius;
    CHECK(region >= Sculptor::kAdaptiveMinVertices);
    float worst = 0.0f, moved = 0.0f, nworst = 0.0f;
    Mesh fresh = lazy.mesh;
    fresh.recomputeAllNormals();
    for (uint32_t v = 0; v < sphere.vertexCount(); ++v) {
        worst = std::max(worst, glm::distance(lazy.mesh.positions[v], exact.mesh.positions[v]));
        moved = std::max(moved, glm::distance(lazy.mesh.positions[v], sphere.positions[v]));
        nworst = std::max(nworst, glm::distance(lazy.mesh.normals[v], fresh.normals[v]));
    }
    // (the exact path updates the normals around the points each dab moved only: none may be left stale)
    Mesh freshExact = exact.mesh;
    freshExact.recomputeAllNormals();
    float eworst = 0.0f;
    for (uint32_t v = 0; v < sphere.vertexCount(); ++v)
        eworst = std::max(eworst, glm::distance(exact.mesh.normals[v], freshExact.normals[v]));
    std::printf("  big region: moved %.4f, lazy vs exact %.5f, normals off %.5f (exact path %.5f)\n", moved, worst,
                nworst, eworst);
    CHECK(moved > 0.05f);
    CHECK(worst < 0.01f * moved);
    CHECK(nworst < 1e-3f);
    CHECK(eworst < 1e-3f);
}

namespace {

// A stroke's frame: the first dab's plane (the lock plane the whole stroke keeps).
struct Stroked { glm::vec3 n{0.0f}, at{0.0f}; };
Stroked strokeWithPlane(Document& d, const BrushRecipe& r, const std::vector<DabParams>& dabs) {
    Stroked s;
    d.sculptor.beginStroke(d.mesh, r.name);
    for (size_t i = 0; i < dabs.size(); ++i) {
        d.sculptor.dabRecipe(d.mesh, d.bvh, r, dabs[i], kNoSymmetry);
        if (i == 0) s.n = d.sculptor.lastFrame.normal, s.at = d.sculptor.lastFrame.areaCenter;
    }
    d.undo.push(d.sculptor.endStroke());
    return s;
}

} // namespace

// Crisp Flatten only cuts (a member's ask: "it should never raise or add new clay"): on a plane with bumps and a
// pit, no vertex rises along the plane's normal by more than float noise, whatever its depth, the pit is left
// exactly as it was, and the bumps in the stroke's path come down onto the plane.
TEST(crisp_flatten_invariant_cut_only_bumpy_plane_and_pit) {
    Mesh m = makePlane(160, 2.0f);
    for (glm::vec3& x : m.positions) {
        const float pit = std::exp(-((x.x - 0.3f) * (x.x - 0.3f) + x.z * x.z) / 0.012f);
        x.y = 0.02f * std::sin(9.0f * x.x) * std::cos(7.0f * x.z) - 0.1f * pit;
    }
    m.recomputeAllNormals();
    Document d = makeDoc(std::move(m));
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<DabParams> dabs;
    for (int i = 0; i <= 24; ++i) {
        DabParams p = dabAt(glm::vec3(-0.7f + 0.06f * float(i), 0.0f, 0.0f), glm::vec3(0, -1, 0), 0.25f);
        dabs.push_back(p);
    }
    const Stroked st = strokeWithPlane(d, builtin("Crisp Flatten"), dabs);
    float raised = 0.0f, peakLeft = 0.0f, pitMoved = 0.0f;
    int peaks = 0;
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        const float rise = glm::dot(d.mesh.positions[v] - before[v], st.n);
        raised = std::max(raised, rise);
        const glm::vec3 b = before[v];
        const float hb = glm::dot(b - st.at, st.n), ha = glm::dot(d.mesh.positions[v] - st.at, st.n);
        if (std::abs(b.z) < 0.1f && b.x > -0.5f && b.x < 0.6f && hb > 0.004f) ++peaks, peakLeft = std::max(peakLeft, ha);
        if (hb < -0.03f) pitMoved = std::max(pitMoved, glm::distance(d.mesh.positions[v], b));
    }
    std::printf("  bumpy plane: highest rise %.2g, %d peak vertices in the path leave at most %.4f above the plane, pit moved %.2g\n",
                raised, peaks, peakLeft, pitMoved);
    CHECK(raised < 1e-5f);
    CHECK(peaks > 8);
    CHECK(peakLeft < 0.002f);
    CHECK(pitMoved < 1e-6f);
}

namespace {

struct Invariants { float raised = 0.0f, worst = 0.0f; int spikes = 0, moved = 0; };

// The geometric invariants of a Crisp Flatten stroke on a dense curved mesh, against the stroke's plane (n, at):
// `raised` = the most any vertex rose along n (cut-only: float noise at most); `spikes` = vertices (the ones
// `inRegion` picks) whose height stands out from the mean of their ring by more than a quarter of the vertex
// spacing: the speckle, debris and stair-steps along a facet's falloff edge. `mirror`: vertices on the -x side
// use the mirrored plane (a symmetry copy's own).
Invariants invariantsOf(const Mesh& after, const std::vector<glm::vec3>& before, glm::vec3 n, glm::vec3 at, float spacing,
                        const std::function<bool(const glm::vec3&)>& inRegion, bool mirror = false) {
    auto plane = [&](const glm::vec3& x, glm::vec3& pn, glm::vec3& pa) {
        pn = n, pa = at;
        if (mirror && x.x < 0.0f) pn.x = -n.x, pa.x = -at.x;
    };
    auto height = [&](const glm::vec3& x, const glm::vec3& ref) {
        glm::vec3 pn, pa;
        plane(ref, pn, pa);
        return glm::dot(x - pa, pn);
    };
    Invariants out;
    for (uint32_t v = 0; v < after.vertexCount(); ++v) {
        out.raised = std::max(out.raised, height(after.positions[v], before[v]) - height(before[v], before[v]));
        out.moved += glm::distance(after.positions[v], before[v]) > 1e-6f;
        if (!inRegion(before[v])) continue;
        auto ring = after.neighbors(v);
        if (ring.empty()) continue;
        float mean = 0.0f;
        for (uint32_t u : ring) mean += height(after.positions[u], before[v]);
        const float o = std::abs(height(after.positions[v], before[v]) - mean / float(ring.size()));
        out.worst = std::max(out.worst, o);
        out.spikes += o > 0.25f * spacing;
    }
    return out;
}

// (`shift` slides the arc off the top toward +x, where the form falls away under the plane: 0 and 0.45 both used)
std::vector<DabParams> arcDabs(float radius, int count, float step, float pressure0 = 1.0f, float pressure1 = 1.0f, float shift = 0.0f) {
    std::vector<DabParams> dabs;
    for (int i = 0; i < count; ++i) {
        const float a = step * float(i);
        const glm::vec3 c = glm::normalize(glm::vec3(std::sin(a), std::cos(a) * 0.98f, 0.15f * std::sin(a * 3.0f)) + glm::vec3(shift, 0.0f, 0.0f));
        DabParams p = dabAt(c, -c, radius);
        p.pressure = pressure0 + (pressure1 - pressure0) * float(i) / float(std::max(count - 1, 1));
        dabs.push_back(p);
    }
    return dabs;
}

float spacingOf(const Mesh& m) { return 1.9f / std::sqrt(float(m.vertexCount()) / 2.0f); }

} // namespace

// Clean edges: on a dense curved patch the facet's border is a smooth ramp, not speckle. After a stroke nothing
// sits above where it was, and no vertex stands out from its neighbours' average height (a spike or a dent along
// the border: the stair-steps and debris of a member's screenshot) by more than a fraction of the vertex spacing.
TEST(crisp_flatten_invariant_dense_patch_cut_only_and_clean_edges) {
    for (const float shift : {0.0f, 0.45f}) {
        Document d = makeDoc(makeIcosphere(6));
        const std::vector<glm::vec3> before = d.mesh.positions;
        const std::vector<DabParams> dabs = arcDabs(0.3f, 15, 0.05f, 1.0f, 1.0f, shift);
        const Stroked st = strokeWithPlane(d, builtin("Crisp Flatten"), dabs);
        const float spacing = spacingOf(d.mesh);
        const Invariants inv = invariantsOf(d.mesh, before, st.n, st.at, spacing,
                                            [&](const glm::vec3& x) { return glm::distance(x, dabs[7].center) < 0.45f; });
        std::printf("  dense patch (shift %.2f): %d moved, highest rise %.2g, worst outlier %.5f (spacing %.4f), spikes %d\n", shift,
                    inv.moved, inv.raised, inv.worst, spacing, inv.spikes);
        CHECK(inv.moved > 150);
        CHECK(inv.raised < 1e-5f);
        CHECK(inv.spikes == 0);
    }
}

// Symmetry (X): each copy keeps its own plane; both are cut-only and clean.
TEST(crisp_flatten_invariant_symmetry_cut_only_and_clean_edges) {
    Document d = makeDoc(makeIcosphere(6));
    const std::vector<glm::vec3> before = d.mesh.positions;
    std::vector<DabParams> dabs = arcDabs(0.3f, 15, 0.05f);
    for (DabParams& p : dabs) { // (off the mirror plane, so the copies are apart)
        p.center = glm::normalize(p.center + glm::vec3(0.45f, 0.0f, 0.0f));
        p.viewDir = -p.center;
    }
    const BrushRecipe r = builtin("Crisp Flatten");
    glm::vec3 n(0.0f), at(0.0f);
    d.sculptor.beginStroke(d.mesh, r.name);
    for (size_t i = 0; i < dabs.size(); ++i) {
        d.sculptor.dabRecipe(d.mesh, d.bvh, r, dabs[i], Symmetry{true, false, false});
        if (i == 0) n = d.sculptor.lastFrame.normal, at = d.sculptor.lastFrame.areaCenter; // (the first copy's)
    }
    d.undo.push(d.sculptor.endStroke());
    const float spacing = spacingOf(d.mesh);
    const glm::vec3 c = dabs[7].center, cm(-c.x, c.y, c.z);
    // (the copy on the -x side takes the mirrored plane, so the first copy's side is the one given)
    const bool firstIsRight = c.x > 0.0f;
    const Invariants inv = invariantsOf(
        d.mesh, before, firstIsRight ? n : glm::vec3(-n.x, n.y, n.z), firstIsRight ? at : glm::vec3(-at.x, at.y, at.z), spacing,
        [&](const glm::vec3& x) { return glm::distance(x, c) < 0.45f || glm::distance(x, cm) < 0.45f; }, true);
    std::printf("  symmetry: %d moved, highest rise %.2g, worst outlier %.5f (spacing %.4f), spikes %d\n", inv.moved,
                inv.raised, inv.worst, spacing, inv.spikes);
    CHECK(inv.moved > 300);
    CHECK(inv.raised < 1e-5f);
    CHECK(inv.spikes == 0);
}

// Overlapping dabs at changing pressure (0.2 to 1 and back: a stroke from a pen): the same invariants hold, the
// partial pulls of a light dab never leave debris for the next to cut around.
TEST(crisp_flatten_invariant_overlap_with_pressure_cut_only_and_clean_edges) {
    for (int dir = 0; dir < 2; ++dir) {
        Document d = makeDoc(makeIcosphere(6));
        const std::vector<glm::vec3> before = d.mesh.positions;
        const std::vector<DabParams> dabs = dir == 0 ? arcDabs(0.3f, 30, 0.025f, 0.2f, 1.0f, 0.45f) : arcDabs(0.3f, 30, 0.025f, 1.0f, 0.2f, 0.45f);
        const Stroked st = strokeWithPlane(d, builtin("Crisp Flatten"), dabs);
        const float spacing = spacingOf(d.mesh);
        const Invariants inv = invariantsOf(d.mesh, before, st.n, st.at, spacing,
                                            [&](const glm::vec3& x) { return glm::distance(x, dabs[15].center) < 0.45f; });
        std::printf("  pressure %s: %d moved, highest rise %.2g, worst outlier %.5f, spikes %d\n", dir ? "1 to 0.2" : "0.2 to 1",
                    inv.moved, inv.raised, inv.worst, inv.spikes);
        CHECK(inv.moved > 150);
        CHECK(inv.raised < 1e-5f);
        CHECK(inv.spikes == 0);
        // Pressure doesn't scale the cut: a pressure-scaled depth per dab left terraces (contour ridges) across the
        // facet, so the same stroke at full pressure lands on the same surface.
        std::vector<DabParams> full = dabs;
        for (DabParams& p : full) p.pressure = 1.0f;
        Document f = makeDoc(makeIcosphere(6));
        strokeWithPlane(f, builtin("Crisp Flatten"), full);
        float worst = 0.0f;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v)
            worst = std::max(worst, glm::distance(d.mesh.positions[v], f.mesh.positions[v]));
        std::printf("  pressure %s vs full pressure: surfaces differ by at most %.2g\n", dir ? "1 to 0.2" : "0.2 to 1", worst);
        CHECK(worst < 1e-5f);
    }
}
